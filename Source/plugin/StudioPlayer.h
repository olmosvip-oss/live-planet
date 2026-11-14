#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include "../engine/Dsp.h"
#include "Peak.h"

// Plays a song's tracks (the stem files) with a live mixer: volume, pan, mute, solo and tone.
// Files are streamed from disk in the background, so long songs don't use much memory.
class StudioPlayer
{
public:
    static constexpr int maxTracks = 16;
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
        // you asked for the dry track, even with effects on
        std::atomic<bool> dragDry { false };
        // the track's effects switched off for now (their settings kept), to compare with and without
        std::atomic<bool> fxBypass { false };
        std::atomic<bool> vocal { false }; // a voice (the auto mix makes room for it)
        bool hasFx() const
        {
            return space.load() > 0.001f || echo.load() > 0.001f || warmth.load() > 0.001f || nostalgia.load() > 0.001f
                || chorus.load() > 0.001f || std::abs (tone.load()) > 0.01f;
        }
        // what you drag is what you hear: the effects are recorded in whenever the track has any (unless you chose dry)
        // (the song's feel is added by the processor: see fileToDrag)
        bool printsFx() const { return ! dragDry.load() && ! fxBypass.load() && (printFx.load() || hasFx()); }
    };

    // the mix knobs (chords / melody / bass) as gains for each group; [0] unused
    std::array<std::atomic<float>, 5> groupGain { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };

    StudioPlayer() { thread.startThread (juce::Thread::Priority::normal); }
    ~StudioPlayer() { clear(); thread.stopThread (2000); }

    std::array<Mix, maxTracks> mix;
    // a track is taken out: the mixer settings below it move up a row with their tracks
    void removeTrackMix (int t)
    {
        for (int i = t; i >= 0 && i < maxTracks; ++i)
        {
            auto& d = mix[(size_t) i];
            if (i + 1 < maxTracks)
            {
                const auto& s = mix[(size_t) i + 1];
                d.gainDb = s.gainDb.load(); d.pan = s.pan.load(); d.tone = s.tone.load(); d.mute = s.mute.load(); d.solo = s.solo.load();
                d.sample = s.sample.load(); d.group = s.group.load(); d.space = s.space.load(); d.echo = s.echo.load();
                d.warmth = s.warmth.load(); d.nostalgia = s.nostalgia.load(); d.chorus = s.chorus.load(); d.printFx = s.printFx.load(); d.dragDry = s.dragDry.load(); d.fxBypass = s.fxBypass.load();
            }
            else
            {
                d.gainDb = 0.0f; d.pan = 0.0f; d.tone = 0.0f; d.mute = false; d.solo = false; d.sample = false; d.group = 0;
                d.space = 0.0f; d.echo = 0.0f; d.warmth = 0.0f; d.nostalgia = 0.0f; d.chorus = 0.0f; d.printFx = false; d.dragDry = false; d.fxBypass = false;
            }
        }
    }
    // a deleted track comes back (undo): the rows from t down move one lower, and t starts fresh
    void insertTrackMix (int t)
    {
        if (t < 0 || t >= maxTracks) return;
        for (int i = maxTracks - 1; i > t; --i)
        {
            auto& d = mix[(size_t) i];
            const auto& s = mix[(size_t) i - 1];
            d.gainDb = s.gainDb.load(); d.pan = s.pan.load(); d.tone = s.tone.load(); d.mute = s.mute.load(); d.solo = s.solo.load();
            d.sample = s.sample.load(); d.group = s.group.load(); d.space = s.space.load(); d.echo = s.echo.load();
            d.warmth = s.warmth.load(); d.nostalgia = s.nostalgia.load(); d.chorus = s.chorus.load(); d.printFx = s.printFx.load();
            d.dragDry = s.dragDry.load(); d.fxBypass = s.fxBypass.load();
        }
        auto& d = mix[(size_t) t];
        d.gainDb = 0.0f; d.pan = 0.0f; d.tone = 0.0f; d.mute = false; d.solo = false; d.space = 0.0f; d.echo = 0.0f;
        d.warmth = 0.0f; d.nostalgia = 0.0f; d.chorus = 0.0f; d.printFx = false; d.dragDry = false; d.fxBypass = false;
    }
    std::atomic<int> skipTrack { -1 }; // a track played live instead (the drum editor's loop)
    // what each track is playing right now (its loudest sample since the screen last looked), for the meters
    std::array<std::atomic<float>, maxTracks> peakL {}, peakR {};
    std::atomic<float> meterGain { 1.0f }; // the song's playback level, so the meters read what you hear

    // ---- the pump: everything but the drums ducks under each kick and swells back (sidechain)
    std::atomic<float> pumpDepth { 0.0f };   // 0 off .. 1 deep

    // ---- the song's feel, for the whole song at once (heard live, and in the master, the mix and printed tracks)
    std::atomic<float> bright { 0.0f };      // -1 darker and warmer .. +1 brighter and airier
    std::atomic<float> sparse { 0.0f };      // 0 as written .. 1 the quieter sections thin out to their core
    std::atomic<float> punch { 0.0f };       // 0 .. 1 drums and bass forward
    // the auto mix: the bass ducks under the kick, chords and melodies keep out of the low end and make room for a voice
    std::atomic<bool> autoMix { true };
    // message thread: for each track, which bars it steps back in when the song is made sparse (1 = steps back)
    void setSparseMap (std::vector<std::vector<unsigned char>> map, double songBpm)
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        std::swap (sparseMap, map);
        sparseBpm = songBpm;
    }
    // message thread: where the kicks are (in beats) and the song's tempo
    void setKicks (std::vector<double> beats, double songBpm)
    {
        std::sort (beats.begin(), beats.end());
        const juce::SpinLock::ScopedLockType sl (lock);
        std::swap (kicks, beats);
        kickBpm = songBpm;
    }
    // the pump's gain at a song position (in file samples); 1 = no ducking
    float pumpGain (double pos) const
    {
        const float depth = pumpDepth.load();
        if (depth <= 0.001f || kicks.empty()) return 1.0f;
        const double beat = pos / fileRate * kickBpm / 60.0;
        auto it = std::upper_bound (kicks.begin(), kicks.end(), beat);
        if (it == kicks.begin()) return 1.0f;
        const double since = (beat - *(it - 1)) * 60.0 / kickBpm;          // seconds since the kick
        const double release = 0.5 * 60.0 / kickBpm;                       // back up over an eighth note
        const double attack = 0.004;
        double duck;
        if (since < attack) duck = since / attack;
        else if (since < release) { const double x = (since - attack) / (release - attack); duck = 1.0 - x * x * (3.0 - 2.0 * x); }
        else duck = 0.0;
        return (float) (1.0 - depth * 0.85 * duck);
    }

    // the auto mix: the bass dips about 4 dB for a moment under each kick, so the two don't fight
    float kickDuck (double pos) const
    {
        const double beat = pos / fileRate * kickBpm / 60.0;
        auto it = std::upper_bound (kicks.begin(), kicks.end(), beat);
        if (it == kicks.begin()) return 1.0f;
        const double since = (beat - *(it - 1)) * 60.0 / kickBpm;
        if (since > 0.11) return 1.0f;
        const double d = since < 0.003 ? since / 0.003 : 1.0 - (since - 0.003) / 0.107;
        return (float) (1.0 - 0.37 * d);
    }

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
    static juce::AudioBuffer<float> bounce (const juce::Array<juce::File>& stems, const std::array<Mix, maxTracks>& mixes, double bpm,
                                            const StudioPlayer* pumpFrom = nullptr)
    {
        juce::ScopedNoDenormals noDenormals;
        StudioPlayer p;
        p.load (stems, 5000);
        if (pumpFrom != nullptr) p.copyPump (*pumpFrom);
        const auto len = p.length();
        if (len <= 0) return {};
        for (size_t t = 0; t < maxTracks; ++t)
        {
            auto& d = p.mix[t]; const auto& m = mixes[t];
            d.gainDb = m.gainDb.load(); d.pan = m.pan.load(); d.tone = m.tone.load(); d.mute = m.mute.load(); d.solo = m.solo.load();
            d.space = m.space.load(); d.echo = m.echo.load(); d.warmth = m.warmth.load(); d.nostalgia = m.nostalgia.load(); d.chorus = m.chorus.load();
            d.group = m.group.load(); d.fxBypass = m.fxBypass.load(); d.vocal = m.vocal.load();
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
    static bool printTrack (const juce::File& stem, const Mix& m, double bpm, const juce::File& out, const StudioPlayer* pumpFrom = nullptr, int trackIndex = -1)
    {
        juce::ScopedNoDenormals noDenormals;
        StudioPlayer p;
        p.load ({ stem }, 5000);
        p.copyRow = trackIndex;
        if (pumpFrom != nullptr) p.copyPump (*pumpFrom);
        const auto len = p.length();
        if (len <= 0) return false;
        auto& d = p.mix[0];
        d.gainDb = m.gainDb.load(); d.pan = m.pan.load(); d.tone = m.tone.load();
        d.space = m.space.load(); d.echo = m.echo.load(); d.warmth = m.warmth.load();
        d.nostalgia = m.nostalgia.load(); d.chorus = m.chorus.load();
        d.group = m.group.load(); d.fxBypass = m.fxBypass.load(); d.vocal = m.vocal.load();
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
            const bool byp = m.fxBypass.load(); // bypassed: heard dry (volume and pan stay)
            const float tone = byp ? 0.0f : m.tone.load();
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
            const float warm = byp ? 0.0f : m.warmth.load(), nost = byp ? 0.0f : m.nostalgia.load();
            const float sp = byp ? 0.0f : m.space.load(), ec = byp ? 0.0f : m.echo.load();
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
            const float cho = byp ? 0.0f : m.chorus.load();
            const bool inserts = warm > 0.002f || nost > 0.002f || cho > 0.002f;
            float pkL = 0.0f, pkR = 0.0f;
            const bool pumped = grp != 4 && pumpDepth.load() > 0.001f && ! kicks.empty();
            // ---- the song's feel and the auto mix
            auto& mf = macroFx[t];
            const bool am = autoMix.load();
            const float br = bright.load();
            const bool melodic = (grp == 1 || grp == 2) && ! m.vocal.load();
            const bool roomForVoice = am && melodic && anyVocal;
            if (std::abs (br - mf.lastBright) > 0.002f || am != mf.lastAuto || roomForVoice != mf.lastRoom || sampleRate != mf.rate)
            {
                mf.lastBright = br; mf.lastAuto = am; mf.lastRoom = roomForVoice; mf.rate = sampleRate;
                const bool tiltOn = std::abs (br) > 0.01f;
                mf.tilt = tiltOn;
                if (tiltOn)
                {
                    { const auto c = BQ::make (2, sampleRate, 3800.0, 0.7, br * 5.0); mf.hs[0].take (c); mf.hs[1].take (c); }   // air up (or down)
                    { const auto c = BQ::make (1, sampleRate, 160.0, 0.7, -br * 2.5); mf.ls[0].take (c); mf.ls[1].take (c); }   // and the lows the other way
                }
                mf.clean = am && melodic;   // chords and melodies: out of the bass's way
                if (mf.clean) { const auto c = BQ::make (3, sampleRate, 95.0, 0.6, 0.0); mf.hp[0].take (c); mf.hp[1].take (c); }
                mf.room = roomForVoice;     // and a little dip where a voice lives
                if (mf.room) { const auto c = BQ::make (0, sampleRate, 2600.0, 0.9, -2.5); mf.pk[0].take (c); mf.pk[1].take (c); }
            }
            // punch: drums and bass forward, the rest a touch back
            const float pu = punch.load();
            const float punchG = grp == 4 ? 1.0f + 0.45f * pu : grp == 3 ? 1.0f + 0.22f * pu : 1.0f - 0.12f * pu;
            // sparse: in the quieter sections, the extra layers step back (a smooth fade, never a click)
            float sparseTarget = 1.0f;
            {
                const float sv = sparse.load();
                if (sv > 0.001f && t < sparseMap.size() && ! sparseMap[t].empty())
                {
                    const int bar = (int) (pos / fileRate * sparseBpm / 240.0);
                    if (bar >= 0 && bar < (int) sparseMap[t].size() && sparseMap[t][(size_t) bar]) sparseTarget = 1.0f - sv;
                }
            }
            const float sparseStep = (sparseTarget - mf.sparseG) / (float) std::max (1, n);
            const bool bassDuck = am && grp == 3 && ! kicks.empty();
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
                if (pumped) { const float pg = pumpGain (pos + i * inc); xl *= pg; xr *= pg; }
                if (mf.tilt) { xl = mf.hs[0].process (mf.ls[0].process (xl)); xr = mf.hs[1].process (mf.ls[1].process (xr)); }
                if (mf.clean) { xl = mf.hp[0].process (xl); xr = mf.hp[1].process (xr); }
                if (mf.room) { xl = mf.pk[0].process (xl); xr = mf.pk[1].process (xr); }
                if (bassDuck) { const float dg = kickDuck (pos + i * inc); xl *= dg; xr *= dg; }
                mf.sparseG += sparseStep;
                const float mg2 = punchG * mf.sparseG;
                xl *= mg2; xr *= mg2;
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

    void copyPump (const StudioPlayer& o)
    {
        std::vector<double> k;
        double b;
        { const juce::SpinLock::ScopedLockType sl (o.lock); k = o.kicks; b = o.kickBpm; }
        setKicks (std::move (k), b);
        pumpDepth = o.pumpDepth.load();
        bright = o.bright.load(); sparse = o.sparse.load(); punch = o.punch.load(); autoMix = o.autoMix.load();
        std::vector<std::vector<unsigned char>> m;
        double sb;
        { const juce::SpinLock::ScopedLockType sl (o.lock); m = o.sparseMap; sb = o.sparseBpm; anyVocal = o.anyVocal; }
        // a print plays one track as track 0: it takes that track's row
        if (copyRow >= 0) m = { copyRow < (int) m.size() ? m[(size_t) copyRow] : std::vector<unsigned char> {} };
        setSparseMap (std::move (m), sb);
    }
    int copyRow = -1;         // set before copyPump for a print of one track
    bool anyVocal = false;    // the song has a voice in it (audio thread reads; set with the mixer)
    void setAnyVocal (bool v) { const juce::SpinLock::ScopedLockType sl (lock); anyVocal = v; }

private:
    std::vector<double> kicks;
    double kickBpm = 120.0;
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
    // a plain biquad (RBJ cookbook): 0 peak, 1 low shelf, 2 high shelf, 3 high-pass
    struct BQ
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        inline float process (float x) { const float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
        // new settings, keeping what's ringing in it (so turning a knob doesn't click)
        void take (const BQ& o) { b0 = o.b0; b1 = o.b1; b2 = o.b2; a1 = o.a1; a2 = o.a2; }
        static BQ make (int type, double sr, double f, double q, double db)
        {
            BQ b;
            const double A = std::pow (10.0, db / 40.0), w = 2.0 * juce::MathConstants<double>::pi * f / sr;
            const double cw = std::cos (w), sw = std::sin (w), alpha = sw / (2.0 * q);
            double b0, b1, b2, a0, a1, a2;
            if (type == 0) { b0 = 1 + alpha * A; b1 = -2 * cw; b2 = 1 - alpha * A; a0 = 1 + alpha / A; a1 = -2 * cw; a2 = 1 - alpha / A; }
            else if (type == 1) { const double sq = 2 * std::sqrt (A) * alpha;
                b0 = A * ((A + 1) - (A - 1) * cw + sq); b1 = 2 * A * ((A - 1) - (A + 1) * cw); b2 = A * ((A + 1) - (A - 1) * cw - sq);
                a0 = (A + 1) + (A - 1) * cw + sq; a1 = -2 * ((A - 1) + (A + 1) * cw); a2 = (A + 1) + (A - 1) * cw - sq; }
            else if (type == 2) { const double sq = 2 * std::sqrt (A) * alpha;
                b0 = A * ((A + 1) + (A - 1) * cw + sq); b1 = -2 * A * ((A - 1) + (A + 1) * cw); b2 = A * ((A + 1) + (A - 1) * cw - sq);
                a0 = (A + 1) - (A - 1) * cw + sq; a1 = 2 * ((A - 1) - (A + 1) * cw); a2 = (A + 1) - (A - 1) * cw - sq; }
            else { b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = (1 + cw) / 2; a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; }
            b.b0 = (float) (b0 / a0); b.b1 = (float) (b1 / a0); b.b2 = (float) (b2 / a0); b.a1 = (float) (a1 / a0); b.a2 = (float) (a2 / a0);
            return b;
        }
    };
    struct MacroFx { BQ hs[2], ls[2], hp[2], pk[2]; float lastBright = 99.0f, sparseG = 1.0f; bool lastAuto = false, lastRoom = false, tilt = false, clean = false, room = false; double rate = 0; };
    std::array<MacroFx, maxTracks> macroFx;
    std::vector<std::vector<unsigned char>> sparseMap;
    double sparseBpm = 120.0;
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
    mutable juce::SpinLock lock;
    std::atomic<juce::int64> lengthSamples { 0 };
};
