#include "Vocal.h"
#include "Song.h"
#include <thread>
#include <set>
#include <atomic>
#include "../engine/Limiter.h"
#include "../engine/Engine.h"
#include "../engine/Sources.h"
#include "OneNote.h"
#include "Chops.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <signalsmith-stretch/signalsmith-stretch.h>

namespace lp::song
{
namespace
{
    constexpr float kTwoPi = 6.2831853f;
    inline float mtof (float n) { return 440.0f * std::pow (2.0f, (n - 69.0f) / 12.0f); }

    // ---------- drums, made from math (no samples): six kits, one per style
    struct KitSpec
    {
        float kickHi, kickLo, kickSweep, kickDecay, kickClick, kickDrive;
        float snareTone, snareBand, snareDecay, snareNoise, snareToneMix;
        float hatFreq, hatDecay, openDecay, hatLevel;
        float clapBand, swish;   // swish > 0: brushes (slow-attack noise instead of a crack)
        float warmth;            // 0..1 darker, rounder
    };

    const KitSpec& kitFor (int style)
    {
        static const KitSpec kits[] = {
            //  kick hi/lo/sweep/decay/click/drive    snare tone/band/decay/noise/tone      hat f/dec/open/lvl     clap  swish warm
            { 120, 46, 26, 6.5f, 0.10f, 1.2f,   170, 1300, 14, 1.3f, 0.5f,   6000, 60, 6,  0.45f,   1100, 0,    0.6f },  // dusty
            { 150, 50, 32, 8.0f, 0.25f, 1.6f,   190, 2200, 18, 1.8f, 0.45f,  8000, 65, 7,  0.55f,   1300, 0,    0.3f },  // boom bap
            { 180, 40, 40, 9.5f, 0.35f, 1.4f,   210, 2600, 22, 1.6f, 0.3f,   9500, 90, 8,  0.6f,    1700, 0,    0.0f },  // trap
            { 130, 48, 28, 9.0f, 0.20f, 1.3f,   200, 2400, 20, 1.5f, 0.35f,  9000, 70, 9,  0.55f,   1500, 0,    0.15f }, // house
            { 100, 44, 20, 9.0f, 0.02f, 1.0f,   180, 3500, 5,  0.9f, 0.1f,   5200, 30, 3,  0.4f,    1200, 0.02f,0.5f },  // brushes
            { 160, 45, 45, 10.f, 0.40f, 2.2f,   240, 3000, 26, 2.0f, 0.4f,   10500,95, 10, 0.5f,    2000, 0,    0.0f },  // electronic
        };
        return kits[juce::jlimit (0, 5, style)];
    }

    struct DrumVoice
    {
        int type = 0; // 0 kick, 1 snare, 2 closed hat, 3 open hat, 4 clap, 5 tom, 6 rim, 7 shaker
        int age = 0;
        float vel = 1, phase = 0;
        bool active = false;
    };

    struct DrumKit
    {
        double sr = 48000;
        KitSpec k {};
        std::array<DrumVoice, 48> voices {};
        lp::dsp::Noise noise;
        lp::dsp::SVF snareBand, hatHigh, clapBand, shakerBand, rimBand;
        float tone = 0;

        void prepare (double rate, int style)
        {
            sr = rate;
            k = kitFor (style);
            snareBand.setup (k.snareBand, 0.8f, (float) sr);
            hatHigh.setup (k.hatFreq, 0.7f, (float) sr);
            clapBand.setup (k.clapBand, 1.2f, (float) sr);
            shakerBand.setup (6500.0f, 1.5f, (float) sr);
            rimBand.setup (1700.0f, 6.0f, (float) sr);
        }

        void hit (int note, float vel)
        {
            int type = note == 36 ? 0 : note == 38 ? 1 : note == 42 ? 2 : note == 46 ? 3 : note == 39 ? 4 : note == 37 ? 6 : note == 70 ? 7 : 5;
            if (type == 2 || type == 3) // a closed hat chokes the open one
                for (auto& v : voices) if (v.active && v.type == 3) v.active = false;
            for (auto& v : voices)
                if (! v.active) { v = { type, 0, vel, 0, true }; return; }
        }

        void render (float* L, float* R, int n)
        {
            const float fsr = (float) sr;
            for (int i = 0; i < n; ++i)
            {
                const float nz = noise.next();
                snareBand.tick (nz);
                hatHigh.tick (nz);
                clapBand.tick (nz);
                shakerBand.tick (nz);
                rimBand.tick (nz);
                float outL = 0, outR = 0;
                for (auto& v : voices)
                {
                    if (! v.active) continue;
                    const float t = (float) v.age / fsr;
                    float s = 0, pan = 0;
                    switch (v.type)
                    {
                        case 0: {
                            const float f = k.kickLo + (k.kickHi - k.kickLo) * std::exp (-t * k.kickSweep);
                            v.phase += kTwoPi * f / fsr;
                            s = std::tanh (std::sin (v.phase) * k.kickDrive) * std::exp (-t * k.kickDecay) * 1.1f
                              + nz * std::exp (-t * 400.0f) * k.kickClick;
                            if (t > 0.9f) v.active = false;
                            break; }
                        case 1: {
                            v.phase += kTwoPi * k.snareTone / fsr;
                            const float env = k.swish > 0 ? std::min (1.0f, t / 0.02f) * std::exp (-t * k.snareDecay) : std::exp (-t * k.snareDecay);
                            s = std::sin (v.phase) * std::exp (-t * 30.0f) * k.snareToneMix + snareBand.bp * env * k.snareNoise;
                            if (t > 0.6f) v.active = false;
                            break; }
                        case 2: s = hatHigh.hp * std::exp (-t * k.hatDecay) * k.hatLevel; pan = 0.3f; if (t > 0.15f) v.active = false; break;
                        case 3: s = hatHigh.hp * std::exp (-t * k.openDecay) * k.hatLevel * 0.75f; pan = 0.3f; if (t > 0.8f) v.active = false; break;
                        case 4: {
                            const float bursts = (t < 0.01f || (t > 0.012f && t < 0.02f) || (t > 0.023f && t < 0.031f)) ? 1.0f : 0.0f;
                            s = clapBand.bp * (bursts * 1.6f + std::exp (-t * 14.0f) * 1.1f);
                            pan = -0.1f;
                            if (t > 0.4f) v.active = false;
                            break; }
                        case 6: s = rimBand.bp * std::exp (-t * 60.0f) * 3.0f; pan = -0.3f; if (t > 0.1f) v.active = false; break;
                        case 7: s = shakerBand.bp * std::min (1.0f, t / 0.008f) * std::exp (-t * 40.0f) * 1.2f; pan = 0.45f; if (t > 0.15f) v.active = false; break;
                        default: {
                            const float f = 110.0f + 60.0f * std::exp (-t * 20.0f);
                            v.phase += kTwoPi * f / fsr;
                            s = std::sin (v.phase) * std::exp (-t * 9.0f) * 0.8f;
                            pan = -0.25f;
                            if (t > 0.5f) v.active = false;
                        }
                    }
                    s *= v.vel;
                    outL += s * (1.0f - pan);
                    outR += s * (1.0f + pan);
                    ++v.age;
                }
                // darker kits roll off the top a little
                tone += (1.0f - 0.8f * k.warmth) * (outL + outR - tone) * 0.5f;
                const float darkL = outL * (1.0f - k.warmth * 0.5f) + tone * 0.5f * k.warmth * 0.5f;
                const float darkR = outR * (1.0f - k.warmth * 0.5f) + tone * 0.5f * k.warmth * 0.5f;
                L[i] = std::tanh (darkL * 0.8f);
                R[i] = std::tanh (darkR * 0.8f);
            }
        }
    };

    // ---------- a soft, round bass (sine plus a little harmonic), also pure math
    struct BassSynth
    {
        double sr = 48000;
        struct V { int note = 0; float vel = 0, env = 0; bool on = false, active = false; double phase = 0, freq = 0, age = 0; };
        std::array<V, 4> voices {};
        float lowpass = 0;

        bool is808 = false;
        double lastFreq = 0.0;

        void noteOn (int note, float vel)
        {
            if (is808)
                for (auto& v : voices)
                    if (v.active && v.on && v.note != note)
                    {
                        // a new note while one rings: slide to it (the trap 808 glide)
                        v.note = note;
                        v.vel = std::max (v.vel, vel);
                        return;
                    }
            for (auto& v : voices) if (v.active && v.on) v.on = false; // one note at a time
            for (auto& v : voices) if (! v.active) { v = { note, vel, 0.0f, true, true, 0.0, mtof ((float) note), 0.0 }; return; }
        }
        void noteOff (int note) { for (auto& v : voices) if (v.note == note) v.on = false; }

        void render (float* L, float* R, int n)
        {
            const float atk = 1.0f - std::exp (-1.0f / (0.006f * (float) sr));
            const float rel = std::exp (-1.0f / ((is808 ? 0.25f : 0.07f) * (float) sr));
            for (int i = 0; i < n; ++i)
            {
                float s = 0;
                for (auto& v : voices)
                {
                    if (! v.active) continue;
                    v.env = v.on ? v.env + (1.0f - v.env) * atk : v.env * rel;
                    if (! v.on && v.env < 0.0005f) { v.active = false; continue; }
                    double f = mtof ((float) v.note);
                    if (is808)
                    {
                        // an 808: a punchy pitch drop at the start, a long round tail, a little grit,
                        // and a smooth slide when it moves to a new note while ringing
                        v.freq = v.freq <= 0 ? f : v.freq + (f - v.freq) * (1.0 - std::exp (-1.0 / (0.07 * sr)));
                        v.age += 1.0 / sr;
                        f = v.freq * (1.0 + 1.5 * std::exp (-v.age * 40.0));
                    }
                    v.phase += 2.0 * juce::MathConstants<double>::pi * f / sr;
                    const float ph = (float) v.phase;
                    if (is808)
                        s += std::tanh (std::sin (ph) * 1.8f) * v.env * v.vel * 0.8f;
                    else
                        s += (std::sin (ph) + 0.25f * std::sin (2.0f * ph) + 0.08f * std::sin (3.0f * ph)) * v.env * v.vel;
                }
                lowpass += 0.25f * (s - lowpass);
                L[i] = R[i] = std::tanh (lowpass * 0.9f);
            }
        }
    };

