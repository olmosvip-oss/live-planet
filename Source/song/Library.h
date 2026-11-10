#pragma once
#include <juce_core/juce_core.h>
#include "Song.h"

// Your own sample library (for example the sounds Splice saved on your Mac).
// Live Planet reads the file and folder names, which usually say the tempo, key and
// what the sound is, e.g. "OS_KJ_95_piano_loop_Fmin.wav" or "Kicks/kick_punchy_01.wav".
// Files are only read from your disk for your own songs; nothing is copied or shared.
namespace lp::song
{

enum class SampleKind { Kick, Snare, Clap, Hat, OpenHat, Perc, Bass808, Loop, Other };
enum class LoopKind { None, Drums, Bass, Keys, Guitar, Synth, Vocal, Strings, Other };

struct LibraryEntry
{
    juce::String path;
    juce::String pack;        // the folder it came in, so drums can come from one kit
    SampleKind kind = SampleKind::Other;
    LoopKind loop = LoopKind::None;
    double bpm = 0.0;         // 0 = not said
    int key = -1;             // -1 = not said
    bool minor = false;
    juce::uint32 styles = 0;  // bit per Style it seems to fit
};

class Library
{
public:
    // Default places to look: ~/Splice and ~/Music/Splice, plus any folders you add.
    static juce::Array<juce::File> defaultFolders();

    // Reads every audio file under the folders (up to `maxFiles`). Safe to call from a background thread.
    void scan (const juce::Array<juce::File>& folders, int maxFiles = 60000);
    bool load (const juce::File& cache);
    void save (const juce::File& cache) const;

    int size() const { return (int) entries.size(); }
    int count (SampleKind k) const;
    int loopCount() const;
    const std::vector<LibraryEntry>& all() const { return entries; }

    // Understands one file name. Public so it can be tested.
    static LibraryEntry parse (const juce::File& file, const juce::File& root);

private:
    std::vector<LibraryEntry> entries;
};

// What a song takes from your library.
struct LibraryPicks
{
    std::map<int, juce::String> drums;    // drum note -> one-shot
    const LibraryEntry* loop = nullptr;   // a melodic loop in the song's key and tempo
    int loopBars = 0;
    juce::String drumPack;
};

// Picks drums that fit the style (ideally all from one pack) and a loop that matches
// the key and sits within a few percent of the tempo. `loopLengthBars` must tell how
// long a file is in bars at a tempo (returns 0 if it can't be read or isn't whole bars).
LibraryPicks pickFromLibrary (const Library& lib, const Target& target, juce::uint32 seed,
                              std::function<int (const juce::String& path, double bpm)> loopLengthBars);

} // namespace lp::song
