#pragma once
// GRAINS: a granular instrument. Any sound is loaded, cut into 8 or 16 equal chops, and played as a cloud of tiny,
// overlapping slices ("grains") while a note is held:
//   hold  the read point stays where position puts it (move it to scan through the sound)
//   step  a sequence of cells, one per step in time with the song, each playing its own chop (cells can be off)
//   keys  C3 and up play one chop each, like a sampler
// Each grain is read with 4-point interpolation and shaped by a smooth window, grains are sprayed a little in
// time and place (no metallic ringing when they pile up), their level is kept the same at any density, and they
// are spread across the stereo field. Then a high-pass and low-pass, the chaos wash (a reverb) and the output.
// Real-time safe: nothing is allocated or locked (beyond a try-lock) on the audio thread.
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <cmath>
#include <memory>
#include <vector>

namespace lp::grains
{
constexpr int maxChops = 16, maxGrains = 384, maxVoices = 16;
enum Mode { Hold = 0, Step, Keys };
enum Env { Gaussian = 0, Hann, Triangle, Trapezoid, Square, numEnvs };
enum Knob { Rate = 0, Length, Position, Space, Chaos, Tune, numKnobs }; // (each chop can have its own of these)

inline const char* envName (int e)
{
    static const char* n[] = { "gaussian", "hann", "triangle", "trapezoid", "square" };
    return n[juce::jlimit (0, numEnvs - 1, e)];
}

// the musical divisions the grains (synced) and the step clock run at, in beats
inline int numDivs() { return 17; }
inline double divBeats (int i)
{
    static const double d[] = { 1.0 / 12, 0.125, 1.0 / 6, 0.1875, 0.25, 1.0 / 3, 0.375, 0.5, 2.0 / 3, 0.75, 1.0, 4.0 / 3, 1.5, 2.0, 3.0, 4.0, 8.0 };
    return d[juce::jlimit (0, 16, i)];
}
inline const char* divName (int i)
{
    static const char* n[] = { "1/32T", "1/32", "1/16T", "1/32D", "1/16", "1/8T", "1/16D", "1/8", "1/4T", "1/8D", "1/4", "1/2T", "1/4D", "1/2", "1/2D", "1", "2" };
    return n[juce::jlimit (0, 16, i)];
}
// the rate knob (0..1): synced, a division (fast at the top); free, 1 to 100 grains a second
inline int rateDiv (float v) { return 16 - juce::jlimit (0, 16, (int) std::lround (v * 16.0f)); }
inline double rateHz (float v) { return std::pow (100.0, (double) juce::jlimit (0.0f, 1.0f, v)); }

// everything you set (plain values: copied whole to the audio thread, and saved)
struct Settings
{
    int mode = Step;
    bool sync = true;
    std::array<float, numKnobs> knob { 0.75f, 120.0f, 0.0f, 0.3f, 0.15f, 0.0f }; // rate 0..1, ms, 0..1, 0..1, 0..1, semitones
    std::array<bool, numKnobs> link { true, true, true, true, true, true };
    std::array<std::array<float, numKnobs>, maxChops> chop {};                   // (a chop's own, used where unlinked)
    int chops = 8;
    int clock = 4;                                                               // the step clock (a division: 1/16)
    std::array<bool, maxChops> cellOn {};
    std::array<int, maxChops> cellChop {};
    int env = Hann;
    bool reverse = false, choke = false;
    bool hpOn = false, lpOn = false;
    float hpHz = 120.0f, lpHz = 9000.0f, hpRes = 0.15f, lpRes = 0.15f, drive = 0.0f, filterMix = 1.0f;
    float outputDb = 0.0f;
    float trimStart = 0.0f, trimEnd = 1.0f;

