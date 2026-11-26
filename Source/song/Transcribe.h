#pragma once
// Reading the notes out of a track's audio (a piano loop, a guitar, keys): which pitches sound, and when,
// on the song's 16th-note grid. Polyphonic, from the harmonics in the spectrum. Not perfect on dense or
// heavily processed audio, best on clean keys, guitars, plucks, bass and leads.
#include <complex>
#include <numeric>
#include "Song.h"
#include "Tune.h"

namespace lp::song::transcribe
{
// a small in-place radix-2 FFT (power-of-two sizes): magnitudes of the first half into mag
inline void fftMagnitudes (std::vector<std::complex<float>>& a, std::vector<float>& mag)
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
        const float ang = -2.0f * juce::MathConstants<float>::pi / (float) len;
        const std::complex<float> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<float> w (1.0f, 0.0f);
            for (size_t k = 0; k < len / 2; ++k, w *= wl)
            {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
            }
        }
    }
    mag.resize (n / 2);
    for (size_t k = 0; k < n / 2; ++k) mag[k] = std::abs (a[k]);
}

struct Options
{
    double bpm = 120.0;
    int key = 0;           // the song's key (pitch class) and mode: notes outside it need to be clearly there
    bool minor = false;
    int lowest = 28, highest = 96;
    int maxVoices = 5;     // at most this many notes at once
};