    // stretch audio to a new length without changing its pitch (WSOLA: overlapping slices,
    // each nudged to where it lines up best with the last one, so it stays smooth).
    // used for short drum slices; longer sounds go through Signalsmith Stretch below
    juce::AudioBuffer<float> wsolaStretch (const juce::AudioBuffer<float>& in, int outLen, bool wrap = true)
    {
        const int inLen = in.getNumSamples();
        constexpr int N = 2048, H = N / 2, search = 384;
        if (inLen < N * 2 || outLen <= 0) return in;
        juce::AudioBuffer<float> out (2, outLen + N);
        out.clear();
        std::vector<float> norm ((size_t) (outLen + N), 0.0f), win ((size_t) N), mono ((size_t) inLen);
        for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) N);
        for (int i = 0; i < inLen; ++i) mono[(size_t) i] = in.getSample (0, i) + in.getSample (1, i);
        const double alpha = (double) inLen / outLen;
        int prev = 0;
        for (int o = 0; o * H < outLen; ++o)
        {
            const int ideal = (int) (o * H * alpha);
            int best = ideal;
            const int natural = prev + H; // where the last slice would naturally carry on
            if (o > 0 && natural + N / 2 < inLen)
            {
                double bestC = -1e30;
                for (int d = -search; d <= search; d += 4)
                {
                    const int cand = ideal + d;
                    if (cand < 0 || cand + N / 2 >= inLen) continue;
                    double c = 0;
                    for (int k = 0; k < N / 2; k += 2) c += (double) mono[(size_t) (natural + k)] * mono[(size_t) (cand + k)];
                    if (c > bestC) { bestC = c; best = cand; }
                }
            }
            best = juce::jlimit (0, inLen - 1, best);
            for (int ch = 0; ch < 2; ++ch)
            {
                const float* src = in.getReadPointer (ch);
                float* dst = out.getWritePointer (ch);
                for (int k = 0; k < N; ++k)
                {
                    const int si = best + k;
                    // wrap round: a loop's end leads back into its start (a slice of a loop doesn't: it just ends)
                    const float v = si < inLen ? src[si] : (wrap ? src[si - inLen] : 0.0f);
                    dst[o * H + k] += v * win[(size_t) k];
                }
            }
            for (int k = 0; k < N; ++k) norm[(size_t) (o * H + k)] += win[(size_t) k];
            prev = best;
        }
        juce::AudioBuffer<float> res (2, outLen);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < outLen; ++i)
                res.setSample (ch, i, norm[(size_t) i] > 1e-3f ? out.getSample (ch, i) / norm[(size_t) i] : out.getSample (ch, i)); // (its start at full level: a hit stays a hit)
        return res;
    }

    // Signalsmith Stretch (MIT, Signalsmith Audio): a phase-vocoder stretcher made for music.
    // changes length and pitch together in one pass, so a loop is touched only once.
    // `wrap`: the sound is a loop, so its end leads back into its start (no fade at the edges).
    // `voice`: keep the formants, so a sung line moved in key doesn't sound chipmunked.
    juce::AudioBuffer<float> stretchAndPitch (const juce::AudioBuffer<float>& in, int outLen, double semis,
                                              double sr, bool wrap, bool voice = false)
    {
        const int inLen = in.getNumSamples();
        if (inLen <= 0 || outLen <= 0) return in;
        const double rate = (double) inLen / outLen;
        // a loop: give it a bit of itself on each side, stretch, then keep the middle
        const int pad = wrap ? std::min (inLen, (int) (0.35 * sr)) : 0;
        const int total = inLen + 2 * pad;
        juce::AudioBuffer<float> src (2, total);
        for (int ch = 0; ch < 2; ++ch)
        {
            const float* x = in.getReadPointer (std::min (ch, in.getNumChannels() - 1));
            float* y = src.getWritePointer (ch);
            for (int i = 0; i < total; ++i)
            {
                int k = i - pad;
                if (wrap) k = ((k % inLen) + inLen) % inLen;
                y[i] = (k >= 0 && k < inLen) ? x[k] : 0.0f;
            }
        }
        const int padOut = (int) std::round (pad / rate);
        const int totalOut = outLen + 2 * padOut;
        signalsmith::stretch::SignalsmithStretch<float> st;
        st.presetDefault (2, (float) sr);
        if (semis != 0.0) st.setTransposeSemitones ((float) semis, (float) (8000.0 / sr));
        if (voice) st.setFormantFactor (1.0f, true);
        juce::AudioBuffer<float> outAll (2, totalOut);
        outAll.clear();
        float* ins[2] = { src.getWritePointer (0), src.getWritePointer (1) };
        float* outs[2] = { outAll.getWritePointer (0), outAll.getWritePointer (1) };
        if (! st.exact (ins, total, outs, totalOut))
            return wsolaStretch (in, outLen); // too short for the spectral stretcher
        juce::AudioBuffer<float> res (2, outLen);
        for (int ch = 0; ch < 2; ++ch) res.copyFrom (ch, 0, outAll, ch, padOut, outLen);
        return res;
    }

    juce::AudioBuffer<float> timeStretch (const juce::AudioBuffer<float>& in, int outLen, double sr = 48000.0)
    {
        return stretchAndPitch (in, outLen, 0.0, sr, true);
    }

    // like a "beats" warp: cut the loop where its hits are (on the 16th grid), pin every slice to its spot on
    // the song's grid, and stretch each slice on its own, so hits land exactly in time. Where nothing new hits
    // (a ringing hat, a reverb tail) it isn't cut at all. Each slice blends into the next over a few ms, using
    // the sound just before the next hit, so nothing is chopped to silence: no clicks between slices.
    juce::AudioBuffer<float> beatStretch (const juce::AudioBuffer<float>& in, int outLen, int bars)
    {
        const int inLen = in.getNumSamples();
        if (inLen <= 0 || outLen <= 0 || bars <= 0) return wsolaStretch (in, outLen);
        const int grid = bars * 16;
        // how sharply the sound rises at each grid point: there's a hit there, or not
        const int w = std::max (32, inLen / grid / 6);
        auto energy = [&] (int from, int n)
        {
            double e = 0;
            for (int i = std::max (0, from); i < std::min (inLen, from + n); ++i)
                e += (double) in.getSample (0, i) * in.getSample (0, i) + (double) in.getSample (std::min (1, in.getNumChannels() - 1), i) * in.getSample (std::min (1, in.getNumChannels() - 1), i);
            return e;
        };
        std::vector<int> cuts { 0 };
        for (int k = 1; k < grid; ++k)
        {
            const int at = (int) ((juce::int64) inLen * k / grid);
            // look a little either side: a hit played slightly early or late still counts
            double best = 0;
            for (int d = -w; d <= w; d += std::max (1, w / 4))
                best = std::max (best, (energy (at + d, w) + 1e-9) / (energy (at + d - w, w) + 1e-9));
            const int minGap = std::max (4096 + 64, inLen / grid);
            if (best > 1.6 && at - cuts.back() >= minGap) cuts.push_back (k);
        }
        // grid index -> sample positions in and out
        auto inAt = [&] (int k) { return (int) ((juce::int64) inLen * k / grid); };
        auto outAt = [&] (int k) { return (int) ((juce::int64) outLen * k / grid); };
        juce::AudioBuffer<float> out (2, outLen);
        out.clear();
        const int xf = std::min (256, std::max (32, inLen / grid / 8)); // the blend (about 5 ms)
        for (size_t c = 0; c < cuts.size(); ++c)
        {
            const int k0 = cuts[c], k1 = c + 1 < cuts.size() ? cuts[c + 1] : grid;
            int s0 = inAt (k0);
            const int s1 = inAt (k1), o0 = outAt (k0), o1 = outAt (k1);
            // start the slice up to ~1 ms early or late, wherever its lead-in lines up best with what's already
            // playing, so a ringing note blends in step instead of half-cancelling itself
            if (c > 0 && o0 >= xf)
            {
                double best = -1e30; int bestD = 0;
                const float* x = in.getReadPointer (0);
                const float* o = out.getReadPointer (0);
                for (int d = -48; d <= 48; ++d)
                {
                    if (s0 + d - xf < 0 || s0 + d >= inLen) continue;
                    double cc = 0;
                    for (int i = 0; i < xf; i += 2) cc += (double) o[o0 - xf + i] * x[s0 + d - xf + i];
                    if (cc > best) { best = cc; bestD = d; }
                }
                s0 += bestD;
            }
            // the slice, plus what follows it (so its end isn't cut off mid-stretch), stretched by the slice's own ratio
            // (the last slice carries on into the loop's start: it's a loop)
            const int extra = std::min (inLen, 4096);
            juce::AudioBuffer<float> piece (2, s1 - s0 + extra);
            for (int ch = 0; ch < 2; ++ch)
            {
                const float* x = in.getReadPointer (std::min (ch, in.getNumChannels() - 1));
                float* y = piece.getWritePointer (ch);
                for (int i = 0; i < s1 - s0 + extra; ++i) y[i] = x[(s0 + i) % inLen];
            }
            const double ratio = (double) (o1 - o0) / (double) (s1 - s0);
            const auto fitted = (s1 - s0) == (o1 - o0) ? piece : wsolaStretch (piece, (int) std::round ((s1 - s0 + extra) * ratio), false);
            const int n = std::min ({ fitted.getNumSamples(), outLen - o0, o1 - o0 });
            for (int ch = 0; ch < 2; ++ch)
            {
                float* o = out.getWritePointer (ch);
                const float* f = fitted.getReadPointer (ch);
                for (int i = 0; i < n; ++i)
                {
                    // fades out over its last few ms while the next slice fades in (not for the loop's end: it wraps)
                    // (equal-power: the two sounds differ, so a plain linear blend would dip in the middle)
                    const float g = n - i >= xf ? 1.0f : std::sin (0.5f * juce::MathConstants<float>::pi * (float) (n - i) / (float) xf);
                    o[o0 + i] += f[i] * g;
                }
                // the lead-in: the sound just before this slice's hit, fading in under the end of the slice before.
                // the first slice's lead-in is the loop's own end, at the end of the output: the repeat runs straight on
                const float* x = in.getReadPointer (std::min (ch, in.getNumChannels() - 1));
                const int at = c > 0 ? o0 - xf : outLen - xf;
                for (int i = 0; i < xf && at + i >= 0; ++i)
                    o[at + i] += x[((s0 - xf + i) % inLen + inLen) % inLen] * std::sin (0.5f * juce::MathConstants<float>::pi * (float) i / (float) xf);
            }
        }
        return out;
    }

    // ---------- audio files from your library
    juce::AudioBuffer<float> loadAudio (const juce::String& path, double targetRate, double stretch = 1.0, double maxSeconds = 60.0)
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (juce::File (path)));
        if (rd == nullptr || rd->lengthInSamples <= 0) return {};
        const int len = (int) std::min<juce::int64> (rd->lengthInSamples, (juce::int64) (rd->sampleRate * maxSeconds));
        juce::AudioBuffer<float> raw (2, len);
        rd->read (&raw, 0, len, 0, true, rd->numChannels > 1);
        if (rd->numChannels == 1) raw.copyFrom (1, 0, raw, 0, 0, len);
        // resample (and gently fit to the bar length) with linear interpolation
        const double step = rd->sampleRate / targetRate / stretch;
        const int outLen = (int) ((len - 1) / step);
        juce::AudioBuffer<float> out (2, std::max (1, outLen));
        for (int ch = 0; ch < 2; ++ch)
        {
            const float* src = raw.getReadPointer (ch);
            float* dst = out.getWritePointer (ch);
            for (int i = 0; i < outLen; ++i)
            {
                const double pos = i * step;
                const int i0 = (int) pos;
                const float f = (float) (pos - i0);
                dst[i] = src[i0] + (src[std::min (i0 + 1, len - 1)] - src[i0]) * f;
            }
        }
        return out;
    }

    // plays your drum one-shots
    struct SampleKit
    {
        std::map<int, juce::AudioBuffer<float>> sounds;
        struct V { const juce::AudioBuffer<float>* buf = nullptr; int pos = 0; float vel = 1; int note = 0; bool active = false; };
        std::array<V, 48> voices {};

        void hit (int note, float vel)
        {
            auto it = sounds.find (note);
            if (it == sounds.end()) return;
            if (note == 42) for (auto& v : voices) if (v.active && v.note == 46) v.active = false; // choke the open hat
            for (auto& v : voices)
                if (! v.active) { v = { &it->second, 0, vel, note, true }; return; }
        }

        void render (float* L, float* R, int n)
        {
            for (int i = 0; i < n; ++i) { L[i] = 0; R[i] = 0; }
            for (auto& v : voices)
            {
                if (! v.active) continue;
                const int len = v.buf->getNumSamples();
                const float* a = v.buf->getReadPointer (0);
                const float* b = v.buf->getReadPointer (1);
                for (int i = 0; i < n; ++i)
                {
                    if (v.pos >= len) { v.active = false; break; }
                    const float g = v.vel * (v.pos > len - 64 ? (float) (len - v.pos) / 64.0f : 1.0f);
                    L[i] += a[v.pos] * g;
                    R[i] += b[v.pos] * g;
                    ++v.pos;
                }
            }
        }
    };

    // a simple time-preserving pitch shifter: two crossfading read heads on a delay line
    void pitchShiftSimple (juce::AudioBuffer<float>& buf, int semis, double sr);

    // moves the pitch without changing the length: resample (pitch and length change together),
    // then stretch back to the original length with WSOLA. Clean even for big moves (up to 6+ semitones).
    void pitchShift (juce::AudioBuffer<float>& buf, int semis, double sr)
    {
        const int n = buf.getNumSamples();
        if (semis == 0 || n < 16384) { pitchShiftSimple (buf, semis, sr); return; }
        if (n > 0) { buf = stretchAndPitch (buf, n, semis, sr, false); return; }
        const double ratio = std::pow (2.0, semis / 12.0);
        const int m = (int) ((n - 4) / ratio);
        juce::AudioBuffer<float> rs (2, m);
        for (int ch = 0; ch < 2; ++ch)
        {
            const float* x = buf.getReadPointer (std::min (ch, buf.getNumChannels() - 1));
            float* y = rs.getWritePointer (ch);
            for (int i = 0; i < m; ++i)
            {
                // 4-point Hermite interpolation
                const double pos = i * ratio;
                const int i1 = (int) pos;
                const float f = (float) (pos - i1);
                const float y0 = x[std::max (0, i1 - 1)], y1 = x[i1], y2 = x[std::min (n - 1, i1 + 1)], y3 = x[std::min (n - 1, i1 + 2)];
                const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
                y[i] = ((c3 * f + c2) * f + c1) * f + y1;
            }
        }
        buf = timeStretch (rs, n);
    }

    void pitchShiftSimple (juce::AudioBuffer<float>& buf, int semis, double sr)
    {
        const double ratio = std::pow (2.0, semis / 12.0);
        const int win = (int) (0.06 * sr);
        const int n = buf.getNumSamples();
        juce::AudioBuffer<float> out (2, n);
        for (int ch = 0; ch < 2; ++ch)
        {
            const float* in = buf.getReadPointer (ch);
            float* o = out.getWritePointer (ch);
            double phase = 0.0;
            const double step = (1.0 - ratio) / win; // how fast the delay changes
            for (int i = 0; i < n; ++i)
            {
                phase += step;
                phase -= std::floor (phase);
                float acc = 0;
                for (int k = 0; k < 2; ++k)
                {
                    const double ph = std::fmod (phase + 0.5 * k, 1.0);
                    const double pos = i - ph * win;
                    const int p0 = (int) std::floor (pos);
                    const float fr = (float) (pos - p0);
                    const float a = p0 >= 0 && p0 < n ? in[p0] : 0.0f;
                    const float b = p0 + 1 >= 0 && p0 + 1 < n ? in[p0 + 1] : 0.0f;
                    const float w = std::sin ((float) ph * juce::MathConstants<float>::pi);
                    acc += (a + (b - a) * fr) * w * w;
                }
                o[i] = acc;
            }
        }
        buf = std::move (out);
    }

    void reverse (juce::AudioBuffer<float>& b)
    {
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            std::reverse (b.getWritePointer (ch), b.getWritePointer (ch) + b.getNumSamples());
    }

    // the pitch a one-shot plays (as a MIDI note): heard, or from its name ("808_F", "Pluck_C#3") if it has no
    // clear pitch (a deep 808 can be hard to hear); `fallback` when neither says
    double rootNoteOf (const juce::AudioBuffer<float>& b, double sr, double fallback, const juce::String& path)
    {
        return lp::song::onenote::rootOf (b, sr, fallback, path);
    }

    struct Event { long sample; int note; float vel; bool on; };

    std::vector<Event> eventsFor (const Part& part, double samplesPerBeat)
    {
        std::vector<Event> ev;
        for (auto& n : part.notes)
        {
            ev.push_back ({ (long) (n.beat * samplesPerBeat), n.pitch, n.velocity, true });
            ev.push_back ({ (long) ((n.beat + n.length) * samplesPerBeat), n.pitch, 0.0f, false });
        }
        // note-offs first at the same moment, so a repeated note retriggers cleanly
        std::sort (ev.begin(), ev.end(), [] (const Event& a, const Event& b)
                   { return a.sample != b.sample ? a.sample < b.sample : (! a.on && b.on); });
        return ev;
    }

    // ---------- mixing: clean low end, kick ducking, and the reference's tone

    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        inline float process (float x) { const float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
        // RBJ cookbook filters. type: 0 peak, 1 low shelf, 2 high shelf, 3 high-pass
        static Biquad make (int type, double sr, double f, double q, double db)
        {
            Biquad b;
            const double A = std::pow (10.0, db / 40.0), w = 2.0 * juce::MathConstants<double>::pi * f / sr;
            const double cw = std::cos (w), sw = std::sin (w), alpha = sw / (2.0 * q);
            double b0, b1, b2, a0, a1, a2;
            if (type == 0) { b0 = 1 + alpha * A; b1 = -2 * cw; b2 = 1 - alpha * A; a0 = 1 + alpha / A; a1 = -2 * cw; a2 = 1 - alpha / A; }
            else if (type == 3) { b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = (1 + cw) / 2; a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; }
            else if (type == 4) { b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = (1 - cw) / 2; a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; }
            else
            {
                const double sa = 2.0 * std::sqrt (A) * alpha, sgn = type == 1 ? 1.0 : -1.0;
                b0 = A * ((A + 1) - sgn * (A - 1) * cw + sa);
                b1 = sgn * 2 * A * ((A - 1) - sgn * (A + 1) * cw);
                b2 = A * ((A + 1) - sgn * (A - 1) * cw - sa);
                a0 = (A + 1) + sgn * (A - 1) * cw + sa;
                a1 = -sgn * 2 * ((A - 1) + sgn * (A + 1) * cw);
                a2 = (A + 1) + sgn * (A - 1) * cw - sa;
            }
            b.b0 = (float) (b0 / a0); b.b1 = (float) (b1 / a0); b.b2 = (float) (b2 / a0); b.a1 = (float) (a1 / a0); b.a2 = (float) (a2 / a0);
            return b;
        }
    };

    // the 10-band tone match (same octaves as Tone): low shelf, 8 bells, high shelf
    struct MatchEq
    {
        std::array<Biquad, 10> f[2];
        bool active = false;
        void setup (const std::array<float, 10>& db, double sr)
        {
            static const double centres[10] = { 31.25, 62.5, 125, 250, 500, 1000, 2000, 4000, 8000, 16000 };
            active = false;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 10; ++i)
                {
                    const double fc = std::min (centres[i], sr * 0.42);
                    const int type = i == 0 ? 1 : (i == 9 ? 2 : 0);
                    f[ch][(size_t) i] = std::abs (db[(size_t) i]) < 0.05f ? Biquad {} : Biquad::make (type, sr, type == 2 ? std::min (11000.0, fc) : fc, 0.9, db[(size_t) i]);
                    active = active || std::abs (db[(size_t) i]) >= 0.05f;
                }
        }
        void process (juce::AudioBuffer<float>& b, int start, int n)
        {
            if (! active) return;
            for (int ch = 0; ch < std::min (2, b.getNumChannels()); ++ch)
            {
                float* d = b.getWritePointer (ch, start);
                for (auto& q : f[ch]) for (int i = 0; i < n; ++i) d[i] = q.process (d[i]);
            }
        }
    };

    // what kind of sound a part is, for mixing
    bool isBassPart (const Part& p)  { return p.kind == PartKind::Bass || juce::String (p.name).containsIgnoreCase ("bass"); }
    bool isDrumPart (const Part& p)  { const juce::String n (p.name); return p.kind == PartKind::Drums || p.kind == PartKind::Fx || n.contains ("drum") || n == "percussion" || n == "fx"; }

    // cleans a track so the whole song sits together: melodic loops leave the low end to the bass,
    // and bass + music duck a little under every kick so the drums punch through; then the reference's tone
    void shapeForMix (const Song& song, const Part& part, juce::AudioBuffer<float>& buf, double sr)
    {
        if (isDrumPart (part)) { /* drums keep their punch */ }
        else
        {
            bool songHasBass = false;
            for (auto& p : song.parts) songHasBass = songHasBass || isBassPart (p);
            const bool bass = isBassPart (part);
            const bool is808 = part.kind == PartKind::Bass && part.variant == 1;
            if (! bass && songHasBass && part.kind == PartKind::Loop)
            {
                for (int ch = 0; ch < buf.getNumChannels(); ++ch)
                {
                    auto hp1 = Biquad::make (3, sr, 110.0, 0.707, 0), hp2 = hp1;
                    float* d = buf.getWritePointer (ch);
                    for (int i = 0; i < buf.getNumSamples(); ++i) d[i] = hp2.process (hp1.process (d[i]));
                }
            }
            // the kicks
            std::vector<int> kicks;
            const double spb = sr * 60.0 / song.bpm;
            for (auto& p : song.parts)
                if (p.kind == PartKind::Drums)
                    for (auto& nt : p.notes)
                        if (nt.pitch == 36) kicks.push_back ((int) (nt.beat * spb));
            if (! kicks.empty() && ! is808) // an 808 is the low end itself: never ducked
            {
                std::sort (kicks.begin(), kicks.end());
                const float depth = juce::Decibels::decibelsToGain (bass ? -5.0f : -2.0f);
                const float rel = std::exp (-1.0f / (0.14f * (float) sr));
                float env = 1.0f;
                size_t k = 0;
                const int atk = (int) (0.004 * sr);
                for (int i = 0; i < buf.getNumSamples(); ++i)
                {
                    while (k < kicks.size() && kicks[k] + atk <= i) ++k;
                    const bool inAttack = k < kicks.size() && i >= kicks[k] && i < kicks[k] + atk;
                    if (inAttack) env = std::min (env, 1.0f - (1.0f - depth) * (float) (i - kicks[k]) / (float) atk);
                    else env = 1.0f - (1.0f - env) * rel;
                    for (int ch = 0; ch < buf.getNumChannels(); ++ch) buf.getWritePointer (ch)[i] *= env;
                }
            }
        }
        if (song.mixShaped)
        {
            MatchEq eq;
            eq.setup (song.matchEqDb, sr);
            eq.process (buf, 0, buf.getNumSamples());
            if (std::abs (song.matchGainDb) > 0.01f) buf.applyGain (juce::Decibels::decibelsToGain (song.matchGainDb));
        }
    }

    // ---------- a loop arranged like a producer would, bar by bar
    void renderArrangedLoop (const Part& part, const juce::AudioBuffer<float>& loop, double barSamples,
                             juce::AudioBuffer<float>& out, int total, double sr)
    {
        const int loopLen = loop.getNumSamples();
        const int nb = (int) part.barSrc.size();
        const int fade = (int) (0.002 * sr);
        juce::AudioBuffer<float> seg (2, (int) std::ceil (barSamples) + 4);
        auto modified = [&] (int b) { return part.chopBars.count (b) || part.stutterBars.count (b) || part.gapBars.count (b) || part.swellBars.count (b); };
        auto srcOf = [&] (int b) { return b >= 0 && b < nb ? part.barSrc[(size_t) b] : -1; };
        for (int b = 0; b < nb; ++b)
        {
            const int src = srcOf (b);
            const bool swell = part.swellBars.count (b) > 0;
            if (src < 0 && ! swell) continue;
            const int a = (int) std::round (b * barSamples), e = std::min (total, (int) std::round ((b + 1) * barSamples));
            const int len = e - a;
            if (len <= 0) continue;
            seg.clear();
            auto copyFrom = [&] (int dst, int from, int n, bool backwards)
            {
                for (int ch = 0; ch < 2; ++ch)
                {
                    const float* in = loop.getReadPointer (ch);
                    float* o = seg.getWritePointer (ch);
                    for (int i = 0; i < n && dst + i < len; ++i)
                    {
                        const int k = backwards ? from + n - 1 - i : from + i;
                        o[dst + i] = k >= 0 && k < loopLen ? in[k] : 0.0f;
                    }
                }
            };
            auto ramp = [&] (int from, int n, bool up)
            {
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < n && from + i < len; ++i)
                        seg.getWritePointer (ch)[from + i] *= up ? (float) i / (float) n : 1.0f - (float) i / (float) n;
            };
            const int srcStart = (int) std::round (std::max (0, src) * barSamples);
            if (swell && src < 0)
            {
                // reversed swell: the loop's first bar played backwards, rising out of nothing into the next bar
                copyFrom (0, 0, len, true);
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < len; ++i) { const float t = (float) i / (float) len; seg.getWritePointer (ch)[i] *= t * t * t; }
            }
            else if (part.chopBars.count (b))
            {
                // the bar's 8ths re-ordered (a new variation from the same sound), each slice faded so it's clean
                static const int orders[4][8] = { { 0, 1, 0, 1, 4, 5, 6, 6 }, { 0, 1, 2, 2, 4, 4, 6, 7 }, { 0, 0, 2, 3, 0, 1, 6, 6 }, { 0, 1, 2, 3, 2, 3, 6, 7 } };
                const int* ord = orders[(size_t) ((b * 7 + src) % 4)];
                const int sl = len / 8;
                for (int k = 0; k < 8; ++k)
                {
                    copyFrom (k * sl, srcStart + ord[k] * sl, k == 7 ? len - 7 * sl : sl, false);
                    ramp (k * sl, fade, true);
                    ramp (k * sl + sl - fade, fade, false);
                }
            }
            else
                copyFrom (0, srcStart, len, false);
            if (part.stutterBars.count (b))
            {
                // the last beat stutters: its first 16th, four times, getting louder
                const int beat = len / 4, s16 = beat / 4;
                for (int k = 0; k < 4; ++k)
                {
                    copyFrom (3 * beat + k * s16, srcStart + 3 * beat, k == 3 ? len - 3 * beat - 3 * s16 : s16, false);
                    for (int ch = 0; ch < 2; ++ch)
                        for (int i = 0; i < s16; ++i) seg.getWritePointer (ch)[std::min (len - 1, 3 * beat + k * s16 + i)] *= 0.6f + 0.13f * (float) k;
                    ramp (3 * beat + k * s16 + s16 - fade, fade, false);
                }
            }
            if (part.gapBars.count (b))
            {
                // the last beat drops out, so the next section hits harder
                const int from = len - len / 4;
                ramp (from - fade, fade, false);
                for (int ch = 0; ch < 2; ++ch) std::fill (seg.getWritePointer (ch) + from, seg.getWritePointer (ch) + len, 0.0f);
            }
            // clean edges wherever the sound doesn't simply carry on from the bar before / into the next
            const bool joinPrev = srcOf (b - 1) >= 0 && ! modified (b - 1) && ! modified (b) && src == (srcOf (b - 1) + 1) % std::max (1, part.loopBars) && src != 0;
            const bool joinNext = srcOf (b + 1) >= 0 && ! modified (b + 1) && ! modified (b) && srcOf (b + 1) == (src + 1) % std::max (1, part.loopBars) && srcOf (b + 1) != 0;
            // where the sound jumps (a new section, a chop): this bar fades out over its last few ms while the next
            // fades in with the sound from just before its own start, so they blend instead of dipping to silence
            const int xf = (int) (0.005 * sr);
            const bool playsBefore = b > 0 && (srcOf (b - 1) >= 0 || part.swellBars.count (b - 1)) && ! part.gapBars.count (b - 1);
            const bool playsAfter = b + 1 < nb && (srcOf (b + 1) >= 0 || part.swellBars.count (b + 1));
            const bool leadIn = ! joinPrev && ! swell && playsBefore && a >= xf && ! part.chopBars.count (b);
            if (! joinPrev && ! swell && ! leadIn) ramp (0, fade, true);
            if (! joinNext && ! part.gapBars.count (b)) ramp (len - (playsAfter ? xf : fade), playsAfter ? xf : fade, false);
            for (int ch = 0; ch < 2; ++ch) out.addFrom (ch, a, seg, ch, 0, len);
            if (leadIn)
                for (int ch = 0; ch < 2; ++ch)
                {
                    const float* in = loop.getReadPointer (ch);
                    float* o = out.getWritePointer (ch);
                    for (int i = 0; i < xf; ++i)
                    {
                        const int k = ((srcStart - xf + i) % loopLen + loopLen) % loopLen; // (bar 1's lead-in: the loop's end)
                        o[a - xf + i] += in[k] * ((float) i / (float) xf);
                    }
                }
        }

        // filter sweeps: muffled intros that open up, outros that close down
        if (! part.barFilter.empty())
        {
            auto openness = [&] (double barPos)
            {
                const int b = (int) barPos;
                auto it = part.barFilter.find (b);
                if (it == part.barFilter.end()) return 1.0f;
                auto nx = part.barFilter.find (b + 1);
                const float v1 = nx != part.barFilter.end() ? nx->second : it->second;
                return it->second + (v1 - it->second) * (float) (barPos - b);
            };
            Biquad f[2];
            for (int i = 0; i < total; i += 64)
            {
                const float o = juce::jlimit (0.0f, 1.0f, openness (i / barSamples));
                if (o >= 0.999f) { for (auto& q : f) q = Biquad {}; continue; }
                const double hz = 180.0 * std::pow (100.0, (double) o);
                for (auto& q : f)
                {
                    // new coefficients, same filter memory (so the sweep is smooth)
                    const auto lp = Biquad::make (4, sr, hz, 0.8, 0);
                    q.b0 = lp.b0; q.b1 = lp.b1; q.b2 = lp.b2; q.a1 = lp.a1; q.a2 = lp.a2;
                }
                const int n = std::min (64, total - i);
                for (int ch = 0; ch < 2; ++ch)
                {
                    float* d = out.getWritePointer (ch, i);
                    for (int k = 0; k < n; ++k) d[k] = f[ch].process (d[k]);
                }
            }
        }
    }

    // ---------- grains: the track re-played as tiny slices that fire in time with the song.
    // short and calm = clean and smooth; short and chaotic = glitchy (octave jumps, reversed slices,
    // scattered positions); "position" reads from further back, so phrases stutter and repeat
    void grainify (juce::AudioBuffer<float>& buf, double sr, double bpm, const Part& p, juce::uint32 seed)
    {
        const float amount = juce::jlimit (0.0f, 1.0f, p.grainAmount);
        if (amount < 0.01f) return;
        const int n = buf.getNumSamples();
        const juce::AudioBuffer<float> src (buf);
        juce::AudioBuffer<float> out (2, n);
        out.clear();
        const double beat = sr * 60.0 / bpm;
        static const double divs[4] = { 1.0, 0.5, 0.25, 0.125 };
        const double step = beat * divs[juce::jlimit (0, 3, p.grainRate)];
        const int size = std::max (64, (int) (juce::jlimit (10.0f, 600.0f, p.grainSizeMs) * 0.001 * sr));
        const float chaos = juce::jlimit (0.0f, 1.0f, p.grainChaos);
        const int back = (int) (juce::jlimit (0.0f, 1.0f, p.grainPosition) * beat * 4.0); // up to a bar back
        juce::Random rng ((juce::int64) seed * 7919 + 13);
        const int fade = std::max (16, size / (chaos > 0.6f ? 16 : 4)); // glitchier grains have harder edges
        for (double t = 0; t < n; t += step)
        {
            const int at = (int) t;
            // nothing there: no grain
            float e = 0;
            for (int k = at; k < std::min (n, at + size); k += 32) e = std::max (e, std::abs (src.getSample (0, k)) + std::abs (src.getSample (1, k)));
            if (e < 1e-4f) continue;
            int from = at - back - (int) (chaos * rng.nextFloat() * (float) beat);
            if (from < 0) from = at;
            const float ratio = chaos > 0.05f && rng.nextFloat() < chaos * 0.35f ? (rng.nextBool() ? 2.0f : 0.5f) : 1.0f;
            const bool backwards = rng.nextFloat() < chaos * 0.4f;
            const int len = std::min (size, (int) (step * (chaos > 0.5f ? 0.9 : 1.6)));
            const float pan = chaos * (rng.nextFloat() - 0.5f);
            for (int i = 0; i < len && at + i < n; ++i)
            {
                const float w = std::min ({ 1.0f, (float) i / (float) fade, (float) (len - i) / (float) fade });
                const double si = from + (backwards ? (double) (len - i) : (double) i) * ratio;
                const int s0 = (int) si;
                if (s0 < 0 || s0 + 1 >= n) continue;
                const float fr = (float) (si - s0);
                for (int ch = 0; ch < 2; ++ch)
                {
                    const float v = src.getSample (ch, s0) + (src.getSample (ch, s0 + 1) - src.getSample (ch, s0)) * fr;
                    out.getWritePointer (ch)[at + i] += v * w * (ch == 0 ? 1.0f - pan : 1.0f + pan);
                }
            }
        }
        // as loud as the original, then blended in
        auto rms = [] (const juce::AudioBuffer<float>& b) { double q = 0; for (int ch = 0; ch < 2; ++ch) { auto* d = b.getReadPointer (ch); for (int i = 0; i < b.getNumSamples(); i += 4) q += (double) d[i] * d[i]; } return std::sqrt (q); };
        const double a = rms (src), g2 = rms (out);
        if (g2 > 1e-9) out.applyGain ((float) std::min (4.0, a / g2));
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* d = buf.getWritePointer (ch);
            const auto* o = out.getReadPointer (ch);
            for (int i = 0; i < n; ++i) d[i] = d[i] * (1.0f - amount) + o[i] * amount;
        }
    }

    bool writeWav (const juce::File& file, const juce::AudioBuffer<float>& buf, double sr)
    {
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> os (file.createOutputStream().release());
        if (os == nullptr) return false;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), sr, 2, 24, {}, 0));
        if (w == nullptr) return false;
        os.release();
        return w->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
    }

    juce::String keyName (int key, bool minor) { return juce::String (lp::noteName (key)) + (minor ? " minor" : " major"); }
}

