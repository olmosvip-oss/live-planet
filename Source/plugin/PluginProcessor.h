#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <deque>
#include "../engine/Engine.h"
#include "WeatherService.h"
#include "SongJob.h"
#include "StudioPlayer.h"
#include "../engine/SampleFx.h"

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

    const juce::String getName() const override { return "Live Planet"; }
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
    std::vector<float> getSamplePeaks() const { const juce::SpinLock::ScopedLockType sl (peaksLock); return samplePeaks; }

    // The notes Live Planet played since you pressed play, written to a MIDI file
    // you can drag into your song. Returns an empty File if nothing was played yet.
    juce::File exportRecentNotes (int bars = 8);
    int recordedNoteCount() const { return recordedOnCount.load(); }

    // song mode
    SongJob songJob;
    void makeSong (bool full, bool newKey);
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
    std::vector<float> fxL, fxR, sendL, sendR;
    std::atomic<bool> previewPlaying { false };
    std::atomic<double> previewPos { 0.0 };   // in 48 kHz samples
    std::atomic<double> dawBpm { 0.0 };       // your project's tempo, as last reported
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
