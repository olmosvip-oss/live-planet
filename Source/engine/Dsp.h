#pragma once
#include <vector>
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace lp::dsp
{

constexpr float kPi = 3.14159265358979f;

// Zero-delay-feedback state variable filter (low, band and high outputs).
struct SVF
{
    float a1 = 0, a2 = 0, a3 = 0, k = 1, ic1 = 0, ic2 = 0;
    float lp = 0, bp = 0, hp = 0;

    void setup (float fc, float q, float sr)
    {
        fc = std::clamp (fc, 10.0f, sr * 0.45f);
        const float g = std::tan (kPi * fc / sr);
        k = 1.0f / q;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    inline void tick (float v0)
    {
        const float v3 = v0 - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2 * v1 - ic1;
        ic2 = 2 * v2 - ic2;
        lp = v2; bp = v1; hp = v0 - k * v1 - v2;
    }
    void reset() { ic1 = ic2 = 0; }
};

struct Noise
{
    uint32_t s = 0x12345678u;
    inline float next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) (int32_t) s * (1.0f / 2147483648.0f); }
};

struct Delay
{
    std::vector<float> buf;
    int mask = 0, w = 0;
    void prepare (int maxSamples)
    {
        int size = 1;
        while (size < maxSamples + 4) size <<= 1;
        buf.assign ((size_t) size, 0.0f);
        mask = size - 1;
        w = 0;
    }
    inline void push (float x) { buf[(size_t) w] = x; w = (w + 1) & mask; }
    // read `d` samples behind the newest sample (d >= 1)
    inline float read (float d) const
    {
        const float rp = (float) w - d;
        const int i0 = (int) std::floor (rp);
        const float f = rp - (float) i0;
        const float a = buf[(size_t) (i0 & mask)];
        const float b = buf[(size_t) ((i0 + 1) & mask)];
        return a + (b - a) * f;
    }
    void clear() { std::fill (buf.begin(), buf.end(), 0.0f); }
};

// Shifts a signal up one octave with two crossfading read heads.
// Used inside the reverb for the "shimmer" that floats upward.
struct OctaveUp
{
    Delay d;
    float phase = 0, window = 2048;
    void prepare() { d.prepare (4096); }
    inline float process (float x)
    {
        d.push (x);
        phase += 1.0f / window;
        if (phase >= 1.0f) phase -= 1.0f;
        float p2 = phase + 0.5f;
        if (p2 >= 1.0f) p2 -= 1.0f;
        const float s1 = std::sin (kPi * phase), s2 = std::sin (kPi * p2);
        return d.read (1.0f + (1.0f - phase) * window) * s1 * s1
             + d.read (1.0f + (1.0f - p2) * window) * s2 * s2;
    }
};

} // namespace lp::dsp
