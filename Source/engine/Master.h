#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include "Limiter.h"
#include <array>
#include <vector>

// Mastering for streaming: a gentle glue compressor, a touch of clean low end and air, then the
// song brought to a platform's loudness (LUFS, measured like the platforms do) under a peak ceiling.
// The same chain runs on the exported file and live, so before / after is a fair comparison.
namespace lp
{

struct MasterPreset { const char* name; float lufs; float ceilingDb; };
inline const MasterPreset* masterPresets (int& count)
{
    static const MasterPreset p[] = {
        { "Spotify",     -14.0f, -1.0f },
        { "Apple Music", -16.0f, -1.0f },
        { "YouTube",     -14.0f, -1.0f },
        { "SoundCloud",  -10.0f, -1.0f },
        { "Club / loud",  -8.0f, -0.5f },
    };
    count = (int) (sizeof (p) / sizeof (p[0]));
    return p;
}

// a biquad, transposed direct form II
struct MBiquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    float process (float x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return (float) y; }
    void reset() { z1 = z2 = 0; }
    static MBiquad shelf (bool high, double sr, double f, double gainDb, double q)
    {
        const double A = std::pow (10.0, gainDb / 40.0), w = 2.0 * juce::MathConstants<double>::pi * f / sr;
        const double cs = std::cos (w), al = std::sin (w) / (2.0 * q), sq = 2.0 * std::sqrt (A) * al;
        double b0, b1, b2, a0, a1, a2;
        if (high)
        {
            b0 = A * ((A + 1) + (A - 1) * cs + sq); b1 = -2 * A * ((A - 1) + (A + 1) * cs); b2 = A * ((A + 1) + (A - 1) * cs - sq);
            a0 = (A + 1) - (A - 1) * cs + sq; a1 = 2 * ((A - 1) - (A + 1) * cs); a2 = (A + 1) - (A - 1) * cs - sq;
        }
        else
        {
            b0 = A * ((A + 1) - (A - 1) * cs + sq); b1 = 2 * A * ((A - 1) - (A + 1) * cs); b2 = A * ((A + 1) - (A - 1) * cs - sq);
            a0 = (A + 1) + (A - 1) * cs + sq; a1 = -2 * ((A - 1) + (A + 1) * cs); a2 = (A + 1) + (A - 1) * cs - sq;
        }
        MBiquad q2; q2.b0 = b0 / a0; q2.b1 = b1 / a0; q2.b2 = b2 / a0; q2.a1 = a1 / a0; q2.a2 = a2 / a0; return q2;
    }
    static MBiquad highpass (double sr, double f, double q)
    {
        const double w = 2.0 * juce::MathConstants<double>::pi * f / sr, cs = std::cos (w), al = std::sin (w) / (2.0 * q);
        const double a0 = 1 + al;
        MBiquad h; h.b0 = (1 + cs) / 2 / a0; h.b1 = -(1 + cs) / a0; h.b2 = (1 + cs) / 2 / a0; h.a1 = -2 * cs / a0; h.a2 = (1 - al) / a0; return h;
    }
};

// integrated loudness (ITU-R BS.1770 / EBU R128): K-weighting, 400 ms blocks, absolute and relative gates
inline float measureLufs (const float* L, const float* R, int n, double sr)
{
    // the K-weighting filters exactly as the standard defines them (at any sample rate)
    auto pre = [sr]
    {
        const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
        const double K = std::tan (juce::MathConstants<double>::pi * f0 / sr), Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;
        MBiquad q; q.b0 = (Vh + Vb * K / Q + K * K) / a0; q.b1 = 2.0 * (K * K - Vh) / a0; q.b2 = (Vh - Vb * K / Q + K * K) / a0;
        q.a1 = 2.0 * (K * K - 1.0) / a0; q.a2 = (1.0 - K / Q + K * K) / a0; return q;
    };
    auto rlb = [sr]
    {
        const double f0 = 38.13547087602444, Q = 0.5003270373238773, K = std::tan (juce::MathConstants<double>::pi * f0 / sr);
        const double a0 = 1.0 + K / Q + K * K;
        MBiquad q; q.b0 = 1.0; q.b1 = -2.0; q.b2 = 1.0; q.a1 = 2.0 * (K * K - 1.0) / a0; q.a2 = (1.0 - K / Q + K * K) / a0; return q;
    };
    MBiquad s[2] = { pre(), pre() };
    MBiquad h[2] = { rlb(), rlb() };
    const int hop = (int) (0.1 * sr), block = 4 * hop;
    std::vector<double> sq;
    sq.reserve ((size_t) (n / hop + 1));
    double acc = 0;
    std::vector<double> hopSum;
    for (int i = 0; i < n; ++i)
    {
        const float l = h[0].process (s[0].process (L[i])), r = h[1].process (s[1].process (R[i]));
        acc += (double) l * l + (double) r * r;
        if ((i + 1) % hop == 0) { hopSum.push_back (acc); acc = 0; }
    }
    std::vector<double> blocks;
    for (size_t k = 3; k < hopSum.size(); ++k)
        blocks.push_back ((hopSum[k] + hopSum[k - 1] + hopSum[k - 2] + hopSum[k - 3]) / block);
    auto lufsOf = [] (double ms) { return -0.691 + 10.0 * std::log10 (std::max (1e-12, ms)); };
    double sum = 0; int cnt = 0;
    for (double b : blocks) if (lufsOf (b) > -70.0) { sum += b; ++cnt; }
    if (cnt == 0) return -70.0f;
    const double rel = lufsOf (sum / cnt) - 10.0;
    sum = 0; cnt = 0;
    for (double b : blocks) if (lufsOf (b) > rel) { sum += b; ++cnt; }
    return cnt == 0 ? -70.0f : (float) lufsOf (sum / cnt);
}

