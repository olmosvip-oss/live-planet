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
#include "VoiceHelper.h"
#include "../engine/SampleFx.h"
#include "../engine/Master.h"
#include "../engine/Sampler.h"
#include "../engine/GrainSynth.h"
#include "../engine/Limiter.h"
#include "../song/LyricMelody.h"

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
    // the notes you play (your MIDI keyboard, or Ableton's computer keys) while Draw & sing records a melody:
    // when (in samples from the start), which note, on / off. Read with takeMidiTake() once it stops.
    struct TakeNote { juce::int64 pos = 0; int note = 0; float vel = 0.0f; bool on = false; };
    void startMidiTake() { midiTakeReset = true; midiTaking = true; }
    std::vector<TakeNote> takeMidiTake()
    {
        midiTaking = false;
        std::vector<TakeNote> out;
        int s1, n1, s2, n2;
        midiTakeFifo.prepareToRead (midiTakeFifo.getNumReady(), s1, n1, s2, n2);
        for (int i = 0; i < n1; ++i) out.push_back (midiTakeBuf[(size_t) (s1 + i)]);
        for (int i = 0; i < n2; ++i) out.push_back (midiTakeBuf[(size_t) (s2 + i)]);
        midiTakeFifo.finishedRead (n1 + n2);
        return out;
    }
    std::atomic<bool> midiTaking { false }, midiTakeReset { false };
    juce::int64 midiTakePos = 0;               // (audio thread)
    juce::AbstractFifo midiTakeFifo { 4096 };
    std::array<TakeNote, 4096> midiTakeBuf {};
    std::atomic<bool> recording { false };
    std::atomic<int> recPos { 0 };
    std::atomic<float> inPeak { 0.0f };
    juce::AudioBuffer<float> recBuf;
    double recRate = 48000.0;
    void samplerOff();
    lp::Sampler sampler;
    // GRAINS: the granular instrument (its own page). While that page is open, your keys play it.
    lp::grains::Synth grainSynth;
    std::atomic<bool> grainsActive { false };
    std::shared_ptr<lp::grains::Source> grainSource;  // (message thread) the sound it's playing
    lp::grains::Settings grainSettings;                // (message thread) everything set on the page
    bool loadGrainSound (const juce::File& f);          // (message thread)
    void sendGrainSettings() { grainSynth.setSettings (grainSettings); }
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
    // the chop pads (vocal chops): each pad a chop, played from the window, your computer keys or a MIDI
    // controller (while the window is open, notes go to the pads); what you play is noted with where the song was
    LiveDrums livePads;
    std::atomic<int> padsTrack { -1 };
    struct PadHit { int pad; float vel; double beat; bool playing; };
    juce::SpinLock padLock;
    std::vector<PadHit> padHits;
    void hitPad (int pad, float vel)
    {
        livePads.audition (pad, vel);
        notePadHit (pad, vel, 0);
    }
    void notePadHit (int pad, float vel, int offsetSamples)
    {
        const double sbpm = songBpm.load(), sr = getSampleRate() > 0 ? getSampleRate() : 48000.0;
        const double beat = playheadPos.load() / StudioPlayer::fileRate * sbpm / 60.0 + offsetSamples / sr * sbpm / 60.0;
        const juce::SpinLock::ScopedLockType sl (padLock);
        if (padHits.size() < 512) padHits.push_back ({ pad, vel, beat, songPlaying.load() });
    }
    std::vector<PadHit> takePadHits()
    {
        std::vector<PadHit> out;
        const juce::SpinLock::ScopedLockType sl (padLock);
        std::swap (out, padHits);
        return out;
    }
    // the drum editor: your computer keys or a MIDI controller play the kit (and record into the loop)
    std::atomic<int> drumKeysTrack { -1 };
    std::atomic<bool> drumPadsOpen { false }; // the Drum pads window takes the hits
    std::atomic<juce::uint32> drumKeysLease { 0 }; // renewed every frame while the drum editor shows
    static int drumNoteFor (int n)
    {
        switch (n)
        {
            case 35: case 36: return 36;
            case 37: return 37;
            case 38: case 40: return 38;
            case 39: return 39;
            case 42: case 44: return 42;
            case 46: return 46;
            case 41: case 43: case 45: case 47: case 48: case 50: return 45;
            case 49: case 57: return 49;
            case 70: return 70;
            default: { static const int order[8] = { 36, 38, 39, 37, 42, 46, 45, 70 }; return order[((n - 36) % 8 + 8) % 8]; }
        }
    }
    void hitDrum (int note, float vel)
    {
        liveDrums.audition (note, vel);
        notePadHit (note, vel, 0);
    }
    // ---- Voice (experimental): the local voice helper, and a sung version heard with the song
    lp::voice::Client voiceClient;
    // the song's section (all tracks as you hear them) as a WAV for the helper to sing over (background thread)
    juce::File renderSectionForVoice (int sectionIndex, int count = 1);
    // melodies for these lyrics over those sections' chords (one per version, each its own), as the voice helper reads
    // them. Slow (it listens to the tracks for the chords): call it off the message thread
    juce::var writeVoiceMelodies (int sectionIndex, int count, const juce::String& lyrics, bool rap, int howMany);
    std::vector<lp::song::lyricmelody::Line> writeVoiceLines (int sectionIndex, int count, const juce::String& lyrics, bool rap, juce::uint32 seed);
    // your take from the mic (Voice > I sing it): `skipSeconds` in (the count-in), `seconds` long, at a good level
    bool saveGuideTake (const juce::File& dest, double skipSeconds, double seconds, juce::String& message);
    // Draw & sing's guide: the drawn notes played as a soft tone along with the song (seconds from its start), and a
    // short tone for a note being placed or moved. Set from the editor; played on the audio thread.
    struct GuideNote { double t0 = 0, t1 = 0; int pitch = 60; };
    void setGuide (std::vector<GuideNote> notes)
    {
        auto fresh = std::make_unique<std::vector<GuideNote>> (std::move (notes));
        { const juce::SpinLock::ScopedLockType sl (guideLock); std::swap (guideNotes, fresh); }
    }   // (the old list is freed here, off the audio thread)
    std::atomic<bool> guideOn { false };
    void auditionNote (int pitch) { auditionPitch = pitch; ++auditionSerial; }
    // a version playing along with the song: 48 kHz, starting at bar 1 (null = none)
    // (offset: where in the song, in 48 kHz samples, the buffer starts: 0 = bar 1)
    void setVoicePreview (std::shared_ptr<juce::AudioBuffer<float>> b, juce::int64 offset = 0)
    {
        std::shared_ptr<juce::AudioBuffer<float>> old;
        { const juce::SpinLock::ScopedLockType sl (voicePreviewLock); old = std::move (voicePreview); voicePreview = std::move (b); voicePreviewOffset = offset; }
    }
    juce::int64 voicePreviewOffset = 0;
    std::map<juce::String, juce::File> drawnFiles; // (message thread) Draw & sing's sung takes, by token, once they're in the song
    // a phrase sung again on its own: put into the take it belongs to (the rest of the section as it is now)
    struct DrawnMerge { std::shared_ptr<const juce::AudioBuffer<float>> base; double start = 0; std::vector<std::pair<double, double>> ranges; };
    std::map<juce::String, DrawnMerge> drawnMerge; // (message thread) by token
    // a short sound played once, straight away (Draw & sing: the note you're moving, in your voice); null = the tone
    void auditionBuffer (std::shared_ptr<juce::AudioBuffer<float>> b)
    {
        std::shared_ptr<juce::AudioBuffer<float>> old;
        { const juce::SpinLock::ScopedLockType sl (voicePreviewLock); old = std::move (auditionBuf); auditionBuf = std::move (b); auditionBufPos = 0; auditionBufDone = false; }
    }
    std::shared_ptr<juce::AudioBuffer<float>> auditionBuf;
    int auditionBufPos = 0;
    bool auditionBufDone = false;
    bool hasVoicePreview() const { const juce::SpinLock::ScopedLockType sl (voicePreviewLock); return voicePreview != nullptr; }
    juce::SpinLock voicePreviewLock;
    std::shared_ptr<juce::AudioBuffer<float>> voicePreview;
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
    int seenVocalSerial = -1;     // the vocal chains the studio has
    bool lastSongChecked = false; // (the app) reopened the last song yet?
    int startTicks = 0;
    int tempoOffTicks = 0;      // how long your DAW's tempo has been off the song's
    double askedBpm = 0.0;      // the tempo the song was last asked to move to
    std::atomic<bool> restoredFromProject { false };
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
    std::vector<float> fxL, fxR, sendL, sendR, liveL, liveR;
    std::atomic<bool> previewPlaying { false };
    juce::SpinLock guideLock;
    std::unique_ptr<std::vector<GuideNote>> guideNotes;
    std::atomic<int> auditionPitch { 60 }, auditionSerial { 0 };
    int auditionSeen = 0, auditionLeft = 0, guideIdx = -1;
    double guidePhase = 0.0, guideFreq = 0.0, guideEnv = 0.0, auditionPhase = 0.0, auditionFreq = 0.0;
    void playGuide (float* L, float* R, int n, double songPos, double inc); // (audio thread)
    void playAudition (float* L, float* R, int n);                          // (audio thread)
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
    // following FL Studio's pattern loop (see processBlock)
    const bool isFL = juce::PluginHostType().isFruityLoops();
    bool wasFollowing = false;
    double lastHostPpq = -1.0, lastBlockBeats = 0.0, ppqCarry = 0.0;
    bool canRecordMic() const; // only the standalone app has a mic input
    static BusesProperties lpBuses();
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
