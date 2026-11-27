#pragma once
#include <juce_core/juce_core.h>
#include <juce_cryptography/juce_cryptography.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <vector>
#include <complex>

// An inaudible tag in a vocal: who it was made for (32 bits of a hash of the license's email), spread as a
// very quiet noise pattern under the voice (it follows the voice's own loudness, about 30 dB below it, and only
// above 2 kHz). It survives being recorded into a DAW, trimmed, mixed under music, gain changes and resampling
// to another rate (the scan works at 44.1 kHz). It is a deterrent and a way to trace a leak, not a lock:
// heavy processing (pitch shifting, time stretching, low bit-rate encoding) can weaken it.
//
// Layout: one "cycle" is 16 sync blocks then 32 payload blocks, each block B samples at 44.1 kHz (~0.19 s),
// so a cycle is ~8.9 s and repeats through the whole vocal. Each block carries +-PN(block), a fixed
// pseudo-random +-1 sequence; the sign is the bit.
namespace lp::watermark
{
constexpr int kRate = 44100, kBlock = 8192, kSync = 16, kBits = 32, kCycle = (kSync + kBits) * kBlock;
constexpr juce::uint32 kSyncPattern = 0xB38Fu; // (16 bits)
constexpr float kLevel = 0.032f;                // the tag's level, relative to the voice's own (about -30 dB)

inline juce::uint32 idOf (const juce::String& owner)
{
    const auto e = owner.trim().toLowerCase().upToFirstOccurrenceOf ("|", false, false);
    const juce::SHA256 sha (("Loopwright watermark v1|" + e).toUTF8());
    const auto raw = sha.getRawData();
    const auto* d = (const juce::uint8*) raw.getData();
    return (juce::uint32) d[0] << 24 | (juce::uint32) d[1] << 16 | (juce::uint32) d[2] << 8 | d[3];
}
inline juce::String hexOf (juce::uint32 id) { return juce::String::toHexString ((juce::int64) id).paddedLeft ('0', 8).toUpperCase(); }

// the chip at position i (0..kBlock) of block slot s (0..47): +-1, fixed for ever
inline float chip (int slot, int i)
{
    i >>= 1; // (each chip lasts 2 samples)
    juce::uint32 x = (juce::uint32) slot * 2654435761u ^ (juce::uint32) i * 2246822519u ^ 0x9E3779B9u;
    x ^= x >> 15; x *= 0x2C1B3C6Du; x ^= x >> 12; x *= 0x297A2D39u; x ^= x >> 15;
    return (x & 1u) ? 1.0f : -1.0f;
}
inline int bitOf (juce::uint32 id, int slot)
{
    if (slot < kSync) return (int) ((kSyncPattern >> (kSync - 1 - slot)) & 1u);
    return (int) ((id >> (kBits - 1 - (slot - kSync))) & 1u);
}

// (simple one-pole-pair high-pass at about 2 kHz, run forwards)
struct HighPass
{
    float a = 0, x1 = 0, y1 = 0, x1b = 0, y1b = 0;
    explicit HighPass (double sr, double hz = 2000.0) { a = (float) std::exp (-2.0 * juce::MathConstants<double>::pi * hz / sr); }
    float operator() (float x)
    {
        const float y = a * (y1 + x - x1); x1 = x; y1 = y;
        const float z = a * (y1b + y - x1b); x1b = y; y1b = z;
        return z;
    }
};

// adds the tag for `id` to every channel (in place). Works at any sample rate: the pattern is laid out on the
// 44.1 kHz grid, so a resampled copy still lines up.
inline void embed (juce::AudioBuffer<float>& buf, double sr, juce::uint32 id)
{
    const int n = buf.getNumSamples(), chs = buf.getNumChannels();
    if (n <= 0 || chs <= 0 || sr <= 0) return;
    // the voice's loudness (20 ms RMS, smoothed), the tag follows it
    std::vector<float> env ((size_t) n, 0.0f);
    const float att = (float) std::exp (-1.0 / (0.020 * sr));
    float e2 = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        float s = 0.0f;
        for (int c = 0; c < chs; ++c) s += buf.getSample (c, i);
        s /= (float) chs;
        e2 = att * e2 + (1.0f - att) * s * s;
        env[(size_t) i] = e2;
    }
    // (look-ahead too: the same smoothing backwards, so the tag never leads or lags a word)
    float b2 = 0.0f;
    for (int i = n; --i >= 0;) { b2 = att * b2 + (1.0f - att) * env[(size_t) i]; env[(size_t) i] = std::sqrt (juce::jmax (env[(size_t) i], b2)); }
    HighPass hp (sr);
    const double step = (double) kRate / sr;
    for (int i = 0; i < n; ++i)
    {
        const auto pos = (juce::int64) ((double) i * step);  // where on the 44.1 kHz grid
        const int inCycle = (int) (pos % kCycle), slot = inCycle / kBlock, at = inCycle % kBlock;
        const float sign = bitOf (id, slot) ? 1.0f : -1.0f;
        const float w = hp (sign * chip (slot, at)) * kLevel * env[(size_t) i];
        for (int c = 0; c < chs; ++c) buf.addSample (c, i, w);
    }
}