class Master
{
public:
    void prepare (double sampleRate, float ceilingDb)
    {
        sr = sampleRate;
        for (int c = 0; c < 2; ++c)
        {
            hp[c] = MBiquad::highpass (sr, 24.0, 0.707);           // clean the sub rumble nobody hears
            air[c] = MBiquad::shelf (true, sr, 11000.0, 1.0, 0.7);  // a little air
        }
        env = 0.0f;
        atk = 1.0f - std::exp (-1.0f / (0.03f * (float) sr));
        rel = 1.0f - std::exp (-1.0f / (0.25f * (float) sr));
        limiter.prepare (sr, ceilingDb);
    }
    int latency() const { return limiter.latency(); }

    // in place. gainDb: brings the song to the loudness (worked out by `fit`)
    void process (float* L, float* R, int n, float gainDb)
    {
        const float g = juce::Decibels::decibelsToGain (gainDb);
        for (int i = 0; i < n; ++i)
        {
            float l = air[0].process (hp[0].process (L[i])), r = air[1].process (hp[1].process (R[i]));
            // glue: 2:1 above a level close to the song's loud parts, at most 3 dB, slow and smooth
            const float lvl = std::sqrt (0.5f * (l * l + r * r));
            env += (lvl - env) * (lvl > env ? atk : rel);
            const float over = juce::Decibels::gainToDecibels (std::max (env, 1e-6f)) - glueThreshDb;
            const float gr = over > 0.0f ? std::min (3.0f, over * 0.5f) : 0.0f;
            const float k = juce::Decibels::decibelsToGain (-gr) * g;
            L[i] = l * k; R[i] = r * k;
        }
        limiter.process (L, R, n);
    }

    // where the glue starts: a few dB under the song's loud moments (set from the song itself)
    float glueThreshDb = -18.0f;

    // the gain that brings `L`/`R` (the unmastered song) to `targetLufs` through this chain
    static float fit (const juce::AudioBuffer<float>& song, double sr, const MasterPreset& p, float glueThresh, float& resultLufs, float& peakDb)
    {
        auto run = [&] (float gainDb, juce::AudioBuffer<float>& out)
        {
            out.makeCopyOf (song);
            Master m;
            m.glueThreshDb = glueThresh;
            m.prepare (sr, p.ceilingDb);
            m.process (out.getWritePointer (0), out.getWritePointer (1), out.getNumSamples(), gainDb);
            return measureLufs (out.getReadPointer (0), out.getReadPointer (1), out.getNumSamples(), sr);
        };
        juce::AudioBuffer<float> out;
        float gain = 0.0f;
        float now = run (gain, out);
        // a few rounds: the limiter takes some loudness back as the gain goes up
        for (int k = 0; k < 7 && std::abs (now - p.lufs) > 0.15f; ++k)
        {
            gain = juce::jlimit (-24.0f, 24.0f, gain + (p.lufs - now));
            now = run (gain, out);
        }
        resultLufs = now;
        peakDb = juce::Decibels::gainToDecibels (std::max (out.getMagnitude (0, 0, out.getNumSamples()), out.getMagnitude (1, 0, out.getNumSamples())));
        return gain;
    }

private:
    double sr = 48000.0;
    MBiquad hp[2], air[2];
    float env = 0.0f, atk = 0.001f, rel = 0.0001f;
    Limiter limiter;
};

} // namespace lp