    Settings()
    {
        for (int c = 0; c < maxChops; ++c) { chop[(size_t) c] = knob; cellOn[(size_t) c] = true; cellChop[(size_t) c] = c; }
    }
    // what a knob is for a chop (its own, if that knob is unlinked)
    float value (int k, int c) const { return link[(size_t) k] || c < 0 ? knob[(size_t) k] : chop[(size_t) juce::jlimit (0, maxChops - 1, c)][(size_t) k]; }
    // a knob turned: every chop (linked), or the picked one
    void set (int k, int c, float v)
    {
        if (link[(size_t) k] || c < 0) { knob[(size_t) k] = v; for (auto& ch : chop) ch[(size_t) k] = v; }
        else chop[(size_t) juce::jlimit (0, maxChops - 1, c)][(size_t) k] = v;
    }
    void resetCells() { for (int c = 0; c < maxChops; ++c) { cellOn[(size_t) c] = true; cellChop[(size_t) c] = c; } }
};

// a loaded sound
struct Source
{
    juce::AudioBuffer<float> audio; // stereo
    double sampleRate = 44100.0;
    juce::String name, path;
    std::vector<float> peaks;       // for drawing (1024 columns, 0..1)
    double bpm = 0.0;               // from its name, when it says
    juce::String key;
};

inline std::shared_ptr<Source> load (const juce::File& f)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (f));
    if (rd == nullptr || rd->lengthInSamples < 64) return nullptr;
    auto s = std::make_shared<Source>();
    const int n = (int) std::min<juce::int64> (rd->lengthInSamples, (juce::int64) (rd->sampleRate * 600.0)); // (10 minutes at most)
    s->audio.setSize (2, n);
    rd->read (&s->audio, 0, n, 0, true, true);
    if (rd->numChannels < 2) s->audio.copyFrom (1, 0, s->audio, 0, 0, n);
    s->sampleRate = rd->sampleRate;
    s->name = f.getFileNameWithoutExtension();
    s->path = f.getFullPathName();
    s->peaks.assign (1024, 0.0f);
    float top = 0.0f;
    for (size_t c = 0; c < s->peaks.size(); ++c)
    {
        const int a = (int) ((juce::int64) n * (juce::int64) c / 1024), b = std::max (a + 1, (int) ((juce::int64) n * (juce::int64) (c + 1) / 1024));
        float m = 0.0f;
        for (int i = a; i < b && i < n; i += 2) m = std::max ({ m, std::abs (s->audio.getSample (0, i)), std::abs (s->audio.getSample (1, i)) });
        s->peaks[c] = m;
        top = std::max (top, m);
    }
    if (top > 0.0f) for (auto& v : s->peaks) v /= top;
    // its tempo and key, when its name says ("vocal chop 01 122bpm & a# minor")
    const auto nm = s->name.toLowerCase();
    for (int i = nm.indexOf ("bpm"); i > 0; i = nm.indexOf (i + 3, "bpm"))
    {
        int a = i;
        while (a > 0 && (juce::CharacterFunctions::isDigit (nm[a - 1]) || nm[a - 1] == ' ' || nm[a - 1] == '_')) --a;
        const double v = nm.substring (a, i).trim().getDoubleValue();
        if (v >= 50.0 && v <= 220.0) { s->bpm = v; break; }
    }
    static const char* keys[] = { "c#", "db", "d#", "eb", "f#", "gb", "g#", "ab", "a#", "bb", "c", "d", "e", "f", "g", "a", "b" };
    auto toks = juce::StringArray::fromTokens (nm.replace ("&", " "), " _-", "");
    for (int i = 0; i < toks.size() && s->key.isEmpty(); ++i)
    {
        auto t = toks[i];
        for (auto* k : keys)
        {
            const juce::String ks (k);
            if (t == ks || t == ks + "m" || t == ks + "min" || t == ks + "maj" || t == ks + "minor" || t == ks + "major")
            {
                const bool minor = t.endsWith ("m") || t.endsWith ("min") || t.endsWith ("minor") || toks[i + 1].startsWith ("min");
                s->key = ks.substring (0, 1).toUpperCase() + ks.substring (1) + (minor ? " minor" : (t.endsWith ("maj") || t.endsWith ("major") || toks[i + 1].startsWith ("maj") ? " major" : ""));
                break;
            }
        }
    }
    return s;
}

struct Transport
{
    bool playing = false;  // the song (or your DAW) is running: grains and steps follow its beat
    double ppq = 0.0;      // where it is, in beats, at the start of the block
    double bpm = 120.0;
};

