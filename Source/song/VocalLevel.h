#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <vector>

// Evens out a sung take: the singing model sometimes jumps 6-10 dB louder for a few tens of milliseconds at the
// start of a note, or makes one syllable much louder than its neighbours. This brings those back to the level
// around them, and leaves everything else as it was (no pumping, no squashing: quiet words stay quiet).
//   - short bursts: the 10 ms level against the level over the surrounding ~0.3 s; anything more than
//     `burstDb` above it is turned down to it (smoothly: 3 ms in, 40 ms out)
//   - syllables: each word's level against the phrase's, pulled at most `evenDb` towards it
namespace lp::song::vocallevel
{
// the jolts themselves (10-100 ms much louder than the note around them, often clipped): cut out and replaced by
// the same note from just after, lined up to its pitch cycle so the joins can't be heard. Answers how many.
inline int repairBursts (juce::AudioBuffer<float>& buf, double sr, float overDb = 4.0f)
{
    const int n = buf.getNumSamples(), chs = buf.getNumChannels();
    if (n <= 0 || chs <= 0 || sr <= 0) return 0;
    std::vector<float> mono ((size_t) n, 0.0f);
    for (int ch = 0; ch < chs; ++ch)
        for (int i = 0; i < n; ++i) mono[(size_t) i] += buf.getSample (ch, i) / (float) chs;
    const int hop = juce::jmax (1, (int) (0.0025 * sr)), win = (int) (0.010 * sr), frames = n / hop;
    std::vector<double> lv ((size_t) frames, 0.0);
    for (int f = 0; f < frames; ++f)
    {
        double s = 0.0;
        const int a = f * hop, b = juce::jmin (n, a + win);
        for (int i = a; i < b; ++i) s += (double) mono[(size_t) i] * mono[(size_t) i];
        lv[(size_t) f] = std::sqrt (s / juce::jmax (1, b - a));
    }
    const int nearF = (int) (0.030 * sr / hop), farF = (int) (0.150 * sr / hop);
    std::vector<double> tmp;
    auto medianOf = [&] (int a, int b)
    {
        tmp.clear();
        for (int k = juce::jmax (0, a); k <= juce::jmin (frames - 1, b); ++k) if (lv[(size_t) k] > 0.003) tmp.push_back (lv[(size_t) k]);
        if ((int) tmp.size() < (farF - nearF) / 3) return 0.0;
        std::nth_element (tmp.begin(), tmp.begin() + (long) tmp.size() / 2, tmp.end());
        return tmp[tmp.size() / 2];
    };
    const double over = std::pow (10.0, overDb / 20.0);
    const int xf = juce::jmax (8, (int) (0.004 * sr)), W = (int) (0.020 * sr);
    int repaired = 0, f = 0;
    while (f < frames)
    {
        const double ref = juce::jmax (medianOf (f - farF, f - nearF), medianOf (f + nearF, f + farF));
        if (ref <= 0.0 || lv[(size_t) f] < ref * over || lv[(size_t) f] < 0.01) { ++f; continue; }
        int g = f;
        while (g < frames && lv[(size_t) g] > ref * over * 0.7) ++g; // (to where it's back near the note's level)
        const int a = juce::jmax (0, f * hop - xf), b = juce::jmin (n, g * hop + win + xf), len = b - a;
        f = g + 1;
        if (len < (int) (0.008 * sr) || len > (int) (0.120 * sr)) continue; // (a jolt is short: longer is a loud word)
        // the copy: the note from just after, shifted by whole pitch cycles (the best match just past the jolt)
        const int minL = len, maxL = len + (int) (0.030 * sr);
        if (b + maxL + W >= n) continue;
        int bestL = -1;
        double best = 0.0;
        for (int L = minL; L <= maxL; ++L)
        {
            double xy = 0.0, xx = 0.0, yy = 0.0;
            for (int i = 0; i < W; i += 2)
            {
                const double p = mono[(size_t) (b + i)], q = mono[(size_t) (b + L + i)];
                xy += p * q; xx += p * p; yy += q * q;
            }
            const double c = xy / std::sqrt (xx * yy + 1e-12);
            if (c > best) { best = c; bestL = L; }
        }
        if (bestL < 0 || best < 0.6) continue; // (nothing steady to copy from: leave it to the level pass)
        // the copy at the level the note has there
        double cs = 0.0, rs = 0.0;
        for (int i = b; i < b + W; ++i) { rs += (double) mono[(size_t) i] * mono[(size_t) i]; cs += (double) mono[(size_t) (i + bestL)] * mono[(size_t) (i + bestL)]; }
        const float k = (float) juce::jlimit (0.5, 2.0, std::sqrt (rs / (cs + 1e-12)));
        for (int ch = 0; ch < chs; ++ch)
        {
            float* d = buf.getWritePointer (ch);
            std::vector<float> src ((size_t) len);
            for (int i = 0; i < len; ++i) src[(size_t) i] = d[a + bestL + i] * k;
            for (int i = 0; i < len; ++i)
            {
                // fade the copy in over the first xf samples and out over the last xf (raised cosine)
                float w = 1.0f;
                if (i < xf) w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * (float) i / (float) xf);
                else if (i >= len - xf) w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * (float) (len - 1 - i) / (float) xf);
                d[a + i] = d[a + i] * (1.0f - w) + src[(size_t) i] * w;
            }
        }
        for (int i = a; i < b; ++i) { float m = 0; for (int ch = 0; ch < chs; ++ch) m += buf.getSample (ch, i); mono[(size_t) i] = m / (float) chs; }
        for (int q = a / hop; q < juce::jmin (frames, b / hop + 1); ++q)
        {
            double s = 0.0;
            const int qa = q * hop, qb = juce::jmin (n, qa + win);
            for (int i = qa; i < qb; ++i) s += (double) mono[(size_t) i] * mono[(size_t) i];
            lv[(size_t) q] = std::sqrt (s / juce::jmax (1, qb - qa));
        }
        ++repaired;
    }
    return repaired;
}

