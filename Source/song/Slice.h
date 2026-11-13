#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

// The hits in a drum recording: where each one starts, how loud it is, and what it is
// (36 kick, 38 snare / clap, 42 closed hat, 46 open hat, 70 other percussion).
namespace lp::song::slice
{
struct Hit { int at; float level; int cls; };

inline std::vector<Hit> findHits (const juce::AudioBuffer<float>& b, double sr)
{
    const int len = b.getNumSamples();
    std::vector<float> mono ((size_t) len);
    for (int i = 0; i < len; ++i) mono[(size_t) i] = 0.5f * (b.getSample (0, i) + b.getSample (std::min (1, b.getNumChannels() - 1), i));
    // hits: the loudness (5 ms steps) jumps up
    const int hop = (int) (0.005 * sr);
    std::vector<float> env;
    for (int i = 0; i + hop <= len; i += hop)
    {
        float m = 0; for (int k = 0; k < hop; ++k) m = std::max (m, std::abs (mono[(size_t) (i + k)]));
        env.push_back (m);
    }
    float peak = 0; for (float v : env) peak = std::max (peak, v);
    if (peak <= 0.0f) return {};
    std::vector<Hit> hits;
    for (size_t k = 0; k < env.size(); ++k)
    {
        // (the very start counts as silence before it, so a hit right on beat 1 is found)
        const float before = k == 0 ? 0.0f : *std::min_element (env.begin() + (long) (k >= 6 ? k - 6 : 0), env.begin() + (long) k);
        if (env[k] > peak * 0.06f && env[k] > before * 1.8f + peak * 0.01f && (hits.empty() || (int) k * hop - hits.back().at > (int) (0.06 * sr)))
        hits.push_back ({ std::max (0, (int) k * hop - hop), env[k], 0 });
    }
    
    // what each hit is: where its energy sits (first 40 ms) and how long it rings
    for (auto& h : hits)
    {
        const int n = std::min ((int) (0.04 * sr), len - h.at);
        float lp = 0, lp2 = 0, lowE = 0, midE = 0, highE = 0, all = 0;
        const float aL = 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi * 150.0f / (float) sr);
        const float aM = 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi * 2500.0f / (float) sr);
        for (int i = 0; i < n; ++i)
        {
        const float x = mono[(size_t) (h.at + i)];
        lp += (x - lp) * aL;
        lp2 += (x - lp2) * aM;
        lowE += lp * lp; midE += (lp2 - lp) * (lp2 - lp); highE += (x - lp2) * (x - lp2); all += x * x;
        }
        const float low = lowE / std::max (1e-9f, all), mid = midE / std::max (1e-9f, all), high = highE / std::max (1e-9f, all);
        // how long it rings: time until it's 20 dB down
        int ring = 0;
        for (int i = h.at / hop; i < (int) env.size() && env[(size_t) i] > h.level * 0.1f; ++i) ++ring;
        const bool longRing = ring * hop > (int) (0.18 * sr);
        // kick: mostly lows. hats: almost nothing under 2.5 kHz. snare / clap: a body in the mids and noise on top.
        // the rest (toms, rims, shakers): percussion
        int cls;
        if (low > 0.45f) cls = 36;
        else if (high > 0.6f && mid < 0.3f) cls = longRing ? 46 : 42;
        else if (mid > 0.25f && high > 0.15f) cls = 38;
        else cls = 70;
        h.cls = cls;
    }
    return hits;
}
}