class Synth
{
public:
    Synth()
    {
        for (int e = 0; e < 3; ++e)
            for (int i = 0; i < tableSize; ++i)
            {
                const double x = (double) i / (tableSize - 1);
                double v = 0.0;
                if (e == Gaussian) { const double g = std::exp (-0.5 * std::pow ((x - 0.5) / 0.16, 2.0)), edge = std::exp (-0.5 * std::pow (0.5 / 0.16, 2.0)); v = (g - edge) / (1.0 - edge); }
                if (e == Hann) v = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * x);
                if (e == Triangle) v = 1.0 - std::abs (2.0 * x - 1.0);
                tables[(size_t) e][(size_t) i] = (float) std::max (0.0, v);
            }
        for (auto& a : activity) a = -1.0f;
    }

    void prepare (double sampleRate, int blockSize)
    {
        sr = sampleRate;
        reverb.setSampleRate (sampleRate);
        wetL.assign ((size_t) std::max (16, blockSize) * 2, 0.0f);
        wetR.assign (wetL.size(), 0.0f);
        for (auto& f : filt) f.reset();
        for (auto& g : grains) g.on = false;
        for (auto& v : voices) v.on = false;
        coeffKey = -1.0f;
    }

    // ---- from the message thread
    void setSource (std::shared_ptr<Source> s)
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        keep.push_back (s);
        while (keep.size() > 3) keep.erase (keep.begin()); // (the one the audio thread may still hold stays alive)
        pendingSource = s.get();
        sourceChanged = true;
    }
    void setSettings (const Settings& s)
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        pending = s;
        settingsChanged = true;
    }
    // a note from the screen (your mouse or computer keys): played in the next block
    void uiNote (int note, float velocity, bool on)
    {
        int s1, n1, s2, n2;
        uiFifo.prepareToWrite (1, s1, n1, s2, n2);
        if (n1 > 0) uiEvents[(size_t) s1] = { note, velocity, on };
        else if (n2 > 0) uiEvents[(size_t) s2] = { note, velocity, on };
        uiFifo.finishedWrite (n1 + n2);
    }
    void allOff() { for (int n = 0; n < 128; ++n) uiNote (n, 0.0f, false); }

    // ---- the audio thread (or an offline render)
    void noteOn (int note, float vel)
    {
        if (st.choke) for (auto& v : voices) v.on = false;
        Voice* v = nullptr;
        for (auto& x : voices) if (x.on && x.note == note) v = &x;
        if (v == nullptr) for (auto& x : voices) if (! x.on) { v = &x; break; }
        if (v == nullptr) v = &voices[0];
        v->on = true; v->note = note; v->vel = juce::jlimit (0.05f, 1.0f, vel);
        v->countdown = 0.0; v->freeClock = 0.0;
        v->age = 0;
        heldCount = countHeld();
        if (! clockRunning) { internalPpq = 0.0; clockRunning = true; }
    }
    void noteOff (int note)
    {
        for (auto& v : voices) if (v.on && v.note == note) v.on = false;
        heldCount = countHeld();
        if (heldCount == 0) clockRunning = false;
    }

    // adds into L / R
    void process (float* L, float* R, int n, const Transport& t)
    {
        {
            const juce::SpinLock::ScopedTryLockType tl (lock);
            if (tl.isLocked())
            {
                if (settingsChanged) { st = pending; settingsChanged = false; }
                if (sourceChanged) { src = pendingSource; sourceChanged = false; for (auto& g : grains) g.on = false; }
            }
        }
        {
            int s1, n1, s2, n2;
            uiFifo.prepareToRead (uiFifo.getNumReady(), s1, n1, s2, n2);
            for (int i = 0; i < n1; ++i) { auto& e = uiEvents[(size_t) (s1 + i)]; e.on ? noteOn (e.note, e.vel) : noteOff (e.note); }
            for (int i = 0; i < n2; ++i) { auto& e = uiEvents[(size_t) (s2 + i)]; e.on ? noteOn (e.note, e.vel) : noteOff (e.note); }
            uiFifo.finishedRead (n1 + n2);
        }
        if (src == nullptr || n <= 0) { publishIdle(); return; }
        if ((size_t) n * 2 > wetL.size()) n = (int) wetL.size() / 2; // (never more than prepared for)
        const int len = src->audio.getNumSamples();
        const float* sl = src->audio.getReadPointer (0);
        const float* sr2 = src->audio.getReadPointer (1);
        const double rateRatio = src->sampleRate / sr;
        const double bpm = t.bpm > 1.0 ? t.bpm : 120.0;
        const double beatsPerSample = bpm / 60.0 / sr;
        const double ppq0 = t.playing ? t.ppq : internalPpq;
        const int a0 = (int) (juce::jlimit (0.0f, 0.99f, st.trimStart) * (float) len);
        const int a1 = std::max (a0 + 64, (int) (juce::jlimit (0.01f, 1.0f, st.trimEnd) * (float) len));
        const int chops = st.chops == 16 ? 16 : 8;
        const double chopLen = (double) (a1 - a0) / chops;

        // the step playing now
        const double clockB = divBeats (st.clock);
        const int stepNow = (int) std::floor (ppq0 / clockB) % chops;
        currentStep = st.mode == Step && heldCount > 0 ? stepNow : -1;

        std::fill (wetL.begin(), wetL.begin() + n, 0.0f);
        std::fill (wetR.begin(), wetR.begin() + n, 0.0f);

        // ---- new grains, sample-accurately
        for (auto& v : voices)
        {
            if (! v.on) continue;
            for (int i = 0; i < n; ++i)
            {
                const double ppq = ppq0 + i * beatsPerSample;
                // which chop this voice reads, and with which values
                int chop = -1;
                bool silent = false;
                if (st.mode == Step)
                {
                    const int s = (int) std::floor (ppq / clockB) % chops;
                    silent = ! st.cellOn[(size_t) s];
                    chop = juce::jlimit (0, chops - 1, st.cellChop[(size_t) s]);
                }
                else if (st.mode == Keys)
                {
                    chop = v.note - 60;
                    if (chop < 0 || chop >= chops) { silent = true; chop = 0; }
                }
                const float rateV = st.value (Rate, chop);
                bool fire = false;
                double interval; // (in samples)
                if (st.sync)
                {
                    const double div = divBeats (rateDiv (rateV));
                    interval = div / beatsPerSample;
                    const double prev = ppq - beatsPerSample;
                    fire = std::floor (ppq / div) != std::floor (prev / div) || v.age == 0;
                }
                else
                {
                    interval = sr / rateHz (rateV);
                    v.countdown -= 1.0;
                    if (v.countdown <= 0.0) { fire = true; v.countdown += interval * (1.0 + (rng.nextFloat() - 0.5f) * 0.3f); } // (a little loose: no buzz)
                }
                ++v.age;
                if (! fire || silent) continue;
                spawn (v, chop, i, interval, a0, a1, chopLen, rateRatio, len);
            }
        }

        // ---- the grains
        int shown = 0;
        float activeLevel = 0.0f;
        for (auto& g : grains)
        {
            if (! g.on) continue;
            for (int i = g.delay; i < n; ++i)
            {
                if (g.age >= g.len) { g.on = false; break; }
                const float w = window (g, (float) g.age / (float) g.len);
                const double p = g.pos;
                const int i0 = (int) std::floor (p);
                if (i0 < 1 || i0 + 2 >= len) { g.on = false; break; } // (ran off the end of the sound)
                const float fr = (float) (p - i0);
                const float l = hermite (sl[i0 - 1], sl[i0], sl[i0 + 1], sl[i0 + 2], fr);
                const float r = hermite (sr2[i0 - 1], sr2[i0], sr2[i0 + 1], sr2[i0 + 2], fr);
                wetL[(size_t) i] += l * w * g.gL;
                wetR[(size_t) i] += r * w * g.gR;
                g.pos += g.inc;
                ++g.age;
            }
            g.delay = 0;
            if (g.on)
            {
                if (shown < (int) activity.size()) { activity[(size_t) shown] = (float) (g.pos / len); activityAmp[(size_t) shown] = window (g, (float) g.age / (float) g.len) * (g.gL + g.gR) * 0.5f / std::max (1e-6f, g.norm); ++shown; }
                activeLevel += 1.0f;
            }
        }
        for (int k = shown; k < (int) activity.size(); ++k) activity[(size_t) k] = -1.0f;
        activeGrains = (int) activeLevel;

        // ---- the filter: a high-pass and a low-pass, a little drive, mixed with the unfiltered
        updateFilters();
        if (st.hpOn || st.lpOn || st.drive > 0.01f)
        {
            const float mix = juce::jlimit (0.0f, 1.0f, st.filterMix);
            const float drv = 1.0f + st.drive * 5.0f, comp = 1.0f / std::tanh (drv);
            for (int i = 0; i < n; ++i)
            {
                float l = wetL[(size_t) i], r = wetR[(size_t) i];
                if (st.hpOn) { l = filt[0].processSingleSampleRaw (l); r = filt[1].processSingleSampleRaw (r); }
                if (st.lpOn) { l = filt[2].processSingleSampleRaw (l); r = filt[3].processSingleSampleRaw (r); }
                if (st.drive > 0.01f) { l = std::tanh (l * drv) * comp; r = std::tanh (r * drv) * comp; }
                wetL[(size_t) i] = wetL[(size_t) i] * (1.0f - mix) + l * mix;
                wetR[(size_t) i] = wetR[(size_t) i] * (1.0f - mix) + r * mix;
            }
        }
        // ---- chaos: the grains washed into a space (a little air, up to a dense, moving wash)
        const float chaos = st.knob[Chaos];
        if (chaos > 0.01f)
        {
            juce::Reverb::Parameters rp;
            rp.roomSize = 0.55f + 0.43f * chaos;
            rp.damping = 0.45f - 0.25f * chaos;
            rp.wetLevel = 0.55f * chaos;
            rp.dryLevel = 1.0f - 0.35f * chaos;
            rp.width = 1.0f;
            reverb.setParameters (rp);
            reverb.processStereo (wetL.data(), wetR.data(), n);
        }
        // ---- out (smoothly)
        const float target = juce::Decibels::decibelsToGain (st.outputDb);
        float pk = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            outGain += (target - outGain) * 0.002f;
            const float l = wetL[(size_t) i] * outGain, r = wetR[(size_t) i] * outGain;
            L[i] += l; R[i] += r;
            pk = std::max ({ pk, std::abs (l), std::abs (r) });
        }
        meter = std::max (pk, meter.load() * 0.85f);
        if (! t.playing && clockRunning) internalPpq += n * beatsPerSample;
    }

    bool busy() const { return heldCount > 0 || activeGrains.load() > 0; }

    // for the screen
    std::array<std::atomic<float>, 96> activity {}, activityAmp {}; // where the grains are reading (0..1 of the sound; -1 none)
    std::atomic<int> activeGrains { 0 }, currentStep { -1 };
    std::atomic<float> meter { 0.0f };
    const Source* audioSource() const { return src; }

