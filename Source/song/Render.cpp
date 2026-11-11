#include "Song.h"
#include "../engine/Engine.h"
#include <juce_audio_formats/juce_audio_formats.h>

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
        struct V { int note = 0; float vel = 0, env = 0; bool on = false, active = false; double phase = 0; };
        std::array<V, 4> voices {};
        float lowpass = 0;

        bool is808 = false;
        double lastFreq = 0.0;

        void noteOn (int note, float vel)
        {
            for (auto& v : voices) if (v.active && v.on) v.on = false; // one note at a time
            for (auto& v : voices) if (! v.active) { v = { note, vel, 0.0f, true, true, 0.0 }; return; }
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
                        // an 808: a punchy pitch drop at the start, a long round tail, a little grit
                        const double age = v.phase / (2.0 * juce::MathConstants<double>::pi * f);
                        f *= 1.0 + 1.5 * std::exp (-age * 40.0);
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
    // each nudged to where it lines up best with the last one, so it stays smooth)
    juce::AudioBuffer<float> timeStretch (const juce::AudioBuffer<float>& in, int outLen)
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
                    // wrap round: a loop's end leads back into its start
                    dst[o * H + k] += src[si < inLen ? si : si - inLen] * win[(size_t) k];
                }
            }
            for (int k = 0; k < N; ++k) norm[(size_t) (o * H + k)] += win[(size_t) k];
            prev = best;
        }
        juce::AudioBuffer<float> res (2, outLen);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < outLen; ++i)
                res.setSample (ch, i, out.getSample (ch, i) / std::max (0.5f, norm[(size_t) i]));
        return res;
    }

    // like a "beats" warp: cut the loop at each 16th (or 8th), pin every slice to its spot on the
    // song's grid, and stretch each slice on its own, so hits land exactly in time
    juce::AudioBuffer<float> beatStretch (const juce::AudioBuffer<float>& in, int outLen, int bars)
    {
        const int inLen = in.getNumSamples();
        int slices = bars * 16;
        while (slices > bars && inLen / slices < 4096 + 64) slices /= 2;
        if (inLen / slices < 4096 + 64) return timeStretch (in, outLen);
        juce::AudioBuffer<float> out (2, outLen);
        out.clear();
        constexpr int fade = 96;
        for (int k = 0; k < slices; ++k)
        {
            const int s0 = (int) ((juce::int64) inLen * k / slices), s1 = (int) ((juce::int64) inLen * (k + 1) / slices);
            const int o0 = (int) ((juce::int64) outLen * k / slices), o1 = (int) ((juce::int64) outLen * (k + 1) / slices);
            juce::AudioBuffer<float> piece (2, s1 - s0);
            for (int ch = 0; ch < 2; ++ch) piece.copyFrom (ch, 0, in, ch, s0, s1 - s0);
            auto fitted = timeStretch (piece, o1 - o0);
            const int n = std::min (fitted.getNumSamples(), outLen - o0);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < n; ++i)
                {
                    // short fade out at the slice end so the next one starts clean on its beat
                    const float g = std::min (1.0f, (float) (n - i) / fade);
                    out.getWritePointer (ch)[o0 + i] += fitted.getSample (ch, i) * g;
                }
        }
        return out;
    }

    // ---------- audio files from your library
    juce::AudioBuffer<float> loadAudio (const juce::String& path, double targetRate, double stretch = 1.0)
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (juce::File (path)));
        if (rd == nullptr || rd->lengthInSamples <= 0) return {};
        const int len = (int) std::min<juce::int64> (rd->lengthInSamples, (juce::int64) (rd->sampleRate * 60.0));
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