void writeMidi (const Song& song, const juce::File& file)
{
    constexpr int tpq = 960;
    juce::MidiFile mf;
    mf.setTicksPerQuarterNote (tpq);

    // conductor track: tempo, metre, key and the section markers (most DAWs show these as markers)
    juce::MidiMessageSequence conductor;
    conductor.addEvent (juce::MidiMessage::textMetaEvent (3, song.title), 0);
    conductor.addEvent (juce::MidiMessage::tempoMetaEvent ((int) (60000000.0 / song.bpm)), 0);
    conductor.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0);
    conductor.addEvent (juce::MidiMessage::keySignatureMetaEvent (0, song.minor), 0);
    for (auto& s : song.sections)
        conductor.addEvent (juce::MidiMessage::textMetaEvent (6, s.name), s.startBar * 4.0 * tpq);
    conductor.addEvent (juce::MidiMessage::endOfTrack(), song.totalBars() * 4.0 * tpq);
    mf.addTrack (conductor);

    for (auto& part : song.parts)
    {
        juce::MidiMessageSequence seq;
        seq.addEvent (juce::MidiMessage::textMetaEvent (3, part.name), 0);
        const int ch = part.kind == PartKind::Drums ? 10 : 1;
        for (auto& n : part.notes)
        {
            seq.addEvent (juce::MidiMessage::noteOn (ch, n.pitch, juce::jlimit (0.05f, 1.0f, n.velocity)), n.beat * tpq);
            seq.addEvent (juce::MidiMessage::noteOff (ch, n.pitch), (n.beat + n.length) * tpq - 1);
        }
        seq.sort();
        seq.updateMatchedPairs();
        mf.addTrack (seq);
    }
    file.deleteFile();
    juce::FileOutputStream os (file);
    if (os.openedOk()) mf.writeTo (os, 1);
}

