#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include "../engine/Dsp.h"
#include "Peak.h"

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
        // this track's own effects: reverb and delay sends, warmth and nostalgia inserts (all 0..1)
        std::atomic<float> space { 0.0f }, echo { 0.0f }, warmth { 0.0f }, nostalgia { 0.0f }, chorus { 0.0f };
        // drag this track with its effects recorded into it ("print"), instead of the dry file
        std::atomic<bool> printFx { false };
    };

    // the mix knobs (chords / melody / bass) as gains for each group; [0] unused
    std::array<std::atomic<float>, 5> groupGain { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };

    StudioPlayer() { thread.startThread (juce::Thread::Priority::normal); }
    ~StudioPlayer() { clear(); thread.stopThread (2000); }

    std::array<Mix, maxTracks> mix;
    std::atomic<int> skipTrack { -1 }; // a track played live instead (the drum editor's loop)
    // what each track is playing right now (its loudest sample since the screen last looked), for the meters
    std::array<std::atomic<float>, maxTracks> peakL {}, peakR {};
    std::atomic<float> meterGain { 1.0f }; // the song's playback level, so the meters read what you hear

    // message thread: (re)open the tracks' files
    // waitMs: how long a read may wait for the disk (0 while playing live; offline prints wait)
    void load (const juce::Array<juce::File>& stems, int waitMs = 0)
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
                r->setReadTimeout (waitMs); // live: never make the audio wait
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

    // the whole song as you hear it in the studio (every track with its mixer and its own effects), for mastering
    static juce::AudioBuffer<float> bounce (const juce::Array<juce::File>& stems, const std::array<Mix, maxTracks>& mixes, double bpm)
    {
        juce::ScopedNoDenormals noDenormals;
        StudioPlayer p;
        p.load (stems, 5000);
        const auto len = p.length();
        if (len <= 0) return {};
        for (size_t t = 0; t < maxTracks; ++t)
        {
            auto& d = p.mix[t]; const auto& m = mixes[t];
            d.gainDb = m.gainDb.load(); d.pan = m.pan.load(); d.tone = m.tone.load(); d.mute = m.mute.load(); d.solo = m.solo.load();
            d.space = m.space.load(); d.echo = m.echo.load(); d.warmth = m.warmth.load(); d.nostalgia = m.nostalgia.load(); d.chorus = m.chorus.load();
        }
        constexpr int block = 1024;
        p.prepare (block);
        const int total = (int) (len + (juce::int64) (fileRate * 3.0));
        juce::AudioBuffer<float> all (2, total), buf (2, block);
        all.clear();
        for (int pos = 0; pos < total; pos += block)
        {
            const int n = std::min (block, total - pos);
            buf.clear();
            p.render (buf.getWritePointer (0), buf.getWritePointer (1), n, (double) pos, 1.0, fileRate, nullptr, nullptr, nullptr, nullptr, bpm);
            for (int ch = 0; ch < 2; ++ch) all.copyFrom (ch, pos, buf, ch, 0, n);
        }
        return all;
    }

    // records one track as you hear it: its volume, pan, tone, warmth / nostalgia / chorus, and its
    // own reverb and delay (with their tails), into a new file. The master knobs aren't included.
    // message thread (takes a second or two for a whole song)
    static bool printTrack (const juce::File& stem, const Mix& m, double bpm, const juce::File& out)
    {
        juce::ScopedNoDenormals noDenormals;
        StudioPlayer p;
        p.load ({ stem }, 5000);
        const auto len = p.length();
        if (len <= 0) return false;
        auto& d = p.mix[0];
        d.gainDb = m.gainDb.load(); d.pan = m.pan.load(); d.tone = m.tone.load();
        d.space = m.space.load(); d.echo = m.echo.load(); d.warmth = m.warmth.load();
        d.nostalgia = m.nostalgia.load(); d.chorus = m.chorus.load();
        constexpr int block = 1024;
        p.prepare (block);
        const auto total = len + (juce::int64) (fileRate * 6.0); // room for the echo and reverb tails
        juce::AudioBuffer<float> buf (2, block);
        out.getParentDirectory().createDirectory();
        out.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (out), fileRate, 2, 24, {}, 0));
        if (w == nullptr) return false;
        juce::int64 lastSound = 0;
        juce::AudioBuffer<float> all (2, (int) total);
        all.clear();
        for (juce::int64 pos = 0; pos < total; pos += block)
        {
            const int n = (int) std::min<juce::int64> (block, total - pos);
            buf.clear();
            p.render (buf.getWritePointer (0), buf.getWritePointer (1), n, (double) pos, 1.0, fileRate, nullptr, nullptr, nullptr, nullptr, bpm);
            for (int ch = 0; ch < 2; ++ch) all.copyFrom (ch, (int) pos, buf, ch, 0, n);
            if (buf.getMagnitude (0, n) > 1.0e-4f) lastSound = pos + n;
        }
        // keep the song's length (so it lines up), plus any tail that rings past the end
        const int keep = (int) std::max (len, std::min (total, lastSound));
        return w->writeFromAudioSampleBuffer (all, 0, keep);
    }
    juce::int64 length() const { return lengthSamples.load(); }

    // audio thread: adds the mixed tracks into L/R. pos and inc are in file samples.
    // samples go to fxL/R (and their echo / room send to sendL/R) instead of L/R.
    // Each track's own reverb and delay sends come back (through a shared room and a tempo delay) into L/R.
    void render (float* L, float* R, int n, double pos, double inc, double sampleRate,
                 float* fxL = nullptr, float* fxR = nullptr, float* sendL = nullptr, float* sendR = nullptr, double bpm = 120.0)
    {
        const juce::SpinLock::ScopedTryLockType tl (lock);
        if (! tl.isLocked() || readers.empty() || pos < 0) return;
        const int need = (int) std::ceil (n * inc) + 3;
        if (scratch.getNumSamples() < need || n > (int) verbL.size()) return; // prepare() makes them big enough
        if (sampleRate != fxRate) setupTrackFx (sampleRate);
        std::fill (verbL.begin(), verbL.begin() + n, 0.0f); std::fill (verbR.begin(), verbR.begin() + n, 0.0f);
        std::fill (delL.begin(), delL.begin() + n, 0.0f);  std::fill (delR.begin(), delR.begin() + n, 0.0f);
        bool anySends = false;
        bool anySolo = false;
        for (size_t t = 0; t < readers.size() && t < maxTracks; ++t) anySolo = anySolo || mix[t].solo.load();
        const auto start = (juce::int64) std::floor (pos);
        for (size_t t = 0; t < readers.size() && t < maxTracks; ++t)
        {
            auto* r = readers[t].get();
            auto& m = mix[t];
            if (r == nullptr || m.mute.load() || (anySolo && ! m.solo.load()) || (int) t == skipTrack.load()) continue;
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
            // this track's inserts and sends
            auto& tf = trackFx[t];
            const float warm = m.warmth.load(), nost = m.nostalgia.load();
            const float sp = m.space.load(), ec = m.echo.load();
            anySends = anySends || sp > 0.001f || ec > 0.001f;
            if (std::abs (warm - tf.lastWarm) > 0.002f || std::abs (nost - tf.lastNost) > 0.002f)
            {
                tf.lastWarm = warm; tf.lastNost = nost;
                const float lpHz = 20000.0f * std::pow (0.3f, warm) * std::pow (0.2f, nost);
                tf.lpL.setup (lpHz, 0.6f, (float) sampleRate); tf.lpR.setup (lpHz, 0.6f, (float) sampleRate);
                tf.hpL.setup (20.0f + 260.0f * nost, 0.6f, (float) sampleRate); tf.hpR.setup (20.0f + 260.0f * nost, 0.6f, (float) sampleRate);
            }
            const float drive = 1.0f + 2.2f * warm + 1.2f * nost;
            const float wobDepth = nost * 0.0028f * (float) sampleRate;
            const float cho = m.chorus.load();
            const bool inserts = warm > 0.002f || nost > 0.002f || cho > 0.002f;
            float pkL = 0.0f, pkR = 0.0f;
            for (int i = 0; i < n; ++i, ph += inc)
            {
                const int i0 = (int) ph;
                const float fr = (float) (ph - i0);
                float xl = a[i0] + (a[i0 + 1] - a[i0]) * fr;
                float xr = b[i0] + (b[i0 + 1] - b[i0]) * fr;
                if (tone < -0.01f) { f.l.tick (xl); f.r.tick (xr); xl = f.l.lp; xr = f.r.lp; }
                else if (tone > 0.01f) { f.l.tick (xl); f.r.tick (xr); xl = f.l.hp; xr = f.r.hp; }
                if (inserts)
                {
                    // nostalgia: a slow tape wobble, a worn band (thin lows, soft highs), a narrower picture
                    if (nost > 0.002f)
                    {
                        tf.phase += 0.5f * 6.2831853f / (float) sampleRate;
                        if (tf.phase > 6.2831853f) tf.phase -= 6.2831853f;
                        const float d = 0.002f * (float) sampleRate + wobDepth * (0.5f + 0.5f * std::sin (tf.phase));
                        tf.wobL.push (xl); tf.wobR.push (xr);
                        xl = tf.wobL.read (d); xr = tf.wobR.read (d);
                        tf.hpL.tick (xl); tf.hpR.tick (xr);
                        xl = tf.hpL.hp; xr = tf.hpR.hp;
                        const float mid = 0.5f * (xl + xr), side = 0.5f * (xl - xr) * (1.0f - 0.6f * nost);
                        xl = mid + side; xr = mid - side;
                    }
                    // warmth: a softer top and gentle tape saturation
                    if (warm > 0.002f || nost > 0.002f)
                    {
                        tf.lpL.tick (xl); tf.lpR.tick (xr);
                        xl = std::tanh (tf.lpL.lp * drive) / drive;
                        xr = std::tanh (tf.lpR.lp * drive) / drive;
                    }
                    // chorus: two slowly moving copies, one each side, for width and shimmer
                    if (cho > 0.002f)
                    {
                        tf.chPhase += 0.7f * 6.2831853f / (float) sampleRate;
                        if (tf.chPhase > 6.2831853f) tf.chPhase -= 6.2831853f;
                        const float base = 0.012f * (float) sampleRate, depth = 0.004f * (float) sampleRate;
                        tf.chL.push (xl); tf.chR.push (xr);
                        const float wl = tf.chL.read (base + depth * std::sin (tf.chPhase));
                        const float wr = tf.chR.read (base + depth * std::cos (tf.chPhase * 1.13f));
                        xl = xl * (1.0f - 0.35f * cho) + wl * 0.75f * cho;
                        xr = xr * (1.0f - 0.35f * cho) + wr * 0.75f * cho;
                    }
                }
                oL[i] += xl * gl;
                oR[i] += xr * gr;
                pkL = std::max (pkL, std::abs (xl * gl));
                pkR = std::max (pkR, std::abs (xr * gr));
                if (toFx) { sendL[i] += xl * gl * send; sendR[i] += xr * gr * send; }
                verbL[(size_t) i] += xl * gl * sp; verbR[(size_t) i] += xr * gr * sp;
                delL[(size_t) i] += xl * gl * ec;  delR[(size_t) i] += xr * gr * ec;
            }
            const float mg = meterGain.load();
            lp::ui::notePeak (peakL[t], pkL * mg);
            lp::ui::notePeak (peakR[t], pkR * mg);
        }
        if (! anySends && ! tailLeft) return;
        tailLeft = anySends ? (int) (sampleRate * 6.0) : std::max (0, tailLeft - n);
        // the shared delay (a dotted 8th at the song's tempo, bouncing left / right), then the room
        const float dTime = juce::jlimit (100.0f, (float) (2.5 * sampleRate), (float) (0.75 * 60.0 / std::max (40.0, bpm) * sampleRate));
        for (int i = 0; i < n; ++i)
        {
            const float eL = dLineL.read (dTime), eR = dLineR.read (dTime);
            dFiltL.tick (eL); dFiltR.tick (eR);
            dLineL.push (delL[(size_t) i] + dFiltR.lp * 0.42f);
            dLineR.push (delR[(size_t) i] * 0.3f + dFiltL.lp * 0.42f);
            L[i] += eL * 0.8f; R[i] += eR * 0.8f;
            verbL[(size_t) i] += eL * 0.25f; verbR[(size_t) i] += eR * 0.25f; // the echoes sit in the room too
        }
        trackVerb.processStereo (verbL.data(), verbR.data(), n);
        for (int i = 0; i < n; ++i) { L[i] += verbL[(size_t) i] * 0.55f; R[i] += verbR[(size_t) i] * 0.55f; }
    }

    void prepare (int maxBlock)
    {
        scratch.setSize (2, maxBlock * 4 + 64);
        for (auto* v : { &verbL, &verbR, &delL, &delR }) v->assign ((size_t) maxBlock + 16, 0.0f);
        fxRate = 0; // set up on the first block, at the real rate
    }

