#pragma once
#include <mutex>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <deque>
#include "../engine/Engine.h"
#include "WeatherService.h"
#include "SongJob.h"
#include "StudioPlayer.h"
#include "LiveDrums.h"
#include "../engine/SampleFx.h"
#include "../engine/Master.h"
#include "../engine/Sampler.h"
#include "../engine/Limiter.h"

class LivePlanetProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    LivePlanetProcessor();
    ~LivePlanetProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Loopwright"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentPreset.load(); }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // --- used by the editor (message thread)
    juce::AudioProcessorValueTreeState params;
    std::atomic<int> themeIndex { 0 }; // the colour of the device (saved with the song)
    lp::Engine engine;

    void searchCity (const juce::String& name);
    bool loadSample (const juce::File& file);
    // a one-shot from your library played across the keyboard (Play mode), with its velocity layers
    bool loadSamplerInstrument (const juce::File& file);
    // your own recording cut into its syllables, one on each key from C3 up (Play mode)
    int loadVocalChops (const juce::File& file);
    // record your voice straight in (the mic input), then it's chopped like a file
    void startRecording();
    int stopRecording (juce::String& message);
    std::atomic<bool> recording { false };
    std::atomic<int> recPos { 0 };
    std::atomic<float> inPeak { 0.0f };
    juce::AudioBuffer<float> recBuf;
    double recRate = 48000.0;
    void samplerOff();
    lp::Sampler sampler;
    std::atomic<bool> samplerOn { false };
    juce::SpinLock samplerInfoLock;
    juce::String samplerName, samplerInfo;     // e.g. "CK Keys 01" / "3 velocity layers, recorded at C3"
    juce::StringArray samplerPaths;
    std::vector<float> samplerPeaks;           // its waveform, for the Texture screen (under samplerInfoLock)
    std::atomic<int> samplerVersion { 0 };
    lp::SampleFx samplerFx;                    // the Space knobs, on the sample instrument
    std::vector<float> smpL, smpR, smpSendL, smpSendR;
    void setSamplerPeaks (const std::vector<std::shared_ptr<juce::AudioBuffer<float>>>& bufs);
    std::vector<float> getSamplePeaks() const { const juce::SpinLock::ScopedLockType sl (peaksLock); return samplePeaks; }

    // The notes Live Planet played since you pressed play, written to a MIDI file
    // you can drag into your song. Returns an empty File if nothing was played yet.
    juce::File exportRecentNotes (int bars = 8);
    // the file to drag for a track: its dry stem, or (Print FX on) the stem with its own effects recorded in
    juce::File fileToDrag (int track);
    void setUpFeel (const SongJob::Status&);
    std::atomic<int> printsRunning { 0 };
    int seenMixInserts = 0;
    // the drum editor: hear each hit, and loop the bars you're editing with the rest of the song
    LiveDrums liveDrums;
    std::atomic<int> liveTrack { -1 };
    std::atomic<double> loopStartBeat { 0.0 }, loopBeats { 4.0 };
    void startDrumLoop (int track, double startBeat, double beats);
    void setDrumLoop (double startBeat, double beats);
    void stopDrumLoop();
    bool drumLoopPlaying() const { return liveTrack.load() >= 0 && previewPlaying.load(); }
    juce::int64 keysQuietSamples = 1LL << 40; // how long since you last played a key (Play mode)
    // a reopened project's track mixer, applied once its song is open again
    juce::SpinLock restoreLock;
    juce::ValueTree restoredMix;
    juce::File restoredMixFolder;
    std::mutex printLock; // one print at a time (a drag waits for one already started)
    int recordedNoteCount() const { return recordedOnCount.load(); }

    // song mode
    SongJob songJob;
    void makeSong (bool full, bool newKey, int takes = 1);
    juce::Array<juce::File> libraryFolders() const;
    void addLibraryFolder (const juce::File& f);
    void rescanLibrary() { songJob.scanLibrary (libraryFolders()); }
    bool songIsPlaying() const { return songPlaying.load(); }

    // the finished song plays from bar 1 whenever the DAW plays
    std::deque<std::shared_ptr<juce::AudioBuffer<float>>> songBuffers; // kept alive here
    std::atomic<juce::AudioBuffer<float>*> songAudio { nullptr };
    std::atomic<double> songBpm { 90.0 };
    std::atomic<bool> songPlaying { false };

    // the studio: the song's tracks with a live mixer, and a preview transport
    StudioPlayer studio;
    lp::SampleFx sampleFx;                      // the knobs, for your Splice samples
    lp::Limiter masterLimiter;                  // the song's loudness, without squashing it

    // mastering: a platform preset, worked out on your mix (in the background), then heard live
    // (after) or not (before), and exported as a mastered file
    void startMaster (int preset);              // -1 = off
    bool masterNow (int preset);
    juce::String masterKeyNow (int preset);
    juce::File masteredFile();                  // up to date with your mix (re-mastered first if it changed)
    juce::String masterKey;
    std::atomic<int> masterPreset { -1 };       // the one that's ready and playing
    std::atomic<bool> masterAfter { true };     // A/B: hear the master (true) or the plain mix
    std::atomic<bool> mastering { false };      // working it out right now
    std::atomic<float> masterGainDb { 0.0f }, masterGlueDb { -18.0f };
    juce::SpinLock masterLock;
    juce::String masterReport;                  // e.g. "Spotify: -14.0 LUFS, peak -1.0 dB"
    juce::File masterFile;
    std::array<lp::Master, 8> liveMasters;      // one per preset, ready on the audio thread
    std::atomic<int> masterResets { 0 };
    std::atomic<float> outPeakL { 0.0f }, outPeakR { 0.0f }; // everything the plugin plays (the master meter)
    std::atomic<float> songMakeupDb { 0.0f };
    juce::File loadedSongFolder;
    // the key boxes changed: move the current song to that key (Auto = the reference's key)
    void applyChosenKey()
    {
        const auto st = songJob.getStatus();
        if (! st.haveSong) return;
        const int k = (int) params.getRawParameterValue ("songkey")->load() - 1;
        const int m = (int) params.getRawParameterValue ("songmode")->load();
        const int key = k >= 0 ? k : (st.profile.valid ? st.profile.key : st.song.key);
        const bool minor = m > 0 ? m == 2 : (st.profile.valid ? st.profile.minor : st.song.minor);
        songJob.setSongKey (key, minor);
    }
    std::vector<float> fxL, fxR, sendL, sendR;
    std::atomic<bool> previewPlaying { false };
    std::atomic<double> previewPos { 0.0 };   // in 48 kHz samples
    std::atomic<double> dawBpm { 0.0 };
    juce::String songPrompt;                  // "describe your song" (message thread only)
    bool songPromptIsAuto = false;            // ...still just the description written from your reference       // your project's tempo, as last reported
    std::atomic<double> playheadPos { 0.0 };  // where the song is right now (preview or DAW)
    int loadedStudioVersion = -1;
    juce::String getSampleName() const { return sampleName; }

    struct SkyStatus
    {
        juce::String city, country, message;
        bool live = false, haveLive = false, busy = false;
        double lat = 0, lon = 0;
        juce::Time fetchedAt;
    };
    SkyStatus getSkyStatus() const;
    lp::Weather getManualWeather() const;

    static juce::StringArray soundNames() { return { "Auto (weather picks)", "Glass", "Choir", "Felt Piano", "Music Box",
                                                     "Nylon Guitar", "Tape Strings", "Vapor Synth", "Rhodes", "Kalimba",
                                                     "Vibes", "Ocarina", "Celesta", "Harp", "Marimba", "Melodica",
                                                     "Organ", "Cello", "Your Sample" }; }
    static constexpr int kSampleChoice = 18;
    std::atomic<bool> hostPlaying { false };
    juce::String getSampleInfo() const { return sampleInfo; }

private:
    void timerCallback() override;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    lp::EngineParams readParams() const;

    WeatherService weatherService;
    juce::String city = "Tokyo", country = "Japan", statusMessage;
    double lat = 35.6895, lon = 139.6917;
    lp::Weather liveWeather;
    bool haveLive = false;
    juce::Time fetchedAt;
    mutable juce::SpinLock skyLock;
    std::atomic<uint32_t> citySeed { lp::hashString ("Tokyo") };

    juce::String samplePath, sampleName, sampleInfo;
    std::deque<std::unique_ptr<lp::SourceSound>> loadedSamples;

    std::bitset<128> held;
    std::vector<lp::KeyEvent> keyEvents;

    struct RecNote { double beat; int channel, note; float velocity; bool on; };
    std::vector<RecNote> recorded;
    juce::SpinLock recLock;
    std::atomic<int> recordedOnCount { 0 };
    bool wasRunning = false, exportTested = false;
    int exportTicks = 0;
    std::atomic<int> currentPreset { 0 };
    std::vector<float> samplePeaks;
    mutable juce::SpinLock peaksLock;
    lp::MidiOut midiOut;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LivePlanetProcessor)
};