// One drum sound's own mix and effects, applied to its audio (the same sound as the studio's dials:
// volume, pan, tone, nostalgia, warmth, chorus, and its own echo and room). The buffer should have room
// at the end for the echo and the room to ring out.
void applyVoiceFx (juce::AudioBuffer<float>& buf, const VoiceFx& v, double sr, double bpm)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buf.getNumSamples();
    if (n < 2 || v.isDefault()) return;
    if (buf.getNumChannels() < 2) { buf.setSize (2, n, true); buf.copyFrom (1, 0, buf, 0, 0, n); }
    float* L = buf.getWritePointer (0);
    float* R = buf.getWritePointer (1);
    const float fsr = (float) sr;
    lp::dsp::SVF toneL, toneR, lpL, lpR, hpL, hpR;
    const float tone = v.tone, warm = v.warmth, nost = v.nostalgia, cho = v.chorus;
    if (std::abs (tone) > 0.005f)
    {
        const float hz = tone < 0 ? 18000.0f * std::pow (0.015f, -tone) : 20.0f * std::pow (60.0f, tone);
        toneL.setup (hz, 0.7f, fsr); toneR.setup (hz, 0.7f, fsr);
    }
    const float lpHz = 20000.0f * std::pow (0.3f, warm) * std::pow (0.2f, nost);
    lpL.setup (lpHz, 0.6f, fsr); lpR.setup (lpHz, 0.6f, fsr);
    hpL.setup (20.0f + 260.0f * nost, 0.6f, fsr); hpR.setup (20.0f + 260.0f * nost, 0.6f, fsr);
    lp::dsp::Delay wobL, wobR, chL, chR;
    wobL.prepare ((int) (0.02 * sr)); wobR.prepare ((int) (0.02 * sr));
    chL.prepare ((int) (0.03 * sr)); chR.prepare ((int) (0.03 * sr));
    const float drive = 1.0f + 2.2f * warm + 1.2f * nost, wobDepth = nost * 0.0028f * fsr;
    float phase = 0.0f, chPhase = 0.0f;
    const float g = juce::Decibels::decibelsToGain (v.gainDb);
    const float gl = g * std::min (1.0f, 1.0f - v.pan), gr = g * std::min (1.0f, 1.0f + v.pan);
    std::vector<float> verbL ((size_t) n, 0.0f), verbR ((size_t) n, 0.0f), delL ((size_t) n, 0.0f), delR ((size_t) n, 0.0f);
    for (int i = 0; i < n; ++i)
    {
        float xl = L[i], xr = R[i];
        if (tone < -0.005f) { toneL.tick (xl); toneR.tick (xr); xl = toneL.lp; xr = toneR.lp; }
        else if (tone > 0.005f) { toneL.tick (xl); toneR.tick (xr); xl = toneL.hp; xr = toneR.hp; }
        if (nost > 0.002f)
        {
            phase += 0.5f * 6.2831853f / fsr;
            if (phase > 6.2831853f) phase -= 6.2831853f;
            const float d = 0.002f * fsr + wobDepth * (0.5f + 0.5f * std::sin (phase));
            wobL.push (xl); wobR.push (xr);
            xl = wobL.read (d); xr = wobR.read (d);
            hpL.tick (xl); hpR.tick (xr);
            xl = hpL.hp; xr = hpR.hp;
            const float mid = 0.5f * (xl + xr), side = 0.5f * (xl - xr) * (1.0f - 0.6f * nost);
            xl = mid + side; xr = mid - side;
        }
        if (warm > 0.002f || nost > 0.002f)
        {
            lpL.tick (xl); lpR.tick (xr);
            xl = std::tanh (lpL.lp * drive) / drive;
            xr = std::tanh (lpR.lp * drive) / drive;
        }
        if (cho > 0.002f)
        {
            chPhase += 0.7f * 6.2831853f / fsr;
            if (chPhase > 6.2831853f) chPhase -= 6.2831853f;
            const float base = 0.012f * fsr, depth = 0.004f * fsr;
            chL.push (xl); chR.push (xr);
            const float wl = chL.read (base + depth * std::sin (chPhase));
            const float wr = chR.read (base + depth * std::cos (chPhase * 1.13f));
            xl = xl * (1.0f - 0.35f * cho) + wl * 0.75f * cho;
            xr = xr * (1.0f - 0.35f * cho) + wr * 0.75f * cho;
        }
        xl *= gl; xr *= gr;
        L[i] = xl; R[i] = xr;
        verbL[(size_t) i] = xl * v.space; verbR[(size_t) i] = xr * v.space;
        delL[(size_t) i] = xl * v.echo;   delR[(size_t) i] = xr * v.echo;
    }
    if (v.echo > 0.002f)
    {
        // the studio's echo: a dotted 8th at the song's tempo, bouncing left / right
        lp::dsp::Delay dL, dR;
        lp::dsp::SVF fL, fR;
        dL.prepare ((int) (2.6 * sr)); dR.prepare ((int) (2.6 * sr));
        fL.setup (3500.0f, 0.6f, fsr); fR.setup (3500.0f, 0.6f, fsr);
        const float dTime = juce::jlimit (100.0f, (float) (2.5 * sr), (float) (0.75 * 60.0 / std::max (40.0, bpm) * sr));
        for (int i = 0; i < n; ++i)
        {
            const float eL = dL.read (dTime), eR = dR.read (dTime);
            fL.tick (eL); fR.tick (eR);
            dL.push (delL[(size_t) i] + fR.lp * 0.42f);
            dR.push (delR[(size_t) i] * 0.3f + fL.lp * 0.42f);
            L[i] += eL * 0.8f; R[i] += eR * 0.8f;
            verbL[(size_t) i] += eL * 0.25f; verbR[(size_t) i] += eR * 0.25f;
        }
    }
    if (v.space > 0.002f || v.echo > 0.002f)
    {
        juce::Reverb room;
        room.setSampleRate (sr);
        juce::Reverb::Parameters rp;
        rp.roomSize = 0.82f; rp.damping = 0.45f; rp.wetLevel = 1.0f; rp.dryLevel = 0.0f; rp.width = 1.0f;
        room.setParameters (rp);
        room.processStereo (verbL.data(), verbR.data(), n);
        for (int i = 0; i < n; ++i) { L[i] += verbL[(size_t) i] * 0.55f; R[i] += verbR[(size_t) i] * 0.55f; }
    }
}

