#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>
#include <cmath>
#include <vector>

// What note a sound plays, and whether it really plays just one. Used for the sounds a hook or an 808
// is played on: a sound that slides around, plays a phrase or a chord can't be put on the song's notes.
namespace lp::song::onenote
{

// a pitch tracker for one stretch of a (low-passed, down-sampled) signal: the MIDI note, or 0
inline double yinAt (const std::vector<float>& x, int start, int win, double rate, double lowHz)
{
    const int maxLag = (int) (rate / lowHz), minLag = std::max (2, (int) (rate / 2000.0));
    if (start < 0 || start + win + maxLag + 2 >= (int) x.size()) return 0.0;
    std::vector<double> d ((size_t) maxLag + 2, 1.0);
    double running = 0;
    for (int tau = 1; tau <= maxLag + 1; ++tau)
    {
        double sum = 0;
        for (int i = 0; i < win; ++i) { const double v = x[(size_t) (start + i)] - x[(size_t) (start + i + tau)]; sum += v * v; }
        running += sum;
        d[(size_t) tau] = running > 0 ? sum * tau / running : 1.0;
    }
    for (int tau = minLag; tau < maxLag; ++tau)
        if (d[(size_t) tau] < 0.15)
        {
            while (tau + 1 < maxLag && d[(size_t) tau + 1] < d[(size_t) tau]) ++tau;
            const double a = d[(size_t) tau - 1], b = d[(size_t) tau], c = d[(size_t) tau + 1];
            const double den = a - 2 * b + c;
            const double t = tau + (std::abs (den) > 1e-12 ? 0.5 * (a - c) / den : 0.0);
            return 69.0 + 12.0 * std::log2 (rate / t / 440.0);
        }
    return 0.0;
}

// mono, gently low-passed and down-sampled to about 12 kHz (plenty for a fundamental, and fast)
inline std::vector<float> prepare (const juce::AudioBuffer<float>& b, double sr, int maxSamples, double& rate)
{
    const int n = std::min (b.getNumSamples(), maxSamples);
    const int f = std::max (1, (int) std::round (sr / 12000.0));
    rate = sr / f;
    std::vector<float> out;
    out.reserve ((size_t) (n / f + 1));
    const int ch1 = std::min (1, b.getNumChannels() - 1);
    for (int i = 0; i + f <= n; i += f)
    {
        float s = 0;
        for (int k = 0; k < f; ++k) s += b.getSample (0, i + k) + b.getSample (ch1, i + k);
        out.push_back (s * 0.5f / (float) f);
    }
    return out;
}

struct Reading
{
    double midi = 0.0;      // its note (the median of what was heard)
    float steady = 0.0f;    // how much of the sound sits on that note (0..1; octave jumps of the tracker count as the same note)
    float voiced = 0.0f;    // how much of the sound has a clear pitch at all
    float body = 0.0f;      // how much of its sound is that note's own first harmonics (a chord's "note" has almost none)
};

// listens to the first 1.5 s (after the attack): one steady note, or not
inline Reading read (const juce::AudioBuffer<float>& b, double sr)
{
    Reading r;
    double rate = 0;
    const auto x = prepare (b, sr, (int) (1.5 * sr), rate);
    if (x.size() < 600) return r;
    float peak = 0; for (float v : x) peak = std::max (peak, std::abs (v));
    if (peak <= 0) return r;
    const int win = (int) (0.05 * rate), hop = (int) (0.02 * rate), skip = (int) (0.06 * rate); // past the attack (an 808's click and drop)
    std::vector<double> notes;
    int frames = 0;
    for (int s = skip; s + win + (int) (rate / 30.0) + 2 < (int) x.size(); s += hop)
    {
        float m = 0; for (int i = 0; i < win; ++i) m = std::max (m, std::abs (x[(size_t) (s + i)]));
        if (m < peak * 0.1f) continue; // the tail, faded out
        ++frames;
        if (const double p = yinAt (x, s, win, rate, 30.0); p > 0.0) notes.push_back (p);
    }
    if (frames == 0 || notes.empty()) return r;
    auto sorted = notes;
    std::nth_element (sorted.begin(), sorted.begin() + (long) sorted.size() / 2, sorted.end());
    r.midi = sorted[sorted.size() / 2];
    int on = 0;
    for (double p : notes)
    {
        const double d = std::fmod (std::abs (p - r.midi) + 0.5, 12.0) - 0.5; // distance, octaves folded
        if (std::abs (d) < 0.75 || std::abs (d - 12.0) < 0.75) ++on;
    }
    r.steady = (float) on / (float) notes.size();
    r.voiced = (float) notes.size() / (float) frames;
    // a chord fools a pitch tracker into a deep "note" nobody plays (C E G -> a low C): check that note
    // and its next two harmonics really carry the sound
    {
        const int n = std::min ((int) x.size() - skip, (int) (0.2 * rate));
        double all = 0, harm = 0;
        for (int i = 0; i < n; ++i) all += (double) x[(size_t) (skip + i)] * x[(size_t) (skip + i)];
        const double f0 = 440.0 * std::pow (2.0, (r.midi - 69.0) / 12.0);
        for (int h = 1; h <= 3 && h * f0 < rate * 0.45; ++h)
        {
            const double w = 2.0 * juce::MathConstants<double>::pi * h * f0 / rate;
            double re = 0, im = 0;
            for (int i = 0; i < n; ++i) { const double v = x[(size_t) (skip + i)]; re += v * std::cos (w * i); im -= v * std::sin (w * i); }
            harm += 2.0 * (re * re + im * im) / n; // the energy of that one frequency
        }
        r.body = all > 0 ? (float) std::min (1.0, harm / all) : 0.0f;
    }
    return r;
}

// a note name in a file name ("Pluck_C#3", "808 - F", "Keys_Eb"): its pitch class, and its octave if written (else -99).
// -1 when the name has none. A key ("G minor", "Gmin") counts too: its root is what such a sound plays.
inline int noteInName (const juce::String& path, int& octave)
{
    octave = -99;
    auto words = juce::StringArray::fromTokens (juce::File (path).getFileNameWithoutExtension().replaceCharacters ("-. ()[]", "_______"), "_", "");
    words.removeEmptyStrings();
    for (int i = words.size() - 1; i >= 0; --i)
    {
        const auto w = words[i];
        const int letter = juce::String ("C D EF G A B").indexOfChar (juce::CharacterFunctions::toUpperCase (w[0]));
        if (letter < 0 || w[0] == ' ') continue;
        juce::String rest = w.substring (1).toLowerCase();
        int pc = letter;
        if (rest.startsWith ("#") || (rest.startsWith ("s") && ! rest.startsWith ("sh"))) { ++pc; rest = rest.substring (1); }
        else if (rest.startsWith ("b") && rest.length() <= 4) { --pc; rest = rest.substring (1); }
        if (rest.startsWith ("maj") || rest.startsWith ("min")) rest = rest.substring (3);
        else if (rest.startsWith ("m")) rest = rest.substring (1);
        if (! (rest.isEmpty() || rest.containsOnly ("0123456789-"))) continue;
        if (rest.isNotEmpty() && rest.length() <= 2) octave = rest.getIntValue();
        return ((pc % 12) + 12) % 12;
    }
    return -1;
}

// the note a one-shot plays (fractional MIDI): its name says which note (producers label them), the
// sound says which octave and how sharp or flat; `fallback` when neither knows
inline double rootOf (const juce::AudioBuffer<float>& b, double sr, double fallback, const juce::String& path)
{
    int oct = 0;
    const int pc = noteInName (path, oct);
    const auto heard = read (b, sr);
    const bool clear = heard.midi > 0.0 && heard.voiced > 0.3f && heard.steady > 0.6f && heard.body > 0.2f;
    if (pc >= 0)
    {
        if (clear)
        {
            // the name's note in the octave the sound is in; keep the heard tuning when it agrees
            const double near = pc + 12.0 * std::round ((heard.midi - pc) / 12.0);
            return std::abs (heard.midi - near) < 0.5 ? heard.midi : near;
        }
        if (oct != -99) return (oct + 2) * 12.0 + pc; // Ableton's naming: C3 is middle C
        return pc + 12.0 * std::round ((fallback - pc) / 12.0);
    }
    return clear ? heard.midi : fallback;
}

} // namespace lp::song::onenote
