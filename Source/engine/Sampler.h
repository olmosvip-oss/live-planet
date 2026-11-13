#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <memory>
#include <vector>

// A one-shot played across the keyboard, like a sampler: each key plays the sound at its pitch.
// Several recordings of the same sound (soft / medium / hard) become velocity layers: how hard you
// play picks the layer, and within it, softer is also darker and quieter, like a real instrument.
namespace lp
{

class Sampler
{
public:
    struct Layer
    {
        std::shared_ptr<juce::AudioBuffer<float>> buf;
        double rate = 48000.0;
        double rootNote = 60.0;   // the MIDI note the recording plays (fractional: it can be a little sharp or flat)
        float velLo = 0.0f, velHi = 1.0f;
        int keyLo = 0, keyHi = 127;  // the keys it plays on
        bool fixedPitch = false;     // chops: every key plays its own slice at its own pitch
        juce::String label;          // e.g. the chop's number, for the screen
    };

    // ---- message thread
    void setLayers (std::vector<Layer> l)
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        std::swap (layers, l);
        for (auto& v : voices) v.active = false;
        // `l` (the old sounds) is freed here, after the lock
    }
    int numLayers() const { const juce::SpinLock::ScopedLockType sl (lock); return (int) layers.size(); }

    // the Texture knobs, for a sample instrument: where in the sound it starts, a little drift,
    // a soft octave-up ghost (sparkle), and how long it rings after you let go
    std::atomic<float> startFrac { 0.0f }, driftCents { 0.0f }, sparkle { 0.0f }, releaseSec { 0.18f };

    // ---- audio thread
    void noteOn (int note, float vel, int offset)
    {
        startVoice (note, vel, offset, 1.0f, false);
        const float sp = sparkle.load();
        if (sp > 0.01f) startVoice (note + 12, vel, offset, sp, true);
    }

private:
    void startVoice (int note, float vel, int offset, float gainMul, bool ghost)
    {
        if (layers.empty()) return;
        const Layer* l = nullptr;
        for (auto& c : layers) if (note >= c.keyLo && note <= c.keyHi && vel >= c.velLo && vel <= c.velHi) l = &c;
        if (l == nullptr) for (auto& c : layers) if (note >= c.keyLo && note <= c.keyHi) l = &c;
        if (l == nullptr) return; // no sound on this key
        Voice* slot = nullptr;
        for (auto& v : voices) if (v.active && v.note == note && ! v.releasing && v.ghost == ghost) { v.releasing = true; } // a restrike lets the old one go
        if (l->fixedPitch) for (auto& v : voices) if (v.active && v.layer == l) v.releasing = true;   // a chop cuts itself off, like a pad on an MPC
        for (auto& v : voices) if (! v.active) { slot = &v; break; }
        if (slot == nullptr) { slot = &voices[0]; for (auto& v : voices) if (v.env < slot->env) slot = &v; }
        const float span = std::max (0.05f, l->velHi - l->velLo);
        const float within = juce::jlimit (0.0f, 1.0f, (vel - l->velLo) / span);
        *slot = {};
        slot->active = true;
        slot->layer = l;
        slot->note = ghost ? note - 12 : note;
        slot->ghost = ghost;
        const double drift = (rng.nextDouble() * 2.0 - 1.0) * driftCents.load();
        slot->step = (l->fixedPitch ? (ghost ? 2.0 : 1.0) : std::pow (2.0, (note - l->rootNote) / 12.0)) * std::pow (2.0, drift / 1200.0) * l->rate / hostRate;
        slot->pos = l->fixedPitch ? 0.0 : startFrac.load() * 0.8 * l->buf->getNumSamples();
        slot->gain = (0.25f + 0.75f * std::pow (juce::jlimit (0.0f, 1.0f, vel), 1.5f)) * 0.9f * gainMul; // soft about 10 dB under hard
        // softer is darker: a one-pole low-pass from ~2.5 kHz (soft) to wide open (hard)
        const float cutoff = 2500.0f * std::pow (8.0f, (layers.size() > 1 ? 0.5f + 0.5f * within : vel));
        slot->lpA = 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi * std::min (cutoff, 20000.0f) / (float) hostRate);
        slot->delay = offset;
    }
