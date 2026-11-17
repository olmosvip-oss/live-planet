#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include "Song.h"
#include "OneNote.h"

// A vocal cut into chops: where each sung syllable starts (the loudness jumps up), how long it lasts,
// and the note it's sung on. Syllables that hold one clear note come first (they can be moved onto
// any note of a hook and still sound sung); the rest follow, so every part of the vocal gets a pad.
namespace lp::song::chops
{
inline std::vector<ChopSlice> find (const juce::AudioBuffer<float>& src, double sr, int maxChops = 16)
{
    std::vector<ChopSlice> out;
    const int n = src.getNumSamples();
    if (n < (int) (0.2 * sr) || src.getNumChannels() == 0) return out;
    const int ch1 = std::min (1, src.getNumChannels() - 1);
    const int hop = (int) (0.01 * sr);
    std::vector<float> envl;
    for (int i = 0; i + hop <= n; i += hop) envl.push_back (0.5f * (src.getRMSLevel (0, i, hop) + src.getRMSLevel (ch1, i, hop)));
    float peak = 0; for (float v : envl) peak = std::max (peak, v);
    if (peak <= 0.0f) return out;
    std::vector<int> starts;
    for (size_t k = 0; k < envl.size(); ++k)
    {
        const float before = k == 0 ? 0.0f : *std::max_element (envl.begin() + (long) (k >= 5 ? k - 5 : 0), envl.begin() + (long) k);
        if (envl[k] > peak * 0.15f && envl[k] > before * 1.8f && (starts.empty() || (int) k * hop - starts.back() > (int) (0.12 * sr)))
            starts.push_back ((int) k * hop);
    }
    if (starts.size() < 3) { starts.clear(); for (int i = 0; i + (int) (0.3 * sr) < n; i += (int) (0.3 * sr)) starts.push_back (i); }

    struct Heard { int a0, len; onenote::Reading r; float score; };
    std::vector<Heard> heard;
    for (size_t k = 0; k < starts.size() && heard.size() < 64; ++k)
    {
        const int a0 = starts[k];
        // up to the next syllable (at most 0.6 s), with the quiet tail trimmed off
        int a1 = std::min ({ n, k + 1 < starts.size() ? starts[k + 1] : n, a0 + (int) (0.6 * sr) });
        while (a1 - a0 > (int) (0.12 * sr) && envl[(size_t) std::min ((int) envl.size() - 1, (a1 - 1) / hop)] < peak * 0.05f) a1 -= hop;
        if (a1 - a0 < (int) (0.1 * sr)) continue;
        const int look = std::min (a1 - a0, (int) (0.38 * sr));
        juce::AudioBuffer<float> seg (2, look);
        for (int c = 0; c < 2; ++c) seg.copyFrom (c, 0, src, std::min (c, src.getNumChannels() - 1), a0, look);
        const auto r = onenote::read (seg, sr);
        float lvl = 0; for (int i = a0 / hop; i < a1 / hop && i < (int) envl.size(); ++i) lvl = std::max (lvl, envl[(size_t) i]);
        const bool clear = r.midi > 0.0 && r.voiced > 0.5f && r.steady > 0.8f && r.body > 0.12f;
        const bool ok = r.midi > 0.0 && r.voiced > 0.3f && r.steady > 0.65f && r.body > 0.12f;
        const float score = (clear ? 2.0f : ok ? 1.0f : 0.0f) + lvl / peak + (a1 - a0 > (int) (0.17 * sr) ? 0.5f : 0.0f);
        heard.push_back ({ a0, a1 - a0, (ok ? r : onenote::Reading {}), score });
    }
    // the best, in the order they're sung (so pad 1 is the vocal's first good syllable)
    std::vector<size_t> idx (heard.size());
    for (size_t i = 0; i < idx.size(); ++i) idx[i] = i;
    std::stable_sort (idx.begin(), idx.end(), [&] (size_t a, size_t b) { return heard[a].score > heard[b].score; });
    if ((int) idx.size() > maxChops) idx.resize ((size_t) maxChops);
    std::sort (idx.begin(), idx.end());
    for (auto i : idx) out.push_back ({ heard[i].a0 / sr, heard[i].len / sr, heard[i].r.midi });
    return out;
}

// how many of the chops hold a clear note (a hook can be sung with them)
inline int sung (const std::vector<ChopSlice>& c)
{
    int k = 0;
    for (auto& s : c) if (s.midi > 0.0) ++k;
    return k;
}
} // namespace lp::song::chops
