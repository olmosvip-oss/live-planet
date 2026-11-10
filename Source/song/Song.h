#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <string>
#include <vector>
#include <map>
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
    float brightness = 0.5f;    // how bright the sound is, 0..1
    double lengthSeconds = 0.0;
    std::vector<RefSection> sections;
    juce::String sourceName;
};

// Analyses mono audio. Runs in well under a second per minute of music.
Profile analyse (const float* mono, int numSamples, double sampleRate);

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
    int forceKey = -1;            // ...and the key
    bool forceMinor = false;
};

// The tempo / key / groove a song would get before any sample choices
struct Target { double bpm; int key; bool minor; Style style; };
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

} // namespace lp::song