// Renders one part on its own (also used when you swap, reverse or re-pitch a track in the studio).
bool renderPart (const Song& song, const Part& partIn, const RenderSettings& rs, juce::AudioBuffer<float>& buf,
                 std::function<bool()> shouldStop)
{
    const double sr = rs.sampleRate;
    const double spb = sr * 60.0 / song.bpm;
    const int total = (int) ((song.totalBars() * 4.0 + 8.0) * spb);
    constexpr int block = 256;
    lp::Weather calm;
    calm.rainMm = 0; calm.windKmh = 6; calm.cloudPct = 30; calm.temperatureC = 18; calm.humidityPct = 60;
    buf.setSize (2, total);
    buf.clear();

    // your edits: transposing a played part moves its notes, so it stays clean and in time
    Part part = partIn;
    if (part.kind != PartKind::Loop && part.kind != PartKind::Drums && part.pitchSemis != 0)
        for (auto& n : part.notes) n.pitch += part.pitchSemis;

    const auto events = eventsFor (part, spb);
    size_t next = 0;
    auto* L = buf.getWritePointer (0);
    auto* R = buf.getWritePointer (1);

    if (part.kind == PartKind::Loop)
    {
        // your loop, fitted exactly to its bars and placed at each start
        const double barSamples = 4.0 * spb;
        auto probe = loadAudio (part.samplePath, sr);
        if (probe.getNumSamples() > 0 && part.loopBars > 0)
        {
            const double stretch = (double) probe.getNumSamples() / (part.loopBars * barSamples);
            // tiny differences: a resample is cleanest. bigger ones: stretch the time, keep the pitch,
            // so every loop lands exactly on the song's (your DAW's) tempo
            auto loop = std::abs (stretch - 1.0) < 0.012 ? loadAudio (part.samplePath, sr, stretch)
                         : (juce::String (part.name).containsIgnoreCase ("drum")
                                ? beatStretch (probe, (int) std::round (part.loopBars * barSamples), part.loopBars) // crisp hits
                                : timeStretch (probe, (int) std::round (part.loopBars * barSamples)));              // smooth notes
            if (part.reversed) reverse (loop);
            const int ln = loop.getNumSamples();
            for (int startBar : part.loopStartBars)
            {
                const int at = (int) (startBar * barSamples);
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < ln && at + i < total; ++i)
                    {
                        const float fade = std::min ({ 1.0f, (float) i / 240.0f, (float) (ln - i) / 240.0f });
                        buf.getWritePointer (ch)[at + i] += loop.getSample (ch, i) * fade;
                    }
            }
        }
    }
    else if (part.kind == PartKind::Drums && ! song.drumSamples.empty())
    {
        SampleKit kit;
        for (auto& [note, path] : song.drumSamples)
        {
            auto a = loadAudio (path, sr);
            if (a.getNumSamples() > 0) kit.sounds[note] = std::move (a);
        }
        for (int pos = 0; pos < total; pos += block)
        {
            const int n = std::min (block, total - pos);
            int done = 0;
            while (next < events.size() && events[next].sample < pos + n)
            {
                const int at = (int) std::max<long> (0, events[next].sample - pos);
                if (at > done) { kit.render (L + pos + done, R + pos + done, at - done); done = at; }
                if (events[next].on) kit.hit (events[next].note, events[next].vel);
                ++next;
            }
            kit.render (L + pos + done, R + pos + done, n - done);
        }
    }
    else if (part.kind == PartKind::Drums)
    {
        DrumKit kit;
        kit.prepare (sr, part.variant);
        for (int pos = 0; pos < total; pos += block)
        {
            const int n = std::min (block, total - pos);
            int done = 0;
            while (next < events.size() && events[next].sample < pos + n)
            {
                const int at = (int) std::max<long> (0, events[next].sample - pos);
                if (at > done) { kit.render (L + pos + done, R + pos + done, at - done); done = at; }
                if (events[next].on) kit.hit (events[next].note, events[next].vel);
                ++next;
            }
            kit.render (L + pos + done, R + pos + done, n - done);
        }
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

    // level each part to where it sits in a mix (measured only where it actually plays)
    {
        double sum = 0;
        long count = 0;
        for (int i = 0; i < total; i += 4)
        {
            const float v = std::abs (L[i]) + std::abs (R[i]);
            if (v > 2e-4f) { sum += (double) L[i] * L[i] + (double) R[i] * R[i]; ++count; }
        }
        const double rms = count > 0 ? std::sqrt (sum / (2.0 * (double) count)) : 0.0;
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
        }
        if (rms > 1e-6)
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
    const int shift = part.pitchSemis + (part.kind == PartKind::Loop ? part.autoTune : 0); // your pitch + tuning into the song's key
    if (shift != 0 && (part.kind == PartKind::Loop || part.kind == PartKind::Drums))
        pitchShift (buf, shift, sr);
    return true;
}

bool render (const Song& song, const RenderSettings& rs, const juce::File& folder, Rendered& out,
             std::function<void (float)> progress, std::function<bool()> shouldStop)
{
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

    for (size_t pi = 0; pi < song.parts.size(); ++pi)
    {
        const auto& part = song.parts[pi];
        juce::AudioBuffer<float> buf;
        if (! renderPart (song, part, rs, buf, shouldStop)) return false;
        for (int ch = 0; ch < 2; ++ch)
            mix.addFrom (ch, 0, buf, ch, 0, total, 1.6f);

        const auto stem = stemDir.getChildFile (juce::String ((int) pi + 1).paddedLeft ('0', 2) + " " + part.name + ".wav");
        if (! writeWav (stem, buf, sr)) return false;
        out.stems.add (stem);
        if (progress) progress ((float) (pi + 1) / (float) (song.parts.size() + 1));
        if (shouldStop && shouldStop()) return false;
    }

    // the full mix: gentle glue, then never above -1 dB
    float peak = 0.0f;
    for (int ch = 0; ch < 2; ++ch)
    {
        auto* d = mix.getWritePointer (ch);
        for (int i = 0; i < total; ++i) { d[i] = std::tanh (d[i] * 0.9f); peak = std::max (peak, std::abs (d[i])); }
    }
    if (peak > 0.0f) mix.applyGain (0.89f / peak);
    out.mixFile = folder.getChildFile (song.title + " - full mix.wav");
    if (! writeWav (out.mixFile, mix, sr)) return false;
    out.mixAudio = std::make_shared<juce::AudioBuffer<float>> (std::move (mix));

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