// Renders one part on its own (also used when you swap, reverse or re-pitch a track in the studio).
// one hit of a drum track's kit, on its own (for hearing it while you edit the pattern)
juce::AudioBuffer<float> renderDrumHit (const Song& song, const Part& part, int note, double sr)
{
    juce::ScopedNoDenormals noDenormals;
    const int len = (int) (1.4 * sr);
    juce::AudioBuffer<float> out (2, len);
    out.clear();
    if (! part.kitIn (song).empty())
    {
        const auto& kitMap = part.kitIn (song);
        auto it = kitMap.find (note);
        if (it == kitMap.end()) return {};
        auto a = loadAudio (it->second, sr);
        const int n = std::min (len, a.getNumSamples());
        for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, 0, a, ch, 0, n);
        out.setSize (2, std::max (1, n), true);
    }
    else
    {
        DrumKit kit;
        kit.prepare (sr, part.variant);
        kit.hit (note, 1.0f);
        constexpr int block = 256;
        for (int pos = 0; pos < len; pos += block)
        {
            const int n = std::min (block, len - pos);
            kit.render (out.getWritePointer (0, pos), out.getWritePointer (1, pos), n);
        }
    }
    if (part.pitchSemis != 0) pitchShiftSimple (out, part.pitchSemis, sr);
    if (auto vf = part.voiceFx.find (note); vf != part.voiceFx.end() && ! vf->second.isDefault())
    {
        // its own effects, with room for the echo and the room to ring
        const bool tail = vf->second.space > 0.002f || vf->second.echo > 0.002f;
        if (tail) out.setSize (2, out.getNumSamples() + (int) (2.5 * sr), true, true);
        applyVoiceFx (out, vf->second, sr, song.bpm);
    }
    return out;
}

// A flipped sample: each bar cut into slices (16ths for drums, 8ths otherwise) and played in a new order, the way a producer
// chops a loop so it isn't the same one everyone has: the downbeats stay put (it still grooves), other slices swap with
// ones in the same place in the beat, some play backwards, some stutter. The pattern repeats with the loop, so it's a
// riff, not noise. Stays in the song's time and key (slices are only moved, never re-pitched).
static void flipSlices (juce::AudioBuffer<float>& buf, double sr, double spb, const Song& song, const Part& part)
{
    const bool drums = part.name.find ("drum") != std::string::npos || part.name.find ("perc") != std::string::npos;
    const int steps = drums ? 16 : 8;
    const double barLen = spb * 4.0, stepLen = barLen / steps;
    const int n = buf.getNumSamples(), chans = buf.getNumChannels();
    const float amt = juce::jlimit (1, 3, part.flipAmount) / 3.0f; // 0.33 .. 1
    const int cycle = std::max (1, std::min (4, part.loopBars > 0 ? part.loopBars : 2)); // (the pattern repeats with the loop)
    const juce::AudioBuffer<float> src (buf);
    const int fade = std::max (8, (int) (0.003 * sr));
    for (int bar = 0; bar < song.totalBars(); ++bar)
    {
        const int b0 = (int) std::round (bar * barLen);
        if (b0 >= n) break;
        // (only bars where it plays)
        if (src.getMagnitude (0, b0, std::min ((int) barLen, n - b0)) < 1e-4f) continue;
        juce::Random rng ((juce::int64) part.flipSeed * 7919 + (bar % cycle) * 104729);
        std::vector<int> from ((size_t) steps);
        std::vector<bool> back ((size_t) steps, false);
        for (int k = 0; k < steps; ++k)
        {
            from[(size_t) k] = k;
            const bool downbeat = k % (steps / 2) == 0;   // 1 and 3 stay
            if (downbeat) continue;
            if (rng.nextFloat() < 0.35f + 0.5f * amt)
            {
                // a slice from the same place in a beat (an offbeat for an offbeat), so the groove holds
                const int per = steps / 4, pos = k % per;
                from[(size_t) k] = rng.nextInt (4) * per + (rng.nextFloat() < 0.25f * amt ? rng.nextInt (per) : pos);
            }
            back[(size_t) k] = rng.nextFloat() < (drums ? 0.06f : 0.14f) * amt * 1.5f;
        }
        // the end of every other bar: a stutter (the slice before, repeated)
        if (bar % 2 == 1 && rng.nextFloat() < 0.4f + 0.4f * amt)
            for (int k = steps - (drums ? 3 : 2); k < steps; ++k) from[(size_t) k] = from[(size_t) (steps - (drums ? 4 : 3))];
        for (int k = 0; k < steps; ++k)
        {
            const int d0 = b0 + (int) std::round (k * stepLen), s0 = b0 + (int) std::round (from[(size_t) k] * stepLen);
            const int len = std::min ({ (int) std::round (stepLen), n - d0, n - s0 });
            if (len <= 0) continue;
            // (a fade only where the slice is cut from something else, so straight runs stay seamless)
            const bool joinPrev = k > 0 && ! back[(size_t) k] && ! back[(size_t) k - 1] && from[(size_t) k] == from[(size_t) k - 1] + 1;
            const bool joinNext = k + 1 < steps && ! back[(size_t) k] && ! back[(size_t) k + 1] && from[(size_t) k + 1] == from[(size_t) k] + 1;
            for (int ch = 0; ch < chans; ++ch)
            {
                auto* d = buf.getWritePointer (ch);
                const auto* s = src.getReadPointer (ch);
                for (int i = 0; i < len; ++i)
                {
                    float g = 1.0f;
                    if (! joinPrev && i < fade) g = (float) i / fade;
                    if (! joinNext && len - 1 - i < fade) g = std::min (g, (float) (len - 1 - i) / fade);
                    d[d0 + i] = (back[(size_t) k] ? s[s0 + len - 1 - i] : s[s0 + i]) * g;
                }
            }
        }
    }
}