private:
    struct Grain { bool on = false; double pos = 0, inc = 1; int len = 0, age = 0, delay = 0, env = 1; float gL = 0, gR = 0, norm = 1, ramp = 0.1f; };
    struct Voice { bool on = false; int note = 60; float vel = 1; double countdown = 0, freeClock = 0; juce::int64 age = 0; };
    struct UiEvent { int note = 60; float vel = 0; bool on = false; };
    static constexpr int tableSize = 1024;

    static float hermite (float y0, float y1, float y2, float y3, float x)
    {
        const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * x + c2) * x + c1) * x + y1;
    }
    float window (const Grain& g, float x) const
    {
        x = juce::jlimit (0.0f, 1.0f, x);
        if (g.env == Trapezoid || g.env == Square)
            return std::min ({ 1.0f, x / g.ramp, (1.0f - x) / g.ramp });
        const float f = x * (tableSize - 1);
        const int i = std::min (tableSize - 2, (int) f);
        const auto& t = tables[(size_t) g.env];
        return t[(size_t) i] + (t[(size_t) i + 1] - t[(size_t) i]) * (f - (float) i);
    }
    int countHeld() const { int c = 0; for (auto& v : voices) c += v.on ? 1 : 0; return c; }

    void spawn (Voice& v, int chop, int offset, double interval, int a0, int a1, double chopLen, double rateRatio, int len)
    {
        Grain* g = nullptr;
        for (auto& x : grains) if (! x.on) { g = &x; break; }
        if (g == nullptr) return; // (all busy: this one is skipped, the rest keep their level)
        const double lenMs = juce::jlimit (10.0, 1000.0, (double) st.value (Length, chop));
        const int glen = std::max (32, (int) (lenMs * 0.001 * sr));
        const float pos = juce::jlimit (0.0f, 1.0f, st.value (Position, chop));
        const float chaos = juce::jlimit (0.0f, 1.0f, st.value (Chaos, chop));
        const float space = juce::jlimit (0.0f, 1.0f, st.value (Space, chop));
        const double semis = st.value (Tune, chop) + (st.mode == Keys ? 0.0 : (double) (v.note - 60));
        // where it reads: hold scans the whole (trimmed) sound, step and keys their chop
        double start = st.mode == Hold || chop < 0 ? a0 + pos * (a1 - a0) : a0 + (chop + pos) * chopLen;
        // a little spray (time and place), more with chaos: piled-up grains never ring
        const double spray = std::min (glen * rateRatio * 0.3, 0.03 * sr * rateRatio) + chaos * (st.mode == Hold ? 0.08 * (a1 - a0) : 0.5 * chopLen);
        start += (rng.nextDouble() - 0.5) * 2.0 * spray;
        const double cents = chaos * 22.0 * (rng.nextDouble() * 2.0 - 1.0);
        const double inc = rateRatio * std::pow (2.0, (semis + cents / 100.0) / 12.0) * (st.reverse ? -1.0 : 1.0);
        if (st.reverse) start += glen * std::abs (inc);
        start = juce::jlimit (2.0, (double) len - 4.0, start);
        // the same level at any density: grains overlapping k deep are each 1/sqrt(k) as loud
        // (each shape's average power: how much of a grain is at full level)
        const double power = st.env == Square ? 0.97 : st.env == Trapezoid ? 0.84 : st.env == Hann ? 0.375 : st.env == Triangle ? 0.333 : 0.27;
        const double overlap = std::max (1.0, (double) glen / std::max (1.0, interval) * power);
        const float norm = (float) (1.0 / std::sqrt (overlap)) * v.vel * 0.9f;
        const float pan = space * (rng.nextFloat() * 2.0f - 1.0f);
        const float ang = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        g->on = true;
        g->pos = start; g->inc = inc; g->len = glen; g->age = 0; g->delay = offset; g->env = st.env;
        g->gL = norm * std::cos (ang) * 1.4142f; g->gR = norm * std::sin (ang) * 1.4142f; g->norm = norm;
        g->ramp = st.env == Trapezoid ? 0.12f : juce::jlimit (0.002f, 0.5f, (float) (0.0015 * sr / glen)); // (square: 1.5 ms edges, no clicks)
    }

    void updateFilters()
    {
        const float key = st.hpHz * 3.1f + st.lpHz * 0.7f + st.hpRes * 17.0f + st.lpRes * 29.0f + (st.hpOn ? 1 : 0) + (st.lpOn ? 2 : 0);
        if (key == coeffKey) return;
        coeffKey = key;
        const double nyq = sr * 0.45;
        const auto hp = juce::IIRCoefficients::makeHighPass (sr, juce::jlimit (20.0, nyq, (double) st.hpHz), 0.707 + st.hpRes * 6.0);
        const auto lp = juce::IIRCoefficients::makeLowPass (sr, juce::jlimit (40.0, nyq, (double) st.lpHz), 0.707 + st.lpRes * 6.0);
        filt[0].setCoefficients (hp); filt[1].setCoefficients (hp);
        filt[2].setCoefficients (lp); filt[3].setCoefficients (lp);
    }
    void publishIdle()
    {
        for (auto& a : activity) a = -1.0f;
        activeGrains = 0;
        currentStep = -1;
        meter = meter.load() * 0.85f;
    }

    double sr = 44100.0;
    Settings st, pending;
    const Source* src = nullptr;
    const Source* pendingSource = nullptr;
    std::vector<std::shared_ptr<Source>> keep;
    bool settingsChanged = false, sourceChanged = false;
    juce::SpinLock lock;
    std::array<Grain, maxGrains> grains {};
    std::array<Voice, maxVoices> voices {};
    std::array<std::array<float, tableSize>, 3> tables {};
    std::vector<float> wetL, wetR;
    std::array<juce::IIRFilter, 4> filt;
    float coeffKey = -1.0f;
    juce::Reverb reverb;
    juce::Random rng { 1234 };
    float outGain = 1.0f;
    int heldCount = 0;
    bool clockRunning = false;
    double internalPpq = 0.0;
    juce::AbstractFifo uiFifo { 256 };
    std::array<UiEvent, 256> uiEvents {};
};