private:
    struct Filter { lp::dsp::SVF l, r; float lastTone = 99.0f; };
    struct TrackFx { lp::dsp::SVF lpL, lpR, hpL, hpR; lp::dsp::Delay wobL, wobR, chL, chR; float phase = 0, chPhase = 0, lastWarm = -1, lastNost = -1; };

    void setupTrackFx (double sr)
    {
        fxRate = sr;
        for (auto& tf : trackFx)
        {
            tf.wobL.prepare ((int) (0.02 * sr)); tf.wobR.prepare ((int) (0.02 * sr));
            tf.chL.prepare ((int) (0.03 * sr)); tf.chR.prepare ((int) (0.03 * sr));
            tf.lastWarm = tf.lastNost = -1;
        }
        dLineL.prepare ((int) (2.6 * sr)); dLineR.prepare ((int) (2.6 * sr));
        dFiltL.setup (3500.0f, 0.6f, (float) sr); dFiltR.setup (3500.0f, 0.6f, (float) sr);
        trackVerb.setSampleRate (sr);
        juce::Reverb::Parameters rp;
        rp.roomSize = 0.82f; rp.damping = 0.45f; rp.wetLevel = 1.0f; rp.dryLevel = 0.0f; rp.width = 1.0f;
        trackVerb.setParameters (rp);
    }
    std::array<TrackFx, maxTracks> trackFx;
    std::vector<float> verbL, verbR, delL, delR;
    lp::dsp::Delay dLineL, dLineR;
    lp::dsp::SVF dFiltL, dFiltR;
    juce::Reverb trackVerb;
    double fxRate = 0;
    int tailLeft = 0;
    juce::TimeSliceThread thread { "Loopwright studio" };
    std::vector<std::unique_ptr<juce::BufferingAudioReader>> readers;
    std::array<Filter, maxTracks> filters;
    juce::AudioBuffer<float> scratch;
    juce::SpinLock lock;
    std::atomic<juce::int64> lengthSamples { 0 };
};