bool renderPart (const Song& song, const Part& partIn, const RenderSettings& rs, juce::AudioBuffer<float>& buf,
                 std::function<bool()> shouldStop)
{
    juce::ScopedNoDenormals noDenormals;
    const double sr = rs.sampleRate;
    const double spb = sr * 60.0 / song.bpm;
    const int total = (int) ((song.totalBars() * 4.0 + 8.0) * spb);
    constexpr int block = 256;
    lp::Weather calm;
    calm.rainMm = 0; calm.windKmh = 6; calm.cloudPct = 30; calm.temperatureC = 18; calm.humidityPct = 60;
    buf.setSize (2, total);
    buf.clear();
    bool loopPitched = false; // a loop moved into key while it was being stretched

    // your edits: transposing a played part moves its notes, so it stays clean and in time
    Part part = partIn;
    if (part.kind != PartKind::Loop && part.kind != PartKind::Drums && part.pitchSemis != 0)
        for (auto& n : part.notes) n.pitch += part.pitchSemis;

    // drums get the song's feel (swing, a laid-back snare, accents), from the reference or its style
    if (part.kind == PartKind::Drums && song.groove.valid && song.groove.amount > 0.0f)
        for (auto& n : part.notes)
        {
            const double ms = grooveJitterMs (song.groove, n.beat, n.pitch);
            applyGroove (song.groove, n.beat, n.velocity, n.pitch);
            n.beat = std::max (0.0, n.beat + ms / 1000.0 * song.bpm / 60.0);
        }
    const auto events = eventsFor (part, spb);
    size_t next = 0;
    auto* L = buf.getWritePointer (0);
    auto* R = buf.getWritePointer (1);

    if (part.kind == PartKind::Fx)
    {
        // transition sounds from your library: risers whose peak lands on the downbeat, impacts on it...
        std::map<juce::String, juce::AudioBuffer<float>> cache;
        for (auto& h : part.fxHits)
        {
            auto& a = cache[h.path];
            if (a.getNumSamples() == 0) a = loadAudio (h.path, sr);
            const int n = a.getNumSamples();
            if (n == 0) continue;
            int anchor = 0;
            if (h.peakOnBeat)
            {
                // the loudest 20 ms of the sound
                const int w = (int) (0.02 * sr);
                double best = -1;
                for (int i = 0; i + w <= n; i += w / 2)
                {
                    double e = 0;
                    for (int k = i; k < i + w; ++k) e += std::abs (a.getSample (0, k)) + std::abs (a.getSample (1, k));
                    if (e > best) { best = e; anchor = i + w / 2; }
                }
            }
            // level it: its loudest 300 ms set to the hit's target loudness (gainDb), whatever the file's own level
            float hitGainDb = h.gainDb;
            {
                const int w = std::min (n, (int) (0.3 * sr));
                double best = 0;
                for (int i = 0; i + w <= n; i += w / 4)
                {
                    double e = 0;
                    for (int k = i; k < i + w; k += 2) e += (double) a.getSample (0, k) * a.getSample (0, k) + (double) a.getSample (1, k) * a.getSample (1, k);
                    best = std::max (best, e / w);
                }
                if (best > 1e-12) hitGainDb = h.gainDb - (float) (10.0 * std::log10 (best));
                hitGainDb = std::min (hitGainDb, 6.0f);
            }
            const int at = (int) std::round (h.beat * spb) - anchor;
            const int maxLen = (int) (8 * 4 * spb); // never longer than 8 bars
            // a very long riser starts part-way (faded in); nothing plays before the song starts
            const int firstUsed = std::max ({ 0, -at, anchor - maxLen });
            const float g = juce::Decibels::decibelsToGain (hitGainDb);
            const int fadeIn = (int) (0.03 * sr);
            for (int ch = 0; ch < 2; ++ch)
            {
                const float* src = a.getReadPointer (ch);
                float* dst = buf.getWritePointer (ch);
                for (int i = firstUsed; i < n && i < firstUsed + maxLen * 2; ++i)
                {
                    const int o = at + i;
                    if (o < 0 || o >= total) continue;
                    const float fi = firstUsed > 0 ? std::min (1.0f, (float) (i - firstUsed) / (float) fadeIn) : 1.0f;
                    dst[o] += src[i] * g * fi;
                }
            }
        }
    }
    else if (part.kind == PartKind::Melody && part.samplePath.isNotEmpty()
             && (juce::String (part.name).startsWith ("vocal (") || juce::String (part.name).startsWith ("layer (")))
    {
        // your (tuned) vocal, from the start of the song, as you recorded it (the whole song long)
        auto v = loadAudio (part.samplePath, sr, 1.0, 900.0);
        // your pitch (- / +): moved without changing its timing; a voice keeps its own character (formants)
        if (part.pitchSemis != 0 && v.getNumSamples() > 0)
        {
            const bool sung = juce::String (part.name).startsWith ("vocal (") || juce::String (part.name).containsIgnoreCase ("vocal");
            v = stretchAndPitch (v, v.getNumSamples(), part.pitchSemis, sr, false, sung);
        }
        const int n = std::min (v.getNumSamples(), total);
        for (int i = 0; i < n; ++i) { L[i] += v.getSample (0, i); R[i] += v.getSample (1, i); }
    }
    else if (part.samplePath.isNotEmpty() && (part.kind == PartKind::Bass || (part.kind == PartKind::Melody && part.name == "hook")))
    {
        // a sound from your library played as notes (an 808, a pluck, keys): each note re-pitches it
        // from its own pitch; a glide note slides there from the note before, without starting again
        auto src = loadAudio (part.samplePath, sr);
        const int n = src.getNumSamples();
        if (n > 64)
        {
            double root = rootNoteOf (src, sr, part.kind == PartKind::Bass ? 36.0 : 60.0, part.samplePath);
            const float* a = src.getReadPointer (0);
            const float* b = src.getReadPointer (1);
            double pos = 0.0, ratio = 1.0;
            long voiceEnd = -1;
            const int rel = (int) ((part.kind == PartKind::Bass ? 0.03 : 0.12) * sr), glideLen = (int) (0.07 * sr);
            auto notes = part.notes;
            std::sort (notes.begin(), notes.end(), [] (const Note& x, const Note& y) { return x.beat < y.beat; });
            for (size_t k = 0; k < notes.size(); ++k)
            {
                const auto& note = notes[k];
                const long s0 = (long) (note.beat * spb);
                const long nextStart = k + 1 < notes.size() ? (long) (notes[k + 1].beat * spb) : (long) total;
                const long s1 = std::min ((long) ((note.beat + note.length) * spb), nextStart);
                const double target = std::pow (2.0, (note.pitch - root) / 12.0);
                const bool slide = note.glide && voiceEnd >= s0 - (long) (0.02 * sr);
                const double from = slide ? ratio : target;
                if (! slide) pos = 0.0;
                const float g = 0.85f * note.velocity;
                const bool legato = k + 1 < notes.size() && notes[k + 1].glide && s1 >= nextStart - (long) (0.02 * sr);
                const long end = legato ? nextStart : s1 + rel;
                for (long i = s0; i < end && i < total; ++i)
                {
                    const long d = i - s0;
                    ratio = slide && d < glideLen ? from + (target - from) * (double) d / glideLen : target;
                    const int i0 = (int) pos;
                    if (i0 + 1 >= n) break;
                    const float f = (float) (pos - i0);
                    float env = std::min (1.0f, (float) d / (float) std::max (1, (int) (0.002 * sr)));
                    if (slide) env = 1.0f;
                    if (! legato && i >= s1) env *= 1.0f - (float) (i - s1) / (float) rel;
                    L[i] += (a[i0] + (a[i0 + 1] - a[i0]) * f) * g * env;
                    R[i] += (b[i0] + (b[i0 + 1] - b[i0]) * f) * g * env;
                    pos += ratio;
                }
                voiceEnd = legato ? nextStart : s1;
            }
        }
    }
    else if (part.kind == PartKind::Melody && part.samplePath.isNotEmpty())
    {
        // vocal chops: the recording cut into short sung slices (its pads), each note playing one of them,
        // moved onto the note it's written on (or as it was sung, for a hit you played on the pads)
        auto src = loadAudio (part.samplePath, sr);
        const int n = src.getNumSamples();
        const auto chopList = part.chops.empty() ? lp::song::chops::find (src, sr) : part.chops;
        std::vector<size_t> sungIdx;
        for (size_t i = 0; i < chopList.size(); ++i) if (chopList[i].midi > 0.0) sungIdx.push_back (i);
        if (! chopList.empty() && n > 0)
        {
            int k = 0;
            for (auto& note : part.notes)
            {
                size_t which;
                if (note.slice >= 0 && note.slice < (int) chopList.size()) which = (size_t) note.slice;
                else if (! sungIdx.empty()) which = sungIdx[(size_t) ((k++ + note.pitch * 3) % (int) sungIdx.size())];
                else which = (size_t) (k++ % (int) chopList.size());
                const auto& sl = chopList[which];
                double shift = sl.midi > 0.0 ? note.pitch - sl.midi : 0.0;
                shift -= 12.0 * std::round (shift / 12.0);                    // its own octave: the voice stays natural
                const double ratio = std::pow (2.0, shift / 12.0);
                const int s0 = (int) (sl.start * sr), slen = std::min ((int) (sl.len * sr), n - s0);
                if (s0 < 0 || slen <= 2) continue;
                const long at = (long) (note.beat * spb);
                const int outLen = (int) std::min ((double) slen / ratio, note.length * spb);
                const int fadeIn = (int) (0.004 * sr), fadeOut = std::min (outLen / 3, (int) (0.03 * sr));
                for (int i = 0; i < outLen && at + i < total; ++i)
                {
                    if (at + i < 0) continue;
                    const double pos = s0 + i * ratio;
                    const int i0 = (int) pos;
                    if (i0 + 1 >= n) break;
                    const float f = (float) (pos - i0);
                    const float g = note.velocity * std::min ({ 1.0f, (float) i / (float) fadeIn, (float) (outLen - i) / (float) std::max (1, fadeOut) });
                    L[at + i] += (src.getSample (0, i0) + (src.getSample (0, i0 + 1) - src.getSample (0, i0)) * f) * g;
                    R[at + i] += (src.getSample (1, i0) + (src.getSample (1, i0 + 1) - src.getSample (1, i0)) * f) * g;
                }
            }
        }
    }
    else if (part.kind == PartKind::Loop)
    {
        // your loop, fitted exactly to its bars and placed at each start
        const double barSamples = 4.0 * spb;
        auto probe = loadAudio (part.samplePath, sr);
        if (probe.getNumSamples() > 0 && part.loopBars > 0)
        {
            const double stretch = (double) probe.getNumSamples() / (part.loopBars * barSamples);
            // tiny differences: a resample is cleanest. bigger ones: stretch the time, keep the pitch,
            // so every loop lands exactly on the song's (your DAW's) tempo
            const bool drumLoop = juce::String (part.name).containsIgnoreCase ("drum");
            const int loopLen = (int) std::round (part.loopBars * barSamples);
            const int semis = part.pitchSemis + part.autoTune; // your pitch + tuning into the song's key
            const bool voice = juce::String (part.name).containsIgnoreCase ("vocal") || juce::String (part.name).containsIgnoreCase ("vox");
            juce::AudioBuffer<float> loop;
            if (drumLoop)
                loop = std::abs (stretch - 1.0) < 0.012 ? loadAudio (part.samplePath, sr, stretch)
                                                        : beatStretch (probe, loopLen, part.loopBars); // crisp hits
            else if (std::abs (stretch - 1.0) < 0.012 && semis == 0)
                loop = loadAudio (part.samplePath, sr, stretch);                                       // already fits
            else
            {
                // smooth notes: fitted to the bars and moved into key in one clean pass
                loop = stretchAndPitch (probe, loopLen, semis, sr, true, voice);
                loopPitched = true;
            }
            // just a part of the sample (whole bars): that part repeats wherever the loop played
            if (part.regionBars > 0 && part.regionStart >= 0 && part.regionStart + part.regionBars <= part.loopBars && part.regionBars < part.loopBars)
            {
                const int a0 = (int) std::round (part.regionStart * barSamples), len = (int) std::round (part.regionBars * barSamples);
                juce::AudioBuffer<float> cut (2, len);
                for (int ch = 0; ch < 2; ++ch) cut.copyFrom (ch, 0, loop, ch, std::min (a0, loop.getNumSamples() - len), len);
                loop = std::move (cut);
                const int oldL = part.loopBars, R = part.regionBars;
                for (auto& v : part.barSrc) if (v >= 0) v %= R;
                std::vector<int> starts;
                for (int st0 : part.loopStartBars) for (int b = st0; b < st0 + oldL; b += R) starts.push_back (b);
                part.loopStartBars = starts;
                part.loopBars = R;
            }
            if (part.reversed) reverse (loop);
            const int ln = loop.getNumSamples();
            if (! part.barSrc.empty())
                renderArrangedLoop (part, loop, barSamples, buf, total, sr); // a produced arrangement, bar by bar
            else
            {
                // back-to-back repeats run straight on (the loop's end leads into its start); only a repeat with
                // nothing before or after it fades in or out
                std::set<int> starts (part.loopStartBars.begin(), part.loopStartBars.end());
                for (int startBar : part.loopStartBars)
                {
                    const int at = (int) (startBar * barSamples);
                    const bool before = starts.count (startBar - part.loopBars) > 0, after = starts.count (startBar + part.loopBars) > 0;
                    for (int ch = 0; ch < 2; ++ch)
                        for (int i = 0; i < ln && at + i < total; ++i)
                        {
                            const float fade = std::min ({ 1.0f, before ? 1.0f : (float) i / 240.0f, after ? 1.0f : (float) (ln - i) / 240.0f });
                            buf.getWritePointer (ch)[at + i] += loop.getSample (ch, i) * fade;
                        }
                }
            }
        }
    }
    else if (part.kind == PartKind::Drums)
    {
        // the kit: your samples (or a sliced loop), or the built-in drum machine
        const bool sampled = ! part.kitIn (song).empty();
        std::map<int, juce::AudioBuffer<float>> sounds;
        if (sampled)
            for (auto& [note, path] : part.kitIn (song))
            {
                auto a = loadAudio (path, sr);
                if (a.getNumSamples() > 0) sounds[note] = std::move (a);
            }
        // plays the hits that `want` picks into L / R
        auto playHits = [&] (float* oL, float* oR, std::function<bool (int)> want)
        {
            SampleKit sk;
            DrumKit dk;
            if (sampled) sk.sounds = sounds; else dk.prepare (sr, part.variant);
            size_t e = 0;
            for (int pos = 0; pos < total; pos += block)
            {
                const int n = std::min (block, total - pos);
                int done = 0;
                auto play = [&] (int from, int count) { if (sampled) sk.render (oL + pos + from, oR + pos + from, count); else dk.render (oL + pos + from, oR + pos + from, count); };
                while (e < events.size() && events[e].sample < pos + n)
                {
                    const int at = (int) std::max<long> (0, events[e].sample - pos);
                    if (at > done) { play (done, at - done); done = at; }
                    if (events[e].on && want (events[e].note)) { if (sampled) sk.hit (events[e].note, events[e].vel); else dk.hit (events[e].note, events[e].vel); }
                    ++e;
                }
                play (done, n - done);
            }
        };
        // a drum with its own effects plays on its own, through them, then joins the rest
        std::set<int> own;
        for (auto& [note, f] : part.voiceFx) if (! f.isDefault()) own.insert (note);
        playHits (L, R, [&own] (int note) { return own.count (note) == 0; });
        for (int note : own)
        {
            juce::AudioBuffer<float> v (2, total);
            v.clear();
            playHits (v.getWritePointer (0), v.getWritePointer (1), [note] (int x) { return x == note; });
            applyVoiceFx (v, part.voiceFx.at (note), sr, song.bpm);
            buf.addFrom (0, 0, v, 0, 0, total);
            buf.addFrom (1, 0, v, 1, 0, total);
        }
        next = events.size();
    }
    else if (part.kind == PartKind::Bass)
    {
        BassSynth bass;
        bass.sr = sr;
        bass.is808 = part.variant == 1;
        for (int pos = 0; pos < total; pos += block)
        {
            const int n = std::min (block, total - pos);
            int done = 0;
            while (next < events.size() && events[next].sample < pos + n)
            {
                const int at = (int) std::max<long> (0, events[next].sample - pos);
                if (at > done) { bass.render (L + pos + done, R + pos + done, at - done); done = at; }
                if (events[next].on) bass.noteOn (events[next].note, events[next].vel); else bass.noteOff (events[next].note);
                ++next;
            }
            bass.render (L + pos + done, R + pos + done, n - done);
        }
    }
    else
    {
        // tonal parts are played by Live Planet's own instruments, exactly like keys on a keyboard
        lp::Engine engine;
        engine.prepare (sr);
        engine.jumpToWeather (calm);
        lp::EngineParams p;
        p.sound = part.sound;
        p.nostalgia = rs.nostalgia;
        p.space = part.kind == PartKind::Pad ? std::min (1.0f, rs.space + 0.2f) : rs.space;
        p.warmth = rs.warmth;
        p.echo = part.kind == PartKind::Melody || part.kind == PartKind::Counter ? rs.echo : rs.echo * 0.3f;
        p.atmos = 0.0f;
        p.position = 0.1f;
        lp::MidiOut midi;
        std::bitset<128> held;
        std::vector<lp::KeyEvent> keys;
        for (int pos = 0; pos < total; pos += block)
        {
            const int n = std::min (block, total - pos);
            keys.clear();
            while (next < events.size() && events[next].sample < pos + n)
            {
                const auto& e = events[next++];
                keys.push_back ({ (int) std::max<long> (0, e.sample - pos), e.note, e.vel, e.on });
            }
            lp::TransportInfo t;
            t.shouldPlay = false;
            t.keys = keys.data();
            t.numKeys = (int) keys.size();
            midi.clear();
            engine.process (L + pos, R + pos, n, p, t, held, midi);
            lp::VisualEvent ve;
            while (engine.popVisual (ve)) {}
            if (shouldStop && (pos & 0xffff) == 0 && shouldStop()) return false;
        }
    }

    // level each part to where it sits in a mix, by how loud it SOUNDS (the K-weighting behind LUFS:
    // lows count less, the presence range more), measured over the moments it really plays
    {
        auto hp = Biquad::make (3, sr, 60.0, 0.5, 0), shelf = Biquad::make (2, sr, 1500.0, 0.7, 4.0);
        std::vector<float> kw ((size_t) total);
        for (int i = 0; i < total; ++i) kw[(size_t) i] = shelf.process (hp.process (0.5f * (L[i] + R[i])));
        // 400 ms blocks; the ones that are playing (within 20 dB of the loudest) count
        const int blk = (int) (0.4 * sr);
        std::vector<double> blocks;
        for (int i = 0; i + blk <= total; i += blk / 2)
        {
            double e = 0;
            for (int k = i; k < i + blk; k += 2) e += (double) kw[(size_t) k] * kw[(size_t) k];
            blocks.push_back (e / (blk / 2));
        }
        double loudest = 0;
        for (double e : blocks) loudest = std::max (loudest, e);
        double sum = 0; int count = 0;
        for (double e : blocks) if (e > loudest * 0.01 && e > 1e-10) { sum += e; ++count; }
        const double rms = count > 0 ? std::sqrt (sum / count) : 0.0;
        float targetDb = -16.0f;
        switch (part.kind)
        {
            case PartKind::Chords:  targetDb = -17.0f; break;
            case PartKind::Melody:  targetDb = -15.5f; break;
            case PartKind::Counter: targetDb = -23.0f; break;
            case PartKind::Pad:     targetDb = -24.0f; break;
            case PartKind::Bass:    targetDb = -16.0f; break;
            case PartKind::Drums:   targetDb = -17.0f; break;
            case PartKind::Loop:    targetDb = -21.0f; break;
            case PartKind::Fx:      targetDb = 0.0f; break; // each transition sound already has its own level
        }
        targetDb += part.mixDb;
        if (rms > 1e-6 && part.kind != PartKind::Fx)
        {
            const float g = juce::jlimit (0.05f, 12.0f, juce::Decibels::decibelsToGain (targetDb) / (float) rms);
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* d = buf.getWritePointer (ch);
                for (int i = 0; i < total; ++i) d[i] = std::tanh (d[i] * g * 1.05f) / 1.05f;
            }
        }
    }

    // loops and drums are re-pitched with a pitch shifter (keeps them in time)
    const int shift = loopPitched ? 0 : part.pitchSemis + (part.kind == PartKind::Loop ? part.autoTune : 0); // your pitch + tuning into the song's key
    if (shift != 0 && (part.kind == PartKind::Loop || part.kind == PartKind::Drums))
        pitchShift (buf, shift, sr);
    if (part.kind != PartKind::Fx)
        grainify (buf, sr, song.bpm, part, (juce::uint32) std::hash<std::string>{} (part.name));
    // its place left / right (equal-power, so it doesn't jump in level)
    if (std::abs (part.pan) > 0.01f)
    {
        const float a = (juce::jlimit (-1.0f, 1.0f, part.pan) + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        const float gl = std::cos (a) * 1.41421356f, gr = std::sin (a) * 1.41421356f;
        buf.applyGain (0, 0, buf.getNumSamples(), std::min (1.0f, gl));
        buf.applyGain (1, 0, buf.getNumSamples(), std::min (1.0f, gr));
    }
    shapeForMix (song, part, buf, sr);
    if (part.flipSeed != 0) flipSlices (buf, sr, spb, song, part);
    // a sample that follows the song's chords: each stretch on one chord moved to that chord (its timing kept)
    if (! part.slotShift.empty())
    {
        const juce::AudioBuffer<float> src (buf);
        const double slotLen = spb * 2.0;
        const int n = buf.getNumSamples(), pad = (int) (0.06 * sr), fade = (int) (0.012 * sr);
        auto shiftOf = [&part] (int s) { auto f = part.slotShift.find (s); return f != part.slotShift.end() ? f->second : 0; };
        const int slots = (int) std::ceil (n / slotLen);
        for (int s = 0; s < slots;)
        {
            const int semis = shiftOf (s);
            int e = s + 1;
            while (e < slots && shiftOf (e) == semis) ++e;
            if (semis != 0)
            {
                const int a = (int) std::round (s * slotLen), b = std::min (n, (int) std::round (e * slotLen));
                const int a0 = std::max (0, a - pad), b0 = std::min (n, b + pad);
                if (b > a && b0 - a0 > 64)
                {
                    juce::AudioBuffer<float> seg (2, b0 - a0);
                    for (int ch = 0; ch < 2; ++ch) seg.copyFrom (ch, 0, src, std::min (ch, src.getNumChannels() - 1), a0, b0 - a0);
                    auto moved = stretchAndPitch (seg, b0 - a0, semis, sr, false, false);
                    for (int ch = 0; ch < 2; ++ch)
                    {
                        auto* d = buf.getWritePointer (ch);
                        for (int i = a; i < b; ++i)
                        {
                            float w = 1.0f; // (blended in and out over a few ms where the chord changes)
                            if (i - a < fade) w = (float) (i - a) / fade;
                            if (b - 1 - i < fade) w = std::min (w, (float) (b - 1 - i) / fade);
                            d[i] = d[i] * (1.0f - w) + moved.getSample (ch, i - a0) * w;
                        }
                    }
                }
            }
            s = e;
        }
    }
    // clips you copied or moved: each of those bars plays another bar's sound (from the track as it was made)
    if (! part.barFrom.empty())
    {
        const juce::AudioBuffer<float> src (buf);
        const double barLen = spb * 4.0;
        const int n = buf.getNumSamples(), fade = (int) (0.004 * sr);
        for (auto [to, from] : part.barFrom)
        {
            const int d0 = (int) std::round (to * barLen), s0 = (int) std::round (from * barLen), len = (int) std::round (barLen);
            if (d0 < 0 || s0 < 0 || d0 >= n || s0 >= n) continue;
            // (straight on from the bar before when that was copied from the bar before its source: no fade, seamless)
            const bool joinPrev = part.barFrom.count (to - 1) && part.barFrom.at (to - 1) == from - 1;
            const bool joinNext = part.barFrom.count (to + 1) && part.barFrom.at (to + 1) == from + 1;
            for (int ch = 0; ch < buf.getNumChannels(); ++ch)
            {
                auto* d = buf.getWritePointer (ch);
                const auto* s = src.getReadPointer (ch);
                for (int i = 0; i < len && d0 + i < n && s0 + i < n; ++i)
                {
                    float g = 1.0f;
                    if (! joinPrev && i < fade) g = (float) i / fade;
                    if (! joinNext && len - 1 - i < fade) g = std::min (g, (float) (len - 1 - i) / fade);
                    d[d0 + i] = s[s0 + i] * g;
                }
            }
        }
    }
    // the bars you deleted: silent (a short fade either side, so nothing clicks; a tail rings on a little)
    if (! part.silentBars.empty())
    {
        const double barLen = spb * 4.0;
        const int fade = (int) (0.008 * sr), tail = (int) (0.06 * sr);
        const int n = buf.getNumSamples();
        for (int bar : part.silentBars)
        {
            if (part.silentBars.count (bar - 1)) continue; // (a run of bars: done from its first)
            int last = bar;
            while (part.silentBars.count (last + 1)) ++last;
            const int a = juce::jlimit (0, n, (int) std::round (bar * barLen) + (bar > 0 ? tail : 0));
            const int b = juce::jlimit (0, n, (int) std::round ((last + 1) * barLen));
            if (b <= a) continue;
            for (int ch = 0; ch < buf.getNumChannels(); ++ch)
            {
                auto* d = buf.getWritePointer (ch);
                for (int i = 0; i < fade && a - fade + i >= 0; ++i) d[a - fade + i] *= 1.0f - (float) i / (float) fade;
                for (int i = a; i < b; ++i) d[i] = 0.0f;
                for (int i = 0; i < fade && b + i < n; ++i) d[b + i] *= (float) i / (float) fade;
            }
        }
    }
    return true;
}

