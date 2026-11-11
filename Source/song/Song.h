#pragma once
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

// How alike two sounds are in character (bright / dark, lo-fi / polished), 0..1
inline float toneLikeness (const Tone& a, const Tone& b)
{
    if (! a.valid || ! b.valid) return 0.5f;
    const float roll = std::abs (std::log2 (std::max (200.0f, a.rolloffHz) / std::max (200.0f, b.rolloffHz))); // octaves apart
    const float air = std::abs (a.airDb - b.airDb) / 12.0f;
    return std::exp (-(roll * 1.2f + air));
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
    float brightness = 0.5f;    // how bright the sound is, 0..1
    double lengthSeconds = 0.0;
    Tone tone;                  // its overall sound
    // the reference split into drums / bass / vocals / music, measured: how loud each is where it plays
    bool hasStems = false;
    bool vocalsHeard = false;   // vocals are found by what sits in the middle of the stereo picture (needs a stereo file)
    std::array<float, 4> stemDb { -100.0f, -100.0f, -100.0f, -100.0f };
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
    juce::Array<int> candidateTunes; // ...for each candidate
    // produced arrangement (loops): for every bar of the song, which bar of the loop plays (-1 = silent),
    // and what is done to it, like a producer would
    std::vector<int> barSrc;
    std::map<int, float> barFilter;  // bar -> low-pass openness at the start of that bar (0 muffled .. 1 open)
    std::set<int> chopBars;          // the bar's 8ths re-ordered into a new variation
    std::set<int> stutterBars;       // the last beat stutters
    std::set<int> gapBars;           // the last beat drops out (right before a chorus hits)
    std::set<int> swellBars;         // a reversed swell of the loop rising into the next bar
    std::vector<FxHit> fxHits;       // Fx parts: the transition sounds and where they land
    float mixDb = 0.0f;              // where it sits in the mix vs. its kind's usual level (lead up, background down)
    float pan = 0.0f;                // -1 left .. 0 centre .. +1 right
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
    int totalBars() const { return sections.empty() ? 0 : sections.back().startBar + sections.back().bars; }
    double lengthSeconds() const { return totalBars() * 4.0 * 60.0 / bpm; }
};

struct Options
{
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
};

// The tempo / key / groove a song would get before any sample choices
struct Target
{
    double bpm; int key; bool minor; Style style;
    juce::StringArray genreWords; // from "describe your song": look for these in your sample folders
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

// Renders one part on its own, with its edits (used by the studio to redo a single track).
bool renderPart (const Song& song, const Part& part, const RenderSettings& rs, juce::AudioBuffer<float>& out,
                 std::function<bool()> shouldStop = {});

} // namespace lp::song
