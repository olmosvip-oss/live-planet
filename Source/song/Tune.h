#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <signalsmith-stretch/signalsmith-stretch.h>
#include <vector>

// Auto-tune: a vocal moved onto the notes of a key. Its pitch is followed moment by moment (5 ms steps),
// each moment is pulled to the nearest note of the key, and the voice is shifted there with its formants
// kept (so it still sounds like you). `strength` 0..1 is how far it's pulled; `speedMs` how quickly:
// gentle and slow sounds natural, full and instant is the hard, robotic sound.
namespace lp::song::tune
{

// the pitch (as a MIDI note) of each frame, 0 where there's no clear pitch (breaths, consonants, silence)
inline std::vector<double> track (const std::vector<float>& x, double sr, int hop)
{
    const int win = (int) (0.04 * sr);
    const int minLag = (int) (sr / 1000.0), maxLag = (int) (sr / 70.0);
    std::vector<double> out;
    std::vector<double> d ((size_t) maxLag + 2);
    float peak = 0; for (float v : x) peak = std::max (peak, std::abs (v));
    for (int start = 0; start + win + maxLag < (int) x.size(); start += hop)
    {
        double e = 0;
        for (int i = 0; i < win; ++i) e += (double) x[(size_t) (start + i)] * x[(size_t) (start + i)];
        if (std::sqrt (e / win) < peak * 0.03) { out.push_back (0.0); continue; }
        // YIN: the difference function, normalised, first dip under the threshold
        double running = 0; d[0] = 1;
        int found = -1;
        for (int tau = 1; tau <= maxLag; ++tau)
        {
            double sum = 0;
            for (int i = 0; i < win; i += 2) { const double v = x[(size_t) (start + i)] - x[(size_t) (start + i + tau)]; sum += v * v; }
            running += sum;
            d[(size_t) tau] = running > 0 ? sum * tau / running : 1;
            if (found < 0 && tau > minLag && tau >= 3 && d[(size_t) tau - 1] < 0.15 && d[(size_t) tau - 1] <= d[(size_t) tau] && d[(size_t) tau - 1] <= d[(size_t) tau - 2])
                found = tau - 1;
        }
        if (found < 0) { out.push_back (0.0); continue; }
        const double a = d[(size_t) found - 1], b = d[(size_t) found], c = d[(size_t) found + 1];
        const double den = a - 2 * b + c;
        const double t = found + (std::abs (den) > 1e-12 ? 0.5 * (a - c) / den : 0.0);
        out.push_back (69.0 + 12.0 * std::log2 (sr / t / 440.0));
    }
    return out;
}

// the nearest note of the key (minor: natural minor) to a pitch
inline double nearestInKey (double midi, int key, bool minor)
{
    static const int maj[7] = { 0, 2, 4, 5, 7, 9, 11 }, mn[7] = { 0, 2, 3, 5, 7, 8, 10 };
    const int* sc = minor ? mn : maj;
    double best = midi, bestD = 1e9;
    const int base = (int) std::floor (midi) - 12;
    for (int n = base; n <= base + 24; ++n)
    {
        const int pc = ((n - key) % 12 + 12) % 12;
        bool in = false;
        for (int k = 0; k < 7; ++k) in = in || sc[k] == pc;
        if (in && std::abs (n - midi) < bestD) { bestD = std::abs (n - midi); best = n; }
    }
    return best;
}

inline juce::AudioBuffer<float> apply (const juce::AudioBuffer<float>& in, double sr, int key, bool minor, float strength, float speedMs,
                                       double* averageCorrection = nullptr)
{
    const int n = in.getNumSamples();
    const int hop = std::max (64, (int) (0.005 * sr));
    std::vector<float> mono ((size_t) n);
    for (int i = 0; i < n; ++i) mono[(size_t) i] = 0.5f * (in.getSample (0, i) + in.getSample (std::min (1, in.getNumChannels() - 1), i));
    const auto pitch = track (mono, sr, hop);
    // the correction (in semitones) for each frame, smoothed by the speed
    std::vector<float> corr (pitch.size(), 0.0f);
    const float a = speedMs <= 0.5f ? 1.0f : 1.0f - std::exp (-(float) hop / (float) sr / (speedMs * 0.001f));
    float c = 0.0f;
    double sumAbs = 0; int voiced = 0;
    for (size_t f = 0; f < pitch.size(); ++f)
    {
        const float want = pitch[f] > 0.0 ? (float) (nearestInKey (pitch[f], key, minor) - pitch[f]) * strength : 0.0f;
        c += (want - c) * (pitch[f] > 0.0 ? a : 0.2f); // between notes it lets go
        corr[f] = c;
        if (pitch[f] > 0.0) { sumAbs += std::abs (want); ++voiced; }
    }
    if (averageCorrection) *averageCorrection = voiced > 0 ? sumAbs / voiced : 0.0;
    // shift the voice, block by block, keeping its formants
    signalsmith::stretch::SignalsmithStretch<float> st;
    st.presetDefault (2, (float) sr);
    st.setFormantFactor (1.0f, true);
    const int lat = st.inputLatency() + st.outputLatency();
    const int total = n + lat;
    juce::AudioBuffer<float> src (2, total), dst (2, total);
    src.clear();
    for (int ch = 0; ch < 2; ++ch) src.copyFrom (ch, 0, in, std::min (ch, in.getNumChannels() - 1), 0, n);
    for (int pos = 0; pos < total; pos += hop)
    {
        const int len = std::min (hop, total - pos);
        const int frame = juce::jlimit (0, (int) corr.size() - 1, (pos - st.inputLatency()) / hop);
        st.setTransposeSemitones (corr.empty() ? 0.0f : corr[(size_t) frame], (float) (8000.0 / sr));
        const float* ins[2] = { src.getReadPointer (0, pos), src.getReadPointer (1, pos) };
        float* outs[2] = { dst.getWritePointer (0, pos), dst.getWritePointer (1, pos) };
        st.process (ins, len, outs, len);
    }
    juce::AudioBuffer<float> out (2, n);
    for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, 0, dst, ch, lat, n);
    return out;
}

} // namespace lp::song::tune
