#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <string>
#include <vector>
#include <map>
#include <array>
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
};

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
    std::vector<RefSection> sections;
    juce::String sourceName;
};

// Analyses mono audio. Runs in well under a second per minute of music.
Profile analyse (const float* mono, int numSamples, double sampleRate);

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
inline int loopTune (const KeyReading& r, bool clear, int nameKey, bool nameMinor, int songKey, bool songMinor)
{
    auto score = [&] (int k, bool m) { return r.scores[(size_t) ((k + 120) % 12 + (m ? 12 : 0))]; };
    if (nameKey >= 0)
    {
        const int rk = nameMinor ? (nameKey + 3) % 12 : (nameKey + 9) % 12;
        const float s = std::max (score (nameKey, nameMinor), score (rk, ! nameMinor));
        if (! clear || s >= r.best - 0.15f) return tuneShift (nameKey, nameMinor, songKey, songMinor);
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

enum class PartKind { Chords, Melody, Counter, Pad, Bass, Drums, Loop };

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