public:
    void noteOff (int note)
    {
        for (auto& v : voices) if (v.active && v.note == note) v.releasing = true;
    }
    void allOff() { for (auto& v : voices) v.releasing = true; }

    // adds into L / R
    void process (float* L, float* R, int n, double sampleRate)
    {
        const juce::SpinLock::ScopedTryLockType tl (lock);
        if (! tl.isLocked()) return;
        hostRate = sampleRate;
        const float rel = std::exp (-1.0f / (juce::jlimit (0.02f, 4.0f, releaseSec.load()) * (float) sampleRate)); // let go: a fade
        const float atk = 1.0f / (0.002f * (float) sampleRate);             // 2 ms, so nothing clicks
        for (auto& v : voices)
        {
            if (! v.active || v.layer == nullptr) continue;
            const auto& b = *v.layer->buf;
            const int len = b.getNumSamples();
            const float* a = b.getReadPointer (0);
            const float* c = b.getReadPointer (std::min (1, b.getNumChannels() - 1));
            for (int i = std::max (0, v.delay); i < n; ++i)
            {
                const int i0 = (int) v.pos;
                if (i0 + 2 >= len) { v.active = false; break; }
                const float f = (float) (v.pos - i0);
                // 4-point Hermite: clean when a note is played far from where it was recorded
                auto herm = [f] (const float* x, int k)
                {
                    const float y0 = x[std::max (0, k - 1)], y1 = x[k], y2 = x[k + 1], y3 = x[k + 2];
                    const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
                    return ((c3 * f + c2) * f + c1) * f + y1;
                };
                float l = herm (a, i0), r = herm (c, i0);
                v.zl += (l - v.zl) * v.lpA; v.zr += (r - v.zr) * v.lpA;
                if (v.releasing) { v.env *= rel; if (v.env < 0.0005f) { v.active = false; break; } }
                else v.env = std::min (1.0f, v.env + atk);
                L[i] += v.zl * v.gain * v.env;
                R[i] += v.zr * v.gain * v.env;
                v.pos += v.step;
            }
            v.delay = 0;
        }
    }

private:
    struct Voice
    {
        bool active = false, releasing = false, ghost = false;
        const Layer* layer = nullptr;
        int note = 0, delay = 0;
        double pos = 0.0, step = 1.0;
        float gain = 1.0f, env = 0.0f, lpA = 1.0f, zl = 0.0f, zr = 0.0f;
    };
    mutable juce::SpinLock lock;
    std::vector<Layer> layers;
    std::array<Voice, 32> voices {};
    double hostRate = 48000.0;
    juce::Random rng;
};

// Cuts a recording (a vocal, a phrase) into its syllables: where the loudness jumps up.
// Returns [start, length] pairs (in samples), 0.08..1.5 s long, at most `maxSlices`.
inline std::vector<std::pair<int, int>> sliceByOnsets (const juce::AudioBuffer<float>& b, double sr, int maxSlices = 24)
{
    std::vector<std::pair<int, int>> out;
    const int n = b.getNumSamples(), hop = std::max (1, (int) (0.01 * sr));
    std::vector<float> env;
    for (int i = 0; i + hop <= n; i += hop)
        env.push_back (0.5f * (b.getRMSLevel (0, i, hop) + b.getRMSLevel (std::min (1, b.getNumChannels() - 1), i, hop)));
    float peak = 0; for (float v : env) peak = std::max (peak, v);
    if (peak <= 0.0f) return out;
    std::vector<int> starts;
    for (size_t k = 3; k < env.size(); ++k)
    {
        const float before = *std::min_element (env.begin() + (long) k - 3, env.begin() + (long) k);
        const bool rise = env[k] > peak * 0.08f && env[k] > before * 2.0f + peak * 0.01f;
        const bool fromQuiet = env[k] > peak * 0.08f && env[k - 1] <= peak * 0.05f;
        if ((rise || fromQuiet) && (starts.empty() || (int) k * hop - starts.back() > (int) (0.1 * sr)))
            starts.push_back (std::max (0, (int) k * hop - hop * 2)); // a little before, so the consonant isn't cut
    }
    if (starts.size() < 2) // one long sound: even pieces
        for (int i = 0; i + (int) (0.25 * sr) < n; i += (int) (0.25 * sr)) starts.push_back (i);
    for (size_t k = 0; k < starts.size() && (int) out.size() < maxSlices; ++k)
    {
        int end = k + 1 < starts.size() ? starts[k + 1] : n;
        end = std::min (end, starts[k] + (int) (1.5 * sr));
        // stop where it goes quiet (the gap between syllables isn't part of the chop)
        for (int i = starts[k] / hop + 5; i < end / hop && i < (int) env.size(); ++i)
            if (env[(size_t) i] < peak * 0.02f) { end = i * hop; break; }
        if (end - starts[k] >= (int) (0.08 * sr)) out.push_back ({ starts[k], end - starts[k] });
    }
    return out;
}

} // namespace lp
