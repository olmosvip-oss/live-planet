#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <deque>
#include "../engine/Engine.h"
#include "WeatherService.h"

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

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // --- used by the editor (message thread)
    juce::AudioProcessorValueTreeState params;
    std::atomic<int> themeIndex { 0 }; // the colour of the device (saved with the song)
    lp::Engine engine;

    void searchCity (const juce::String& name);
    bool loadSample (const juce::File& file);
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
                                                     "Nylon Guitar", "Tape Strings", "Vapor Synth", "Your Sample" }; }

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

    juce::String samplePath, sampleName;
    std::deque<std::unique_ptr<lp::SourceSound>> loadedSamples;

    std::bitset<128> held;
    lp::MidiOut midiOut;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LivePlanetProcessor)
};