// Settings <-> a var (saved with your project, and as presets)
inline juce::var toVar (const Settings& s)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("mode", s.mode); o->setProperty ("sync", s.sync); o->setProperty ("chops", s.chops); o->setProperty ("clock", s.clock);
    juce::Array<juce::var> k, l, cells, ch;
    for (int i = 0; i < numKnobs; ++i) { k.add (s.knob[(size_t) i]); l.add (s.link[(size_t) i]); }
    for (int c = 0; c < maxChops; ++c)
    {
        cells.add (juce::Array<juce::var> { s.cellOn[(size_t) c], s.cellChop[(size_t) c] });
        juce::Array<juce::var> one;
        for (int i = 0; i < numKnobs; ++i) one.add (s.chop[(size_t) c][(size_t) i]);
        ch.add (one);
    }
    o->setProperty ("knob", k); o->setProperty ("link", l); o->setProperty ("cells", cells); o->setProperty ("chop", ch);
    o->setProperty ("env", s.env); o->setProperty ("reverse", s.reverse); o->setProperty ("choke", s.choke);
    o->setProperty ("hpOn", s.hpOn); o->setProperty ("lpOn", s.lpOn); o->setProperty ("hpHz", s.hpHz); o->setProperty ("lpHz", s.lpHz);
    o->setProperty ("hpRes", s.hpRes); o->setProperty ("lpRes", s.lpRes); o->setProperty ("drive", s.drive); o->setProperty ("filterMix", s.filterMix);
    o->setProperty ("outputDb", s.outputDb); o->setProperty ("trimStart", s.trimStart); o->setProperty ("trimEnd", s.trimEnd);
    return juce::var (o);
}
inline Settings fromVar (const juce::var& v)
{
    Settings s;
    if (! v.isObject()) return s;
    s.mode = juce::jlimit (0, 2, (int) v.getProperty ("mode", s.mode));
    s.sync = (bool) v.getProperty ("sync", s.sync);
    s.chops = (int) v.getProperty ("chops", 8) == 16 ? 16 : 8;
    s.clock = juce::jlimit (0, 16, (int) v.getProperty ("clock", s.clock));
    if (auto* a = v["knob"].getArray()) for (int i = 0; i < std::min ((int) numKnobs, a->size()); ++i) s.knob[(size_t) i] = (float) (*a)[i];
    if (auto* a = v["link"].getArray()) for (int i = 0; i < std::min ((int) numKnobs, a->size()); ++i) s.link[(size_t) i] = (bool) (*a)[i];
    for (auto& c : s.chop) c = s.knob;
    if (auto* a = v["cells"].getArray()) for (int c = 0; c < std::min (maxChops, a->size()); ++c) { s.cellOn[(size_t) c] = (bool) (*a)[c][0]; s.cellChop[(size_t) c] = (int) (*a)[c][1]; }
    if (auto* a = v["chop"].getArray())
        for (int c = 0; c < std::min (maxChops, a->size()); ++c)
            if (auto* one = (*a)[c].getArray()) for (int i = 0; i < std::min ((int) numKnobs, one->size()); ++i) s.chop[(size_t) c][(size_t) i] = (float) (*one)[i];
    s.env = juce::jlimit (0, numEnvs - 1, (int) v.getProperty ("env", s.env));
    s.reverse = (bool) v["reverse"]; s.choke = (bool) v["choke"];
    s.hpOn = (bool) v["hpOn"]; s.lpOn = (bool) v["lpOn"];
    s.hpHz = v.getProperty ("hpHz", s.hpHz); s.lpHz = v.getProperty ("lpHz", s.lpHz);
    s.hpRes = v.getProperty ("hpRes", s.hpRes); s.lpRes = v.getProperty ("lpRes", s.lpRes);
    s.drive = v.getProperty ("drive", 0.0f); s.filterMix = v.getProperty ("filterMix", 1.0f);
    s.outputDb = v.getProperty ("outputDb", 0.0f);
    s.trimStart = v.getProperty ("trimStart", 0.0f); s.trimEnd = v.getProperty ("trimEnd", 1.0f);
    return s;
}