// mono: the track at sampleRate from the song's start. Returns notes in beats.
inline std::vector<Note> notesOf (const float* mono, int numSamples, double sampleRate, const Options& o)
{
    std::vector<Note> out;
    if (numSamples < 4096 || sampleRate <= 0.0 || o.bpm <= 0.0) return out;
    constexpr int order = 13, N = 1 << order, hop = 1024; // (8192: low notes resolved)
    std::vector<float> window ((size_t) N), buf;
    std::vector<std::complex<float>> cbuf ((size_t) N);
    for (int i = 0; i < N; ++i) window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) N);
    const int frames = std::max (0, (numSamples - N) / hop + 1);
    const int P = o.highest - o.lowest + 1;
    std::vector<std::vector<float>> sal ((size_t) frames, std::vector<float> ((size_t) P, 0.0f));
    std::vector<float> energy ((size_t) frames, 0.0f);
    const double binHz = sampleRate / N;
    for (int f = 0; f < frames; ++f)
    {
        const float* x = mono + (size_t) f * hop;
        for (int i = 0; i < N; ++i) cbuf[(size_t) i] = { x[i] * window[(size_t) i], 0.0f };
        fftMagnitudes (cbuf, buf);
        float e = 0.0f;
        for (int k = 1; k < N / 2; ++k) e += buf[(size_t) k];
        energy[(size_t) f] = e;
        auto mag = [&] (double hz)
        {
            const double b = hz / binHz;
            const int k = (int) std::round (b);
            if (k < 1 || k + 1 >= N / 2) return 0.0f;
            return std::max ({ buf[(size_t) k - 1] * 0.6f, buf[(size_t) k], buf[(size_t) k + 1] * 0.6f });
        };
        for (int p = 0; p < P; ++p)
        {
            const double f0 = 440.0 * std::pow (2.0, (o.lowest + p - 69) / 12.0);
            float s = 0.0f, w = 1.0f;
            for (int h = 1; h <= 6; ++h, w *= 0.75f)
            {
                if (f0 * h > sampleRate * 0.45) break;
                s += w * std::log1p (mag (f0 * h));
            }
            sal[(size_t) f][(size_t) p] = s;
        }
    }
    // the 16th-note grid: how strongly each pitch sounds in each step, and where new notes start
    const double secPer16 = 60.0 / o.bpm / 4.0;
    const int steps = (int) std::ceil ((double) numSamples / sampleRate / secPer16);
    std::vector<std::vector<float>> grid ((size_t) steps, std::vector<float> ((size_t) P, 0.0f));
    std::vector<float> loud ((size_t) steps, 0.0f);
    for (int s = 0; s < steps; ++s)
    {
        const int f0 = (int) (s * secPer16 * sampleRate / hop), f1 = std::min (frames, (int) ((s + 1) * secPer16 * sampleRate / hop) + 1);
        int cnt = 0;
        for (int f = std::max (0, f0); f < f1; ++f, ++cnt)
        {
            for (int p = 0; p < P; ++p) grid[(size_t) s][(size_t) p] += sal[(size_t) f][(size_t) p];
            loud[(size_t) s] += energy[(size_t) f];
        }
        if (cnt > 0) { for (auto& v : grid[(size_t) s]) v /= (float) cnt; loud[(size_t) s] /= (float) cnt; }
    }
    float loudest = 0.0f;
    for (float v : loud) loudest = std::max (loudest, v);
    if (loudest <= 0.0f) return out;
    const int majSteps[7] = { 0, 2, 4, 5, 7, 9, 11 }, minSteps[7] = { 0, 2, 3, 5, 7, 8, 10 };
    std::array<bool, 12> inKey {};
    for (int i = 0; i < 7; ++i) inKey[(size_t) ((o.key + (o.minor ? minSteps[i] : majSteps[i])) % 12)] = true;
    // each step: the strongest peaks (not a note's own octave or fifth above), above the step's own noise
    std::vector<std::vector<bool>> on ((size_t) steps, std::vector<bool> ((size_t) P, false));
    for (int s = 0; s < steps; ++s)
    {
        if (loud[(size_t) s] < loudest * 0.04f) continue; // (silence)
        const auto& g = grid[(size_t) s];
        float top = 0.0f, mean = 0.0f;
        for (float v : g) { top = std::max (top, v); mean += v; }
        mean /= (float) P;
        std::vector<int> picks;
        std::vector<int> order ((size_t) P);
        std::iota (order.begin(), order.end(), 0);
        std::sort (order.begin(), order.end(), [&g] (int a, int b) { return g[(size_t) a] > g[(size_t) b]; });
        for (int p : order)
        {
            if ((int) picks.size() >= o.maxVoices) break;
            const float v = g[(size_t) p];
            const bool keyOk = inKey[(size_t) ((o.lowest + p) % 12)];
            if (v < mean + (top - mean) * (keyOk ? 0.45f : 0.7f)) break;
            const bool peak = (p == 0 || v >= g[(size_t) p - 1]) && (p + 1 >= P || v >= g[(size_t) p + 1]);
            if (! peak) continue;
            bool harmonic = false; // (an overtone of a lower note already picked)
            for (int q : picks)
                for (int iv : { 12, 19, 24, 28, 31 })
                    if (p == q + iv && v < g[(size_t) q] * 1.05f) harmonic = true;
            if (! harmonic) picks.push_back (p);
        }
        for (int p : picks) on[(size_t) s][(size_t) p] = true;
    }
    // notes: a pitch held over steps; it starts again where it gets clearly louder (a new hit of the same note)
    for (int p = 0; p < P; ++p)
    {
        for (int s = 0; s < steps;)
        {
            if (! on[(size_t) s][(size_t) p]) { ++s; continue; }
            int e = s + 1;
            while (e < steps && on[(size_t) e][(size_t) p]
                   && ! (grid[(size_t) e][(size_t) p] > grid[(size_t) e - 1][(size_t) p] * 1.18f && loud[(size_t) e] > loud[(size_t) e - 1] * 1.4f)) ++e;
            Note n;
            n.beat = s * 0.25;
            n.length = (e - s) * 0.25;
            n.pitch = o.lowest + p;
            n.velocity = juce::jlimit (0.35f, 1.0f, 0.4f + 0.6f * std::sqrt (loud[(size_t) s] / loudest));
            out.push_back (n);
            s = e;
        }
    }
    std::sort (out.begin(), out.end(), [] (const Note& a, const Note& b) { return a.beat < b.beat || (a.beat == b.beat && a.pitch < b.pitch); });
    return out;
}