bool render (const Song& song, const RenderSettings& rs, const juce::File& folder, Rendered& out,
             std::function<void (float)> progress, std::function<bool()> shouldStop)
{
    // tiny fading numbers (after a track goes quiet) are very slow on CPUs: treat them as zero
    juce::ScopedNoDenormals noDenormals;
    folder.createDirectory();
    const auto stemDir = folder.getChildFile ("stems");
    stemDir.createDirectory();
    out = {};
    out.folder = folder;

    const double sr = rs.sampleRate;
    const double spb = sr * 60.0 / song.bpm;
    const int total = (int) ((song.totalBars() * 4.0 + 8.0) * spb); // a few extra beats for tails
    constexpr int block = 256;
    juce::AudioBuffer<float> mix (2, total);
    mix.clear();

    lp::Weather calm; // a gentle, neutral sky for the song's sounds
    calm.rainMm = 0; calm.windKmh = 6; calm.cloudPct = 30; calm.temperatureC = 18; calm.humidityPct = 60;

    double tmark = juce::Time::getMillisecondCounterHiRes();
    auto mark = [&tmark] (const char* what) { if (! std::getenv ("LP_TIMING")) return; const double n = juce::Time::getMillisecondCounterHiRes();
                                              std::fprintf (stderr, "[timing]   render: %-22s %.1f s\n", what, (n - tmark) / 1000.0); tmark = n; };
    Song dry = song;          // first pass: every track, without the reference's tone yet
    dry.mixShaped = false;
    for (size_t pi = 0; pi < song.parts.size(); ++pi)
    {
        const auto& part = song.parts[pi];
        juce::AudioBuffer<float> buf;
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        if (! renderPart (dry, part, rs, buf, shouldStop)) return false;
        if (std::getenv ("LP_TIMING")) std::fprintf (stderr, "[timing]   part %-14s %.1f s\n", part.name.c_str(), (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0);
        for (int ch = 0; ch < 2; ++ch)
            mix.addFrom (ch, 0, buf, ch, 0, total, 1.6f);

        const auto stem = stemDir.getChildFile (juce::String ((int) pi + 1).paddedLeft ('0', 2) + " " + part.name + ".wav");
        mark ("part rendered+mixed");
        if (! writeWav (stem, buf, sr)) return false;
        mark ("stem written");
        out.stems.add (stem);
        if (progress) progress ((float) (pi + 1) / (float) (song.parts.size() + 1));
        if (shouldStop && shouldStop()) return false;
    }

    // the reference's tone: compare the mix with it, and gently move every track toward it
    // (half way, at most 6 dB per octave, smoothed) - its overall colour, never its audio
    std::array<float, 10> eq {};
    if (song.refTone.valid)
    {
        std::vector<float> mono ((size_t) total);
        for (int i = 0; i < total; ++i) mono[(size_t) i] = 0.5f * (mix.getSample (0, i) + mix.getSample (1, i));
        const auto now = measureTone (mono.data(), total, sr);
        if (now.valid)
        {
            std::array<float, 10> diff {};
            for (int i = 0; i < 10; ++i) diff[(size_t) i] = song.refTone.bandsDb[(size_t) i] - now.bandsDb[(size_t) i];
            for (int i = 0; i < 10; ++i)
            {
                const float a = diff[(size_t) std::max (0, i - 1)], b = diff[(size_t) i], c = diff[(size_t) std::min (9, i + 1)];
                eq[(size_t) i] = juce::jlimit (-6.0f, 6.0f, 0.5f * (0.25f * a + 0.5f * b + 0.25f * c));
            }
            if (sr < 36000.0) eq[9] = 0.0f;
            MatchEq m;
            m.setup (eq, sr);
            m.process (mix, 0, total);
        }
    }
    out.matchEqDb = eq;
    mark ("match eq");

    // loudness like the reference (without one, a calm level)
    double sq = 0;
    for (int ch = 0; ch < 2; ++ch) { auto* d = mix.getReadPointer (ch); for (int i = 0; i < total; ++i) sq += (double) d[i] * d[i]; }
    const float rmsDb = (float) juce::Decibels::gainToDecibels (std::sqrt (sq / (2.0 * total)), -100.0);
    const float target = song.refTone.valid ? juce::jlimit (-20.0f, -10.0f, song.refTone.rmsDb) : -16.0f;
    const float loud = juce::jlimit (-24.0f, 18.0f, target - rmsDb);
    // every track gets the same tone and level, so the stems add up to this mix (and edits match later);
    // but they keep headroom so that, added up, they never clip: the limiter brings them up the rest of the way
    float mixPeak = 0.0f;
    for (int ch = 0; ch < 2; ++ch) mixPeak = std::max (mixPeak, mix.getMagnitude (ch, 0, total));
    const float peakAfter = mixPeak * juce::Decibels::decibelsToGain (loud);
    const float headroomDb = peakAfter > 0.79f ? juce::Decibels::gainToDecibels (0.79f / peakAfter) : 0.0f;
    out.matchGainDb = loud + juce::Decibels::gainToDecibels (1.6f) + headroomDb;
    out.playbackMakeupDb = -headroomDb;
    // every stem gets the same tone and level (a few at a time, on all the computer's cores)
    std::atomic<bool> failed { false };
    std::atomic<int> nextStem { 0 };
    auto retone = [&]
    {
        juce::ScopedNoDenormals nd;
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        for (int k; (k = nextStem++) < out.stems.size() && ! failed.load();)
        {
            const auto& stem = out.stems.getReference (k);
            juce::AudioBuffer<float> b;
            {
                std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (stem));
                if (rd == nullptr) continue;
                b.setSize (2, (int) rd->lengthInSamples);
                rd->read (&b, 0, (int) rd->lengthInSamples, 0, true, true);
            }
            MatchEq se;
            se.setup (eq, sr);
            se.process (b, 0, b.getNumSamples());
            b.applyGain (juce::Decibels::decibelsToGain (out.matchGainDb));
            if (! writeWav (stem, b, sr)) failed = true;
        }
    };
    {
        const int workers = juce::jlimit (1, 4, juce::SystemStats::getNumCpus() - 1);
        std::vector<std::thread> pool;
        for (int w = 1; w < workers; ++w) pool.emplace_back (retone);
        retone();
        for (auto& t : pool) t.join();
    }
    if (failed.load() || (shouldStop && shouldStop())) return false;
    mark ("stems re-toned");
    // the full mix: up to its loudness through a look-ahead limiter (peaks under -1 dB, nothing squashed or distorted)
    {
        lp::Limiter lim;
        lim.prepare (sr, -1.0f);
        lim.process (mix.getWritePointer (0), mix.getWritePointer (1), total, loud);
        const int lat = lim.latency();
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* d = mix.getWritePointer (ch);
            std::memmove (d, d + lat, sizeof (float) * (size_t) (total - lat));
            std::fill (d + total - lat, d + total, 0.0f);
        }
    }
    out.mixFile = folder.getChildFile (song.title + " - full mix.wav");
    if (! writeWav (out.mixFile, mix, sr)) return false;
    out.mixAudio = std::make_shared<juce::AudioBuffer<float>> (std::move (mix));
    mark ("mix written");

    out.midiFile = folder.getChildFile (song.title + ".mid");
    writeMidi (song, out.midiFile);

    // the plan the Ableton helper reads: tempo, sections (for locators) and the files
    auto plan = std::make_unique<juce::DynamicObject>();
    plan->setProperty ("title", song.title);
    plan->setProperty ("bpm", song.bpm);
    plan->setProperty ("key", keyName (song.key, song.minor));
    juce::Array<juce::var> secs;
    for (auto& s : song.sections)
    {
        auto o = std::make_unique<juce::DynamicObject>();
        o->setProperty ("name", juce::String (s.name));
        o->setProperty ("beat", s.startBar * 4);
        o->setProperty ("bars", s.bars);
        secs.add (juce::var (o.release()));
    }
    plan->setProperty ("sections", secs);
    juce::Array<juce::var> stems;
    for (auto& f : out.stems) stems.add (f.getFullPathName());
    plan->setProperty ("stems", stems);
    plan->setProperty ("midi", out.midiFile.getFullPathName());
    plan->setProperty ("mix", out.mixFile.getFullPathName());
    out.planFile = folder.getChildFile ("plan.json");
    out.planFile.replaceWithText (juce::JSON::toString (juce::var (plan.release())));
    if (progress) progress (1.0f);
    return true;
}

} // namespace lp::song
