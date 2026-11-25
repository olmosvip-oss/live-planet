#pragma once
#include "Tune.h"

// Stacks from one vocal: doubles, harmonies and a choir, made here (no AI needed).
// A harmony follows the vocal note by note: each moment's note is found (like Auto-tune does), the note
// `steps` notes up or down the song's scale is the harmony's, and the voice is moved there with its formants
// kept. A double is the same line sung again: a few cents off, a few milliseconds late, panned away. A choir
// is several of them at once, each "singer" with a slightly different voice (formants) and timing.
namespace lp::song::stack
{
// the note `steps` notes of the scale above (or below, negative) a note that's in it
inline int stepInScale (int midi, int steps, const tune::Settings& s)
{
    int n = midi;
    const int dir = steps > 0 ? 1 : -1;
    for (int k = 0; k < std::abs (steps); ++k)
    {
        do n += dir; while (! tune::allowed (n, s) && std::abs (n - midi) < 24);
    }
    return n;
}

struct Voice
{
    int steps = 0;            // 0 the line itself, 2 a third above, -4 a fifth below...
    float cents = 0.0f;       // a little off, like a second take
    float formant = 1.0f;     // another singer's voice (0.9 deeper, 1.1 brighter)
    float delayMs = 0.0f;     // a little late
    float pan = 0.0f;         // -1 left .. 1 right
    float gainDb = 0.0f;
};

// one voice of the stack, at the same length as the vocal
inline juce::AudioBuffer<float> sing (const juce::AudioBuffer<float>& in, double sr, const tune::Settings& s, const Voice& v)
{
    const int n = in.getNumSamples();
    const int hop = std::max (64, (int) (0.005 * sr));
    std::vector<float> mono ((size_t) n);
    for (int i = 0; i < n; ++i) mono[(size_t) i] = 0.5f * (in.getSample (0, i) + in.getSample (std::min (1, in.getNumChannels() - 1), i));
    std::vector<float> shift;
    if (v.steps != 0)
    {
        // where the line is, note by note, and the scale note `steps` away from each
        const auto pitch = tune::track (mono, sr, hop);
        const auto tgt = tune::targets (pitch, s);
        shift.assign (pitch.size(), 0.0f);
        float c = 0.0f;
        for (size_t f = 0; f < pitch.size(); ++f)
        {
            if (tgt[f] > 0.0)
            {
                const float want = (float) (stepInScale ((int) std::round (tgt[f]), v.steps, s) - std::round (tgt[f]));
                c = (f == 0 || tgt[f - 1] <= 0.0) ? want : c + (want - c) * 0.35f; // (a quick glide between harmony notes)
            }
            shift[f] = c; // (between notes it keeps the last one: a consonant keeps its vowel's shift)
        }
    }
    signalsmith::stretch::SignalsmithStretch<float> st;
    st.presetDefault (2, (float) sr);
    st.setFormantFactor (v.formant, true);
    const int lat = st.inputLatency() + st.outputLatency();
    const int delay = (int) std::round (v.delayMs * 0.001 * sr);
    const int total = n + lat;
    juce::AudioBuffer<float> src (2, total), dst (2, total);
    src.clear();
    for (int ch = 0; ch < 2; ++ch) src.copyFrom (ch, 0, in, std::min (ch, in.getNumChannels() - 1), 0, n);
    juce::Random rng ((juce::int64) (v.cents * 1000.0f + v.steps * 77 + v.delayMs * 13));
    float drift = 0.0f;
    for (int pos = 0; pos < total; pos += hop)
    {
        const int len = std::min (hop, total - pos);
        const int frame = juce::jlimit (0, std::max (0, (int) shift.size() - 1), (pos - st.inputLatency()) / hop);
        drift += (rng.nextFloat() - 0.5f) * 0.6f - drift * 0.02f; // (a singer is never perfectly steady)
        const float semis = (shift.empty() ? 0.0f : shift[(size_t) frame]) + (v.cents + drift) * 0.01f;
        st.setTransposeSemitones (semis, (float) (8000.0 / sr));
        const float* ins[2] = { src.getReadPointer (0, pos), src.getReadPointer (1, pos) };
        float* outs[2] = { dst.getWritePointer (0, pos), dst.getWritePointer (1, pos) };
        st.process (ins, len, outs, len);
    }
    // placed: late by its delay, panned, at its level
    juce::AudioBuffer<float> out (2, n);
    out.clear();
    const float g = juce::Decibels::decibelsToGain (v.gainDb);
    const float a = (juce::jlimit (-1.0f, 1.0f, v.pan) + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
    const float gl = std::cos (a) * 1.41421356f * g, gr = std::sin (a) * 1.41421356f * g;
    for (int i = 0; i + delay < n; ++i)
    {
        const float m = 0.5f * (dst.getSample (0, lat + i) + dst.getSample (1, lat + i));
        out.setSample (0, i + delay, m * gl);
        out.setSample (1, i + delay, m * gr);
    }
    return out;
}

enum class Kind { Double, ThirdAbove, FifthBelow, Choir };

inline std::vector<Voice> voicesFor (Kind k)
{
    switch (k)
    {
        case Kind::Double:     return { { 0, -9.0f, 1.0f, 22.0f, -0.75f, -3.0f }, { 0, 8.0f, 1.0f, 34.0f, 0.75f, -3.0f } };
        case Kind::ThirdAbove: return { { 2, 0.0f, 1.0f, 12.0f, -0.35f, -4.0f }, { 2, 7.0f, 1.0f, 28.0f, 0.35f, -6.0f } };
        case Kind::FifthBelow: return { { -4, 0.0f, 1.0f, 14.0f, 0.35f, -4.0f }, { -4, -6.0f, 1.0f, 30.0f, -0.35f, -6.0f } };
        case Kind::Choir:      return { { 0, -10.0f, 0.93f, 26.0f, -0.85f, -6.0f }, { 0, 9.0f, 1.07f, 38.0f, 0.85f, -6.0f },
                                        { 2, -5.0f, 1.08f, 18.0f, -0.5f, -7.0f },  { 2, 6.0f, 0.95f, 44.0f, 0.55f, -8.0f },
                                        { -4, 4.0f, 0.9f, 30.0f, 0.3f, -7.0f },    { -4, -7.0f, 1.04f, 22.0f, -0.3f, -8.0f } };
    }
    return {};
}

inline const char* nameOf (Kind k)
{
    switch (k)
    {
        case Kind::Double: return "double";
        case Kind::ThirdAbove: return "harmony above";
        case Kind::FifthBelow: return "harmony below";
        case Kind::Choir: return "choir";
    }
    return "stack";
}

// the whole stack, mixed (only where the vocal sings: the silence around it is left alone)
inline juce::AudioBuffer<float> make (const juce::AudioBuffer<float>& in, double sr, const tune::Settings& s, Kind k)
{
    const int n = in.getNumSamples();
    int a = n, b = 0;
    for (int ch = 0; ch < in.getNumChannels(); ++ch)
    {
        const float* d = in.getReadPointer (ch);
        for (int i = 0; i < n; ++i) if (std::abs (d[i]) > 1.0e-4f) { a = std::min (a, i); b = std::max (b, i); }
    }
    juce::AudioBuffer<float> out (2, n);
    out.clear();
    if (b <= a) return out;
    a = std::max (0, a - (int) (0.05 * sr));
    b = std::min (n, b + (int) (0.3 * sr));
    juce::AudioBuffer<float> part (2, b - a);
    for (int ch = 0; ch < 2; ++ch) part.copyFrom (ch, 0, in, std::min (ch, in.getNumChannels() - 1), a, b - a);
    for (auto& v : voicesFor (k))
    {
        const auto one = sing (part, sr, s, v);
        for (int ch = 0; ch < 2; ++ch) out.addFrom (ch, a, one, ch, 0, b - a);
    }
    return out;
}
} // namespace lp::song::stack