// One voice (a sung vocal, a hummed line, a lead): its melody, followed moment by moment with a pitch tracker,
// each sung note snapped to the nearest note of the song's key, put on the 16th-note grid. A new syllable on
// the same note is a new note; breaths, consonants and silence are rests. (The polyphonic reading above hears a
// voice's overtones and its room as extra notes, so a voice always comes here instead.)
inline std::vector<Note> melodyOf (const float* mono, int numSamples, double sampleRate, const Options& o)
{
    std::vector<Note> out;
    if (numSamples < 4096 || sampleRate <= 0.0 || o.bpm <= 0.0) return out;
    const int hop = std::max (64, (int) std::round (sampleRate * 0.008)); // (8 ms steps)
    std::vector<float> x (mono, mono + numSamples);
    const auto pitch = tune::track (x, sampleRate, hop);
    const double frameSec = (double) hop / sampleRate;
    tune::Settings ks;
    ks.key = o.key; ks.scale = o.minor ? 1 : 0;
    const auto tgt = tune::targets (pitch, ks); // (a note held until the voice clearly moves: no flicker)
    // each frame's level (for the velocities, and to hear a new syllable on the same note)
    std::vector<float> level (pitch.size(), 0.0f);
    float loudest = 0.0f;
    for (size_t f = 0; f < pitch.size(); ++f)
    {
        const size_t a = f * (size_t) hop, b = std::min ((size_t) numSamples, a + (size_t) hop);
        double e = 0.0;
        for (size_t i = a; i < b; ++i) e += (double) x[i] * x[i];
        level[f] = b > a ? (float) std::sqrt (e / (double) (b - a)) : 0.0f;
        loudest = std::max (loudest, level[f]);
    }
    if (loudest <= 0.0f) return out;
    // runs of one note (a gap of a couple of frames inside a note is bridged)
    struct Run { size_t a, b; int pitch; };
    std::vector<Run> runs;
    const size_t bridge = (size_t) std::max (1.0, 0.03 / frameSec);
    for (size_t f = 0; f < tgt.size();)
    {
        if (tgt[f] <= 0.0) { ++f; continue; }
        const int p = (int) tgt[f];
        size_t e = f + 1, lastOn = f;
        while (e < tgt.size())
        {
            if ((int) tgt[e] == p) { lastOn = e; ++e; continue; }
            if (tgt[e] <= 0.0 && e - lastOn <= bridge) { ++e; continue; }
            break;
        }
        runs.push_back ({ f, lastOn + 1, p });
        f = lastOn + 1;
    }
    // too short to be a note (a scoop into one, a slide between two): given to its neighbour, or dropped
    const size_t shortest = (size_t) std::max (2.0, 0.07 / frameSec);
    std::vector<Run> kept;
    for (auto& r : runs)
    {
        if (r.b - r.a >= shortest) { kept.push_back (r); continue; }
        if (! kept.empty() && r.a - kept.back().b <= bridge) kept.back().b = r.b; // (a slide out of the last note)
    }
    // a new syllable on the same note: the level dips, then jumps back up
    std::vector<Run> notes;
    for (auto& r : kept)
    {
        size_t start = r.a;
        float lowSince = level[r.a];
        for (size_t f = r.a + 1; f < r.b; ++f)
        {
            lowSince = std::min (lowSince, level[f]);
            if (f - start >= shortest && f + 2 < r.b && level[f] > lowSince * 2.2f && level[f] > loudest * 0.08f
                && level[f - 1] < level[f] * 0.75f)
            {
                notes.push_back ({ start, f, r.pitch });
                start = f; lowSince = level[f];
            }
        }
        notes.push_back ({ start, r.b, r.pitch });
    }
    // onto the 16th-note grid, in beats; voices sit around the singer's own octave
    const double beatsPerSec = o.bpm / 60.0;
    auto q = [] (double beat) { return std::round (beat * 4.0) / 4.0; };
    for (auto& r : notes)
    {
        const double b0 = q (r.a * frameSec * beatsPerSec), b1 = std::max (b0 + 0.25, q ((r.b * frameSec + 0.05) * beatsPerSec)); // (held through the breath after it)
        float pk = 0.0f;
        for (size_t f = r.a; f < r.b; ++f) pk = std::max (pk, level[f]);
        if (! out.empty() && out.back().beat >= b0 - 1e-6)
        {
            // two notes landed on the same 16th: the longer one stays
            if (out.back().length >= b1 - b0) continue;
            out.pop_back();
        }
        if (! out.empty()) out.back().length = std::min (out.back().length, b0 - out.back().beat);
        Note n;
        n.beat = b0;
        n.length = b1 - b0;
        n.pitch = juce::jlimit (o.lowest, o.highest, r.pitch);
        n.velocity = juce::jlimit (0.35f, 1.0f, 0.4f + 0.6f * std::sqrt (pk / loudest));
        out.push_back (n);
    }
    out.erase (std::remove_if (out.begin(), out.end(), [] (const Note& n) { return n.length < 0.2; }), out.end());
    return out;
}
} // namespace lp::song::transcribe