// starting points (Loopwright's own)
struct Preset { const char* name; Settings s; };
inline std::vector<Preset> presets()
{
    std::vector<Preset> p;
    auto make = [&p] (const char* name, int mode, bool sync, float rate, float ms, float pos, float space, float chaos, float tune, int env, std::function<void (Settings&)> more = {})
    {
        Settings s;
        s.mode = mode; s.sync = sync;
        s.knob = { rate, ms, pos, space, chaos, tune };
        for (auto& c : s.chop) c = s.knob;
        s.env = env;
        if (more) more (s);
        p.push_back ({ name, s });
    };
    make ("Init", Step, true, 0.75f, 120.0f, 0.0f, 0.3f, 0.15f, 0.0f, Hann);
    make ("Soft cloud", Hold, false, 0.68f, 600.0f, 0.3f, 0.6f, 0.25f, 0.0f, Hann);
    make ("Frozen drone", Hold, false, 0.86f, 1000.0f, 0.45f, 0.8f, 0.5f, 0.0f, Gaussian);
    make ("Glass shimmer", Hold, false, 0.74f, 320.0f, 0.5f, 0.75f, 0.4f, 12.0f, Gaussian, [] (Settings& s) { s.hpOn = true; s.hpHz = 500.0f; });
    make ("Low smear", Hold, false, 0.7f, 450.0f, 0.2f, 0.4f, 0.3f, -12.0f, Hann, [] (Settings& s) { s.lpOn = true; s.lpHz = 3500.0f; });
    make ("Stutter 1/16", Step, true, 0.81f, 80.0f, 0.0f, 0.2f, 0.05f, 0.0f, Square,
          [] (Settings& s) { s.clock = 4; for (int c = 0; c < maxChops; ++c) s.cellOn[(size_t) c] = c % 2 == 0 || c % 4 == 3; });
    make ("Rhythm chops", Step, true, 0.56f, 160.0f, 0.0f, 0.25f, 0.08f, 0.0f, Trapezoid,
          [] (Settings& s) { s.clock = 7; const int order[] = { 0, 3, 1, 6, 2, 5, 7, 4 }; for (int c = 0; c < 8; ++c) s.cellChop[(size_t) c] = order[c]; });
    make ("Glitch", Step, true, 0.94f, 60.0f, 0.1f, 0.5f, 0.55f, 0.0f, Triangle,
          [] (Settings& s) { s.chops = 16; s.clock = 2; s.reverse = false; for (int c = 0; c < 16; ++c) { s.cellChop[(size_t) c] = (c * 7) % 16; s.cellOn[(size_t) c] = c % 5 != 4; } });
    make ("Reverse swell", Hold, false, 0.66f, 700.0f, 0.6f, 0.5f, 0.35f, 0.0f, Gaussian, [] (Settings& s) { s.reverse = true; });
    make ("Tape halftime", Step, true, 0.56f, 260.0f, 0.0f, 0.15f, 0.1f, -12.0f, Hann,
          [] (Settings& s) { s.clock = 10; s.lpOn = true; s.lpHz = 6000.0f; s.drive = 0.3f; });
    make ("Keys: sampler", Keys, false, 0.62f, 220.0f, 0.0f, 0.3f, 0.05f, 0.0f, Hann);
    make ("Keys: pads", Keys, false, 0.8f, 700.0f, 0.2f, 0.7f, 0.45f, 0.0f, Gaussian);
    return p;
}
} // namespace lp::grains
