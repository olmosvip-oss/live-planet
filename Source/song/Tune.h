#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <signalsmith-stretch/signalsmith-stretch.h>
#include <vector>
#include <algorithm>
#include <cmath>

// Auto-tune: a vocal moved onto the notes of a key. Its pitch is followed moment by moment (5 ms steps),
// each moment is pulled to the nearest note of the key, and the voice is shifted there with its formants
// kept (so it still sounds like you). `strength` 0..1 is how far it's pulled; `speedMs` how quickly:
// gentle and slow sounds natural, full and instant is the hard, robotic sound.
namespace lp::song::tune
{

// the pitch (as a MIDI note) of each frame (`hop` samples apart), 0 where there's no clear pitch (breaths,
// consonants, silence). Heard at about 16 kHz (plenty for a voice, and many times quicker), then cleaned
// up: single-frame jumps and octave slips are taken out.
inline std::vector<double> track (const std::vector<float>& xIn, double srIn, int hop)
{
    const int dec = std::max (1, (int) std::round (srIn / 16000.0));
    const double sr = srIn / dec;
    std::vector<float> x;
    x.reserve (xIn.size() / (size_t) dec + 1);
    {
        // a gentle low-pass, then every dec-th sample
        float l1 = 0, l2 = 0;
        const float a = 1.0f - std::exp (-2.0f * 3.14159265f * 5000.0f / (float) srIn);
        for (size_t i = 0; i < xIn.size(); ++i)
        {
            l1 += (xIn[i] - l1) * a; l2 += (l1 - l2) * a;
            if (i % (size_t) dec == 0) x.push_back (l2);
        }
    }
    const int win = (int) (0.032 * sr);
    const int minLag = (int) (sr / 1000.0), maxLag = (int) (sr / 70.0);
    const double step = (double) hop / dec;
    std::vector<double> out;
    std::vector<double> d ((size_t) maxLag + 2);
    float peak = 0; for (float v : x) peak = std::max (peak, std::abs (v));
    const size_t frames = (size_t) std::max (0.0, std::floor ((double) xIn.size() / hop));
    out.assign (frames, 0.0);
    for (size_t f = 0; f < frames; ++f)
    {
        const int start = (int) (f * step);
        if (start + win + maxLag >= (int) x.size()) break;
        double e = 0;
        for (int i = 0; i < win; ++i) e += (double) x[(size_t) (start + i)] * x[(size_t) (start + i)];
        if (std::sqrt (e / win) < peak * 0.025) continue;
        // YIN: the difference function, normalised; the first dip under the threshold (or the deepest, if clear enough)
        double running = 0; d[0] = 1;
        int found = -1, deepest = -1;
        for (int tau = 1; tau <= maxLag; ++tau)
        {
            double sum = 0;
            for (int i = 0; i < win; ++i) { const double v = x[(size_t) (start + i)] - x[(size_t) (start + i + tau)]; sum += v * v; }
            running += sum;
            d[(size_t) tau] = running > 0 ? sum * tau / running : 1;
            if (tau > minLag && (deepest < 0 || d[(size_t) tau] < d[(size_t) deepest])) deepest = tau;
            if (found < 0 && tau > minLag + 1 && d[(size_t) tau - 1] < 0.12 && d[(size_t) tau - 1] <= d[(size_t) tau] && d[(size_t) tau - 1] <= d[(size_t) tau - 2])
                found = tau - 1;
        }
        if (found < 0 && deepest > 0 && deepest < maxLag && d[(size_t) deepest] < 0.25) found = deepest;
        if (found < 1 || found >= maxLag) continue;
        const double a = d[(size_t) found - 1], b = d[(size_t) found], c = d[(size_t) found + 1];
        const double den = a - 2 * b + c;
        const double t = found + (std::abs (den) > 1e-12 ? 0.5 * (a - c) / den : 0.0);
        out[f] = 69.0 + 12.0 * std::log2 (sr / t / 440.0);
    }
    // clean up: an octave slip next to steady neighbours is folded back; a lone voiced or unvoiced frame goes
    for (size_t f = 1; f + 1 < out.size(); ++f)
    {
        const double p = out[f], l = out[f - 1], r = out[f + 1];
        if (p <= 0.0) { if (l > 0.0 && r > 0.0 && std::abs (l - r) < 1.0) out[f] = 0.5 * (l + r); continue; }
        if (l <= 0.0 && r <= 0.0) { out[f] = 0.0; continue; }
        const double ref = l > 0.0 ? l : r;
        if (std::abs (p - ref - 12.0) < 1.0) out[f] = p - 12.0;
        else if (std::abs (p - ref + 12.0) < 1.0) out[f] = p + 12.0;
    }
    return out;
}

// the scales you can tune to (as steps from the key's note)
inline const std::vector<int>& scaleSteps (int scale)
{
    static const std::vector<int> s[] = { { 0, 2, 4, 5, 7, 9, 11 }, { 0, 2, 3, 5, 7, 8, 10 }, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 },
                                          { 0, 2, 3, 5, 7, 8, 11 }, { 0, 2, 4, 7, 9 }, { 0, 3, 5, 7, 10 } };
    return s[std::clamp (scale, 0, 5)];
}
inline const char* scaleName (int scale)
{
    static const char* n[] = { "Major", "Minor", "Chromatic", "Harmonic minor", "Major pentatonic", "Minor pentatonic" };
    return n[std::clamp (scale, 0, 5)];
}

// how it's tuned. speed: 0 instant (the hard, robotic sound) .. 1 slow (gentle). humanize: long notes keep
// their own movement (vibrato, scoops) around the right note. amount: how far it's pulled (1 = all the way).
// removed: notes you took out (bit per pitch class, C = bit 0): nothing is tuned to them.
struct Settings { int key = 0, scale = 0; float speed = 0.35f, humanize = 0.4f, amount = 1.0f; int removed = 0; };

