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
inline void process (juce::AudioBuffer<float>& buf, double sr, float burstDb = 3.0f, float evenDb = 4.0f)
{
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
    // the surrounding level: a 0.3 s window's median of the loud frames (so a burst doesn't raise its own reference)
    const int around = (int) (0.15 * sr / hop);
    const double floorLvl = 0.003; // (about -50 dB: below this it's silence, left alone)
    std::vector<double> ref ((size_t) frames, 0.0), tmp;
    for (int f = 0; f < frames; ++f)
    {
        tmp.clear();
        for (int k = juce::jmax (0, f - around); k <= juce::jmin (frames - 1, f + around); k += 2)
            if (fast[(size_t) k] > floorLvl) tmp.push_back (fast[(size_t) k]);
        if (tmp.size() < 8) { ref[(size_t) f] = 0.0; continue; }
        std::nth_element (tmp.begin(), tmp.begin() + (long) (tmp.size() * 6 / 10), tmp.end());
        ref[(size_t) f] = tmp[tmp.size() * 6 / 10];
    }
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
