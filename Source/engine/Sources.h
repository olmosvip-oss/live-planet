#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <vector>

namespace lp
{

// A sound the grain engine chops up. Built-in ones are made from pure math in
// this file (no recorded samples), so they belong entirely to this project.
struct SourceSound
{
    juce::String name;
    std::vector<float> data;   // mono
    double sampleRate = 48000.0;
    double rootHz = 261.6256;  // the pitch of the sound (C4 by default)
};

namespace sources
{
    constexpr double kRate = 48000.0;
    constexpr double kRoot = 261.6256;
    constexpr double kTwoPi = 6.283185307179586;

    struct Partial { double ratio; double amp; };

    inline void normalise (std::vector<float>& d)
    {
        float peak = 0.0f;
        for (auto v : d) peak = std::max (peak, std::abs (v));
        if (peak > 0.0f)
            for (auto& v : d) v *= 0.9f / peak;
    }

    // Additive tone: each partial slowly breathes in and out so grains taken
    // from different spots sound slightly different.
    inline SourceSound additive (const juce::String& name, const std::vector<Partial>& partials,
                                 double seconds, double vibratoDepth, uint32_t seed)
    {
        SourceSound s;
        s.name = name;
        s.sampleRate = kRate;
        s.rootHz = kRoot;
        const int n = (int) (seconds * kRate);
        s.data.assign ((size_t) n, 0.0f);
        juce::Random rng ((juce::int64) seed);

        for (auto& p : partials)
        {
            double phase = rng.nextDouble() * kTwoPi;
            const double lfoRate = 0.07 + rng.nextDouble() * 0.35;
            const double lfoPhase = rng.nextDouble() * kTwoPi;
            const double lfoDepth = 0.25 + rng.nextDouble() * 0.45;
            for (int i = 0; i < n; ++i)
            {
                const double t = i / kRate;
                const double vib = 1.0 + vibratoDepth * std::sin (kTwoPi * 5.1 * t);
                phase += kTwoPi * kRoot * p.ratio * vib / kRate;
                const double breathe = 1.0 - lfoDepth * (0.5 + 0.5 * std::sin (kTwoPi * lfoRate * t + lfoPhase));
                s.data[(size_t) i] += (float) (p.amp * breathe * std::sin (phase));
            }
        }
        normalise (s.data);
        return s;
    }

    inline SourceSound glass()
    {
        return additive ("Glass", { { 1.0, 1.0 }, { 2.0, 0.42 }, { 3.0, 0.22 }, { 4.01, 0.16 },
                                    { 5.03, 0.08 }, { 6.0, 0.07 }, { 8.02, 0.04 }, { 1.003, 0.5 } },
                         4.0, 0.0, 11);
    }

    inline SourceSound musicBox()
    {
        return additive ("Music Box", { { 1.0, 1.0 }, { 2.0, 0.35 }, { 3.0, 0.12 }, { 5.4, 0.07 },
                                        { 8.93, 0.025 }, { 4.0, 0.12 }, { 6.0, 0.05 } },
                         4.0, 0.0, 23);
    }

    inline SourceSound feltKeys()
    {
        std::vector<Partial> p;
        for (int k = 1; k <= 12; ++k)
            p.push_back ({ (double) k * (1.0 + 0.0004 * k * k), 1.0 / (k * k) });
        auto s = additive ("Felt Keys", p, 4.0, 0.0, 37);
        // a little soft breath noise under it
        juce::Random rng (99);
        float lp = 0.0f;
        for (auto& v : s.data)
        {
            lp += 0.05f * ((rng.nextFloat() * 2.0f - 1.0f) - lp);
            v += lp * 0.25f;
        }
        normalise (s.data);
        return s;
    }

    // Harmonics shaped by "ah" vowel formants, three slightly detuned singers.
    inline SourceSound choir()
    {
        const double formants[3][3] = { { 700, 130, 1.0 }, { 1220, 150, 0.5 }, { 2600, 260, 0.25 } };
        std::vector<Partial> p;
        for (int voice = 0; voice < 3; ++voice)
        {
            const double detune = voice == 0 ? 1.0 : (voice == 1 ? 1.0035 : 0.9967);
            for (int k = 1; k <= 30; ++k)
            {
                const double f = kRoot * k;
                double a = 0.15 / k;
                for (auto& fm : formants)
                    a += fm[2] * std::exp (-0.5 * std::pow ((f - fm[0]) / fm[1], 2.0));
                p.push_back ({ k * detune, a / k * 4.0 });
            }
        }
        return additive ("Choir", p, 4.0, 0.004, 51);
    }
} // namespace sources

} // namespace lp
