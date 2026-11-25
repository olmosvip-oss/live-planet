#pragma once
#include "Song.h"
#include "Chords.h"
#include <juce_core/juce_core.h>

// A melody for your lyrics, written over the song's own chords: each line gets its bars, each word its note (a
// longer word a longer note), the rhythm a rap flow or a sung line, and the notes a shape that repeats from line to
// line (that's what makes it stick), every second line an answer that comes home to the key's note. A sung line's
// last word is held and falls a step. It's written here, note by note, so it fits the song and every word is there.
namespace lp::song::lyricmelody
{
struct Note { double beat = 0, len = 0; int pitch = 0; juce::String word; bool slur = false; // beat: from the section's start
              float vibrato = 0.0f; bool slide = false; int id = 0; };   // (id: which drawn note it is, as it moves)                                   // (drawn notes: sung with vibrato, slid into)
struct Line { std::vector<Note> notes; };

// syllables in a word, from its vowel groups (good enough to give a long word a longer note)
inline int syllables (const juce::String& word)
{
    const auto w = word.toLowerCase().retainCharacters ("abcdefghijklmnopqrstuvwxyz");
    if (w.isEmpty()) return 0;
    int n = 0;
    bool prevVowel = false;
    for (int i = 0; i < w.length(); ++i)
    {
        const bool v = juce::String ("aeiouy").containsChar (w[i]);
        if (v && ! prevVowel) ++n;
        prevVowel = v;
    }
    if (n > 1 && w.endsWith ("e") && ! w.endsWith ("le") && ! w.endsWith ("ee")) --n; // (a silent e)
    // a vowel run into "ing" is two (do-ing, go-ing, be-ing, see-ing)
    if (w.length() > 4 && w.endsWith ("ing") && juce::String ("aeiouy").containsChar (w[w.length() - 4])) ++n;
    return juce::jmax (1, n);
}

// the sung lines of a lyric (tags like [Chorus] and empty lines left out)
inline juce::StringArray sungLines (const juce::String& lyrics)
{
    juce::StringArray lines, out;
    lines.addLines (lyrics);
    for (auto& l : lines)
        if (l.trim().isNotEmpty() && ! l.trim().startsWith ("[")) out.add (l.trim());
    return out;
}

// `chordList`: the song's chords (beats from the song's start); `sectionBeat`: where the section starts in the song
inline std::vector<Line> write (const juce::StringArray& lines, int bars, int key, bool minor, bool rap,
                                const std::vector<chords::Chord>& chordList, double sectionBeat, juce::uint32 seed)
{
    std::vector<Line> out;
    const int L = lines.size();
    if (L == 0 || bars <= 0) return out;
    juce::Random rng ((juce::int64) seed * 2654435761u + 7);
    const int barsPer = juce::jmax (1, bars / L);
    // the voice's middle: the key's note in the octave around E4 (the helper moves it to the voice's own range)
    int home = 60 + key % 12;
    if (home > 66) home -= 12;
    auto chordAt = [&] (double beatInSection) -> chords::Chord
    {
        const double b = sectionBeat + beatInSection;
        for (auto& c : chordList) if (c.valid && b >= c.beat && b < c.beat + c.length) return c;
        chords::Chord h; h.valid = true; h.root = key; h.minor = minor; return h;
    };
    // the chord's notes from just under the middle up: rung 0..4
    auto rung = [&] (const chords::Chord& c, int idx)
    {
        const auto pcs = chords::tones (c);
        std::vector<int> ladder;
        for (int p = home - 5; p <= home + 12 && ladder.size() < 6; ++p)
            for (int t : pcs) if (((p % 12) + 12) % 12 == t) { ladder.push_back (p); break; }
        return ladder.empty() ? home : ladder[(size_t) juce::jlimit (0, (int) ladder.size() - 1, idx)];
    };
    auto tonicNear = [&] (int p)
    {
        int best = p, d = 99;
        for (int q = p - 6; q <= p + 6; ++q)
            if (((q - key) % 12 + 12) % 12 == 0 && std::abs (q - p) < d) { d = std::abs (q - p); best = q; }
        return best;
    };

    // the shape the lines share (rung per word, going round): rap stays close, sung lines arch
    static const std::vector<std::vector<int>> rapShapes = { { 1, 1, 2, 1, 1, 0, 1, 2 }, { 2, 1, 1, 2, 1, 1, 0, 1 }, { 1, 2, 2, 1, 0, 1, 1, 2 }, { 1, 1, 1, 2, 2, 1, 1, 0 } };
    static const std::vector<std::vector<int>> sungShapes = { { 1, 2, 3, 3, 2, 1, 2, 1 }, { 2, 3, 4, 3, 2, 2, 1, 0 }, { 0, 1, 2, 3, 2, 3, 2, 1 }, { 2, 2, 3, 2, 1, 2, 1, 0 } };
    const auto& pool = rap ? rapShapes : sungShapes;
    const auto shapeA = pool[(size_t) rng.nextInt ((int) pool.size())];
    const auto shapeB = pool[(size_t) rng.nextInt ((int) pool.size())];
    const bool pickup = rng.nextFloat() < (rap ? 0.5f : 0.35f); // (start just before the bar, on the "and")

    for (int li = 0; li < L; ++li)
    {
        juce::StringArray words;
        words.addTokens (lines[li], " \t", "");
        words.removeEmptyStrings();
        std::vector<std::pair<juce::String, int>> w;
        int syl = 0;
        for (auto& x : words) { const int s = syllables (x); if (s == 0) continue; w.push_back ({ x, s }); syl += s; }
        Line line;
        if (w.empty()) { out.push_back (line); continue; }
        const double lineStart = li * barsPer * 4.0;
        const double room = barsPer * 4.0 - (rap ? 0.5 : 1.0);
        // the pace: the longest note per syllable that fits (rap: 8ths, else 16ths; sung: quarters, 8ths, 16ths)
        double slot = room / syl;
        for (double c : rap ? std::vector<double> { 1.0, 0.75, 0.5, 0.25 } : std::vector<double> { 1.0, 0.5, 0.25 })
            if (syl * c <= room * 0.92) { slot = c; break; }
        double t = lineStart + (pickup && li > 0 ? -0.5 : 0.0);
        const bool answer = li % 2 == 1;
        const auto& shape = (li / 2) % 2 == 0 ? shapeA : shapeB; // (lines 1-2 and 5-6 share a shape, 3-4 another)
        for (size_t k = 0; k < w.size(); ++k)
        {
            const bool last = k + 1 == w.size();
            double len = w[k].second * slot;
            if (last)
            {
                // the line's last word: held (sung) to just before the next line, or a little (rap)
                const double end = lineStart + room;
                len = rap ? juce::jmin (juce::jmax (len, 0.5), juce::jmax (len, end - t)) : juce::jmax (len, juce::jmin (4.0, end - t));
            }
            const auto c = chordAt (t + 0.01);
            int p = rung (c, shape[k % shape.size()]);
            if (last) p = answer ? tonicNear (p) : rung (c, rap ? 2 : 3); // (the answer comes home; the call stays open)
            if (! rap && last && len >= 1.5)
            {
                // a held note falls a step at the end (a sung line's sigh)
                const double first = len * 0.65;
                line.notes.push_back ({ t, first, p, w[k].first, false });
                int down = p - 1;
                while (down > p - 3 && ! (minor ? juce::Array<int> { 0, 2, 3, 5, 7, 8, 10 } : juce::Array<int> { 0, 2, 4, 5, 7, 9, 11 }).contains (((down - key) % 12 + 12) % 12)) --down;
                line.notes.push_back ({ t + first, len - first, down, w[k].first, true });
            }
            else
                line.notes.push_back ({ t, len, p, w[k].first, false });
            t += len;
        }
        out.push_back (line);
    }
    return out;
}

// the melody as the voice helper reads it: one segment per line, times in ms from the section's start
inline juce::var toScore (const std::vector<Line>& lines, double bpm)
{
    const double spb = 60.0 / bpm;
    juce::Array<juce::var> segs;
    for (auto& line : lines)
    {
        if (line.notes.empty()) continue;
        const double start = juce::jmax (0.0, line.notes.front().beat);
        juce::Array<juce::var> notes;
        double t = start;
        for (auto& n : line.notes)
        {
            const double nb = juce::jmax (0.0, n.beat);
            if (nb > t + 0.02 && nb <= t + 0.15 && notes.size() > 0)
                if (auto* prev = notes.getReference (notes.size() - 1).getDynamicObject())
                    prev->setProperty ("dur", (double) prev->getProperty ("dur") + (nb - t) * spb);
            if (nb > t + 0.15) // (a breath; shorter gaps the voice just carries through)
            {
                auto* rest = new juce::DynamicObject();
                rest->setProperty ("word", "<SP>"); rest->setProperty ("pitch", 0); rest->setProperty ("dur", (nb - t) * spb);
                notes.add (juce::var (rest));
            }
            auto* o = new juce::DynamicObject();
            o->setProperty ("word", n.word); o->setProperty ("pitch", n.pitch);
            o->setProperty ("dur", juce::jmax (0.05, n.len) * spb); o->setProperty ("slur", n.slur);
            notes.add (juce::var (o));
            t = nb + n.len;
        }
        auto* s = new juce::DynamicObject();
        s->setProperty ("start", juce::roundToInt (start * spb * 1000.0));
        s->setProperty ("notes", notes);
        segs.add (juce::var (s));
    }
    auto* root = new juce::DynamicObject();
    root->setProperty ("segments", segs);
    return juce::var (root);
}
} // namespace lp::song::lyricmelody
