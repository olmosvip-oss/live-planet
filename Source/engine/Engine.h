#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <bitset>
#include <memory>
#include <vector>
#include "Weather.h"
#include "Theory.h"
#include "Sources.h"
#include "Dsp.h"
#include "Profiles.h"

namespace lp
{


struct EngineParams
{
    int   keyRoot   = -1;   // -1 = let the weather pick, else 0..11
    int   scale     = -1;   // -1 = let the weather pick, else ScaleId
    SoundId sound   = SoundId::Auto;
    float grainMs   = 140.0f;
    float texture   = 0.35f; // how scattered the grains are
    float sparkle   = 0.3f;  // octave-up glitter grains
    float position  = 0.3f;  // where in the sound grains are taken from
    float warmth    = 0.4f;  // tape warmth and wobble
    float echo      = 0.3f;
    float space     = 0.6f;  // reverb
    float shimmer   = 0.35f; // reverb that rises an octave
    float atmos     = 0.2f;  // real rain and wind noise
    float padLevel  = 0.75f;
    float dropLevel = 0.7f;
    float bassLevel = 0.45f;
    float gainDb    = 0.0f;
};

struct TransportInfo
{
    bool   hostPlaying = false;
    double bpm = 0.0;   // 0 = use the weather's own tempo
    double ppq = 0.0;   // position in quarter notes when the host is playing
    bool   shouldPlay = true; // false: stay silent (DAW stopped, play button off, no keys held)
};

struct MidiOut
{
    struct Ev { int offset; int channel; int note; float velocity; bool on; };
    std::array<Ev, 512> events {};
    int count = 0;
    void clear() { count = 0; }
    void add (int offset, int ch, int note, float vel, bool on)
    {
        if (count < (int) events.size()) events[(size_t) count++] = { offset, ch, note, vel, on };
    }
};

// Something the screen should draw: a note being played, or lightning.
struct VisualEvent { int note; float velocity; int layer; }; // layer 0 pad, 1 drop, 2 bass, 3 thunder

class Engine
{
public:
    Engine();

    void prepare (double sampleRate);
    void setTargetWeather (const Weather& w) { target = w; }
    void jumpToWeather (const Weather& w) { target = w; current = w; }
    void setCitySeed (uint32_t s) { if (s != citySeed) soundBlock = -1; citySeed = s; } // new city: pick a sound at the next bar
    void setUserSource (const SourceSound* s) { userSource.store (s); }

    void process (float* L, float* R, int n, const EngineParams& p, const TransportInfo& t,
                  const std::bitset<128>& held, MidiOut& midi);

    bool popVisual (VisualEvent& e);
    const Weather& getCurrentWeather() const { return current; }
    Weather getTargetWeather() const { return target; } // the real sky (current glides toward it)

    // what is playing right now, for the screen (read from the UI thread; small
    // races only affect a label)
    std::atomic<bool> uiRunning { false };
    std::atomic<int> uiStep { 0 }, uiSound { 0 }, uiKey { 0 }, uiScale { 0 }, uiChordRoot { 0 }, uiChordMinor { 0 };
    std::atomic<float> uiBpm { 80.0f };

private:
    struct Grain
    {
        const float* data = nullptr;
        int dataLen = 0;
        double pos = 0, inc = 1;
        int len = 0, age = 0;
        float gl = 0, gr = 0;
        bool active = false;
    };

    struct Voice
    {
        bool active = false;
        int note = 0;
        float vel = 0, env = 0;
        int stage = 0;             // 0 attack, 1 hold, 2 release/decay
        float attackPerSample = 0;
        float releaseMul = 0.9999f;
        double countdown = 0;
        int layer = 0;             // 0 pad, 1 drop
        float pan = 0;
        double bellPhase = 0;
        float bellEnv = 0;
        bool sparkleVoice = false;
    };

    // a note played straight from the sound, start to finish (piano, guitar, music box)
    struct Shot
    {
        const float* data = nullptr;
        int len = 0, age = 0, note = 0, layer = 0;
        double pos = 0, inc = 1;
        float gl = 0, gr = 0, env = 1, mul = 1;
        int fadeIn = 48;
        bool active = false;
    };

