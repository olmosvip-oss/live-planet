#pragma once
// More voices from your own: the singing model takes a voice's character from a short clip of it, so a clip of you
// re-shaped (its formants moved: a bigger or smaller "singer", its colour changed: breathier, grittier, warmer)
// becomes another voice to sing with. All of them are yours.
#include <juce_audio_basics/juce_audio_basics.h>
#include <signalsmith-stretch/signalsmith-stretch.h>

namespace lp::song::variants
{
struct Variant
{
    const char* name;  // added to your voice's name: "Me - Deep"
    const char* tags;  // shown on its card
    float formant;     // 0.86 a bigger, deeper singer .. 1.2 a smaller, brighter one
    float semis;       // where it sits (the model sings your melody anyway; this nudges the character)
    int colour;        // 0 as is, 1 airy, 2 raspy, 3 warm
};

inline const std::vector<Variant>& all()
{
    static const std::vector<Variant> v {
        { "Deep",        "low, full, baritone",  0.86f, -2.0f, 0 },
        { "Warm",        "soft, round, mellow",  0.95f, -1.0f, 3 },
        { "Raspy",       "gritty, rock, rap",    0.97f,  0.0f, 2 },
        { "Bright",      "light, pop, forward",  1.10f,  2.0f, 0 },
        { "Airy",        "high, breathy, r&b", 1.18f,  3.0f, 1 },
    };
    return v;
}

// mono in, mono out (same length)
inline juce::AudioBuffer<float> make (const juce::AudioBuffer<float>& in, double sr, const Variant& v)
{
    const int n = in.getNumSamples();
    juce::AudioBuffer<float> out (1, n);
    out.clear();
    if (n <= 0) return out;
    signalsmith::stretch::SignalsmithStretch<float> st;
    st.presetDefault (1, (float) sr);
    st.setFormantFactor (v.formant, true);
    st.setTransposeSemitones (v.semis, (float) (8000.0 / sr));
    const int lat = st.inputLatency() + st.outputLatency(), total = n + lat, hop = 1024;
    std::vector<float> src ((size_t) total, 0.0f), dst ((size_t) total, 0.0f);
    std::copy (in.getReadPointer (0), in.getReadPointer (0) + n, src.begin());
    for (int pos = 0; pos < total; pos += hop)
    {
        const int len = std::min (hop, total - pos);
        const float* ins[1] = { src.data() + pos };
        float* outs[1] = { dst.data() + pos };
        st.process (ins, len, outs, len);
    }
    auto* y = out.getWritePointer (0);
    for (int i = 0; i < n; ++i) y[i] = dst[(size_t) (i + lat)];
    // its colour
    if (v.colour == 1)
    {
        // airy: the top lifted, a breath of noise riding the voice's own level
        juce::IIRFilter shelf;
        shelf.setCoefficients (juce::IIRCoefficients::makeHighShelf (sr, 6000.0, 0.7, 2.4));
        shelf.processSamples (y, n);
        juce::Random rng (7);
        float env = 0.0f;
        juce::IIRFilter hp;
        hp.setCoefficients (juce::IIRCoefficients::makeHighPass (sr, 5000.0));
        std::vector<float> breath ((size_t) n);
        for (int i = 0; i < n; ++i) { env = std::max (std::abs (y[i]), env * 0.9995f); breath[(size_t) i] = (rng.nextFloat() * 2.0f - 1.0f) * env * 0.06f; }
        hp.processSamples (breath.data(), n);
        for (int i = 0; i < n; ++i) y[i] += breath[(size_t) i];
    }
    else if (v.colour == 2)
    {
        // raspy: driven a little, the grit kept in the upper mids
        juce::IIRFilter mid;
        mid.setCoefficients (juce::IIRCoefficients::makePeakFilter (sr, 2500.0, 0.9, 1.6));
        for (int i = 0; i < n; ++i) y[i] = std::tanh (y[i] * 3.2f) / 3.2f * 2.2f;
        mid.processSamples (y, n);
    }
    else if (v.colour == 3)
    {
        // warm: the top softened, the body fuller
        juce::IIRFilter lp, low;
        lp.setCoefficients (juce::IIRCoefficients::makeLowPass (sr, 7500.0));
        low.setCoefficients (juce::IIRCoefficients::makeLowShelf (sr, 250.0, 0.7, 1.5));
        lp.processSamples (y, n);
        low.processSamples (y, n);
    }
    // the same loudness as the clip it came from
    const float a = in.getRMSLevel (0, 0, n), b = out.getRMSLevel (0, 0, n);
    if (b > 1e-6f) out.applyGain (juce::jlimit (0.1f, 4.0f, a / b));
    const float pk = out.getMagnitude (0, 0, n);
    if (pk > 0.98f) out.applyGain (0.98f / pk);
    return out;
}
} // namespace lp::song::variants