inline void process (juce::AudioBuffer<float>& buf, double sr, float burstDb = 1.5f, float evenDb = 4.0f, bool repair = true)
{
    if (repair) repairBursts (buf, sr);
    const int n = buf.getNumSamples(), chs = buf.getNumChannels();
    if (n <= 0 || chs <= 0 || sr <= 0) return;
    // the level, every 2.5 ms (RMS of the channels over 10 ms)
    const int hop = juce::jmax (1, (int) (0.0025 * sr)), win = (int) (0.010 * sr);
    const int frames = n / hop + 1;
    std::vector<double> fast ((size_t) frames, 0.0);
    for (int f = 0; f < frames; ++f)
    {
        const int c = f * hop, a = juce::jmax (0, c - win / 2), b = juce::jmin (n, c + win / 2);
        double s = 0.0;
        for (int ch = 0; ch < chs; ++ch)
        {
            const float* p = buf.getReadPointer (ch);
            for (int i = a; i < b; ++i) s += (double) p[i] * p[i];
        }
        fast[(size_t) f] = std::sqrt (s / juce::jmax (1, (b - a) * chs));
    }
    // the level around: the note as it goes on (30-150 ms later) and as it was (30-150 ms before), the louder of the
    // two (so a burst at a note's start is held to the note's own level, and a word after a gap isn't pulled down)
    const int nearF = (int) (0.030 * sr / hop), farF = (int) (0.150 * sr / hop);
    const double floorLvl = 0.003; // (about -50 dB: below this it's silence, left alone)
    std::vector<double> ref ((size_t) frames, 0.0), tmp;
    auto medianOf = [&] (int a, int b)
    {
        tmp.clear();
        for (int k = juce::jmax (0, a); k <= juce::jmin (frames - 1, b); ++k)
            if (fast[(size_t) k] > floorLvl) tmp.push_back (fast[(size_t) k]);
        if ((int) tmp.size() < (farF - nearF) / 3) return 0.0;
        std::nth_element (tmp.begin(), tmp.begin() + (long) tmp.size() / 2, tmp.end());
        return tmp[tmp.size() / 2];
    };
    for (int f = 0; f < frames; ++f)
        ref[(size_t) f] = juce::jmax (medianOf (f - farF, f - nearF), medianOf (f + nearF, f + farF));
    // the phrase level (over ~1.5 s, loud frames): words far from it are pulled towards it a little
    const int phrase = (int) (0.75 * sr / hop);
    std::vector<double> phr ((size_t) frames, 0.0);
    for (int f = 0; f < frames; f += 8)
    {
        tmp.clear();
        for (int k = juce::jmax (0, f - phrase); k <= juce::jmin (frames - 1, f + phrase); k += 4)
            if (fast[(size_t) k] > floorLvl) tmp.push_back (fast[(size_t) k]);
        double v = 0.0;
        if (tmp.size() >= 8) { std::nth_element (tmp.begin(), tmp.begin() + (long) tmp.size() / 2, tmp.end()); v = tmp[tmp.size() / 2]; }
        for (int k = f; k < juce::jmin (frames, f + 8); ++k) phr[(size_t) k] = v;
    }
    // the gain each frame wants (in dB, only ever down for bursts; up or down a little for evening)
    std::vector<float> want ((size_t) frames, 0.0f);
    const double burst = std::pow (10.0, burstDb / 20.0);
    for (int f = 0; f < frames; ++f)
    {
        const double x = fast[(size_t) f];
        if (x <= floorLvl) continue;
        float g = 0.0f;
        if (ref[(size_t) f] > 0.0 && x > ref[(size_t) f] * burst) g = (float) (20.0 * std::log10 (ref[(size_t) f] * burst / x));
        if (phr[(size_t) f] > 0.0)
        {
            // (a word: its own ~60 ms level against the phrase)
            const double d = 20.0 * std::log10 (juce::jmax (ref[(size_t) f], 1e-9) / phr[(size_t) f]);
            g += (float) juce::jlimit (-(double) evenDb, (double) evenDb, -d * 0.5);
        }
        want[(size_t) f] = g;
    }
    // smooth it: down fast (3 ms), back up slowly (40 ms), and look ahead so a burst is caught before it starts
    std::vector<float> gain ((size_t) frames, 0.0f);
    const float down = 1.0f - std::exp (-(float) hop / (float) (0.003 * sr)), up = 1.0f - std::exp (-(float) hop / (float) (0.040 * sr));
    const int ahead = juce::jmax (1, (int) (0.006 * sr / hop));
    float g = 0.0f;
    for (int f = 0; f < frames; ++f)
    {
        float t = want[(size_t) f];
        for (int k = f + 1; k <= juce::jmin (frames - 1, f + ahead); ++k) t = juce::jmin (t, want[(size_t) k]);
        g += (t < g ? down : up) * (t - g);
        gain[(size_t) f] = g;
    }
    // apply (interpolated between frames)
    for (int i = 0; i < n; ++i)
    {
        const double pos = (double) i / hop;
        const int f = juce::jmin (frames - 1, (int) pos), f2 = juce::jmin (frames - 1, f + 1);
        const float db = gain[(size_t) f] + (gain[(size_t) f2] - gain[(size_t) f]) * (float) (pos - f);
        if (std::abs (db) < 0.01f) continue;
        const float m = std::pow (10.0f, db / 20.0f);
        for (int ch = 0; ch < chs; ++ch) buf.getWritePointer (ch)[i] *= m;
    }
}
} // namespace lp::song::vocallevel