inline bool allowed (int midiNote, const Settings& s)
{
    const int pc = ((midiNote % 12) + 12) % 12;
    if (s.removed & (1 << pc)) return false;
    const int rel = ((pc - s.key) % 12 + 12) % 12;
    for (int st : scaleSteps (s.scale)) if (st == rel) return true;
    return false;
}
inline double nearestAllowed (double midi, const Settings& s)
{
    double best = std::round (midi), bestD = 1e9;
    const int base = (int) std::floor (midi) - 7;
    for (int n = base; n <= base + 14; ++n)
        if (allowed (n, s) && std::abs (n - midi) < bestD) { bestD = std::abs (n - midi); best = n; }
    return best;
}

// the nearest note of a key (minor: natural minor) to a pitch
inline double nearestInKey (double midi, int key, bool minor)
{
    Settings s;
    s.key = key; s.scale = minor ? 1 : 0;
    return nearestAllowed (midi, s);
}

// the note each frame is tuned to (0: none). A sung note stays on its note until the voice has clearly
// moved to another one, so it doesn't flicker between two notes.
inline std::vector<double> targets (const std::vector<double>& pitch, const Settings& s)
{
    std::vector<double> t (pitch.size(), 0.0);
    double cur = 0.0;
    for (size_t f = 0; f < pitch.size(); ++f)
    {
        if (pitch[f] <= 0.0) { cur = 0.0; continue; }
        const double near = nearestAllowed (pitch[f], s);
        if (cur <= 0.0 || (near != cur && std::abs (pitch[f] - cur) > 0.62)) cur = near;
        t[f] = cur;
    }
    return t;
}

// the shift (semitones) for each frame
inline std::vector<float> correction (const std::vector<double>& pitch, const Settings& s, int hop, double sr, double* averageCorrection = nullptr)
{
    const auto tgt = targets (pitch, s);
    const double frameSec = hop / sr;
    // humanize: correct each moment's average (over up to 0.25 s of the same note), not every wiggle
    const int w = std::max (0, (int) std::round (s.humanize * 0.25 / frameSec / 2.0));
    std::vector<float> corr (pitch.size(), 0.0f);
    const double tau = (double) s.speed * s.speed * 0.4; // 0 .. 400 ms
    const float a = tau < 0.001 ? 1.0f : (float) (1.0 - std::exp (-frameSec / tau));
    float c = 0.0f;
    double sumAbs = 0; int voiced = 0;
    for (size_t f = 0; f < pitch.size(); ++f)
    {
        if (pitch[f] > 0.0)
        {
            double sum = 0; int k = 0;
            for (int j = -w; j <= w; ++j)
            {
                const long g = (long) f + j;
                if (g < 0 || g >= (long) pitch.size() || pitch[(size_t) g] <= 0.0 || tgt[(size_t) g] != tgt[f]) continue;
                sum += pitch[(size_t) g]; ++k;
            }
            const double centre = k > 0 ? sum / k : pitch[f];
            const float want = (float) ((tgt[f] - centre) * s.amount);
            // a new note: the retune speed glides it there; the first frame of a phrase starts right on it
            if (f == 0 || pitch[f - 1] <= 0.0) c = want;
            else c += (want - c) * a;
            sumAbs += std::abs (tgt[f] - pitch[f]); ++voiced;
        }
        else
            c *= 0.9f; // between notes it lets go, slowly (a consonant keeps the shift of its vowel)
        corr[f] = c;
    }
    if (averageCorrection) *averageCorrection = voiced > 0 ? sumAbs / voiced : 0.0;
    return corr;
}

inline juce::AudioBuffer<float> apply (const juce::AudioBuffer<float>& in, double sr, const Settings& s, double* averageCorrection = nullptr)
{
    const int n = in.getNumSamples();
    const int hop = std::max (64, (int) (0.005 * sr));
    std::vector<float> mono ((size_t) n);
    for (int i = 0; i < n; ++i) mono[(size_t) i] = 0.5f * (in.getSample (0, i) + in.getSample (std::min (1, in.getNumChannels() - 1), i));
    const auto pitch = track (mono, sr, hop);
    const auto corr = correction (pitch, s, hop, sr, averageCorrection);
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
        const int frame = juce::jlimit (0, std::max (0, (int) corr.size() - 1), (pos - st.inputLatency()) / hop);
        st.setTransposeSemitones (corr.empty() ? 0.0f : corr[(size_t) frame], (float) (8000.0 / sr));
        const float* ins[2] = { src.getReadPointer (0, pos), src.getReadPointer (1, pos) };
        float* outs[2] = { dst.getWritePointer (0, pos), dst.getWritePointer (1, pos) };
        st.process (ins, len, outs, len);
    }
    juce::AudioBuffer<float> out (2, n);
    for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, 0, dst, ch, lat, n);
    return out;
}

// the older way of asking: a key, major or minor, how far and how quickly
inline juce::AudioBuffer<float> apply (const juce::AudioBuffer<float>& in, double sr, int key, bool minor, float strength, float speedMs,
                                       double* averageCorrection = nullptr)
{
    Settings s;
    s.key = key; s.scale = minor ? 1 : 0; s.amount = strength;
    s.speed = (float) std::sqrt (std::max (0.0f, speedMs) / 400.0f);
    s.humanize = speedMs <= 0.5f ? 0.0f : 0.5f;
    return apply (in, sr, s, averageCorrection);
}

} // namespace lp::song::tune
