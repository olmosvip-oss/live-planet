#pragma once
#include "Song.h"
#include "Chords.h"

// The hook: a short, catchy melody written over the song's own chords, the same in every chorus
// (that's what makes it stick). Built like a songwriter would: a 2-bar motif, said again, said
// again with a twist, then an answer that comes home. Strong beats land on the chord, the notes
// in between walk through the key. It's written here, note by note, so it's yours.
namespace lp::song::hook
{

// where a hook plays: the choruses (and drops); no chorus: the loudest sections. `where` says it in words.
inline std::vector<const Section*> hookSections (const Song& song, juce::String& where)
{
    std::vector<const Section*> targets;
    for (auto& s : song.sections)
    {
        const juce::String n (s.name);
        if (n.startsWithIgnoreCase ("chorus") || n.startsWithIgnoreCase ("drop") || n.startsWithIgnoreCase ("hook")) targets.push_back (&s);
    }
    if (targets.empty())
    {
        float loud = 0;
        for (auto& s : song.sections) loud = std::max (loud, s.energy);
        for (auto& s : song.sections) if (s.energy >= loud - 0.05f) targets.push_back (&s);
    }
    juce::StringArray names;
    for (auto* s : targets) names.addIfNotAlreadyThere (juce::String (s->name).trimCharactersAtEnd ("0123456789 ") + "es");
    where = names.joinIntoString (", ");
    return targets;
}

// Vocal chops the way producers play them on pads: a one-bar rhythm said again and again, on two or three
// notes of each chord (so it moves with the song but stays easy to hum), each spot in the rhythm always the
// same syllable, and every fourth bar an answer that lands home. Each note says which chop (pad) it plays.
inline std::vector<Note> writeChops (const Song& song, const std::vector<chords::Chord>& chordList, const std::vector<ChopSlice>& slices,
                                     juce::uint32 seed, juce::String& where)
{
    juce::Random rng ((juce::int64) seed * 2654435761u + 91);
    std::vector<int> sung;
    for (int i = 0; i < (int) slices.size(); ++i) if (slices[(size_t) i].midi > 0.0) sung.push_back (i);
    if (sung.empty()) for (int i = 0; i < (int) slices.size(); ++i) sung.push_back (i);
    if (sung.empty()) return {};

    // one bar: when each chop hits (beats) and which of the chord's notes it sings (0 low .. 3 high)
    struct Motif { std::vector<double> t; std::vector<int> tone; };
    static const std::vector<Motif> motifs = {
        { { 0.0, 0.75, 1.5, 2.5, 3.0 },             { 2, 2, 1, 2, 0 } },
        { { 0.0, 0.5, 1.5, 2.0, 2.75, 3.5 },        { 1, 2, 2, 1, 1, 0 } },
        { { 0.5, 1.0, 1.75, 2.5, 3.5 },             { 2, 3, 2, 1, 1 } },
        { { 0.0, 0.75, 1.5, 2.0, 3.0 },             { 0, 0, 2, 1, 2 } },
        { { 0.0, 1.5, 2.0, 2.5, 3.5 },              { 2, 1, 2, 3, 2 } },
        { { 0.0, 0.25, 0.75, 1.5, 2.5, 3.25 },      { 1, 1, 2, 2, 1, 0 } },
        { { 0.75, 1.5, 2.25, 3.0 },                 { 2, 1, 1, 0 } },
        { { 0.0, 0.75, 1.0, 1.5, 2.25, 3.0 },       { 2, 2, 1, 2, 1, 1 } },
    };
    const auto& m = motifs[(size_t) rng.nextInt ((int) motifs.size())];
    // the answer bar: the same start, then down to the key's home note, held
    const int keep = std::max (2, (int) m.t.size() - 2);

    // which chop sings each spot of the rhythm: two or three of the clearest, always in the same places
    const int voices = std::min ((int) sung.size(), 2 + rng.nextInt (2));
    std::vector<int> pick;
    {
        auto pool = sung;
        for (int i = (int) pool.size() - 1; i > 0; --i) std::swap (pool[(size_t) i], pool[(size_t) rng.nextInt (i + 1)]);
        pick.assign (pool.begin(), pool.begin() + voices);
    }
    std::vector<int> spotChop;
    for (size_t i = 0; i < m.t.size(); ++i) spotChop.push_back (pick[(size_t) ((i * 7 + (size_t) m.tone[i]) % (size_t) voices)]);

    const int home = 60 + song.key % 12 - (song.key % 12 > 5 ? 12 : 0) + 3; // the chops sit around E4..A4
    auto chordAt = [&] (double beat) -> chords::Chord
    {
        for (auto& c : chordList) if (c.valid && beat >= c.beat && beat < c.beat + c.length) return c;
        chords::Chord h; h.valid = true; h.root = song.key; h.minor = song.minor; return h;
    };
    // the chord's notes from just under the home note up: tone 0..3
    auto toneOf = [&] (const chords::Chord& c, int idx)
    {
        std::vector<int> pcs = chords::tones (c);
        std::vector<int> ladder;
        for (int pch = home - 4; pch <= home + 14 && ladder.size() < 6; ++pch)
            for (int t : pcs) if (((pch % 12) + 12) % 12 == t) { ladder.push_back (pch); break; }
        if (ladder.empty()) return home;
        return ladder[(size_t) juce::jlimit (0, (int) ladder.size() - 1, idx)];
    };

    std::vector<Note> out;
    for (auto* sec : hookSections (song, where))
    {
        const double end = (sec->startBar + sec->bars) * 4.0;
        for (int b = 0; b < sec->bars; ++b)
        {
            const double t0 = (sec->startBar + b) * 4.0;
            const bool answer = b % 4 == 3 || b == sec->bars - 1;
            const size_t count = answer ? (size_t) keep : m.t.size();
            for (size_t i = 0; i < count; ++i)
            {
                const double t = t0 + m.t[i];
                if (t >= end) break;
                const double nextT = i + 1 < count ? t0 + m.t[i + 1] : t0 + 4.0 + (answer ? 0.0 : m.t[0]);
                const auto c = chordAt (t + 0.01);
                int pitch = toneOf (c, m.tone[i]);
                double len = std::min (nextT - t, 0.5);
                if (answer && i + 1 == count)
                {
                    // home: the key's own note nearest where the line is
                    int h = home - 4;
                    while (((h - song.key) % 12 + 12) % 12 != 0) ++h;
                    pitch = h;
                    len = std::min (1.5, end - t);
                }
                const bool down = std::abs (m.t[i] - std::round (m.t[i])) < 0.01;
                out.push_back ({ t, std::max (0.15, len * 0.92), pitch, down ? 0.9f : 0.74f, false, spotChop[i] });
            }
        }
    }
    return out;
}

inline std::vector<Note> write (const Song& song, const std::vector<chords::Chord>& chordList, juce::uint32 seed, bool chops,
                                juce::String& where)
{
    juce::Random rng ((juce::int64) seed * 2654435761u + 17);
    const int majS[7] = { 0, 2, 4, 5, 7, 9, 11 }, minS[7] = { 0, 2, 3, 5, 7, 8, 10 };
    const int* scale = song.minor ? minS : majS;
    // the key's home note around middle C (F#3..F4), then the hook sits a little above it
    const int home = 60 + song.key % 12 - (song.key % 12 > 5 ? 12 : 0) + (chops ? 0 : 12 * 0);
    auto midiOf = [&] (int deg)
    {
        const int oct = (int) std::floor (deg / 7.0);
        return home + scale[((deg % 7) + 7) % 7] + 12 * oct;
    };
    auto chordAt = [&] (double beat) -> chords::Chord
    {
        for (auto& c : chordList) if (c.valid && beat >= c.beat && beat < c.beat + c.length) return c;
        chords::Chord home; home.valid = true; home.root = song.key; home.minor = song.minor; return home;
    };
    auto isChordTone = [&] (int deg, const chords::Chord& c)
    {
        const int pc = ((midiOf (deg) % 12) + 12) % 12;
        for (int t : chords::tones (c)) if (t == pc) return true;
        return false;
    };
    auto nearestChordTone = [&] (int deg, const chords::Chord& c)
    {
        for (int d = 0; d <= 3; ++d)
        {
            if (isChordTone (deg + d, c)) return deg + d;
            if (isChordTone (deg - d, c)) return deg - d;
        }
        return deg;
    };

    // ---- the rhythm of the 2-bar motif (beats from its start)
    static const std::vector<std::vector<double>> rhythms = {
        { 0.0, 0.75, 1.5, 2.0, 3.0, 4.0, 4.75, 5.5, 6.5 },
        { 0.5, 1.0, 1.5, 2.5, 3.5, 4.5, 5.0, 5.5, 6.0 },
        { 0.0, 1.0, 1.75, 2.5, 4.0, 5.0, 5.75, 6.5, 7.0 },
        { 0.0, 0.5, 1.5, 2.0, 2.75, 4.0, 4.5, 5.5, 6.0, 6.75 },
        { 0.75, 1.5, 2.25, 3.0, 4.75, 5.5, 6.25, 7.0 },
    };
    static const std::vector<std::vector<double>> chopRhythms = { // choppier: more 16ths and repeats
        { 0.0, 0.5, 0.75, 1.5, 2.0, 2.75, 3.5, 4.0, 4.5, 4.75, 5.5, 6.5 },
        { 0.0, 0.75, 1.5, 2.25, 3.0, 3.5, 4.0, 4.75, 5.5, 6.25, 7.0 },
        { 0.5, 0.75, 1.5, 2.5, 2.75, 3.5, 4.5, 4.75, 5.5, 6.5, 6.75 },
    };
    const auto& pool = chops ? chopRhythms : rhythms;
    const auto rhythm = pool[(size_t) rng.nextInt ((int) pool.size())];

    // ---- its shape: steps through the key (degrees), rising then falling, with one leap for character
    std::vector<int> steps;
    {
        int leapAt = 1 + rng.nextInt ((int) rhythm.size() - 2);
        for (size_t i = 0; i < rhythm.size(); ++i)
        {
            const bool firstHalf = i < rhythm.size() / 2;
            int st = (rng.nextFloat() < 0.65f ? 1 : 2) * (firstHalf ? 1 : -1);
            if (rng.nextFloat() < 0.25f) st = 0;                           // a repeated note (very hook-like)
            if ((int) i == leapAt) st = firstHalf ? 3 + rng.nextInt (2) : -3;
            steps.push_back (i == 0 ? 0 : st);
        }
    }

    // ---- where it plays: the choruses (and drops); no chorus: the loudest sections
    std::vector<const Section*> targets;
    for (auto& s : song.sections)
    {
        const juce::String n (s.name);
        if (n.startsWithIgnoreCase ("chorus") || n.startsWithIgnoreCase ("drop") || n.startsWithIgnoreCase ("hook")) targets.push_back (&s);
    }
    if (targets.empty())
    {
        float loud = 0;
        for (auto& s : song.sections) loud = std::max (loud, s.energy);
        for (auto& s : song.sections) if (s.energy >= loud - 0.05f) targets.push_back (&s);
    }
    juce::StringArray names;
    for (auto* s : targets) names.addIfNotAlreadyThere (juce::String (s->name).trimCharactersAtEnd ("0123456789 ") + "es");
    where = names.joinIntoString (", ");

    // ---- write it: A, A again, A with a twist, then B (the answer, coming home)
    std::vector<Note> out;
    const int startDeg = 2 + rng.nextInt (3); // start on the 3rd..5th of the key: room to move both ways
    for (auto* sec : targets)
    {
        const int units = std::max (1, sec->bars / 2);
        int prevDeg = startDeg;
        for (int u = 0; u < units; ++u)
        {
            const double t0 = (sec->startBar + u * 2) * 4.0;
            const int role = u % 4;                     // 0 A, 1 A, 2 A', 3 B
            const bool answer = role == 3 || u == units - 1;
            int deg = startDeg;
            for (size_t i = 0; i < rhythm.size(); ++i)
            {
                const double t = t0 + rhythm[i];
                if (t >= (sec->startBar + sec->bars) * 4.0) break;
                deg += steps[i];
                if (role == 2 && i >= rhythm.size() - 3) deg += 1;   // the twist: the end lifts
                const auto c = chordAt (t + 0.01);
                const bool strong = std::abs (t - std::round (t)) < 0.01;
                if (strong) deg = nearestChordTone (deg, c);
                // the answer comes home: its last note is the key's home note
                const bool last = i == rhythm.size() - 1 || (i + 1 < rhythm.size() && t0 + rhythm[i + 1] >= (sec->startBar + sec->bars) * 4.0);
                if (answer && last) deg = (int) std::round (deg / 7.0) * 7;
                deg = juce::jlimit (-3, 10, deg);                   // a singable range
                const double next = i + 1 < rhythm.size() ? t0 + rhythm[i + 1] : t0 + 8.0;
                double len = std::min (next - t, chops ? 0.5 : 1.0);
                if (answer && last) len = std::min (2.0, (sec->startBar + sec->bars) * 4.0 - t);
                out.push_back ({ t, std::max (0.2, len * 0.95), midiOf (deg), strong ? 0.85f : 0.68f });
                prevDeg = deg;
            }
            juce::ignoreUnused (prevDeg);
        }
    }
    return out;
}

} // namespace lp::song::hook