struct Found { bool found = false; juce::uint32 id = 0; float confidence = 0.0f; int cycles = 0; };

// (an in-place radix-2 FFT, for the scan)
inline void fft (std::vector<std::complex<double>>& a, bool inverse)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = 2.0 * juce::MathConstants<double>::pi / (double) len * (inverse ? 1.0 : -1.0);
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0);
            for (size_t j = 0; j < len / 2; ++j)
            {
                const auto u = a[i + j], v = a[i + j + len / 2] * w;
                a[i + j] = u + v; a[i + j + len / 2] = u - v;
                w *= wl;
            }
        }
    }
    if (inverse) for (auto& x : a) x /= (double) n;
}

// looks for a tag in a mono signal at 44.1 kHz (resample first)
inline Found scan (const std::vector<float>& x44)
{
    Found f;
    const int n = (int) x44.size();
    if (n < kCycle) return f;
    // whiten: high-pass, then each sample divided by the local loudness (the tag is proportional to it)
    std::vector<float> w ((size_t) n);
    HighPass hp ((double) kRate);
    for (int i = 0; i < n; ++i) w[(size_t) i] = hp (x44[(size_t) i]);
    {
        std::vector<float> env ((size_t) n);
        const float att = std::exp (-1.0f / (0.010f * kRate));
        float e = 0.0f;
        for (int i = 0; i < n; ++i) { e = att * e + (1.0f - att) * w[(size_t) i] * w[(size_t) i]; env[(size_t) i] = e; }
        float b = 0.0f;
        for (int i = n; --i >= 0;) { b = att * b + (1.0f - att) * env[(size_t) i]; env[(size_t) i] = juce::jmax (env[(size_t) i], b); }
        for (int i = 0; i < n; ++i) w[(size_t) i] = env[(size_t) i] > 1e-12f ? juce::jlimit (-4.0f, 4.0f, w[(size_t) i] / std::sqrt (env[(size_t) i])) : 0.0f;
    }
    // fold every cycle onto one (the tag repeats each cycle, the voice and music don't)
    std::vector<double> F ((size_t) kCycle, 0.0);
    for (int i = 0; i < n; ++i) F[(size_t) (i % kCycle)] += w[(size_t) i];
    f.cycles = n / kCycle;
    // where does the cycle start? correlate with the sync blocks (all offsets at once, by FFT)
    constexpr int T = kSync * kBlock, N = 1 << 19; // (kCycle + T == N)
    static_assert (kCycle + T == N, "fft size");
    std::vector<std::complex<double>> G ((size_t) N), t ((size_t) N);
    for (int j = 0; j < N; ++j) G[(size_t) j] = F[(size_t) (j % kCycle)];
    for (int j = 0; j < T; ++j) t[(size_t) j] = (bitOf (0, j / kBlock) ? 1.0 : -1.0) * chip (j / kBlock, j % kBlock);
    fft (G, false); fft (t, false);
    for (int j = 0; j < N; ++j) G[(size_t) j] *= std::conj (t[(size_t) j]);
    fft (G, true);
    int bestO = 0;
    double best = -1e300, sum = 0.0, sq = 0.0;
    for (int o = 0; o < kCycle; ++o)
    {
        const double r = G[(size_t) o].real();
        sum += r; sq += r * r;
        if (r > best) { best = r; bestO = o; }
    }
    const double mean = sum / kCycle, sd = std::sqrt (juce::jmax (1e-18, sq / kCycle - mean * mean));
    const double peak = (best - mean) / sd;  // (how far the best start stands out: noise gives about 4.5)
    // read the 32 bits from there
    juce::uint32 id = 0;
    double weakest = 1e300, total = 0.0;
    for (int k = 0; k < kBits; ++k)
    {
        const int slot = kSync + k;
        double s = 0.0;
        for (int i = 0; i < kBlock; ++i) s += F[(size_t) ((bestO + slot * kBlock + i) % kCycle)] * chip (slot, i);
        id = (id << 1) | (s > 0 ? 1u : 0u);
        weakest = juce::jmin (weakest, std::abs (s));
        total += std::abs (s);
    }
    // (a sync block's score, on the same scale as a bit's, is best / kSync)
    const double perBlock = best / kSync;
    f.found = peak > 7.0 && weakest > 0.2 * perBlock;
    f.confidence = (float) juce::jlimit (0.0, 1.0, (peak - 5.0) / 10.0);
    f.id = id;
    juce::ignoreUnused (total);
    return f;
}
} // namespace lp::watermark
