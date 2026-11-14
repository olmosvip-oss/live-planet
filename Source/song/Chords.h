#pragma once
#include "Song.h"

// The song's chords, read from what its tracks actually play, and written out as MIDI
// (a chords track and a bass track) so any sample can be swapped for your own synth.
namespace lp::song::chords
{

struct Chord
{
    bool valid = false;
    int root = 0;        // pitch class
    bool minor = false;
    bool dim = false;
    bool seventh = false;
    int sevenInterval = 10; // 10 = a dominant / minor 7th, 11 = a major 7th
    int bass = 0;        // pitch class of the lowest note (the root, or an inversion)
    double beat = 0.0, length = 4.0;
};

inline juce::String name (const Chord& c)
{
    if (! c.valid) return "-";
    juce::String s = lp::noteName (c.root);
    s << (c.dim ? "dim" : c.minor ? "m" : "") << (c.seventh ? (! c.minor && ! c.dim && c.sevenInterval == 11 ? "maj7" : "7") : "");
    if (c.bass != c.root) s << "/" << lp::noteName (c.bass);
    return s;
}

// the chord tones (pitch classes) of a chord
inline std::vector<int> tones (const Chord& c)
{
    std::vector<int> t { c.root, (c.root + (c.minor || c.dim ? 3 : 4)) % 12, (c.root + (c.dim ? 6 : 7)) % 12 };
    if (c.seventh) t.push_back ((c.root + c.sevenInterval) % 12);
    return t;
}

// `music`: pitch-class energy of the harmony per slot; `bass`: of the bass line (may be empty).
// Picks, for every slot, the chord of the key that fits best (holding the last one where it's unclear).
inline std::vector<Chord> read (const std::vector<std::array<float, 12>>& music, const std::vector<std::array<float, 12>>& bass,
                                int key, bool minor, double slotBeats)
{
    // the chords that belong to the key (and the minor key's major V)
    struct Cand { int root; bool min, dim; float prior; };
    std::vector<Cand> cands;
    const int majSteps[7] = { 0, 2, 4, 5, 7, 9, 11 }, minSteps[7] = { 0, 2, 3, 5, 7, 8, 10 };
    const char* majQ = "MmmMMmd";  // I ii iii IV V vi vii°
    const char* minQ = "mdMmmMM";  // i ii° III iv v VI VII
    const float majPrior[7] = { 1.0f, 0.8f, 0.6f, 0.95f, 0.95f, 0.9f, 0.4f };
    const float minPrior[7] = { 1.0f, 0.4f, 0.85f, 0.9f, 0.75f, 0.9f, 0.85f };
    for (int d = 0; d < 7; ++d)
    {
        const char q = minor ? minQ[d] : majQ[d];
        cands.push_back ({ (key + (minor ? minSteps[d] : majSteps[d])) % 12, q == 'm', q == 'd', minor ? minPrior[d] : majPrior[d] });
    }
    if (minor) cands.push_back ({ (key + 7) % 12, false, false, 0.6f }); // V (harmonic minor)

    std::vector<Chord> out (music.size());
    Chord last;
    float loudest = 0.0f;
    for (auto& m : music) { float t = 0; for (float v : m) t += v; loudest = std::max (loudest, t); }
    for (size_t i = 0; i < music.size(); ++i)
    {
        auto m = music[i];
        float total = 0;
        for (float v : m) total += v;
        Chord c;
        c.beat = i * slotBeats;
        c.length = slotBeats;
        if (total < loudest * 0.04f || total <= 0.0f) { out[i] = c; last = {}; continue; } // nothing harmonic playing
        for (auto& v : m) v /= total;
        // the bass helps: its note is very likely the root
        std::array<float, 12> b {};
        if (i < bass.size())
        {
            float bt = 0;
            for (float v : bass[i]) bt += v;
            if (bt > 0) for (int k = 0; k < 12; ++k) b[(size_t) k] = bass[i][(size_t) k] / bt;
        }
        float best = -1e9f, second = -1e9f;
        const Cand* pick = nullptr;
        for (auto& cd : cands)
        {
            const int third = (cd.root + (cd.min || cd.dim ? 3 : 4)) % 12, fifth = (cd.root + (cd.dim ? 6 : 7)) % 12;
            float in = m[(size_t) cd.root] * 1.0f + m[(size_t) third] * 0.9f + m[(size_t) fifth] * 0.7f;
            float outside = 0;
            for (int k = 0; k < 12; ++k) if (k != cd.root && k != third && k != fifth) outside += m[(size_t) k];
            float score = in - 0.45f * outside + 0.6f * b[(size_t) cd.root] + 0.08f * cd.prior;
            if (last.valid && last.root == cd.root && last.minor == cd.min) score += 0.04f; // chords tend to last
            if (score > best) { second = best; best = score; pick = &cd; }
            else if (score > second) second = score;
        }
        if (pick == nullptr) { out[i] = c; continue; }
        c.valid = true; c.root = pick->root; c.minor = pick->min; c.dim = pick->dim;
        // unclear (two chords nearly tie): keep the one before
        if (last.valid && best - second < 0.02f) { c.root = last.root; c.minor = last.minor; c.dim = last.dim; }
        // a 7th when it's clearly there
        // (the key's own 7th: a minor 7th on minor chords and on V, a major 7th on the other major chords)
        c.sevenInterval = (c.minor || c.dim || c.root == (key + 7) % 12) ? 10 : 11;
        if (minor && ! c.minor && ! c.dim && c.root != (key + 7) % 12) c.sevenInterval = ((c.root - key + 12) % 12 == 10) ? 10 : 11; // VII7
        c.seventh = m[(size_t) ((c.root + c.sevenInterval) % 12)] > 0.12f;
        // the bass note: an inversion when the bass sits on a chord tone other than the root
        c.bass = c.root;
        int bestB = -1; float bv = 0.35f;
        for (int k = 0; k < 12; ++k) if (b[(size_t) k] > bv) { bv = b[(size_t) k]; bestB = k; }
        if (bestB >= 0)
            for (int t : tones (c)) if (t == bestB) c.bass = bestB;
        out[i] = c;
        last = c;
    }
    // join neighbours that are the same chord
    std::vector<Chord> joined;
    for (auto& c : out)
    {
        if (! joined.empty() && joined.back().valid == c.valid && (! c.valid || (joined.back().root == c.root && joined.back().minor == c.minor
                                                                                  && joined.back().bass == c.bass && joined.back().seventh == c.seventh)))
            joined.back().length += c.length;
        else
            joined.push_back (c);
    }
    return joined;
}

// a chords track (close voicing around middle C) and a bass track, plus the chord names as markers
// the chords (voiced close, like a player would) and their bass notes, as two MIDI tracks; the chord names
// go on `markers` if given
inline void chordTracks (const std::vector<Chord>& chords, int tpq, juce::MidiMessageSequence& ch, juce::MidiMessageSequence& bs,
                         juce::MidiMessageSequence* markers)
{
    int prevTop = 64;
    for (auto& c : chords)
    {
        if (! c.valid) continue;
        const double t0 = c.beat * tpq, t1 = (c.beat + c.length) * tpq - 1;
        if (markers != nullptr) markers->addEvent (juce::MidiMessage::textMetaEvent (6, name (c)), t0);
        // voice the chord close to the last one (smooth, like a player would)
        std::vector<int> notes;
        for (int pc : tones (c))
        {
            int n = 60 + ((pc - 60) % 12 + 12) % 12;          // C4..B4
            while (n - prevTop > 6) n -= 12;
            while (prevTop - n > 6) n += 12;
            notes.push_back (juce::jlimit (52, 76, n));
        }
        std::sort (notes.begin(), notes.end());
        prevTop = notes[notes.size() / 2];
        for (int n : notes)
        {
            ch.addEvent (juce::MidiMessage::noteOn (1, n, 0.7f), t0);
            ch.addEvent (juce::MidiMessage::noteOff (1, n), t1);
        }
        const int bassNote = 36 + c.bass; // C2..B2
        bs.addEvent (juce::MidiMessage::noteOn (2, bassNote, 0.8f), t0);
        bs.addEvent (juce::MidiMessage::noteOff (2, bassNote), t1);
    }
    for (auto* q : { &ch, &bs }) { q->sort(); q->updateMatchedPairs(); }
}

inline void writeMidi (const std::vector<Chord>& chords, double bpm, const juce::String& title, const juce::File& file)
{
    constexpr int tpq = 960;
    juce::MidiFile mf;
    mf.setTicksPerQuarterNote (tpq);
    juce::MidiMessageSequence conductor, ch, bs;
    conductor.addEvent (juce::MidiMessage::tempoMetaEvent ((int) (60000000.0 / bpm)), 0);
    conductor.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0);
    conductor.addEvent (juce::MidiMessage::textMetaEvent (3, title + " - chords"), 0);
    ch.addEvent (juce::MidiMessage::textMetaEvent (3, "chords"), 0);
    bs.addEvent (juce::MidiMessage::textMetaEvent (3, "bass"), 0);
    chordTracks (chords, tpq, ch, bs, &conductor);
    mf.addTrack (conductor);
    mf.addTrack (ch);
    mf.addTrack (bs);
    file.deleteFile();
    juce::FileOutputStream os (file);
    if (os.openedOk()) mf.writeTo (os, 1);
}

} // namespace lp::song::chords