    struct Pending { int samplesLeft; int kind; int note; float vel; }; // kind 0 pad on, 1 drop on, 2 midi off

    void runStep (long step, int offset, const EngineParams& p, const std::bitset<128>& held, MidiOut& midi);
    void changeChord (const std::vector<int>& padNotes, int bassNote, int offset, float strumMs, MidiOut& midi);
    void startPad (int note, float vel, int offset, MidiOut& midi);
    void startDrop (int note, float vel, int offset, MidiOut& midi, float decaySec, bool withBell);
    void triggerThunder();
    void stopAll (int offset, MidiOut& midi);
    void spawnShot (const SourceSound& src, int note, float gain, float pan, int layer, float cents = 0.0f);
    void dampShots (int layer, int note, float seconds);
    void resolveSound (long bar, const EngineParams& p, int offset, MidiOut& midi);
    void restrike (int offset, float strumMs, MidiOut& midi);
    const SourceSound& sourceFor (SoundId id) const;
    void renderChunk (float* L, float* R, int n, const EngineParams& p);
    void spawnGrain (std::vector<Grain>& pool, const SourceSound& src, double startPos, double ratio,
                     int len, float gain, float pan);
    void pushVisual (int note, float vel, int layer);
    void computeHarmony (long bar, const EngineParams& p, const std::bitset<128>& held,
                         std::vector<int>& padNotes, int& bassNote, uint32_t& chordId);

    double sr = 48000.0;
    Weather target, current;
    uint32_t citySeed = 7;

    std::vector<Shot> shots;
    SoundId soundNow = SoundId::Glass, autoSound = SoundId::Glass, lastRequested = (SoundId) -1;
    long soundBlock = -1;
    bool running = false;
    float p_padLevel = 0.75f, p_dropLevel = 0.7f, p_bassLevel = 0.45f, p_sparkle = 0.3f, p_texture = 0.35f, p_position = 0.3f;
    float atmosGate = 0.0f;
    std::atomic<const SourceSound*> userSource { nullptr };

    std::vector<Grain> padGrains, dropGrains;
    std::array<float, 1025> hann {};
    std::array<Voice, 20> padVoices {};
    std::array<Voice, 32> dropVoices {};
    std::vector<Pending> pending;

    // bass: one soft sine voice that glides between roots
    float bassFreq = 55.0f, bassTargetFreq = 55.0f, bassEnv = 0.0f, bassTargetEnv = 0.0f;
    double bassPhase = 0;
    int bassMidiNote = -1;

    // thunder
    float thunderEnv = 0.0f, thunderFreq = 60.0f;
    dsp::SVF thunderFilter;

    // harmony state
    uint32_t currentChordId = 0xffffffffu;
    std::vector<int> currentPad;
    std::vector<int> melodyPool;
    int lastDrop = 84;
    int keyRoot = 0;
    ScaleId scaleId = ScaleId::Major;
    std::vector<int> chordPcs;
    std::vector<int> scratchPad, scratchMelody, scratchPcs;
    std::bitset<128> lastHeld;
    ScaleId autoScale = ScaleId::Major;
    bool autoScaleSet = false;
    long autoScaleBlock = 0;
    long lastStep = -999999;
    double freePpq = 0.0;
    double curBpm = 80.0;
    float lastRoom = -1.0f, lastDamp = -1.0f;
    double thunderPhase = 0.0;
    std::vector<float> shimFbL, shimFbR;

    // effects
    dsp::Delay wobbleL, wobbleR, echoL, echoR;
    dsp::SVF echoFiltL, echoFiltR, toneL, toneR, rainFilt, windFilt;
    dsp::OctaveUp shimmerL, shimmerR;
    juce::Reverb reverb;
    dsp::Noise noise;
    float wobblePhase = 0, flutterPhase = 0, windLfo = 0;
    float shimmerFbL = 0, shimmerFbR = 0;
    float dcL = 0, dcR = 0, dcInL = 0, dcInR = 0;
    std::vector<float> padL, padR, dropL, dropR, revL, revR;

    // screen events
    juce::AbstractFifo visualFifo { 256 };
    std::array<VisualEvent, 256> visualBuf {};

    juce::Random rng { 1234 };
};

} // namespace lp
