#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include "../engine/Dsp.h"

// Plays a song's tracks (the stem files) with a live mixer: volume, pan, mute, solo and tone.
// Files are streamed from disk in the background, so long songs don't use much memory.
class StudioPlayer
{
public:
    static constexpr int maxTracks = 12;
    static constexpr double fileRate = 48000.0; // songs are recorded at 48 kHz

    struct Mix
    {
        std::atomic<float> gainDb { 0.0f }, pan { 0.0f }, tone { 0.0f }; // tone: -1 darker .. 0 .. +1 thinner
        std::atomic<bool> mute { false }, solo { false };
        // your samples go through the knobs' effects; group: 0 none, 1 chords, 2 melody, 3 bass, 4 drums
        std::atomic<bool> sample { false };
        std::atomic<int> group { 0 };
    };

    // the mix knobs (chords / melody / bass) as gains for each group; [0] unused
    std::array<std::atomic<float>, 5> groupGain { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };

    StudioPlayer() { thread.startThread (juce::Thread::Priority::normal); }
    ~StudioPlayer() { clear(); thread.stopThread (2000); }

    std::array<Mix, maxTracks> mix;

    // message thread: (re)open the tracks' files
    void load (const juce::Array<juce::File>& stems)
    {
        std::vector<std::unique_ptr<juce::BufferingAudioReader>> fresh;
        juce::int64 length = 0;
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        for (auto& f : stems)
        {
            std::unique_ptr<juce::BufferingAudioReader> r;
            if (auto* raw = fm.createReaderFor (f))
            {
                r = std::make_unique<juce::BufferingAudioReader> (raw, thread, (int) (fileRate * 4));
                r->setReadTimeout (0); // never make the audio wait
                length = std::max (length, raw->lengthInSamples);
            }
            fresh.push_back (std::move (r));
        }
        {
            const juce::SpinLock::ScopedLockType sl (lock);
            std::swap (readers, fresh);
            lengthSamples = length;
        }
        // `fresh` (the old readers) is released here, outside the lock
    }

    void clear() { load ({}); }
    juce::int64 length() const { return lengthSamples.load(); }

    // audio thread: adds the mixed tracks into L/R. pos and inc are in file samples.
    // samples go to fxL/R (and their echo / room send to sendL/R) instead of L/R
    void render (float* L, float* R, int n, double pos, double inc, double sampleRate,
                 float* fxL = nullptr, float* fxR = nullptr, float* sendL = nullptr, float* sendR = nullptr)
    {
        const juce::SpinLock::ScopedTryLockType tl (lock);
        if (! tl.isLocked() || readers.empty() || pos < 0) return;
        const int need = (int) std::ceil (n * inc) + 3;
        if (scratch.getNumSamples() < need) return; // prepare() makes it big enough
        bool anySolo = false;
        for (size_t t = 0; t < readers.size() && t < maxTracks; ++t) anySolo = anySolo || mix[t].solo.load();
        const auto start = (juce::int64) std::floor (pos);
        for (size_t t = 0; t < readers.size() && t < maxTracks; ++t)
        {
            auto* r = readers[t].get();
            auto& m = mix[t];
            if (r == nullptr || m.mute.load() || (anySolo && ! m.solo.load())) continue;
            if (start >= r->lengthInSamples) continue;
            scratch.clear();
            r->read (&scratch, 0, need, start, true, true);
            const bool toFx = fxL != nullptr && m.sample.load();
            const int grp = juce::jlimit (0, 4, m.group.load());
            const float g = juce::Decibels::decibelsToGain (m.gainDb.load()) * (toFx ? groupGain[(size_t) grp].load() : 1.0f);
            const float send = grp == 4 ? 0.3f : 1.0f; // drums: a little room, stays punchy
            float* oL = toFx ? fxL : L;
            float* oR = toFx ? fxR : R;
            const float p = m.pan.load();
            const float gl = g * std::min (1.0f, 1.0f - p), gr = g * std::min (1.0f, 1.0f + p);
            // tone: a gentle low-pass going down, a high-pass going up
            const float tone = m.tone.load();
            auto& f = filters[t];
            if (std::abs (tone - f.lastTone) > 0.001f)
            {
                f.lastTone = tone;
                const float hz = tone < 0 ? 18000.0f * std::pow (0.015f, -tone) : 20.0f * std::pow (60.0f, tone);
                f.l.setup (hz, 0.7f, (float) sampleRate);
                f.r.setup (hz, 0.7f, (float) sampleRate);
            }
            const float* a = scratch.getReadPointer (0);
            const float* b = scratch.getReadPointer (1);
            double ph = pos - (double) start;
            for (int i = 0; i < n; ++i, ph += inc)
            {
                const int i0 = (int) ph;
                const float fr = (float) (ph - i0);
                float xl = a[i0] + (a[i0 + 1] - a[i0]) * fr;
                float xr = b[i0] + (b[i0 + 1] - b[i0]) * fr;
                if (tone < -0.01f) { f.l.tick (xl); f.r.tick (xr); xl = f.l.lp; xr = f.r.lp; }
                else if (tone > 0.01f) { f.l.tick (xl); f.r.tick (xr); xl = f.l.hp; xr = f.r.hp; }
                oL[i] += xl * gl;
                oR[i] += xr * gr;
                if (toFx) { sendL[i] += xl * gl * send; sendR[i] += xr * gr * send; }
            }
        }
    }

    void prepare (int maxBlock) { scratch.setSize (2, maxBlock * 4 + 64); }

private:
    struct Filter { lp::dsp::SVF l, r; float lastTone = 99.0f; };
    juce::TimeSliceThread thread { "Live Planet studio" };
    std::vector<std::unique_ptr<juce::BufferingAudioReader>> readers;
    std::array<Filter, maxTracks> filters;
    juce::AudioBuffer<float> scratch;
    juce::SpinLock lock;
    std::atomic<juce::int64> lengthSamples { 0 };
};
