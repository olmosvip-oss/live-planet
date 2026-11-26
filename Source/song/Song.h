#pragma once
#include <cmath>
#include <juce_audio_basics/juce_audio_basics.h>
#include <string>
#include <vector>
#include <map>
#include <array>
#include <set>
#include "../engine/Weather.h"
#include "../engine/Sources.h"

// Song mode: listen to a reference song, then write a brand-new song with a similar shape.
// Only the broad character is borrowed (tempo, key and mood, chord movement, section
// lengths, energy). Melodies, rhythms and sounds are newly made, so nothing is copied.
namespace lp::song
{

// ---------- what we learn from a reference song

struct RefSection
{
    std::string label;          // "intro", "verse", "chorus"...
    int startBar = 0, bars = 8;
    float energy = 0.5f;        // 0..1, relative to the loudest part of the song
    std::vector<int> chordRoot; // per bar: pitch class of the chord root, -1 = unclear
    std::vector<bool> chordMinor;
    // what plays here, from splitting the reference into parts (measured only, never reused)
    std::array<float, 4> stemDb { -100.0f, -100.0f, -100.0f, -100.0f }; // drums, bass, vocals, music
    std::array<bool, 4> present { true, true, true, true };
};

enum Stem { StemDrums = 0, StemBass = 1, StemVocals = 2, StemMusic = 3 };

// The sound of a recording, measured (never copied): balance across 10 octave bands,
// how bright and airy it is, how loud. Used to pick samples that sound like your reference
// and to give the new song's mix the same overall tone.
struct Tone
{
    bool valid = false;
    std::array<float, 10> bandsDb {}; // octaves at 31, 63, 125, 250, 500, 1k, 2k, 4k, 8k, 16k Hz, relative to their average
    float rolloffHz = 8000.0f;        // 85% of the energy is below this
    float airDb = -30.0f;             // energy above 8 kHz vs. all
    float rmsDb = -20.0f;             // loudness
    float crestDb = 12.0f;            // peak vs. loudness: how punchy / how squashed
};
Tone measureTone (const float* mono, int numSamples, double sampleRate);

// How a sample feels: its tone, how held (vs. plucky) and how busy it is
struct Feel
{
    bool valid = false;
    Tone tone;
    float sustain = 0.5f; // 0 short plucks / hits .. 1 long held notes
    float busy = 0.5f;    // hits per second, roughly 0..1 (0 = calm, 1 = very busy)
};
Feel measureFeel (const float* mono, int numSamples, double sampleRate);

// The notes ringing in each bar of a loop (12 pitch classes per bar, A440 = 9), for `bars` equal bars
std::vector<std::array<float, 12>> barChroma (const float* mono, int numSamples, double sampleRate, int bars);

// How well two loops' chords agree, bar by bar (0 = clash .. 1 = the same chords). Each is shifted by its tuning
// and read on the song's bars (lengths in song bars), compared over 8 bars.
inline float chordAgreement (const std::vector<std::array<float, 12>>& a, int tuneA, int barsA,
                             const std::vector<std::array<float, 12>>& b, int tuneB, int barsB)
{
    if (a.empty() || b.empty() || barsA <= 0 || barsB <= 0) return 0.6f;
    double sum = 0; int n = 0;
    for (int j = 0; j < 8; ++j)
    {
        const auto& x = a[(size_t) (((j % barsA) * (int) a.size()) / barsA)];
        const auto& y = b[(size_t) (((j % barsB) * (int) b.size()) / barsB)];
        double mx = 0, my = 0;
        for (int k = 0; k < 12; ++k) { mx += x[(size_t) k]; my += y[(size_t) k]; }
        mx /= 12; my /= 12;
        double num = 0, dx = 0, dy = 0;
        for (int k = 0; k < 12; ++k)
        {
            const double u = x[(size_t) ((k - tuneA + 120) % 12)] - mx, v = y[(size_t) ((k - tuneB + 120) % 12)] - my;
            num += u * v; dx += u * u; dy += v * v;
        }
        if (dx < 1e-12 || dy < 1e-12) continue;
        sum += num / std::sqrt (dx * dy);
        ++n;
    }
    return n > 0 ? (float) ((sum / n + 1.0) * 0.5) : 0.6f;
}

// How alike two sounds are in character (bright / dark, lo-fi / polished), 0..1
inline float toneLikeness (const Tone& a, const Tone& b)
{
    if (! a.valid || ! b.valid) return 0.5f;
    const float roll = std::abs (std::log2 (std::max (200.0f, a.rolloffHz) / std::max (200.0f, b.rolloffHz))); // octaves apart
    const float air = std::abs (a.airDb - b.airDb) / 12.0f;
    return std::exp (-(roll * 1.2f + air));
}

// The feel of a drummer: how late the in-between notes land (swing), how laid back the snare is, which
// hats are hit harder. Measured from a reference (or set by the style), then applied to drawn drums.
// Shifts are in 16ths (0.33 = a 16th note's third late: a strong swing).
struct Groove
{
    bool valid = false;
    float swing16 = 0.0f;   // the "e" and "a" of each beat
    float swing8 = 0.0f;    // the "and" of each beat
    float snareLate = 0.0f; // snares / claps on the backbeat
    float kickLate = 0.0f;
    std::array<float, 16> accent { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 }; // hat loudness per 16th, 0..1
    float humanizeMs = 4.0f; // a little looseness, like a person playing
    float amount = 1.0f;     // how much of it to use (0 = straight, 1 = as heard, 1.5 = more)
};

// where (in beats) and how hard a drum note plays once the groove is applied
inline void applyGroove (const Groove& g, double& beat, float& velocity, int pitch)
{
    if (! g.valid || g.amount <= 0.0f) return;
    const double pos16 = beat * 4.0;
    const double k = std::round (pos16);
    if (std::abs (pos16 - k) > 0.2) return; // played off the grid on purpose (rolls, flams): leave it
    const int st = ((int) k % 16 + 16) % 16;
    double shift = 0.0;
    if (st % 2 == 1) shift += g.swing16;
    else if (st % 4 == 2) shift += g.swing8;
    if ((pitch == 38 || pitch == 39) && st % 4 == 0) shift += g.snareLate;
    if (pitch == 36 && st % 4 == 0) shift += g.kickLate;
    // (the same note always gets the same small looseness, so it doesn't change on every render)
    const auto h = (juce::uint32) (std::hash<long long>{} ((long long) (k * 131) + pitch * 7919));
    beat = (k + shift * g.amount) / 4.0;
    if (pitch == 42 || pitch == 46 || pitch == 70)
        velocity *= 1.0f - g.amount * 0.35f * (1.0f - juce::jlimit (0.0f, 1.0f, g.accent[(size_t) st]));
    const float velJitter = ((float) ((h >> 10) % 1000) / 999.0f * 2.0f - 1.0f) * 0.06f * std::min (1.0f, g.amount);
    velocity = juce::jlimit (0.05f, 1.0f, velocity * (1.0f + velJitter));
}
inline double grooveJitterMs (const Groove& g, double beat, int pitch)
{
    if (! g.valid || g.amount <= 0.0f) return 0.0;
    const double k = std::round (beat * 4.0);
    const auto h = (juce::uint32) (std::hash<long long>{} ((long long) (k * 131) + pitch * 7919));
    return ((double) (h % 1000) / 999.0 * 2.0 - 1.0) * g.humanizeMs * std::min (1.0f, g.amount);
}

struct Profile
{
    bool valid = false;
    double bpm = 90.0;
    int key = 0;                // pitch class of the home note
    bool minor = false;
    float density = 0.5f;       // how busy the rhythm is, 0..1
    // the groove: how strongly each 16th of a bar is hit, low (kick) / mid (snare) / high (hats), 0..1
    std::array<float, 16> grooveLow {}, grooveMid {}, grooveHigh {};
    bool hasGroove = false;
    Groove groove;              // the timing feel of its drums (swing, laid-back snare, accents)
    float brightness = 0.5f;    // how bright the sound is, 0..1
    double lengthSeconds = 0.0;
    Tone tone;                  // its overall sound
    // the reference split into drums / bass / vocals / music, measured: how loud each is where it plays
    bool hasStems = false;
    bool vocalsHeard = false;   // vocals are found by what sits in the middle of the stereo picture (needs a stereo file)
    std::array<float, 4> stemDb { -100.0f, -100.0f, -100.0f, -100.0f };
    std::array<std::array<float, 10>, 4> stemBands {}; // each part's tone (octave bands, dB around their average)
    float sustain = 0.5f;       // 0 short, plucky notes .. 1 long, held, singing notes
    float lowSustain = 0.5f;    // 0 punchy bass hits .. 1 a constant, held 808
    std::vector<RefSection> sections;
    juce::String sourceName;
};

// Analyses mono audio. Runs in well under a second per minute of music.
// knownBpm > 0: the tempo is already known (say, from the file name), so the groove and sections use its grid
// side: (left - right) / 2 of a stereo file, so vocals in the middle can be told apart (optional)
Profile analyse (const float* mono, int numSamples, double sampleRate, double knownBpm = 0.0, const float* side = nullptr);

// What the ear heard: how well every key fits ([key + 12 * minor]), and whether a third was heard
struct KeyReading { std::array<float, 24> scores {}; float best = 0; float tuningCents = 0; bool hasThird = true; };

// Listens for the key of a (short) recording. Returns false if it has no clear key.
bool detectKey (const float* mono, int numSamples, double sampleRate, int& key, bool& minor, KeyReading* reading = nullptr);

// Semitones to move something in one key so its notes fit another (relative keys count as the same).
inline int tuneShift (int fromKey, bool fromMinor, int toKey, bool toMinor)
{
    const int a = fromMinor ? (fromKey + 3) % 12 : fromKey;
    const int b = toMinor ? (toKey + 3) % 12 : toKey;
    int d = ((b - a) % 12 + 12) % 12;
    if (d > 6) d -= 12;
    return d;
}

// Decides how far to tune a loop into the song's key, from what the ear heard plus what the file name says
// (nameKey -1 = the name says nothing). Returns semitones, or 99 when it's too unsure to use the loop.
// The name is trusted unless the audio clearly disagrees; among keys that fit about equally well, the one
// needing the least tuning wins, so a loop is only moved when it clearly needs it.
inline int loopTune (const KeyReading& r, bool clear, int nameKey, bool nameMinor, int songKey, bool songMinor, bool* usedName = nullptr)
{
    auto score = [&] (int k, bool m) { return r.scores[(size_t) ((k + 120) % 12 + (m ? 12 : 0))]; };
    if (usedName != nullptr) *usedName = false;
    if (nameKey >= 0)
    {
        // sample packs label their keys, and the labels are almost always right: the ear only
        // overrules one when it is very sure and the label's key really doesn't fit what it hears
        const int rk = nameMinor ? (nameKey + 3) % 12 : (nameKey + 9) % 12;
        const float s = std::max (score (nameKey, nameMinor), score (rk, ! nameMinor));
        if (! clear || r.best < 0.72f || s >= r.best - 0.3f)
        {
            if (usedName != nullptr) *usedName = true;
            return tuneShift (nameKey, nameMinor, songKey, songMinor);
        }
    }
    if (! clear) return 99;
    int bestShift = 99; float bestS = -2;
    for (int i = 0; i < 24; ++i)
    {
        const int k = i % 12; const bool m = i >= 12;
        float s = r.scores[(size_t) i];
        if (! r.hasThird) s = std::max (s, score (k, ! m)); // no third heard: major or minor is a guess
        if (s < r.best - 0.06f) continue;
        const int sh = tuneShift (k, m, songKey, songMinor);
        if (std::abs (sh) < std::abs (bestShift) || (std::abs (sh) == std::abs (bestShift) && s > bestS)) { bestShift = sh; bestS = s; }
    }
    return bestShift;
}

// ---------- the new song

enum class PartKind { Chords, Melody, Counter, Pad, Bass, Drums, Loop, Fx };

// One drum sound's own mix and effects (the kick, the snare...), on top of the drum track's knobs
struct VoiceFx
{
    float gainDb = 0.0f, pan = 0.0f, tone = 0.0f;                                       // like the track's dials
    float space = 0.0f, echo = 0.0f, warmth = 0.0f, nostalgia = 0.0f, chorus = 0.0f;   // 0..1
    bool isDefault() const
    {
        return std::abs (gainDb) < 0.05f && std::abs (pan) < 0.005f && std::abs (tone) < 0.005f && space < 0.002f
            && echo < 0.002f && warmth < 0.002f && nostalgia < 0.002f && chorus < 0.002f;
    }
};

// A vocal track's chain (the Vocal chain window): seven modules in order, each on or off, recorded into the track
struct VocalChain
{
    bool on = false;                       // off: the vocal as it was
    bool eqOn = true, compOn = true, satOn = false, deessOn = true, airOn = true, delayOn = false, verbOn = true;
    float lowCutHz = 90.0f;                // EQ: rumble below this goes
    float mudDb = 0.0f;                    //     -12..0 at ~300 Hz (boxy, muddy)
    float presenceDb = 0.0f;               //     -6..+8 at ~3 kHz (words up front)
    float airDb = 0.0f;                    //     -6..+8 shelf from 10 kHz (sheen)
    float compAmount = 0.4f;               // compress: 0..1 (gentle .. pinned)
    float drive = 0.2f;                    // saturate: 0..1 (warmth .. grit)
    float deess = 0.35f;                   // de-ess: 0..1 (how hard S sounds are held back)
    float width = 0.25f;                   // air & width: 0..1 (a soft double out to the sides)
    float delayMix = 0.18f; int delayNote = 1; // delay: 0..1 level; 0 = 1/4, 1 = dotted 1/8, 2 = 1/8
    float verbSize = 0.5f, verbMix = 0.16f; // reverb: 0..1 each
    float compSpeed = 0.5f;                // compress: 0 slow and smooth .. 1 fast and tight
    float satTone = 0.0f, satMix = 1.0f;   // saturate: -1 dark .. +1 bright; how much of it (0..1)
    float deessHz = 6500.0f;               // de-ess: where the S lives (4..10 kHz)
    float focusHz = 220.0f;                // width: below this stays in the middle
    float delayFeedback = 0.35f, delayTone = 0.0f, delayDuck = 0.5f; bool delayPingPong = true; // repeats, -1 dark..+1 bright, ducking
    float verbPredelayMs = 20.0f, verbTone = 0.0f, verbDuck = 0.3f; // reverb: gap before it, -1 dark..+1 bright, ducking
    float mudHz = 300.0f, presenceHz = 3000.0f, airHz = 10000.0f; // EQ: where its three bands sit (drag the points)
    juce::String analysis;                 // what Analyze found, in plain words (one finding per line)
};

// One transition sound placed in the song (a riser, an impact...)
struct FxHit
{
    juce::String path;
    double beat = 0;        // where it lands, in beats
    bool peakOnBeat = false; // risers / reverse swells: their loudest moment lands on the beat; others start on it
    float gainDb = 0.0f;
};

// The groove of a song: which drum kit, which patterns, which bass.
enum class Style { Dusty, BoomBap, Trap, House, Brushes, Electronic };

// a feel for each style, when there's no reference to take it from
inline Groove styleGroove (Style st)
{
    Groove g;
    g.valid = true;
    switch (st)
    {
        case Style::Dusty:      g.swing16 = 0.25f; g.snareLate = 0.06f; g.humanizeMs = 6.0f; break;
        case Style::BoomBap:    g.swing16 = 0.18f; g.snareLate = 0.05f; g.humanizeMs = 5.0f; break;
        case Style::Brushes:    g.swing16 = 0.20f; g.swing8 = 0.15f; g.humanizeMs = 7.0f; break;
        case Style::House:      g.swing16 = 0.10f; g.humanizeMs = 2.0f; break;
        case Style::Trap:       g.humanizeMs = 2.0f; break;
        case Style::Electronic: g.humanizeMs = 1.0f; break;
    }
    // hats: the beat and the "and" a little stronger than the 16ths in between
    for (int i = 0; i < 16; ++i) g.accent[(size_t) i] = i % 4 == 0 ? 1.0f : i % 2 == 0 ? 0.85f : 0.65f;
    return g;
}

constexpr int kNumStyles = 6;
inline const char* styleName (Style s)
{
    static const char* n[] = { "dusty", "boom bap", "trap", "house", "brushes", "electronic" };
    return n[juce::jlimit (0, kNumStyles - 1, (int) s)];
}

struct Note
{
    double beat;     // start, in quarter notes from the start of the song
    double length;   // in quarter notes
    int pitch;       // MIDI note (drums: 36 kick, 38 snare, 42 closed hat, 46 open hat, 39 clap, 45 tom)
    float velocity;  // 0..1
    bool glide = false; // slides into this note from the one before (808s)
    int slice = -1;     // vocal chops: which chop (pad) it plays (-1: picked for its note)
};

// your vocal, auto-tuned: the untouched recording, and how it's tuned (the Auto-tune window)
struct VocalTune
{
    juce::String source;  // the vocal as you recorded it (empty: not a tuned vocal)
    int key = 0, scale = 0; // scale: 0 major, 1 minor, 2 chromatic, 3 harmonic minor, 4 major pentatonic, 5 minor pentatonic
    float speed = 0.35f, humanize = 0.45f, amount = 1.0f;
    int removed = 0;      // notes taken out (bit per pitch class)
};

// one chop of a vocal (a pad): where it is in the recording, in seconds, and the note it's sung on (0: none)
struct ChopSlice { double start = 0.0, len = 0.0, midi = 0.0; };

// Where a track is silent (the parts of clips you deleted or trimmed away), as stretches of ticks:
// 64 ticks a bar (a 1/64 note), so a clip can be trimmed or cut anywhere, not just on bars.
constexpr int kTicksPerBar = 64;
struct Spans
{
    std::map<int, int> m; // start -> end (end not included), never overlapping or touching
    void add (int a, int b)
    {
        if (b <= a) return;
        auto it = m.upper_bound (a);
        if (it != m.begin()) { auto p = std::prev (it); if (p->second >= a) { a = p->first; b = std::max (b, p->second); it = m.erase (p); } }
        while (it != m.end() && it->first <= b) { b = std::max (b, it->second); it = m.erase (it); }
        m[a] = b;
    }
    void remove (int a, int b)
    {
        if (b <= a) return;
        std::vector<std::pair<int, int>> keep;
        for (auto it = m.begin(); it != m.end();)
        {
            if (it->second <= a || it->first >= b) { ++it; continue; }
            if (it->first < a) keep.push_back ({ it->first, a });
            if (it->second > b) keep.push_back ({ b, it->second });
            it = m.erase (it);
        }
        for (auto& [x, y] : keep) m[x] = y;
    }
    bool covers (int t) const { auto it = m.upper_bound (t); if (it == m.begin()) return false; --it; return t < it->second; }
    bool any (int a, int b) const { for (auto& [x, y] : m) if (x < b && y > a) return true; return false; }
    bool covers (int a, int b) const { auto it = m.upper_bound (a); if (it == m.begin()) return false; --it; return it->first <= a && it->second >= b; }
    // the first tick at or after t where silence starts or ends (or `limit`)
    int nextEdge (int t, int limit) const
    {
        int best = limit;
        for (auto& [x, y] : m) { if (x > t) best = std::min (best, x); if (y > t) best = std::min (best, y); }
        return best;
    }
    bool empty() const { return m.empty(); }
};

struct Part
{
    std::string name;
    PartKind kind;
    lp::SoundId sound = lp::SoundId::Glass; // instrument for tonal parts
    float gainDb = 0.0f;
    std::vector<Note> notes;
    int variant = 0;                 // drums: the kit (Style); bass: 1 = 808
    juce::String samplePath;         // Loop parts: the audio file to repeat
    int loopBars = 0;                // Loop parts: how many bars the file lasts
    std::vector<int> loopStartBars;  // Loop parts: where it plays
    // what you see in the studio, and your edits
    juce::String source;             // e.g. "Splice: LD_92_keys_loop_Fmin" or "Live Planet: Rhodes"
    juce::String remix;              // how it was changed, e.g. "fitted to 140 bpm (+0.8%)"
    juce::StringArray candidates;    // other samples it could swap to
    int candidate = 0;
    int pitchSemis = 0;
    bool reversed = false;
    int autoTune = 0;                // loops: semitones to bring the sample into the song's key
    bool locked = false;             // you love it: "write the rest again" leaves it as it is
    juce::Array<int> candidateTunes; // ...for each candidate
    // produced arrangement (loops): for every bar of the song, which bar of the loop plays (-1 = silent),
    // and what is done to it, like a producer would
    std::vector<int> barSrc;
    std::map<int, float> barFilter;  // bar -> low-pass openness at the start of that bar (0 muffled .. 1 open)
    std::set<int> chopBars;          // the bar's 8ths re-ordered into a new variation
    std::set<int> stutterBars;       // the last beat stutters
    std::set<int> gapBars;           // the last beat drops out (right before a chorus hits)
    std::set<int> swellBars;         // a reversed swell of the loop rising into the next bar
    juce::uint32 colour = 0;         // its colour in the arrangement, if you picked one
    juce::String group;              // the group (folder) it's in, by name (empty: none)
    bool midi = false;               // a MIDI track: notes you write yourself (it can start empty), on its sound
    bool resound = false;            // "change its sound": its notes played on a one-shot from your library (samplePath), chords and all
    juce::uint32 flipSeed = 0;       // a flipped sample: its slices re-ordered, reversed, stuttered (0 = played as it is)
    int flipAmount = 2;              // 1 light, 2 medium, 3 heavy
    Spans silent;                    // the stretches you deleted or trimmed away (ticks: kTicksPerBar a bar)
    std::map<int, int> barFrom;
    std::set<int> splitTicks;        // where you cut its clips (ticks)
    // clips moved or copied off the grid: [dst0, dst1) plays the track from src0 on (in ticks, fractions allowed),
    // with the stretches that were silent in it then (mask, in source ticks). Played over everything else, last wins.
    struct MovedClip { double dst0 = 0, dst1 = 0, src0 = 0; std::vector<std::pair<double, double>> mask; };
    std::vector<MovedClip> moved;
    std::map<int, int> slotShift;    // a sample that follows the song's chords: semitones it moves, each half bar         // where you cut its clips (a clip starts at each of these bars)      // clips you copied (option-drag) or moved: this bar plays that bar's sound        // bars you deleted from this track in the arrangement (it's silent there)
    std::vector<FxHit> fxHits;       // Fx parts: the transition sounds and where they land
    float mixDb = 0.0f;              // where it sits in the mix vs. its kind's usual level (lead up, background down)
    int regionStart = 0, regionBars = 0; // loops: play only this part of the sample (in the song's bars; 0 = all of it)
    // grains: the track re-played as tiny slices in time with the song (0 = off)
    float grainAmount = 0.0f, grainSizeMs = 120.0f, grainChaos = 0.0f, grainPosition = 0.0f;
    int grainRate = 2;               // 0 = 1/4, 1 = 1/8, 2 = 1/16, 3 = 1/32 notes
    float pan = 0.0f;                // -1 left .. 0 centre .. +1 right
    std::map<int, juce::String> kit; // a drum part with its own sounds (a sliced drum loop); empty = the song's kit
    std::map<int, VoiceFx> voiceFx; // drums: a sound's own mix and effects (note -> settings; only the changed ones)
    VocalChain vocal;               // a vocal track's chain (off unless you turn it on)
    VocalTune tune;                 // a tuned vocal: how it's tuned
    std::vector<ChopSlice> chops;   // vocal chops: the recording's chops (its pads); empty = found when it's recorded
    const std::map<int, juce::String>& kitIn (const struct Song& s) const;
};

struct Section
{
    std::string name;   // "intro", "verse 1", "chorus 1"...
    int startBar = 0, bars = 8;
    float energy = 0.5f;
};

struct Song
{
    juce::String title;
    double bpm = 90.0;
    int key = 0;
    bool minor = false;
    Style style = Style::Dusty;
    std::vector<Section> sections;
    std::vector<Part> parts;
    std::map<int, juce::String> drumSamples; // drum note -> one-shot file from your library (empty = coded kit)
    Tone refTone;                            // the reference's sound, to shape the mix toward
    std::array<float, 10> matchEqDb {};      // the tone shaping found when the song was mixed (applied to every track)
    float matchGainDb = 0.0f;                // ...and the level
    float playbackMakeupDb = 0.0f;           // the stems keep headroom; this (into a limiter) brings them up to the mix's loudness
    bool mixShaped = false;
    Groove groove;                           // the drums' feel (from the reference, or the style)
    int totalBars() const { return sections.empty() ? 0 : sections.back().startBar + sections.back().bars; }
    // the drum sounds a drum part plays: its own (sliced), or the song's kit
    double lengthSeconds() const { return totalBars() * 4.0 * 60.0 / bpm; }
};

inline const std::map<int, juce::String>& Part::kitIn (const Song& s) const { return kit.empty() ? s.drumSamples : kit; }

// what the words you typed ask of the sounds: +1 / -1 / 0 (don't care)
struct SoundWish { int dark = 0, plucky = 0, punchy = 0, lofi = 0, warm = 0, airy = 0; };

struct Options
{
    int takes = 1;                // 3: write two more versions (same structure, tempo and key; other sounds)
    uint32_t pickSeed = 0;        // which sounds are picked (0 = from `seed`); takes change only this
    juce::String titleSuffix;     // e.g. " (take 2)"
    juce::StringArray avoid;      // takes: samples (and drum packs) earlier takes used, so this one sounds different
    bool shiftKey = true;         // move to a nearby key so it doesn't sit on top of the reference
    bool full = true;             // full length, or a short ~1 minute sketch
    lp::SoundId mainSound = lp::SoundId::Auto; // the plugin's chosen instrument (Auto = let the weather pick)
    uint32_t seed = 1;            // a different seed = a different song from the same reference
    int style = -1;               // -1 = pick from the reference / weather, else a Style
    double forceBpm = 0.0;        // set when a loop from your library decides the tempo
    double dawBpm = 0.0;          // > 0: write the song at your DAW's tempo
    int forceKey = -1;            // ...and the key
    bool forceMinor = false;
    // from "describe your song" (see Prompt.h)
    double promptBpm = 0.0;       // a tempo you typed
    double genreLo = 0.0, genreHi = 0.0, genreBpm = 0.0; // the genre's usual tempo range
    int promptKey = -1;           // a key you typed
    bool promptMinor = false;
    int promptMood = 0;           // -1 dark, +1 bright
    juce::StringArray genreWords;
    std::vector<int> wantKinds;   // instruments you asked for (LoopKind)
    SoundWish wish;               // "dark", "plucky", "punchy", "lofi", "warm", "airy"...
};

// The tempo / key / groove a song would get before any sample choices
struct Target
{
    double bpm; int key; bool minor; Style style;
    juce::StringArray genreWords; // from "describe your song": look for these in your sample folders
    SoundWish wish;
};
Target chooseTarget (const Profile& ref, const lp::Weather& sky, const Options& options);

// Writes a song. With an invalid profile it uses the weather and a classic song shape instead.
Song generate (const Profile& ref, const lp::Weather& sky, const Options& options);

// ---------- getting it out

struct RenderSettings
{
    double sampleRate = 48000.0;
    float nostalgia = 0.2f, space = 0.6f, warmth = 0.4f, echo = 0.3f;
};

struct Rendered
{
    juce::File folder, midiFile, mixFile, planFile;
    juce::Array<juce::File> stems;
    std::shared_ptr<juce::AudioBuffer<float>> mixAudio; // the finished song, for playing it inside the plugin
    std::array<float, 10> matchEqDb {};  // the tone shaping used (kept, so edited tracks get the same)
    float matchGainDb = 0.0f;
    float playbackMakeupDb = 0.0f;
};

// Renders every part to its own WAV file plus a full mix, and writes a multi-track MIDI file
// with section markers and a plan file the Ableton helper reads. `progress` gets 0..1.
// Returns false if `shouldStop` turns true or a file can't be written.
bool render (const Song& song, const RenderSettings& settings, const juce::File& folder, Rendered& out,
             std::function<void (float)> progress = {}, std::function<bool()> shouldStop = {});

void writeMidi (const Song& song, const juce::File& file);

// One hit of a drum part's kit (your samples or the built-in kit), about a second long.
juce::AudioBuffer<float> renderDrumHit (const Song& song, const Part& part, int note, double sampleRate);

// Renders one part on its own, with its edits (used by the studio to redo a single track).
bool renderPart (const Song& song, const Part& part, const RenderSettings& rs, juce::AudioBuffer<float>& out,
                 std::function<bool()> shouldStop = {});

} // namespace lp::song
