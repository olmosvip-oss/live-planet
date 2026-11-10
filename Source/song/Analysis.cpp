#include "Song.h"
#include <complex>
#include <numeric>

namespace lp::song
{
namespace
{
    using cpx = std::complex<float>;

    void fft (std::vector<cpx>& a)
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
            const cpx wl (std::cos (ang), std::sin (ang));
            for (size_t i = 0; i < n; i += len)
            {
                cpx w (1.0f, 0.0f);
                for (size_t j = 0; j < len / 2; ++j)
                {
                    const cpx u = a[i + j], v = a[i + j + len / 2] * w;
                    a[i + j] = u + v;
                    a[i + j + len / 2] = u - v;
                    w *= wl;
                }
            }
        }
    }

    // Krumhansl-Schmuckler key profiles
    const float kMajor[12] = { 6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f };
    const float kMinor[12] = { 6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f };

    float corr (const float* a, const float* b, int rot)
    {
        float ma = 0, mb = 0;
        for (int i = 0; i < 12; ++i) { ma += a[i]; mb += b[i]; }
        ma /= 12; mb /= 12;
        float num = 0, da = 0, db = 0;
        for (int i = 0; i < 12; ++i)
        {
            const float x = a[(i + rot) % 12] - ma, y = b[i] - mb;
            num += x * y; da += x * x; db += y * y;
        }
        return num / std::sqrt (std::max (1e-9f, da * db));
    }
}

bool detectKey (const float* input, int numSamples, double sampleRate, int& key, bool& minor)
{
    constexpr int N = 8192;
    const int step = std::max (1, (int) std::round (sampleRate / 22050.0));
    const double sr = sampleRate / step;
    std::vector<float> x ((size_t) (numSamples / step));
    for (size_t i = 0; i < x.size(); ++i) x[i] = input[i * (size_t) step];
    if ((int) x.size() < N) return false;
    std::vector<float> win (N);
    for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) (N - 1));
    float total[12] {};
    double bass[12] {};
    std::vector<cpx> b (N);
    for (int start = 0; start + N <= (int) x.size(); start += N / 2)
    {
        for (int i = 0; i < N; ++i) b[(size_t) i] = cpx (x[(size_t) (start + i)] * win[(size_t) i], 0.0f);
        fft (b);
        for (int k = 1; k < N / 2; ++k)
        {
            const double f = k * sr / N;
            if (f < 90.0 || f > 2500.0) continue;
            const int pc = (int) ((std::lround (12.0 * std::log2 (f / 440.0)) + 69 + 1200) % 12);
            const float m = std::abs (b[(size_t) k]);
            total[pc] += m * m;
            if (f < 260.0) bass[pc] += m * m;
        }
    }
    float best = -2, second = -2;
    for (int k = 0; k < 12; ++k)
        for (int q = 0; q < 2; ++q)
        {
            const float c = corr (total, q ? kMinor : kMajor, k);
            if (c > best) { second = best; best = c; key = k; minor = q == 1; }
            else if (c > second) second = c;
        }
    const int rel = minor ? (key + 3) % 12 : (key + 9) % 12;
    if (bass[rel] > bass[key] * 1.6) { key = rel; minor = ! minor; }
    return best > 0.55f; // a clear enough key
}

Profile analyse (const float* input, int numSamples, double sampleRate)
{
    Profile p;
    if (numSamples < (int) (sampleRate * 8.0)) return p;

    // 1. down to ~22 kHz mono
    const int step = std::max (1, (int) std::round (sampleRate / 22050.0));
    const double sr = sampleRate / step;
    std::vector<float> x ((size_t) (numSamples / step));
    for (size_t i = 0; i < x.size(); ++i)
    {
        float s = 0;
        for (int k = 0; k < step; ++k) s += input[i * (size_t) step + (size_t) k];
        x[i] = s / (float) step;
    }
    p.lengthSeconds = (double) x.size() / sr;

    // 2. spectra: onset strength, low-band onsets (kick), chroma, loudness, brightness
    constexpr int N = 2048, hop = 512;
    const int frames = (int) ((x.size() - N) / hop);
    const double fps = sr / hop;
    std::vector<float> window (N);
    for (int i = 0; i < N; ++i) window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) (N - 1));
    std::vector<float> onset ((size_t) frames, 0.0f), lowOnset ((size_t) frames, 0.0f), midOnset ((size_t) frames, 0.0f),
                       highOnset ((size_t) frames, 0.0f), rms ((size_t) frames), bright ((size_t) frames);
    std::vector<std::array<float, 12>> chroma ((size_t) frames);
    std::vector<float> prevMag (N / 2, 0.0f), mag (N / 2);
    std::vector<cpx> buf (N);
    std::vector<int> pcOfBin (N / 2, -1);
    for (int b = 1; b < N / 2; ++b)
    {
        const double f = b * sr / N;
        if (f > 70.0 && f < 2200.0)
            pcOfBin[(size_t) b] = (int) ((std::lround (12.0 * std::log2 (f / 440.0)) + 69 + 1200) % 12);
    }
    for (int fr = 0; fr < frames; ++fr)
    {
        double e = 0;
        for (int i = 0; i < N; ++i)
        {
            const float v = x[(size_t) (fr * hop + i)];
            e += v * v;
            buf[(size_t) i] = cpx (v * window[(size_t) i], 0.0f);
        }
        rms[(size_t) fr] = (float) std::sqrt (e / N);
        fft (buf);
        float flux = 0, low = 0, midF = 0, highF = 0, num = 0, den = 0;
        for (int b = 1; b < N / 2; ++b)
        {
            mag[(size_t) b] = std::log1p (std::abs (buf[(size_t) b]) * 10.0f);
            const float d = mag[(size_t) b] - prevMag[(size_t) b];
            if (d > 0)
            {
                flux += d;
                const double fb = b * sr / N;
                if (fb < 150.0) low += d;
                else if (fb > 180.0 && fb < 2500.0) midF += d;
                else if (fb > 5000.0) highF += d;
            }
            const float m = std::abs (buf[(size_t) b]);
            num += m * (float) (b * sr / N);
            den += m;
        }
        onset[(size_t) fr] = flux;
        midOnset[(size_t) fr] = midF;
        highOnset[(size_t) fr] = highF;
        lowOnset[(size_t) fr] = low;
        bright[(size_t) fr] = den > 0 ? num / den : 0.0f;
        std::swap (prevMag, mag);
    }

    // a finer look for pitch (key and chords): long windows resolve bass notes properly
    std::array<double, 12> bassChroma {};
    {
        constexpr int N2 = 8192, hop2 = 2048;
        std::vector<float> win2 (N2);
        for (int i = 0; i < N2; ++i) win2[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) (N2 - 1));
        std::vector<int> pc2 (N2 / 2, -1);
        for (int b = 1; b < N2 / 2; ++b)
        {
            const double f = b * sr / N2;
            if (f > 90.0 && f < 2500.0)
                pc2[(size_t) b] = (int) ((std::lround (12.0 * std::log2 (f / 440.0)) + 69 + 1200) % 12);
        }
        std::vector<cpx> b2 (N2);
        for (int start = 0; start + N2 < (int) x.size(); start += hop2)
        {
            for (int i = 0; i < N2; ++i) b2[(size_t) i] = cpx (x[(size_t) (start + i)] * win2[(size_t) i], 0.0f);
            fft (b2);
            std::array<float, 12> c {};
            for (int b = 1; b < N2 / 2; ++b)
                if (pc2[(size_t) b] >= 0)
                {
                    const float m = std::abs (b2[(size_t) b]);
                    c[(size_t) pc2[(size_t) b]] += m * m;
                    if (b * sr / N2 < 260.0) bassChroma[(size_t) pc2[(size_t) b]] += m * m;
                }
            // spread this window's chroma over the onset frames it covers
            const int f0 = (start + N2 / 2 - hop2 / 2) / hop, f1 = std::min (frames, (start + N2 / 2 + hop2 / 2) / hop);
            for (int f = std::max (0, f0); f < f1; ++f) chroma[(size_t) f] = c;
        }
    }

    // onset envelope: remove the slow trend so only the hits remain
    std::vector<float> env ((size_t) frames, 0.0f);
    const int w = (int) (fps * 0.5);
    double acc = 0;
    for (int i = 0; i < frames; ++i)
    {
        acc += onset[(size_t) i];
        if (i >= w) acc -= onset[(size_t) (i - w)];
        const float mean = (float) (acc / std::min (i + 1, w));
        env[(size_t) i] = std::max (0.0f, onset[(size_t) i] - mean);
    }

    // 3. tempo: autocorrelation of the hits, then a careful look at the related tempos
    //    (double, half, and the triplet relations that rolling hi-hats can fool you with)
    auto score = [&] (double bpm)
    {
        const double lag = fps * 60.0 / bpm;
        double sc = 0;
        const int l1 = (int) std::round (lag), l2 = (int) std::round (lag * 2), l4 = (int) std::round (lag * 4);
        for (int i = 0; i + l4 + 1 < frames; ++i)
            sc += env[(size_t) i] * (env[(size_t) (i + l1)] + 0.5f * env[(size_t) (i + l2)] + 0.25f * env[(size_t) (i + l4)]);
        return sc;
    };
    double bestBpm = 100.0, bestScore = -1;
    for (double bpm = 60.0; bpm <= 180.0; bpm += 0.5)
    {
        const double sc = score (bpm) * (0.4 + std::exp (-0.5 * std::pow (std::log2 (bpm / 105.0) / 0.55, 2.0)));
        if (sc > bestScore) { bestScore = sc; bestBpm = bpm; }
    }
    {
        // a bar-level check: the real tempo makes the strongest hits (kicks/snares) line up every beat and bar
        auto barScore = [&] (double bpm)
        {
            const double lag = fps * 60.0 / bpm;
            double sc = 0;
            for (int k : { 1, 2, 4 })
            {
                const int l = (int) std::round (lag * k);
                for (int i = 0; i + l < frames; ++i) sc += (double) (lowOnset[(size_t) i] + midOnset[(size_t) i]) * (lowOnset[(size_t) (i + l)] + midOnset[(size_t) (i + l)]) / k;
            }
            return sc;
        };
        double pick = bestBpm, pickScore = -1;
        for (double m : { 1.0, 2.0, 0.5, 1.5, 2.0 / 3.0, 4.0 / 3.0, 0.75 })
        {
            const double cand = bestBpm * m;
            if (cand < 60.0 || cand > 180.0) continue;
            const double prior = std::exp (-0.5 * std::pow (std::log2 (cand / 110.0) / 0.6, 2.0));
            const double sc = barScore (cand) * (0.5 + prior) * (m == 1.0 ? 1.05 : 1.0);
            if (sc > pickScore) { pickScore = sc; pick = cand; }
        }
        bestBpm = pick;
    }
    p.bpm = std::round (bestBpm * 2.0) / 2.0;
    {
        float envMean0 = 0;
        for (auto v : env) envMean0 += v;
        envMean0 /= (float) frames;
        int hits0 = 0;
        for (int i = 1; i + 1 < frames; ++i)
            if (env[(size_t) i] > envMean0 * 2.0f && env[(size_t) i] >= env[(size_t) i - 1] && env[(size_t) i] > env[(size_t) i + 1]) ++hits0;
        const double hitsPerBeat = hits0 / (frames / (fps * 60.0 / p.bpm));
        if (p.bpm < 85.0 && hitsPerBeat > 2.2 && p.bpm * 2.0 <= 170.0) p.bpm *= 2.0; // a "70" with rolling 16ths is really 140
    }
    const double beat = fps * 60.0 / p.bpm;

    // beat phase, then which beat is the downbeat (strongest low end)
    double bestPhase = 0, bestPhaseScore = -1;
    for (double ph = 0; ph < beat; ph += 0.5)
    {
        double s = 0;
        for (double t = ph; t < frames - 1; t += beat) s += env[(size_t) t];
        if (s > bestPhaseScore) { bestPhaseScore = s; bestPhase = ph; }
    }
    int bestDown = 0;
    double bestDownScore = -1;
    for (int d = 0; d < 4; ++d)
    {
        double s = 0;
        for (double t = bestPhase + d * beat; t < frames - 1; t += beat * 4) s += lowOnset[(size_t) t];
        if (s > bestDownScore) { bestDownScore = s; bestDown = d; }
    }
    const double firstBar = bestPhase + bestDown * beat;
    const double barLen = beat * 4.0;
    const int bars = (int) ((frames - firstBar) / barLen);
    if (bars < 8) return p;

    // 4. key: whole-song chroma against the major / minor profiles
    float total[12] {};
    for (auto& c : chroma) for (int i = 0; i < 12; ++i) total[i] += c[(size_t) i];
    float best = -2;
    for (int k = 0; k < 12; ++k)
    {
        const float a = corr (total, kMajor, k), b = corr (total, kMinor, k);
        if (a > best) { best = a; p.key = k; p.minor = false; }
        if (b > best) { best = b; p.key = k; p.minor = true; }
    }
    {
        const int rel = p.minor ? (p.key + 3) % 12 : (p.key + 9) % 12;
        if (bassChroma[(size_t) rel] > bassChroma[(size_t) p.key] * 1.6)
        {
            p.key = rel;
            p.minor = ! p.minor;
        }
    }

    // 5. per bar: chroma, chord, loudness
    struct Bar { std::array<float, 12> chroma {}; float energy = 0; int root = -1; bool minor = false; };
    std::vector<Bar> barInfo ((size_t) bars);
    float maxEnergy = 1e-9f;
    for (int b = 0; b < bars; ++b)
    {
        const int f0 = (int) (firstBar + b * barLen), f1 = std::min (frames, (int) (firstBar + (b + 1) * barLen));
        auto& bi = barInfo[(size_t) b];
        for (int f = f0; f < f1; ++f)
        {
            for (int i = 0; i < 12; ++i) bi.chroma[(size_t) i] += chroma[(size_t) f][(size_t) i];
            bi.energy += rms[(size_t) f];
        }
        bi.energy /= (float) std::max (1, f1 - f0);
        maxEnergy = std::max (maxEnergy, bi.energy);
        float norm = 1e-9f;
        for (auto v : bi.chroma) norm += v * v;
        norm = std::sqrt (norm);
        for (auto& v : bi.chroma) v /= norm;
        float bestChord = 0.5f; // below this the harmony is too unclear to name
        for (int r = 0; r < 12; ++r)
            for (int q = 0; q < 2; ++q)
            {
                const int third = q ? 3 : 4;
                const float s = (bi.chroma[(size_t) r] + bi.chroma[(size_t) ((r + third) % 12)] * 0.8f
                                + bi.chroma[(size_t) ((r + 7) % 12)] * 0.9f) / std::sqrt (1.0f + 0.64f + 0.81f);
                if (s > bestChord) { bestChord = s; bi.root = r; bi.minor = q == 1; }
            }
    }

    // the groove: average hit strength on each 16th, per band
    {
        std::array<double, 16> lo {}, mi {}, hi {};
        auto peakNear = [&] (const std::vector<float>& e, double f)
        {
            float m = 0;
            for (int d = -1; d <= 1; ++d)
            {
                const int i = (int) std::round (f) + d;
                if (i >= 0 && i < frames) m = std::max (m, e[(size_t) i]);
            }
            return m;
        };
        for (int b = 0; b < bars; ++b)
            for (int st = 0; st < 16; ++st)
            {
                const double f = firstBar + b * barLen + st * barLen / 16.0;
                lo[(size_t) st] += peakNear (lowOnset, f);
                mi[(size_t) st] += peakNear (midOnset, f);
                hi[(size_t) st] += peakNear (highOnset, f);
            }
        auto norm = [] (const std::array<double, 16>& a, std::array<float, 16>& out)
        {
            double mx = 1e-9, mn = 1e9;
            for (auto v : a) { mx = std::max (mx, v); mn = std::min (mn, v); }
            for (size_t i = 0; i < 16; ++i) out[i] = (float) ((a[i] - mn) / std::max (1e-9, mx - mn));
        };
        norm (lo, p.grooveLow);
        norm (mi, p.grooveMid);
        norm (hi, p.grooveHigh);
        p.hasGroove = true;
    }

    // 6. sections: where the music changes (novelty in a bar-by-bar similarity map)
    auto feature = [&] (int b, int i) { return i < 12 ? barInfo[(size_t) b].chroma[(size_t) i] : barInfo[(size_t) b].energy / maxEnergy * 1.5f; };
    auto sim = [&] (int a, int b)
    {
        float d = 0;
        for (int i = 0; i < 13; ++i) { const float v = feature (a, i) - feature (b, i); d += v * v; }
        return std::exp (-d * 2.0f);
    };
    std::vector<float> novelty ((size_t) bars, 0.0f);
    constexpr int K = 4;
    for (int b = K; b + K <= bars; ++b)
    {
        float s = 0;
        for (int i = -K; i < K; ++i)
            for (int j = -K; j < K; ++j)
                s += ((i < 0) == (j < 0) ? 1.0f : -1.0f) * sim (b + i, b + j);
        novelty[(size_t) b] = s;
    }
    std::vector<int> cuts { 0 };
    float nMax = 1e-9f;
    for (auto v : novelty) nMax = std::max (nMax, v);
    for (int b = 4; b < bars - 3; b += 4) // phrases are almost always 4-bar based
    {
        const float v = std::max ({ novelty[(size_t) b], novelty[(size_t) b - 1] * 0.8f, novelty[(size_t) b + 1] * 0.8f });
        if (v > nMax * 0.35f && b - cuts.back() >= 4) cuts.push_back (b);
    }
    // keep sections a sensible size: merge tiny ones, split very long ones
    std::vector<int> fixedCuts;
    for (size_t i = 0; i < cuts.size(); ++i)
    {
        const int start = cuts[i], end = i + 1 < cuts.size() ? cuts[i + 1] : bars;
        if (! fixedCuts.empty() && end - start < 4) continue;
        fixedCuts.push_back (start);
        for (int s = start + 16; end - s >= 8; s += 16) fixedCuts.push_back (s);
    }

    // label: sections that sound alike share a letter; the loudest repeated one is the chorus
    struct Seg { int start, bars; float energy; std::array<float, 13> mean {}; int group = -1; };
    std::vector<Seg> segs;
    for (size_t i = 0; i < fixedCuts.size(); ++i)
    {
        Seg s { fixedCuts[i], (i + 1 < fixedCuts.size() ? fixedCuts[i + 1] : bars) - fixedCuts[i], 0.0f };
        for (int b = s.start; b < s.start + s.bars; ++b)
        {
            s.energy += barInfo[(size_t) b].energy / maxEnergy;
            for (int f = 0; f < 13; ++f) s.mean[(size_t) f] += feature (b, f);
        }
        s.energy /= (float) s.bars;
        for (auto& m : s.mean) m /= (float) s.bars;
        segs.push_back (s);
    }
    int groups = 0;
    for (auto& s : segs)
    {
        for (auto& o : segs)
        {
            if (&o == &s || o.group < 0) continue;
            float d = 0;
            for (int f = 0; f < 13; ++f) d += (s.mean[(size_t) f] - o.mean[(size_t) f]) * (s.mean[(size_t) f] - o.mean[(size_t) f]);
            if (d < 0.05f) { s.group = o.group; break; }
        }
        if (s.group < 0) s.group = groups++;
    }
    int chorusGroup = -1;
    float chorusScore = -1;
    for (int gIdx = 0; gIdx < groups; ++gIdx)
    {
        float e = 0; int n = 0;
        for (auto& s : segs) if (s.group == gIdx) { e += s.energy; ++n; }
        const float score = e / (float) n + (n > 1 ? 0.25f : 0.0f);
        if (score > chorusScore) { chorusScore = score; chorusGroup = gIdx; }
    }
    float meanEnergy = 0;
    for (auto& s : segs) meanEnergy += s.energy;
    meanEnergy /= (float) segs.size();
    for (size_t i = 0; i < segs.size(); ++i)
    {
        auto& s = segs[i];
        RefSection rs;
        rs.startBar = s.start;
        rs.bars = s.bars;
        rs.energy = s.energy;
        if (i == 0 && s.energy < meanEnergy && s.bars <= 8)                      rs.label = "intro";
        else if (i + 1 == segs.size() && s.energy < meanEnergy)                     rs.label = "outro";
        else if (s.group == chorusGroup)                                            rs.label = "chorus";
        else if (i + 1 < segs.size() && segs[i + 1].group == chorusGroup && s.bars <= 4) rs.label = "pre-chorus";
        else if (i > segs.size() / 2 && std::count_if (segs.begin(), segs.end(), [&] (auto& o) { return o.group == s.group; }) == 1)
                                                                                    rs.label = "bridge";
        else                                                                        rs.label = "verse";
        for (int b = s.start; b < s.start + s.bars; ++b)
        {
            rs.chordRoot.push_back (barInfo[(size_t) b].root);
            rs.chordMinor.push_back (barInfo[(size_t) b].minor);
        }
        p.sections.push_back (rs);
    }

    // a short quiet piece right before the outro belongs to the outro
    if (p.sections.size() >= 2)
    {
        auto& last = p.sections.back();
        auto& prev = p.sections[p.sections.size() - 2];
        if (last.label == "outro" && prev.bars <= 4 && prev.label != "chorus")
        {
            prev.label = "outro";
            prev.bars += last.bars;
            prev.energy = (prev.energy + last.energy) * 0.5f;
            prev.chordRoot.insert (prev.chordRoot.end(), last.chordRoot.begin(), last.chordRoot.end());
            prev.chordMinor.insert (prev.chordMinor.end(), last.chordMinor.begin(), last.chordMinor.end());
            p.sections.pop_back();
        }
    }

    // 7. groove and colour
    int hits = 0;
    float envMean = 0;
    for (auto v : env) envMean += v;
    envMean /= (float) frames;
    for (int i = 1; i + 1 < frames; ++i)
        if (env[(size_t) i] > envMean * 2.0f && env[(size_t) i] >= env[(size_t) i - 1] && env[(size_t) i] > env[(size_t) i + 1]) ++hits;
    const double beats = frames / beat;
    p.density = juce::jlimit (0.0f, 1.0f, (float) ((hits / beats - 0.7) / 2.5));
    float br = 0;
    for (auto v : bright) br += v;
    p.brightness = juce::jlimit (0.0f, 1.0f, (br / (float) frames - 800.0f) / 2200.0f);
    p.valid = true;
    return p;
}

} // namespace lp::song
