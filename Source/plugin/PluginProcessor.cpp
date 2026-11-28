#include "PluginProcessor.h"
#include "../song/LyricMelody.h"
#include "License.h"
#include "AppFolder.h"
#include "PluginEditor.h"
#include "Presets.h"
#include "AbletonHelper.h"
#include "../song/Prompt.h"

using APVTS = juce::AudioProcessorValueTreeState;

namespace
{
    std::unique_ptr<juce::AudioParameterFloat> knob (const char* id, const char* name, float lo, float hi,
                                                     float def, const char* unit = "", float skewCentre = -1.0f)
    {
        juce::NormalisableRange<float> range (lo, hi);
        if (skewCentre > lo) range.setSkewForCentre (skewCentre);
        const juce::String u (unit);
        auto toText = [u, hi] (float v, int)
        {
            if (u.isEmpty() && hi <= 1.0f) return juce::String (juce::roundToInt (v * 100.0f)) + " %";
            if (u == "mm" || u == "cm")    return juce::String (v, 1) + " " + u;
            if (u == "C")                  return juce::String (juce::roundToInt (v)) + juce::String (juce::CharPointer_UTF8 ("\xc2\xb0")) + "c";
            if (u == "dB")                 return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " db";
            if (u == "hPa")                return juce::String (juce::roundToInt (v)) + " hpa";
            return juce::String (juce::roundToInt (v)) + " " + u;
        };
        return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, range, def,
                                                            juce::AudioParameterFloatAttributes().withLabel (unit)
                                                                .withStringFromValueFunction (toText));
    }
}

APVTS::ParameterLayout LivePlanetProcessor::createLayout()
{
    APVTS::ParameterLayout l;

    // Sky
    l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "live", 1 }, "Live Weather", true));
    // plays without the DAW running (the play button on the screen)
    l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "play", 1 }, "Play", false));
    // song mode
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "songkey", 1 }, "Song Key",
        juce::StringArray { "Auto", "C", "C#/Db", "D", "D#/Eb", "E", "F", "F#/Gb", "G", "G#/Ab", "A", "A#/Bb", "B" }, 0));
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "songmode", 1 }, "Song Scale",
        juce::StringArray { "Auto", "Major", "Minor" }, 0));
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "songstyle", 1 }, "Song Style",
        juce::StringArray { "auto", "dusty", "boom bap", "trap", "house", "brushes", "electronic" }, 0));
    l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "usesamples", 1 }, "Use My Samples", true));
    l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "songplay", 1 }, "Play Song With DAW", true));
    l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "samplesonly", 1 }, "Samples Only", false));
    l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "dawtempo", 1 }, "Song At DAW Tempo", true));
    // your MIDI keyboard: "play" = notes play the instrument directly, "chord" = your keys set the chord the weather plays
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "keymode", 1 }, "Keyboard",
                                                          juce::StringArray { "play", "chord" }, 0));
    l.add (knob ("temp", "Temperature", -20.0f, 40.0f, 16.0f, "C"));
    l.add (knob ("rain", "Rain", 0.0f, 12.0f, 0.8f, "mm", 2.0f));
    l.add (knob ("snow", "Snow", 0.0f, 4.0f, 0.0f, "cm", 0.8f));
    l.add (knob ("wind", "Wind", 0.0f, 80.0f, 12.0f, "km/h", 25.0f));
    l.add (knob ("clouds", "Clouds", 0.0f, 100.0f, 55.0f, "%"));
    l.add (knob ("pressure", "Pressure", 970.0f, 1045.0f, 1012.0f, "hPa"));
    l.add (knob ("humidity", "Humidity", 0.0f, 100.0f, 70.0f, "%"));
    l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "daylight", 1 }, "Daylight", true));
    l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "thunder", 1 }, "Thunder", false));

    // Music
    juce::StringArray keys { "Auto" };
    for (int i = 0; i < 12; ++i) keys.add (lp::noteName (i));
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "key", 1 }, "Key", keys, 0));
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "scale", 1 }, "Scale",
        juce::StringArray { "Auto", "Major", "Lydian", "Mixolydian", "Dorian", "Minor" }, 0));
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "sound", 1 }, "Sound", soundNames(), 0));
    l.add (knob ("pad", "Chords", 0.0f, 1.0f, 0.75f));
    l.add (knob ("drops", "Melody", 0.0f, 1.0f, 0.7f));
    l.add (knob ("bass", "Bass", 0.0f, 1.0f, 0.45f));

    // Texture
    l.add (knob ("grain", "Grain Size", 20.0f, 400.0f, 140.0f, "ms", 120.0f));
    l.add (knob ("texture", "Scatter", 0.0f, 1.0f, 0.35f));
    l.add (knob ("sparkle", "Sparkle", 0.0f, 1.0f, 0.3f));
    l.add (knob ("position", "Position", 0.0f, 1.0f, 0.3f));
    l.add (knob ("warmth", "Warmth", 0.0f, 1.0f, 0.4f));
    l.add (knob ("echo", "Echo", 0.0f, 1.0f, 0.3f));
    l.add (knob ("space", "Space", 0.0f, 1.0f, 0.6f));
    l.add (knob ("shimmer", "Shimmer", 0.0f, 1.0f, 0.35f));
    l.add (knob ("atmos", "Atmosphere", 0.0f, 1.0f, 0.2f));
    l.add (knob ("gain", "Volume", -24.0f, 6.0f, 0.0f, "dB"));
    l.add (knob ("nostalgia", "Nostalgia", 0.0f, 1.0f, 0.2f));

    // your sample: which part to play, and its tuning
    l.add (knob ("smpStart", "Sample Start", 0.0f, 1.0f, 0.0f));
    l.add (knob ("smpEnd", "Sample End", 0.0f, 1.0f, 1.0f));
    l.add (knob ("smpTune", "Sample Tune", -12.0f, 12.0f, 0.0f, "st"));
    return l;
}

// Inside a DAW it's a plain instrument: stereo out, no audio input. (An instrument that asks for an input
// isn't run by Logic at all: no sound, no tempo.) Only the standalone app takes your mic, for recording vocals;
// in a DAW you record there and drop the file on the Sound page.
juce::AudioProcessor::BusesProperties LivePlanetProcessor::lpBuses()
{
    auto b = BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true);
    // (not while recording a tutorial video: no mic, so macOS never asks for it)
    if (juce::PluginHostType::jucePlugInClientCurrentWrapperType == juce::AudioProcessor::wrapperType_Standalone
        && std::getenv ("LP_DEMO_SCRIPT") == nullptr)
        b = b.withInput ("Mic in", juce::AudioChannelSet::stereo(), true);
    return b;
}

bool LivePlanetProcessor::canRecordMic() const { return getTotalNumInputChannels() > 0; }

LivePlanetProcessor::LivePlanetProcessor()
    : AudioProcessor (lpBuses()),
      params (*this, nullptr, "LivePlanet", createLayout())
{
    engine.setCitySeed (citySeed.load());
    engine.jumpToWeather (getManualWeather());
    weatherService.requestCoordinates (city, lat, lon);
    startTimerHz (2);
    lp::license::checkInBackground(); // (a license code without this computer's activation asks the website once)
    lp::license::checkJordanInBackground(); // (and Jordan's voice license, the same way)
    songJob.writeAbletonSet = [] (const lp::song::Song&, const lp::song::Rendered&)
    {
        return lp::ableton::installHelper (SongJob::latestPointer());
    };

    // for testing: LP_LOAD_SAMPLE=/path/to/file.wav loads a sample at start-up
    if (auto* path = std::getenv ("LP_LOAD_SAMPLE"))
        loadSample (juce::File (juce::String (path)));
    // for testing: LP_DAW_BPM=120 acts as if the DAW reported that tempo
    if (auto* bpm = std::getenv ("LP_DAW_BPM"))
        dawBpm = juce::String (bpm).getDoubleValue();
    // for testing: LP_SONG_REF=/path/to/song.wav gives song mode a reference at start-up
    if (auto* ref = std::getenv ("LP_SONG_REF"))
        songJob.setReference (juce::File (juce::String (ref)));
}

LivePlanetProcessor::~LivePlanetProcessor()
{
    stopTimer();
    while (printsRunning.load() > 0) juce::Thread::sleep (20); // a background print finishing
}

bool LivePlanetProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    if (layouts.inputBuses.isEmpty()) return true;
    const auto in = layouts.getMainInputChannelSet();
    return in.isDisabled() || in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void LivePlanetProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate);
    keyEvents.reserve (1024);
    studio.prepare (std::max (samplesPerBlock, 4096));
    const int maxBlock = std::max (samplesPerBlock, 4096);
    sampleFx.prepare (sampleRate, maxBlock);
    samplerFx.prepare (sampleRate, maxBlock);
    grainSynth.prepare (sampleRate, maxBlock);
    for (auto* v : { &smpL, &smpR, &smpSendL, &smpSendR }) v->assign ((size_t) maxBlock, 0.0f);
    // room for 60 seconds of your voice (made here, so recording never allocates)
    if (recBuf.getNumSamples() < (int) (sampleRate * 60.0)) recBuf.setSize (2, (int) (sampleRate * 60.0));
    recRate = sampleRate;
    masterLimiter.prepare (sampleRate, -1.0f);
    {
        int count = 0;
        const auto* presets = lp::masterPresets (count);
        for (int i = 0; i < count && i < (int) liveMasters.size(); ++i) liveMasters[(size_t) i].prepare (sampleRate, presets[i].ceilingDb);
    }
    for (auto* v : { &fxL, &fxR, &sendL, &sendR, &liveL, &liveR, &recInL, &recInR }) v->assign ((size_t) maxBlock, 0.0f);
}

lp::Weather LivePlanetProcessor::getManualWeather() const
{
    lp::Weather w;
    auto v = [this] (const char* id) { return params.getRawParameterValue (id)->load(); };
    w.temperatureC = v ("temp");
    w.rainMm = v ("rain");
    w.snowCm = v ("snow");
    w.windKmh = v ("wind");
    w.cloudPct = v ("clouds");
    w.pressureHpa = v ("pressure");
    w.humidityPct = v ("humidity");
    w.isDay = v ("daylight") > 0.5f;
    w.thunder = v ("thunder") > 0.5f;
    w.fog = w.humidityPct > 95.0f && w.cloudPct > 80.0f && w.rainMm < 0.1f;
    w.weatherCode = w.thunder ? 95 : (w.snowCm > 0.05f ? 73 : (w.rainMm > 0.05f ? 61 : (w.cloudPct > 85 ? 3 : 1)));
    return w;
}

lp::EngineParams LivePlanetProcessor::readParams() const
{
    auto v = [this] (const char* id) { return params.getRawParameterValue (id)->load(); };
    lp::EngineParams p;
    p.keyRoot = (int) v ("key") - 1;
    p.scale = (int) v ("scale") - 1;
    const int soundIdx = juce::jlimit (0, kSampleChoice, (int) v ("sound"));
    p.sound = soundIdx == 0 ? lp::SoundId::Auto : (lp::SoundId) (soundIdx - 1);
    p.padLevel = v ("pad");
    p.dropLevel = v ("drops");
    p.bassLevel = v ("bass");
    p.grainMs = v ("grain");
    p.texture = v ("texture");
    p.sparkle = v ("sparkle");
    p.position = v ("position");
    p.warmth = v ("warmth");
    p.echo = v ("echo");
    p.space = v ("space");
    p.shimmer = v ("shimmer");
    p.atmos = v ("atmos");
    p.gainDb = v ("gain");
    p.nostalgia = v ("nostalgia");
    p.sampleStart = std::min (v ("smpStart"), v ("smpEnd") - 0.02f);
    p.sampleEnd = std::max (v ("smpEnd"), v ("smpStart") + 0.02f);
    p.sampleTune = v ("smpTune");
    return p;
}

// the drawn notes as a soft, round tone at the song's position (one note at a time, gliding a touch between them)
void LivePlanetProcessor::playGuide (float* L, float* R, int n, double songPos, double inc)
{
    const juce::SpinLock::ScopedTryLockType sl (guideLock);
    if (! sl.isLocked() || guideNotes == nullptr) return;
    const auto& notes = *guideNotes;
    const double sr = getSampleRate() > 0 ? getSampleRate() : 48000.0;
    for (int i = 0; i < n; ++i)
    {
        const double t = (songPos + i * inc) / StudioPlayer::fileRate;
        if (guideIdx < 0 || guideIdx >= (int) notes.size() || t < notes[(size_t) guideIdx].t0 || t >= notes[(size_t) guideIdx].t1)
        {
            guideIdx = -1;
            for (int k = 0; k < (int) notes.size(); ++k)
                if (t >= notes[(size_t) k].t0 && t < notes[(size_t) k].t1) { guideIdx = k; break; }
        }
        const bool on = guideIdx >= 0;
        if (on)
        {
            const double f = 440.0 * std::pow (2.0, (notes[(size_t) guideIdx].pitch - 69) / 12.0);
            guideFreq = guideFreq <= 0.0 ? f : guideFreq + (f - guideFreq) * 0.004;
        }
        guideEnv += ((on ? 1.0 : 0.0) - guideEnv) * (on ? 0.004 : 0.0015);
        if (guideEnv < 1.0e-4 && ! on) { guideFreq = 0.0; continue; }
        guidePhase += 2.0 * juce::MathConstants<double>::pi * guideFreq / sr;
        if (guidePhase > 2.0 * juce::MathConstants<double>::pi) guidePhase -= 2.0 * juce::MathConstants<double>::pi;
        const float v = (float) (0.11 * guideEnv * (std::sin (guidePhase) + 0.25 * std::sin (2.0 * guidePhase)));
        L[i] += v; R[i] += v;
    }
}

// a short tone for a note you place or move (heard whether the song plays or not)
void LivePlanetProcessor::playAudition (float* L, float* R, int n)
{
    const double sr = getSampleRate() > 0 ? getSampleRate() : 48000.0;
    {
        // the note in your voice (48 kHz), when there is one
        const juce::SpinLock::ScopedTryLockType sl (voicePreviewLock);
        if (sl.isLocked() && auditionBuf != nullptr && ! auditionBufDone)
        {
            const auto& b = *auditionBuf;
            const double step = StudioPlayer::fileRate / sr;
            for (int i = 0; i < n; ++i)
            {
                const int k = (int) (auditionBufPos * step);
                if (k + 1 >= b.getNumSamples()) break;
                L[i] += b.getSample (0, k);
                R[i] += b.getSample (std::min (1, b.getNumChannels() - 1), k);
                ++auditionBufPos;
            }
            if ((int) (auditionBufPos * step) + 1 >= b.getNumSamples()) auditionBufDone = true; // (played; freed by the next one, off this thread)
            auditionSeen = auditionSerial.load();
            return;
        }
    }
    if (const int s = auditionSerial.load(); s != auditionSeen)
    {
        auditionSeen = s;
        auditionLeft = (int) (0.3 * sr);
        auditionFreq = 440.0 * std::pow (2.0, (auditionPitch.load() - 69) / 12.0);
        auditionPhase = 0.0;
    }
    const int total = (int) (0.3 * sr);
    for (int i = 0; i < n && auditionLeft > 0; ++i, --auditionLeft)
    {
        const double t = (double) (total - auditionLeft) / sr;
        auditionPhase += 2.0 * juce::MathConstants<double>::pi * auditionFreq / sr;
        const float v = (float) (0.16 * std::min (1.0, t / 0.008) * std::exp (-t * 7.0) * (std::sin (auditionPhase) + 0.25 * std::sin (2.0 * auditionPhase)));
        L[i] += v; R[i] += v;
    }
}

void LivePlanetProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    // your mic: recorded while you record, and metered (it isn't played out, so no feedback)
    {
        const int ins = getTotalNumInputChannels();
        float pk = 0.0f;
        for (int ch = 0; ch < ins; ++ch) pk = std::max (pk, buffer.getMagnitude (ch, 0, n));
        lp::ui::notePeak (inPeak, pk);
        // (an armed track: this block's input, kept for when the song's position is known below)
        if (armTrack.load() >= 0 && ins > 0 && (size_t) n <= recInL.size())
        {
            std::copy (buffer.getReadPointer (0), buffer.getReadPointer (0) + n, recInL.begin());
            const float* r = buffer.getReadPointer (std::min (1, ins - 1));
            std::copy (r, r + n, recInR.begin());
        }
        if (recording.load() && ins > 0)
        {
            const int at = recPos.load(), room = recBuf.getNumSamples() - at, k = std::min (n, room);
            if (k > 0)
            {
                recBuf.copyFrom (0, at, buffer, 0, 0, k);
                recBuf.copyFrom (1, at, buffer, std::min (1, ins - 1), 0, k);
                recPos = at + k;
            }
            if (k < n) recording = false; // a minute is the most: stop there
        }
    }
    buffer.clear();

    // Your keyboard: either play notes directly, or hold keys to set the chord
    const bool chordMode = params.getRawParameterValue ("keymode")->load() > 0.5f;
    keyEvents.clear();
    const bool toPads = padsTrack.load() >= 0;
    const bool toDrums = ! toPads && drumKeysTrack.load() >= 0 && juce::Time::getMillisecondCounter() - drumKeysLease.load() < 800;
    const bool grainsOpen = grainsActive.load();
    const bool taking = midiTaking.load();
    if (midiTakeReset.exchange (false)) { midiTakePos = 0; midiTakeFifo.reset(); }
    for (const auto m : midi)
    {
        const auto msg = m.getMessage();
        if (taking && (msg.isNoteOn() || msg.isNoteOff()))
        {
            // (Draw & sing is recording a melody: every note you play, and when)
            int s1, n1, s2, n2;
            midiTakeFifo.prepareToWrite (1, s1, n1, s2, n2);
            const TakeNote r { midiTakePos + m.samplePosition, msg.getNoteNumber(), msg.getFloatVelocity(), msg.isNoteOn() };
            if (n1 > 0) midiTakeBuf[(size_t) s1] = r; else if (n2 > 0) midiTakeBuf[(size_t) s2] = r;
            midiTakeFifo.finishedWrite (n1 + n2);
        }
        // the Grains page is open: every note plays it (your MIDI keyboard, a clip, or Ableton's computer keys)
        if (grainsOpen && (msg.isNoteOn() || msg.isNoteOff() || msg.isAllNotesOff() || msg.isAllSoundOff()))
        {
            if (msg.isNoteOn()) grainSynth.noteOn (msg.getNoteNumber(), msg.getFloatVelocity());
            else if (msg.isNoteOff()) grainSynth.noteOff (msg.getNoteNumber());
            else for (int nn = 0; nn < 128; ++nn) grainSynth.noteOff (nn);
            continue;
        }
        if (toPads && (msg.isNoteOn() || msg.isNoteOff()))
        {
            // the chop pads are open: C1 (36) and up are pads 1-16 (any other note lands on a pad too)
            if (msg.isNoteOn())
            {
                const int pad = ((msg.getNoteNumber() - 36) % 16 + 16) % 16;
                livePads.audition (pad, msg.getFloatVelocity());
                notePadHit (pad, msg.getFloatVelocity(), m.samplePosition);
            }
            continue;
        }
        if (toDrums && (msg.isNoteOn() || msg.isNoteOff()))
        {
            // the drum editor is open: notes play the kit (General MIDI drum notes, or any pad of a controller)
            if (msg.isNoteOn())
            {
                const int dn = drumNoteFor (msg.getNoteNumber());
                liveDrums.audition (dn, msg.getFloatVelocity());
                notePadHit (dn, msg.getFloatVelocity(), m.samplePosition);
            }
            continue;
        }
        // an armed MIDI track: your notes play its sound (heard through the track, so its meter moves)
        if (const int arm = midiArmTrack.load(); arm >= 0 && ! toPads && ! toDrums && (msg.isNoteOn() || msg.isNoteOff()))
        {
            if (msg.isNoteOn()) liveDrums.audition (msg.getNoteNumber(), msg.getFloatVelocity());
            // (recording: kept with its place in this block; its song position is worked out below)
            if (recordOn.load() && midiRecBlockCount < (int) midiRecBlock.size())
                midiRecBlock[(size_t) midiRecBlockCount++] = { (double) m.samplePosition, msg.getNoteNumber(), msg.getFloatVelocity(), msg.isNoteOn() };
            continue;
        }
        if (msg.isNoteOn())
        {
            held.set ((size_t) msg.getNoteNumber());
            if (! chordMode) keyEvents.push_back ({ m.samplePosition, msg.getNoteNumber(), msg.getFloatVelocity(), true });
        }
        else if (msg.isNoteOff())
        {
            held.reset ((size_t) msg.getNoteNumber());
            if (! chordMode) keyEvents.push_back ({ m.samplePosition, msg.getNoteNumber(), 0.0f, false });
        }
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
        {
            for (int nn = 0; nn < 128; ++nn)
                if (held[(size_t) nn] && ! chordMode) keyEvents.push_back ({ m.samplePosition, nn, 0.0f, false });
            held.reset();
        }
    }
    midi.clear();
    if (taking) midiTakePos += n;

    // Which sky are we using?
    lp::Weather sky = getManualWeather();
    const bool live = params.getRawParameterValue ("live")->load() > 0.5f;
    if (live)
    {
        const juce::SpinLock::ScopedTryLockType tl (skyLock);
        if (tl.isLocked() && haveLive)
            sky = liveWeather;
    }
    engine.setTargetWeather (sky);
    engine.setCitySeed (citySeed.load());

    lp::TransportInfo t;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto bpm = pos->getBpm()) { t.bpm = *bpm; dawBpm = *bpm; }
            if (pos->getIsPlaying())
                if (auto ppq = pos->getPpqPosition())
                {
                    t.hostPlaying = true;
                    t.ppq = *ppq;
                }
        }
    hostPlaying = t.hostPlaying;
    // only make sound when the song is playing, the play button is on, or you hold keys
    t.shouldPlay = t.hostPlaying || params.getRawParameterValue ("play")->load() > 0.5f || (chordMode && held.any());
    // Play mode: while you're playing the keys, only your notes sound. The weather's own music
    // steps aside, and comes back a few seconds after you stop.
    if (! chordMode)
    {
        if (held.any() || ! keyEvents.empty()) keysQuietSamples = 0;
        else keysQuietSamples += n;
        if (keysQuietSamples < (juce::int64) (getSampleRate() * 4.0)) t.shouldPlay = false;
    }
    bool auditioned = false;
    if (liveTrack.load() >= 0 && ! previewPlaying.load()) { liveTrack = -1; studio.skipTrack = -1; } // stopped elsewhere
    const bool haveSong = studio.length() > 0;
    const bool preview = haveSong && previewPlaying.load();
    const bool playSong = haveSong && ! preview && t.hostPlaying && params.getRawParameterValue ("songplay")->load() > 0.5f;
    if (! playSong) { wasFollowing = false; ppqCarry = 0.0; lastHostPpq = -1.0; } // stopped: the next play follows your DAW from where it starts
    songPlaying = playSong || preview;
    if (! (playSong || preview) && trackRecording.load()) { trackRecording = false; trackTakeReady = true; } // (stopped: the take is done)
    if (! (playSong || preview))
    {
        midiRecBlockCount = 0; // (notes played while stopped aren't recorded)
        if (recordOn.load() && songWasPlaying) midiTakeEnded = true; // (stopped while recording: the take is done)
    }
    songWasPlaying = playSong || preview;
    if (playSong || preview) t.shouldPlay = false; // the weather music steps aside while your song plays
    if (! soundPageOpen.load()) t.shouldPlay = false; // (and it only plays while the Sound page is open)
    // your keys play the sample instrument (Play mode), or the built-in instrument
    const bool toGrains = grainsActive.load() && ! chordMode;
    const bool toSampler = samplerOn.load() && ! chordMode && ! toGrains;
    t.keys = toSampler || toGrains ? nullptr : keyEvents.data();
    t.numKeys = toSampler || toGrains ? 0 : (int) keyEvents.size();
    static const std::bitset<128> none;

    midiOut.clear();
    auto* L = buffer.getWritePointer (0);
    auto* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : L;
    engine.process (L, R, n, readParams(), t, chordMode ? held : none, midiOut);

    // remember the notes played since you pressed play, so they can be dragged into the song
    {
        const juce::SpinLock::ScopedTryLockType tl (recLock);
        if (tl.isLocked())
        {
            if (midiOut.running && ! wasRunning) { recorded.clear(); recordedOnCount = 0; }
            for (int i = 0; i < midiOut.count && midiOut.running; ++i)
            {
                const auto& e = midiOut.events[(size_t) i];
                if (recorded.size() >= 20000) recorded.erase (recorded.begin(), recorded.begin() + 10000);
                recorded.push_back ({ midiOut.blockPpq + e.offset * midiOut.ppqPerSample, e.channel, e.note, e.velocity, e.on });
                if (e.on) ++recordedOnCount;
            }
            wasRunning = midiOut.running;
        }
    }

    if (preview || playSong)
    {
        const double sbpm = songBpm.load();
        double pos, inc;
        if (preview)
        {
            pos = previewPos.load();
            inc = StudioPlayer::fileRate / getSampleRate();
        }
        else
        {
            // bar 1 of your project = the start of the song
            const double hostBpm = t.bpm > 0.0 ? t.bpm : sbpm;
            // FL Studio in pattern mode loops a short pattern over and over: keep playing the song on instead
            // of jumping back to its start every bar (a jump back of more than 8 bars is you moving the playhead)
            double ppq = t.ppq;
            if (isFL)
            {
                const double expected = lastHostPpq + lastBlockBeats;
                if (wasFollowing && lastHostPpq >= 0.0 && ppq < expected - 0.25 && expected - ppq <= 32.5)
                    ppqCarry += expected - ppq;
                else if (wasFollowing && lastHostPpq >= 0.0 && std::abs (ppq - expected) > 0.25)
                    ppqCarry = 0.0;
                lastHostPpq = ppq;
                wasFollowing = true;
                lastBlockBeats = n / getSampleRate() * hostBpm / 60.0;
                ppq += ppqCarry;
            }
            pos = std::max (0.0, ppq) * 60.0 / sbpm * StudioPlayer::fileRate;
            inc = StudioPlayer::fileRate / getSampleRate() * (hostBpm / sbpm);
        }
        // your samples go through the knobs (warmth, echo, space, shimmer, nostalgia, atmosphere, volume);
        // the chords / melody / bass knobs set the level of those tracks
        auto v = [this] (const char* id) { return params.getRawParameterValue (id)->load(); };
        studio.groupGain[1] = v ("pad") / 0.75f;
        studio.groupGain[2] = v ("drops") / 0.7f;
        studio.groupGain[3] = v ("bass") / 0.45f;
        const bool fx = (size_t) n <= fxL.size();
        if (fx) for (auto* b : { &fxL, &fxR, &sendL, &sendR }) std::fill (b->begin(), b->begin() + n, 0.0f);
        // the drum (or note) editor's loop: the part played live from what you're drawing, through the
        // track's own mixer and effects (so its knobs are heard as it plays)
        const int lt0 = liveTrack.load();
        const bool liveLoop = preview && lt0 >= 0 && (size_t) n <= liveL.size();
        if (liveLoop)
        {
            const double toBeats = sbpm / 60.0 / StudioPlayer::fileRate;
            std::fill (liveL.begin(), liveL.begin() + n, 0.0f);
            std::fill (liveR.begin(), liveR.begin() + n, 0.0f);
            liveDrums.process (liveL.data(), liveR.data(), n, getSampleRate(), true, pos * toBeats, (pos + n * inc) * toBeats, 1.0f);
            studio.liveInL = liveL.data();
            studio.liveInR = liveR.data();
        }
        // the armed MIDI track's notes from this block, at their place in the song (beats from bar 1)
        if (midiRecBlockCount > 0)
        {
            const double toBeats = sbpm / 60.0 / StudioPlayer::fileRate;
            int s1, n1, s2, n2;
            midiRecFifo.prepareToWrite (midiRecBlockCount, s1, n1, s2, n2);
            for (int i = 0; i < n1 + n2; ++i)
            {
                auto r = midiRecBlock[(size_t) i];
                r.beat = (pos + r.beat * inc) * toBeats;
                midiRecBuf[(size_t) (i < n1 ? s1 + i : s2 + (i - n1))] = r;
            }
            midiRecFifo.finishedWrite (n1 + n2);
            midiRecBlockCount = 0;
        }
        // an armed track records what comes in, from where the song is (while Record is on)
        if (recordOn.load() && armTrack.load() >= 0 && getTotalNumInputChannels() > 0 && (size_t) n <= recInL.size())
        {
            if (! trackRecording.load()) { trackRecStart = pos; trackRecording = true; }
            const juce::SpinLock::ScopedTryLockType wl (trackWriterLock);
            if (wl.isLocked() && trackWriter != nullptr) { const float* ch[] = { recInL.data(), recInR.data() }; trackWriter->write (ch, n); }
        }
        studio.render (L, R, n, pos, inc, getSampleRate(),
                       fx ? fxL.data() : nullptr, fx ? fxR.data() : nullptr, fx ? sendL.data() : nullptr, fx ? sendR.data() : nullptr,
                       preview ? sbpm : (t.bpm > 0.0 ? t.bpm : sbpm));
        if (fx)
        {
            lp::SampleFxSettings fs;
            fs.warmth = v ("warmth"); fs.echo = v ("echo"); fs.space = v ("space"); fs.shimmer = v ("shimmer");
            fs.nostalgia = v ("nostalgia"); fs.atmos = v ("atmos"); fs.gainDb = v ("gain");
            fs.bpm = preview ? sbpm : (t.bpm > 0.0 ? t.bpm : sbpm);
            sampleFx.process (fxL.data(), fxR.data(), sendL.data(), sendR.data(), n, fs);
            for (int i = 0; i < n; ++i) { L[i] += fxL[(size_t) i]; R[i] += fxR[(size_t) i]; }
        }
        const double next = pos + n * inc;
        // the drum editor's loop: your pattern played live (the recorded drum track is skipped)
        const int lt = liveTrack.load();
        const bool looping = preview && lt >= 0;
        studio.liveInL = studio.liveInR = nullptr;
        // a sung version from the Voice page, in time with the song (it starts at bar 1, like the stems)
        {
            const juce::SpinLock::ScopedTryLockType vl (voicePreviewLock);
            if (vl.isLocked() && voicePreview != nullptr)
            {
                const auto& vb = *voicePreview;
                const int vn = vb.getNumSamples();
                const float* va = vb.getReadPointer (0);
                const float* vr = vb.getReadPointer (std::min (1, vb.getNumChannels() - 1));
                for (int i = 0; i < n; ++i)
                {
                    const double fp = pos + i * inc - (double) voicePreviewOffset;
                    const int i0 = (int) fp;
                    if (i0 < 0 || i0 + 1 >= vn) continue;
                    const float fr = (float) (fp - i0);
                    L[i] += va[i0] + (va[i0 + 1] - va[i0]) * fr;
                    R[i] += vr[i0] + (vr[i0 + 1] - vr[i0]) * fr;
                }
            }
        }
        if (guideOn.load()) playGuide (L, R, n, pos, inc); // (Draw & sing: your drawn notes, along with the song)
        if (looping)
        {
            if (! liveLoop) // (no room for it in the mixer this block: played straight, at the track's volume)
            {
                const double toBeats = sbpm / 60.0 / StudioPlayer::fileRate;
                const auto& m = studio.mix[(size_t) juce::jlimit (0, StudioPlayer::maxTracks - 1, lt)];
                const float g = m.mute.load() ? 0.0f : juce::Decibels::decibelsToGain (m.gainDb.load());
                liveDrums.process (L, R, n, getSampleRate(), true, pos * toBeats, next * toBeats, g);
            }
            auditioned = true;
        }
        // the master (after), or the plain mix through the song's limiter (before)
        const int mp = masterPreset.load();
        if (mp >= 0 && masterAfter.load() && mp < (int) liveMasters.size())
        {
            auto& m = liveMasters[(size_t) mp];
            m.glueThreshDb = masterGlueDb.load();
            m.process (L, R, n, masterGainDb.load());
        }
        else
            masterLimiter.process (L, R, n, songMakeupDb.load());
        playheadPos = next;
        if (looping)
        {
            const double toSamples = 60.0 / sbpm * StudioPlayer::fileRate;
            const double a = loopStartBeat.load() * toSamples, b = (loopStartBeat.load() + loopBeats.load()) * toSamples;
            previewPos = next >= b || next < a ? a + std::fmod (std::max (0.0, next - a), std::max (1.0, b - a)) : next;
        }
        else if (preview)
        {
            if (next >= (double) studio.length()) { previewPlaying = false; previewPos = 0.0; }
            else previewPos = next;
        }
    }

    // GRAINS: your keys play it while its page is open; it follows the song's beat when the song (or your DAW) plays
    {
        if (toGrains)
            for (auto& k : keyEvents) k.on ? grainSynth.noteOn (k.note, k.velocity) : grainSynth.noteOff (k.note);
        lp::grains::Transport gt;
        const double sbpm = songBpm.load();
        if (preview) { gt.playing = true; gt.bpm = sbpm > 0.0 ? sbpm : 120.0; gt.ppq = previewPos.load() / StudioPlayer::fileRate * gt.bpm / 60.0; }
        else if (t.hostPlaying) { gt.playing = true; gt.bpm = t.bpm > 0.0 ? t.bpm : 120.0; gt.ppq = t.ppq; }
        else gt.bpm = t.bpm > 0.0 ? t.bpm : (sbpm > 0.0 ? sbpm : 120.0);
        grainSynth.process (L, R, n, gt);
    }

    // the sample instrument, through the Space knobs (warmth, echo, space, shimmer, nostalgia, atmosphere,
    // output), its level on the Melody knob, and the Texture knobs shaping how it plays
    if (samplerOn.load() && ! toGrains && (size_t) n <= smpL.size())
    {
        auto v = [this] (const char* id) { return params.getRawParameterValue (id)->load(); };
        sampler.startFrac = juce::jmax (0.0f, v ("position") - 0.3f) / 0.7f;          // (the default, 30%, starts at the start)
        sampler.driftCents = v ("texture") * 10.0f;
        sampler.sparkle = juce::jmax (0.0f, v ("sparkle") - 0.3f) * 0.6f;
        sampler.releaseSec = v ("grain") / 1000.0f * 1.3f;
        if (! chordMode)
            for (auto& k : keyEvents)
                k.on ? sampler.noteOn (k.note, k.velocity, k.offset) : sampler.noteOff (k.note);
        for (auto* b : { &smpL, &smpR, &smpSendL, &smpSendR }) std::fill (b->begin(), b->begin() + n, 0.0f);
        sampler.process (smpL.data(), smpR.data(), n, getSampleRate());
        const float level = v ("drops") / 0.7f;
        for (int i = 0; i < n; ++i) { smpL[(size_t) i] *= level; smpR[(size_t) i] *= level; smpSendL[(size_t) i] = smpL[(size_t) i]; smpSendR[(size_t) i] = smpR[(size_t) i]; }
        lp::SampleFxSettings fs;
        fs.warmth = v ("warmth"); fs.echo = v ("echo"); fs.space = v ("space"); fs.shimmer = v ("shimmer");
        fs.nostalgia = v ("nostalgia"); fs.atmos = v ("atmos"); fs.gainDb = v ("gain");
        fs.bpm = t.bpm > 0.0 ? t.bpm : 120.0;
        samplerFx.process (smpL.data(), smpR.data(), smpSendL.data(), smpSendR.data(), n, fs);
        for (int i = 0; i < n; ++i) { L[i] += smpL[(size_t) i]; R[i] += smpR[(size_t) i]; }
    }

    playAudition (L, R, n); // (a note being drawn in Draw & sing)
    // a drum you clicked in the editor (when the loop isn't playing it already)
    if (! auditioned)
    {
        // notes played into an open track (its drum grid or note editor, from your keyboard or a click): heard
        // through that track, so its meter moves and its volume, pan and mute count
        const int dk = drumKeysTrack.load() >= 0 ? drumKeysTrack.load() : midiArmTrack.load();
        if (dk >= 0 && dk < StudioPlayer::maxTracks && (size_t) n <= liveL.size())
        {
            std::fill (liveL.begin(), liveL.begin() + n, 0.0f);
            std::fill (liveR.begin(), liveR.begin() + n, 0.0f);
            liveDrums.process (liveL.data(), liveR.data(), n, getSampleRate(), false, 0.0, 0.0, 1.0f);
            const auto& m = studio.mix[(size_t) dk];
            const float g = m.mute.load() ? 0.0f : juce::Decibels::decibelsToGain (m.gainDb.load());
            const float p = m.pan.load();
            const float gl = g * std::min (1.0f, 1.0f - p), gr = g * std::min (1.0f, 1.0f + p);
            float pkL = 0.0f, pkR = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                const float l = liveL[(size_t) i] * gl, r = liveR[(size_t) i] * gr;
                L[i] += l; R[i] += r;
                pkL = std::max (pkL, std::abs (l)); pkR = std::max (pkR, std::abs (r));
            }
            const float mg = studio.meterGain.load();
            lp::ui::notePeak (studio.peakL[(size_t) dk], pkL * mg);
            lp::ui::notePeak (studio.peakR[(size_t) dk], pkR * mg);
        }
        else liveDrums.process (L, R, n, getSampleRate(), false, 0.0, 0.0, 1.0f);
    }
    // the chop pads you play
    if (const int pt = padsTrack.load(); pt >= 0)
    {
        const auto& m = studio.mix[(size_t) juce::jlimit (0, StudioPlayer::maxTracks - 1, pt)];
        livePads.process (L, R, n, getSampleRate(), false, 0.0, 0.0, juce::Decibels::decibelsToGain (m.gainDb.load()));
    }

    // the master meter: what comes out
    {
        float pl = 0.0f, pr = 0.0f;
        for (int i = 0; i < n; ++i) { pl = std::max (pl, std::abs (L[i])); pr = std::max (pr, std::abs (R[i])); }
        lp::ui::notePeak (outPeakL, pl);
        lp::ui::notePeak (outPeakR, pr);
    }
    studio.meterGain = juce::Decibels::decibelsToGain (songMakeupDb.load());

    // Send the notes out so they can play any other instrument too
    for (int i = 0; i < midiOut.count; ++i)
    {
        const auto& e = midiOut.events[(size_t) i];
        midi.addEvent (e.on ? juce::MidiMessage::noteOn (e.channel, e.note, juce::jlimit (0.01f, 1.0f, e.velocity))
                            : juce::MidiMessage::noteOff (e.channel, e.note),
                       juce::jlimit (0, n - 1, e.offset));
    }
}

//==============================================================================

// what the master was made from: the song version and every track's mixer (when it changes, the file is old)
juce::String LivePlanetProcessor::masterKeyNow (int preset)
{
    const auto st = songJob.getStatus();
    juce::String k;
    k << preset << "|" << st.version << "|" << st.files.folder.getFullPathName();
    for (size_t t = 0; t < st.tracks.size() && t < (size_t) StudioPlayer::maxTracks; ++t)
    {
        const auto& m = studio.mix[t];
        for (float v : { m.gainDb.load(), m.pan.load(), m.tone.load(), m.space.load(), m.echo.load(), m.warmth.load(),
                         m.nostalgia.load(), m.chorus.load(), m.mute.load() ? 1.0f : 0.0f, m.solo.load() ? 1.0f : 0.0f, m.fxBypass.load() ? 1.0f : 0.0f })
            k << "|" << juce::String (v, 3);
    }
    k << "|pump " << studio.pumpDepth.load() << "|feel " << studio.bright.load() << " " << studio.sparse.load() << " " << studio.punch.load() << " " << (int) studio.autoMix.load();
    return k;
}

// works out the master for the song as you hear it now, and writes the mastered file (any thread)
bool LivePlanetProcessor::masterNow (int preset)
{
    int count = 0;
    const auto* presets = lp::masterPresets (count);
    if (preset < 0 || preset >= count) return false;
    const auto st = songJob.getStatus();
    if (! st.haveSong) return false;
    const auto key = masterKeyNow (preset);
    juce::Array<juce::File> stems;
    for (auto& t : st.tracks) stems.add (t.stem);
    const auto p = presets[preset];
    juce::ScopedNoDenormals nd;
    auto song = StudioPlayer::bounce (stems, studio.mix, st.song.bpm, &studio);
    if (song.getNumSamples() == 0) return false;
    const double sr = StudioPlayer::fileRate;
    // where the glue starts: a little under the song's loud parts (its 90th-percentile loudness)
    std::vector<float> lv;
    const int win = (int) (0.4 * sr);
    for (int i = 0; i + win <= song.getNumSamples(); i += win)
        lv.push_back (juce::Decibels::gainToDecibels (0.5f * (song.getRMSLevel (0, i, win) + song.getRMSLevel (1, i, win)), -100.0f));
    std::sort (lv.begin(), lv.end());
    const float glue = lv.empty() ? -18.0f : lv[(size_t) (lv.size() * 9 / 10)] - 3.0f;
    float lufs = 0.0f, peak = 0.0f;
    const float gain = lp::Master::fit (song, sr, p, glue, lufs, peak);
    lp::Master m;
    m.glueThreshDb = glue;
    m.prepare (sr, p.ceilingDb);
    m.process (song.getWritePointer (0), song.getWritePointer (1), song.getNumSamples(), gain);
    const int lat = m.latency();
    juce::AudioBuffer<float> out (2, song.getNumSamples() - lat);
    for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, 0, song, ch, lat, out.getNumSamples());
    const auto file = st.files.folder.getChildFile (st.song.title + " - master (" + juce::String (p.name).replace (" / ", " ") + ").wav");
    file.deleteFile();
    juce::WavAudioFormat wav;
    if (std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (file), sr, 2, 24, {}, 0)); w != nullptr)
        w->writeFromAudioSampleBuffer (out, 0, out.getNumSamples());
    masterGlueDb = glue;
    masterGainDb = gain;
    masterPreset = preset;
    const juce::SpinLock::ScopedLockType sl (masterLock);
    masterFile = file;
    masterKey = key;
    masterReport = juce::String (p.name) + ": " + juce::String (lufs, 1) + " LUFS, peak " + juce::String (peak, 1) + " dB";
    return true;
}

void LivePlanetProcessor::startMaster (int preset)
{
    if (preset < 0) { masterPreset = -1; const juce::SpinLock::ScopedLockType sl (masterLock); masterReport = {}; return; }
    if (mastering.load() || ! songJob.getStatus().haveSong) return;
    mastering = true;
    ++printsRunning; // (the plugin waits for this before closing)
    juce::Thread::launch ([this, preset] { masterNow (preset); mastering = false; --printsRunning; });
}

// the mastered file of the song as you hear it right now (made again first if you've changed anything)
juce::File LivePlanetProcessor::masteredFile()
{
    const int preset = masterPreset.load();
    if (preset < 0) return {};
    const auto key = masterKeyNow (preset);
    {
        const juce::SpinLock::ScopedLockType sl (masterLock);
        if (key == masterKey && masterFile.existsAsFile()) return masterFile;
    }
    while (mastering.load()) juce::Thread::sleep (50); // one already on its way
    mastering = true;
    masterNow (preset);
    mastering = false;
    const juce::SpinLock::ScopedLockType sl (masterLock);
    return masterFile;
}

void LivePlanetProcessor::startDrumLoop (int track, double startBeat, double beats)
{
    setDrumLoop (startBeat, beats);
    liveTrack = track;
    studio.skipTrack = track;
    previewPos = startBeat * 60.0 / songBpm.load() * StudioPlayer::fileRate;
    previewPlaying = true;
}

void LivePlanetProcessor::setDrumLoop (double startBeat, double beats)
{
    loopStartBeat = startBeat;
    loopBeats = beats;
}

void LivePlanetProcessor::stopDrumLoop()
{
    if (liveTrack.load() < 0) return;
    previewPlaying = false;
    liveTrack = -1;
    studio.skipTrack = -1;
}

void LivePlanetProcessor::searchCity (const juce::String& name)
{
    weatherService.requestCity (name);
    const juce::SpinLock::ScopedLockType sl (skyLock);
    statusMessage = "Looking up " + name + "...";
}

void LivePlanetProcessor::timerCallback()
{
    if (! isTimerRunning()) return;
    // the app opens with the song you were last on, unless its saved state brought one back
    // (in a DAW a new Loopwright starts fresh: SONGS > Open Recent Song opens any of them)
    if (wrapperType == wrapperType_Standalone && ! lastSongChecked && ++startTicks >= 4)
    {
        lastSongChecked = true;
        const auto st = songJob.getStatus();
        auto path = lp::appFolder().getChildFile ("last-song.txt").loadFileAsString().trim();
        if (juce::File::isAbsolutePath (path)) path = SongJob::followRenames (juce::File (path)).getFullPathName();
        if (! st.haveSong && st.stage == SongJob::Stage::Idle && ! restoredFromProject && juce::File::isAbsolutePath (path)
            && juce::File (path).getChildFile ("song.json").existsAsFile())
            songJob.openSong (juce::File (path));
    }
    {
        double bpm = 90.0;
        songJob.takeFinishedMix (bpm); // (the studio plays the tracks themselves now)
        const auto st = songJob.getStatus();
        // your DAW's tempo isn't the song's (and the song follows your DAW): once it has stayed there a
        // moment, the song is recorded again at that tempo. Until then it's played faster or slower to keep
        // in time, which also moves its pitch; a song recorded at the DAW's tempo plays (and drags) in its key.
        {
            const double daw = dawBpm.load(), sb = st.haveSong ? st.song.bpm : 0.0;
            const bool follow = params.getRawParameterValue ("dawtempo")->load() > 0.5f;
            const bool off = st.haveSong && follow && daw > 30.0 && daw < 300.0 && std::abs (daw - sb) > 0.05;
            if (! off) tempoOffTicks = 0;
            else if (++tempoOffTicks > 3 && ! st.editing && st.stage != SongJob::Stage::Rendering && std::abs (daw - askedBpm) > 0.05)
            {
                askedBpm = daw;
                songJob.setSongBpm (std::round (daw * 100.0) / 100.0);
            }
            if (! off) askedBpm = 0.0;
        }
        if (st.mixInsertSerial != seenMixInserts)
        {
            // an undone delete: its track comes back, and so does a row in the mixer for it
            seenMixInserts = st.mixInsertSerial;
            studio.insertTrackMix (st.mixInsertAt);
        }
        // a renamed song is the same song in a new folder: it keeps its mixer
        if (st.haveSong && st.renamedTo == st.files.folder && loadedSongFolder == st.renamedFrom && loadedSongFolder != st.renamedTo)
        {
            loadedSongFolder = st.renamedTo;
            lp::appFolder().getChildFile ("last-song.txt").replaceWithText (st.renamedTo.getFullPathName());
        }
        // the vocal chains play live in the studio: give it the song's (when a song opens, or a chain changes)
        if (st.haveSong && (st.vocalSerial != seenVocalSerial || st.version != loadedStudioVersion))
        {
            seenVocalSerial = st.vocalSerial;
            // (from the song itself: a section change or a new arrangement keeps each vocal's chain)
            for (int i = 0; i < StudioPlayer::maxTracks; ++i)
                studio.setVocalChain (i, songJob.vocalChainOf (i));
        }
        if (st.haveSong && st.version != loadedStudioVersion)
        {
            loadedStudioVersion = st.version;
            juce::Array<juce::File> stems;
            for (auto& t : st.tracks) stems.add (t.stem);
            // a new song (not just an edited track): every track starts with its own effects, by role,
            // and for songs made of your samples the master knobs start at zero (turn them up if you like)
            bool newSong = st.files.folder != loadedSongFolder;
            loadedSongFolder = st.files.folder;
            // remember the song you're on right away: the app brings it back even after a crash
            if (newSong)
                lp::appFolder().getChildFile ("last-song.txt").replaceWithText (st.files.folder.getFullPathName());
            juce::ValueTree restore;
            if (newSong)
            {
                const juce::SpinLock::ScopedLockType sl (restoreLock);
                if (restoredMixFolder == st.files.folder) { restore = restoredMix; restoredMixFolder = juce::File(); }
            }
            bool anySamples = false;
            for (size_t i = 0; i < st.tracks.size() && i < (size_t) StudioPlayer::maxTracks; ++i)
            {
                auto& m = studio.mix[i];
                const auto& tr = st.tracks[i];
                m.sample = tr.sample;
                m.group = tr.group;
                anySamples = anySamples || tr.sample;
                if (newSong && restore.isValid() && restore.getChild ((int) i).isValid())
                {
                    const auto t = restore.getChild ((int) i);
                    m.gainDb = (float) t["gain"]; m.pan = (float) t["pan"]; m.tone = (float) t["tone"];
                    m.mute = (bool) t["mute"]; m.solo = (bool) t["solo"]; m.space = (float) t["space"];
                    m.echo = (float) t["echo"]; m.warmth = (float) t["warmth"]; m.nostalgia = (float) t["nostalgia"];
                    m.chorus = (float) t["chorus"]; m.printFx = (bool) t["printFx"]; m.dragDry = (bool) t["dragDry"]; m.fxBypass = (bool) t["fxBypass"];
                }
                else if (newSong)
                {
                    m.space = tr.space; m.echo = tr.echo; m.warmth = tr.warmth; m.nostalgia = tr.nostalgia;
                    m.gainDb = 0.0f; m.pan = 0.0f; m.tone = 0.0f; m.mute = false; m.solo = false; m.printFx = false; m.dragDry = false; m.fxBypass = false;
                }
            }
            if (newSong) { masterPreset = -1; ++masterResets; } // a new song needs its own master
            if (newSong && anySamples && ! restore.isValid()) // (a reopened project keeps its knobs)
                for (auto id : { "warmth", "echo", "space", "shimmer", "nostalgia", "atmos" })
                    if (auto* prm = params.getParameter (id)) prm->setValueNotifyingHost (0.0f);
            songBpm = st.song.bpm;
            songMakeupDb = st.song.playbackMakeupDb;
            setUpFeel (st);
            studio.load (stems);
            studio.setKicks (songJob.kickBeats(), st.song.bpm); // the pump follows the kick
        }
        static bool scannedOnce = false;
        if (! scannedOnce && ! SongJob::libraryCache().existsAsFile())
        {
            scannedOnce = true;
            rescanLibrary(); // first time: look for Splice and any sample folders
        }
    }
    // for testing: LP_EXPORT_TEST=1 writes the recorded notes to a MIDI file once 10 seconds have played
    if (std::getenv ("LP_EXPORT_TEST") != nullptr && ! exportTested && recordedOnCount.load() > 0 && ++exportTicks > 20)
    {
        exportTested = true;
        const auto f = exportRecentNotes (8);
        std::printf ("LP_EXPORT_TEST %s %d\n", f.getFullPathName().toRawUTF8(), (int) f.getSize());
        std::fflush (stdout);
    }
    WeatherService::Result r;
    if (! weatherService.takeResult (r)) return;

    const juce::SpinLock::ScopedLockType sl (skyLock);
    if (r.ok)
    {
        city = r.city;
        if (r.country.isNotEmpty() || r.city != city) country = r.country;
        lat = r.lat;
        lon = r.lon;
        liveWeather = r.weather;
        haveLive = true;
        fetchedAt = r.fetchedAt;
        statusMessage = {};
        citySeed = lp::hashString (city);
    }
    else
    {
        statusMessage = r.error;
    }
}

LivePlanetProcessor::SkyStatus LivePlanetProcessor::getSkyStatus() const
{
    const juce::SpinLock::ScopedLockType sl (skyLock);
    SkyStatus s;
    s.city = city;
    s.country = country;
    s.message = statusMessage;
    s.live = params.getRawParameterValue ("live")->load() > 0.5f;
    s.haveLive = haveLive;
    s.busy = weatherService.isBusy();
    s.fetchedAt = fetchedAt;
    s.lat = lat;
    s.lon = lon;
    return s;
}

// ---- sample instruments
namespace
{
    // the words sample makers use for how hard a recording was played
    int velocityWord (const juce::String& token)
    {
        const auto t = token.toLowerCase();
        if (t == "soft" || t == "pp" || t == "p" || t == "quiet" || t == "light" || t == "v1" || t == "vel1") return 1;
        if (t == "med" || t == "medium" || t == "mp" || t == "mf" || t == "mid" || t == "v2" || t == "vel2") return 2;
        if (t == "hard" || t == "f" || t == "ff" || t == "loud" || t == "heavy" || t == "v3" || t == "vel3") return 3;
        if (t == "v4" || t == "vel4") return 4;
        return 0;
    }
    juce::String withoutVelocityWords (const juce::String& name)
    {
        auto t = juce::StringArray::fromTokens (name.replaceCharacters ("-. ", "___"), "_", "");
        t.removeEmptyStrings();
        juce::StringArray keep;
        for (auto& w : t) if (velocityWord (w) == 0) keep.add (w.toLowerCase());
        return keep.joinIntoString ("_");
    }
}

bool LivePlanetProcessor::loadSamplerInstrument (const juce::File& file)
{
    if (! file.existsAsFile()) return false;
    // the same sound recorded softer / harder, next to it (e.g. Keys_C3_soft / Keys_C3_hard)
    juce::Array<juce::File> group { file };
    const auto base = withoutVelocityWords (file.getFileNameWithoutExtension());
    for (auto& f : file.getParentDirectory().findChildFiles (juce::File::findFiles, false, "*.wav;*.aif;*.aiff;*.flac"))
        if (f != file && withoutVelocityWords (f.getFileNameWithoutExtension()) == base) group.add (f);
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    struct Loaded { lp::Sampler::Layer layer; float rms; };
    std::vector<Loaded> loaded;
    double root = 0.0;
    for (auto& f : group)
    {
        std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (f));
        if (rd == nullptr) continue;
        const int len = (int) std::min<juce::int64> (rd->lengthInSamples, (juce::int64) (rd->sampleRate * 20.0));
        if (len < 256) continue;
        auto buf = std::make_shared<juce::AudioBuffer<float>> (2, len);
        rd->read (buf.get(), 0, len, 0, true, rd->numChannels > 1);
        if (rd->numChannels == 1) buf->copyFrom (1, 0, *buf, 0, 0, len);
        // its pitch: from the loudest part of the first (main) recording
        if (root <= 0.0)
        {
            std::vector<float> mono ((size_t) len);
            for (int i = 0; i < len; ++i) mono[(size_t) i] = 0.5f * (buf->getSample (0, i) + buf->getSample (1, i));
            const double hz = lp::sources::detectPitch (mono, rd->sampleRate);
            root = hz > 0.0 ? 69.0 + 12.0 * std::log2 (hz / 440.0) : 60.0;
        }
        const float rms = 0.5f * (buf->getRMSLevel (0, 0, len) + buf->getRMSLevel (1, 0, len));
        loaded.push_back ({ { buf, rd->sampleRate, root, 0.0f, 1.0f }, rms });
    }
    if (loaded.empty()) return false;
    // quiet to loud, each taking an equal share of how hard you can play; all at the same level
    // (the velocity sets the loudness; the layers give the character)
    std::sort (loaded.begin(), loaded.end(), [] (const Loaded& a, const Loaded& b) { return a.rms < b.rms; });
    std::vector<lp::Sampler::Layer> layers;
    float peakAll = 0.0f;
    for (auto& l : loaded) peakAll = std::max (peakAll, l.layer.buf->getMagnitude (0, l.layer.buf->getNumSamples()));
    for (size_t i = 0; i < loaded.size(); ++i)
    {
        auto l = loaded[i].layer;
        l.velLo = (float) i / (float) loaded.size();
        l.velHi = (float) (i + 1) / (float) loaded.size();
        const float pk = l.buf->getMagnitude (0, l.buf->getNumSamples());
        if (pk > 0.0f) l.buf->applyGain (0.8f / pk);
        layers.push_back (l);
    }
    const int n = (int) layers.size();
    {
        std::vector<std::shared_ptr<juce::AudioBuffer<float>>> bufs { layers.back().buf }; // the hardest layer shows the sound best
        setSamplerPeaks (bufs);
    }
    sampler.setLayers (std::move (layers));
    samplerOn = true;
    const juce::SpinLock::ScopedLockType sl (samplerInfoLock);
    samplerName = SongJob::niceName (file.getFileNameWithoutExtension());
    samplerInfo = (n > 1 ? juce::String (n) + " velocity layers" : juce::String ("1 layer, softer plays darker")) + ", recorded at "
                + lp::noteName (((int) std::round (root)) % 12) + juce::String ((int) std::round (root) / 12 - 1);
    samplerPaths = juce::StringArray (file.getFullPathName());
    return true;
}

int LivePlanetProcessor::loadVocalChops (const juce::File& file)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (file));
    if (rd == nullptr) return 0;
    const int len = (int) std::min<juce::int64> (rd->lengthInSamples, (juce::int64) (rd->sampleRate * 120.0));
    juce::AudioBuffer<float> all (2, len);
    rd->read (&all, 0, len, 0, true, rd->numChannels > 1);
    if (rd->numChannels == 1) all.copyFrom (1, 0, all, 0, 0, len);
    // with a song: the recording is tuned to its key first, so every chop is in tune
    if (songJob.hasSong())
    {
        const auto [key, minor] = songJob.songKey();
        all = lp::song::tune::apply (all, rd->sampleRate, key, minor, 1.0f, 25.0f);
    }
    const auto slices = lp::sliceByOnsets (all, rd->sampleRate, 24);
    if (slices.empty()) return 0;
    std::vector<lp::Sampler::Layer> layers;
    float peak = all.getMagnitude (0, len);
    int key = 48; // C3 and up, one chop per key (black keys too)
    for (auto [start, n] : slices)
    {
        auto buf = std::make_shared<juce::AudioBuffer<float>> (2, n);
        for (int ch = 0; ch < 2; ++ch) buf->copyFrom (ch, 0, all, ch, start, n);
        // short fades so nothing clicks
        const int f = std::min (n / 4, (int) (0.004 * rd->sampleRate)), fo = std::min (n / 3, (int) (0.02 * rd->sampleRate));
        buf->applyGainRamp (0, f, 0.0f, 1.0f);
        buf->applyGainRamp (n - fo, fo, 1.0f, 0.0f);
        if (peak > 0.0f) buf->applyGain (0.85f / peak);
        lp::Sampler::Layer l { buf, rd->sampleRate, (double) key, 0.0f, 1.0f, key, key, true, juce::String ((int) layers.size() + 1) };
        layers.push_back (l);
        ++key;
    }
    const int count = (int) layers.size();
    {
        std::vector<std::shared_ptr<juce::AudioBuffer<float>>> bufs;
        for (auto& l : layers) bufs.push_back (l.buf);
        setSamplerPeaks (bufs);
    }
    sampler.setLayers (std::move (layers));
    samplerOn = true;
    const juce::SpinLock::ScopedLockType sl (samplerInfoLock);
    samplerName = "chops of " + file.getFileNameWithoutExtension();
    samplerInfo = juce::String (count) + " chops on C3 to " + lp::noteName ((48 + count - 1) % 12) + juce::String ((48 + count - 1) / 12 - 1)
                + ": play them in Play mode";
    samplerPaths = juce::StringArray ("chops:" + file.getFullPathName());
    return count;
}

void LivePlanetProcessor::setSamplerPeaks (const std::vector<std::shared_ptr<juce::AudioBuffer<float>>>& bufs)
{
    // all of them in a row (chops: one after the other), 240 columns
    std::vector<float> all;
    for (auto& b : bufs)
        for (int i = 0; i < b->getNumSamples(); i += 8) all.push_back (std::abs (b->getSample (0, i)));
    std::vector<float> pk (240, 0.0f);
    for (size_t i = 0; i < all.size(); ++i) pk[i * pk.size() / all.size()] = std::max (pk[i * pk.size() / all.size()], all[i]);
    float top = 0.0f;
    for (float v : pk) top = std::max (top, v);
    if (top > 0.0f) for (auto& v : pk) v /= top;
    const juce::SpinLock::ScopedLockType sl (samplerInfoLock);
    samplerPeaks = pk;
    ++samplerVersion;
}

void LivePlanetProcessor::startRecording()
{
    recPos = 0;
    recording = true;
}

// stops, saves the take (Music/Loopwright/Recordings) and chops it. Returns the number of chops
// (0 = nothing usable came in: the message says why)
int LivePlanetProcessor::stopRecording (juce::String& message)
{
    recording = false;
    const int len = recPos.load();
    if (len < (int) (recRate * 0.3)) { message = "too short: record at least a second"; return 0; }
    const float pk = std::max (recBuf.getMagnitude (0, 0, len), recBuf.getMagnitude (1, 0, len));
    if (pk < 0.003f)
    {
        message = wrapperType == wrapperType_Standalone ? "no sound came in: Options > Audio settings, pick your mic and untick Mute audio input"
                                                         : "no sound came in: set Loopwright's sidechain (audio from) to your mic's track";
        return 0;
    }
    const auto folder = lp::appFolder().getChildFile ("Recordings");
    folder.createDirectory();
    const auto file = folder.getChildFile ("vocal " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H.%M.%S") + ".wav");
    juce::AudioBuffer<float> take (2, len);
    for (int ch = 0; ch < 2; ++ch) take.copyFrom (ch, 0, recBuf, ch, 0, len);
    take.applyGain (0.9f / pk); // a good level, however loud you sang
    juce::WavAudioFormat wav;
    if (std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (file), recRate, 2, 24, {}, 0)); w != nullptr)
        w->writeFromAudioSampleBuffer (take, 0, len);
    const int chops = loadVocalChops (file);
    message = chops > 0 ? juce::String (chops) + " chops from your recording (saved in Music > Loopwright > Recordings)"
                        : juce::String ("couldn't find syllables in it: try singing with short gaps");
    return chops;
}

void LivePlanetProcessor::samplerOff()
{
    samplerOn = false;
    sampler.allOff();
    const juce::SpinLock::ScopedLockType sl (samplerInfoLock);
    samplerName = {}; samplerInfo = {}; samplerPaths.clear();
}

bool LivePlanetProcessor::loadSample (const juce::File& file)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
    if (reader == nullptr) return false;

    const auto maxLen = (juce::int64) (reader->sampleRate * 30.0);
    const int len = (int) std::min (reader->lengthInSamples, maxLen);
    if (len < 4096) return false;

    juce::AudioBuffer<float> buf ((int) reader->numChannels, len);
    reader->read (&buf, 0, len, 0, true, true);

    auto s = std::make_unique<lp::SourceSound>();
    s->name = file.getFileNameWithoutExtension();
    s->sampleRate = reader->sampleRate;
    s->data.resize ((size_t) len);
    float peak = 0.0f;
    for (int i = 0; i < len; ++i)
    {
        float v = 0.0f;
        for (int ch = 0; ch < buf.getNumChannels(); ++ch) v += buf.getSample (ch, i);
        v /= (float) buf.getNumChannels();
        s->data[(size_t) i] = v;
        peak = std::max (peak, std::abs (v));
    }
    if (peak > 0.0f)
        for (auto& v : s->data) v *= 0.9f / peak;

    // remove any offset and soften the very start and end
    double mean = 0;
    for (auto v : s->data) mean += v;
    mean /= (double) len;
    const int fade = std::min (len / 4, (int) (0.01 * reader->sampleRate));
    for (int i = 0; i < len; ++i)
    {
        float g = 1.0f;
        if (i < fade) g = (float) i / (float) fade;
        if (i > len - fade) g = (float) (len - i) / (float) fade;
        s->data[(size_t) i] = (float) ((s->data[(size_t) i] - mean) * g);
    }

    // find the sample's own pitch so it is played in tune and near its natural register
    const double f0 = lp::sources::detectPitch (s->data, reader->sampleRate);
    if (f0 > 0.0)
    {
        s->rootHz = f0;
        s->foldSemis = 8.0f;
        const int midi = juce::roundToInt (69.0 + 12.0 * std::log2 (f0 / 440.0));
        sampleInfo = juce::String ("pitch ") + lp::noteName (midi) + juce::String (midi / 12 - 1);
    }
    else
    {
        s->rootHz = 261.6256;
        s->foldSemis = 6.0f;   // no clear pitch: only nudge it gently
        sampleInfo = "texture (no clear pitch)";
    }

    engine.setUserSource (s.get());
    loadedSamples.push_back (std::move (s));
    while (loadedSamples.size() > 3) loadedSamples.pop_front(); // older ones are long out of use

    samplePath = file.getFullPathName();
    sampleName = file.getFileNameWithoutExtension();
    {
        // a small overview of the waveform for the screen
        std::vector<float> peaks (400, 0.0f);
        const auto& d = loadedSamples.back()->data;
        for (size_t i = 0; i < d.size(); ++i)
        {
            auto& pk = peaks[std::min<size_t> (399, i * 400 / d.size())];
            pk = std::max (pk, std::abs (d[i]));
        }
        const juce::SpinLock::ScopedLockType sl (peaksLock);
        samplePeaks = std::move (peaks);
    }
    for (auto id : { "smpStart", "smpEnd", "smpTune" })
        if (auto* q = params.getParameter (id))
            q->setValueNotifyingHost (q->getDefaultValue());
    if (auto* p = params.getParameter ("sound"))
        p->setValueNotifyingHost (p->convertTo0to1 ((float) kSampleChoice));
    return true;
}

//==============================================================================

// what the song's feel needs to know: which tracks are voices (the auto mix makes room for them), and, for
// "sparse", which tracks step back in the quieter sections (all but the core: drums, bass, one chord or melody part)
void LivePlanetProcessor::setUpFeel (const SongJob::Status& st)
{
    const auto& song = st.song;
    const int bars = std::max (1, song.totalBars());
    bool anyVocal = false;
    std::vector<int> melodic;
    for (size_t i = 0; i < st.tracks.size() && i < (size_t) StudioPlayer::maxTracks; ++i)
    {
        const auto nm = st.tracks[i].name.toLowerCase();
        const bool v = nm.contains ("vocal") || nm.contains ("vox") || nm.contains ("voice") || nm.contains ("chops") || nm.contains ("choir");
        studio.mix[i].vocal = v;
        anyVocal = anyVocal || (v && ! nm.contains ("choir"));
        const int g = st.tracks[i].group;
        if ((g == 1 || g == 2 || nm.contains ("perc")) && ! v) melodic.push_back ((int) i);
    }
    studio.setAnyVocal (anyVocal);
    // does track i play in bars [a, b)?
    auto plays = [&] (int i, int a, int b)
    {
        if (i >= (int) song.parts.size()) return false;
        const auto& p = song.parts[(size_t) i];
        if (! p.barSrc.empty())
        {
            for (int x = a; x < b && x < (int) p.barSrc.size(); ++x) if (p.barSrc[(size_t) x] >= 0) return true;
            return false;
        }
        if (! p.notes.empty())
        {
            for (auto& n : p.notes) if (n.beat >= a * 4.0 && n.beat < b * 4.0) return true;
            return false;
        }
        for (int s0 : p.loopStartBars) if (s0 < b && s0 + std::max (1, p.loopBars) > a) return true;
        return false;
    };
    std::vector<std::vector<unsigned char>> map (st.tracks.size(), std::vector<unsigned char> ((size_t) bars, 0));
    for (auto& sec : song.sections)
    {
        if (sec.energy >= 0.62f) continue; // the big sections keep everything
        const int a = sec.startBar, b = sec.startBar + sec.bars;
        bool kept = false;
        for (int i : melodic)
        {
            if (! plays (i, a, b)) continue;
            const bool perc = st.tracks[(size_t) i].name.toLowerCase().contains ("perc");
            if (! kept && ! perc) { kept = true; continue; } // the core part stays
            for (int x = a; x < b && x < bars; ++x) map[(size_t) i][(size_t) x] = 1;
        }
    }
    studio.setSparseMap (std::move (map), song.bpm);
}

juce::File LivePlanetProcessor::fileToDrag (int t)
{
    const auto st = songJob.getStatus();
    if (! st.haveSong || t < 0 || t >= (int) st.tracks.size()) return {};
    const auto stem = st.tracks[(size_t) t].stem;
    // the song's feel (bright, sparse, punch) is part of what you hear, so it's part of what you drag
    const bool feel = std::abs (studio.bright.load()) > 0.01f || studio.sparse.load() > 0.01f || studio.punch.load() > 0.01f;
    if (t >= StudioPlayer::maxTracks || studio.mix[(size_t) t].dragDry.load() || ! (studio.mix[(size_t) t].printsFx() || (feel && ! studio.mix[(size_t) t].fxBypass.load())))
        return stem;
    const std::lock_guard<std::mutex> lk (printLock);
    const auto& m = studio.mix[(size_t) t];
    // one file per sound of the track: the same settings drag the same file without recording again
    juce::String key;
    for (float v : { m.gainDb.load(), m.pan.load(), m.tone.load(), m.space.load(), m.echo.load(),
                     m.warmth.load(), m.nostalgia.load(), m.chorus.load() })
        key << juce::String (v, 3) << ",";
    key << stem.getLastModificationTime().toMilliseconds() << "," << songBpm << "," << studio.pumpDepth.load()
        << "," << studio.bright.load() << "," << studio.sparse.load() << "," << studio.punch.load() << "," << (int) studio.autoMix.load() << "," << t;
    {
        // its Vocal chain (a different chain is a different print)
        const auto vp = studio.vocalParamsOf (t);
        key << "," << vp.fingerprint().hashCode64();
    }
    // (a folder per sound, so the file keeps the track's plain name when it lands in Ableton)
    const auto printed = st.files.folder.getChildFile ("printed");
    const auto out = printed.getChildFile (juce::String::toHexString (key.hashCode64()).substring (0, 8))
                            .getChildFile (stem.getFileNameWithoutExtension() + " fx.wav");
    if (out.existsAsFile()) return out;
    for (auto& old : printed.findChildFiles (juce::File::findFiles, true, out.getFileName()))
        old.getParentDirectory().deleteRecursively(); // an older print of this track
    return StudioPlayer::printTrack (stem, m, songBpm, out, &studio, t) ? out : stem;
}

void LivePlanetProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = params.copyState();
    {
        const juce::SpinLock::ScopedLockType sl (skyLock);
        state.setProperty ("city", city, nullptr);
        state.setProperty ("country", country, nullptr);
        state.setProperty ("lat", lat, nullptr);
        state.setProperty ("lon", lon, nullptr);
    }
    state.setProperty ("samplePath", samplePath, nullptr);
    {
        const juce::SpinLock::ScopedLockType sl (samplerInfoLock);
        state.setProperty ("samplerPath", samplerOn.load() && ! samplerPaths.isEmpty() ? samplerPaths[0] : juce::String(), nullptr);
    }
    state.setProperty ("theme", themeIndex.load(), nullptr);
    state.setProperty ("preset", currentPreset.load(), nullptr);
    // grains: its sound and everything set on its page
    state.setProperty ("grainPath", grainSource != nullptr ? grainSource->path : juce::String(), nullptr);
    state.setProperty ("grainSettings", juce::JSON::toString (lp::grains::toVar (grainSettings), true), nullptr);
    // the song you made in this project, and each track's mixer, so reopening the project brings it back
    {
        const auto st = songJob.getStatus();
        if (st.haveSong)
        {
            state.setProperty ("songFolder", st.files.folder.getFullPathName(), nullptr);
            state.setProperty ("pump", studio.pumpDepth.load(), nullptr);
            state.setProperty ("feelBright", studio.bright.load(), nullptr);
            state.setProperty ("feelSparse", studio.sparse.load(), nullptr);
            state.setProperty ("feelPunch", studio.punch.load(), nullptr);
            state.setProperty ("autoMix", studio.autoMix.load(), nullptr);
            juce::ValueTree mixTree ("studioMix");
            for (size_t i = 0; i < st.tracks.size() && i < (size_t) StudioPlayer::maxTracks; ++i)
            {
                const auto& m = studio.mix[i];
                juce::ValueTree t ("track");
                t.setProperty ("gain", m.gainDb.load(), nullptr); t.setProperty ("pan", m.pan.load(), nullptr);
                t.setProperty ("tone", m.tone.load(), nullptr); t.setProperty ("mute", m.mute.load(), nullptr);
                t.setProperty ("solo", m.solo.load(), nullptr); t.setProperty ("space", m.space.load(), nullptr);
                t.setProperty ("echo", m.echo.load(), nullptr); t.setProperty ("warmth", m.warmth.load(), nullptr);
                t.setProperty ("nostalgia", m.nostalgia.load(), nullptr); t.setProperty ("chorus", m.chorus.load(), nullptr);
                t.setProperty ("printFx", m.printFx.load(), nullptr);
                t.setProperty ("dragDry", m.dragDry.load(), nullptr);
                t.setProperty ("fxBypass", m.fxBypass.load(), nullptr);
                mixTree.appendChild (t, nullptr);
            }
            state.removeChild (state.getChildWithName ("studioMix"), nullptr);
            state.appendChild (mixTree, nullptr);
        }
    }
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void LivePlanetProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr) return;
    auto state = juce::ValueTree::fromXml (*xml);
    if (! state.isValid()) return;

    params.replaceState (state);
    themeIndex = (int) state.getProperty ("theme", 0);
    currentPreset = (int) state.getProperty ("preset", 0);
    {
        const juce::SpinLock::ScopedLockType sl (skyLock);
        city = state.getProperty ("city", city).toString();
        country = state.getProperty ("country", country).toString();
        lat = (double) state.getProperty ("lat", lat);
        lon = (double) state.getProperty ("lon", lon);
        citySeed = lp::hashString (city);
    }
    weatherService.requestCoordinates (city, lat, lon);

    if (const auto gs = state.getProperty ("grainSettings", "").toString(); gs.isNotEmpty())
    {
        grainSettings = lp::grains::fromVar (juce::JSON::parse (gs));
        sendGrainSettings();
    }
    if (const juce::File gp (state.getProperty ("grainPath", "").toString()); gp.existsAsFile()) loadGrainSound (gp);

    if (const auto sp = state.getProperty ("samplerPath", "").toString(); sp.startsWith ("chops:"))
    {
        if (juce::File (sp.substring (6)).existsAsFile()) loadVocalChops (juce::File (sp.substring (6)));
    }
    else if (juce::File (sp).existsAsFile())
        loadSamplerInstrument (juce::File (sp));

    // the song this project was using: open it again, with its mixer as you left it
    const auto savedFolder = state.getProperty ("songFolder", "").toString();
    const juce::File songFolder = juce::File::isAbsolutePath (savedFolder) ? SongJob::followRenames (juce::File (savedFolder)) : juce::File();
    if (songFolder.getChildFile ("song.json").existsAsFile())
    {
        restoredFromProject = true; // (the project's own song wins over "the last song")
        {
            const juce::SpinLock::ScopedLockType sl (restoreLock);
            restoredMix = state.getChildWithName ("studioMix").createCopy();
            restoredMixFolder = songFolder;
        }
        songJob.openSong (songFolder);
        studio.pumpDepth = (float) state.getProperty ("pump", 0.0f);
        studio.bright = (float) state.getProperty ("feelBright", 0.0f);
        studio.sparse = (float) state.getProperty ("feelSparse", 0.0f);
        studio.punch = (float) state.getProperty ("feelPunch", 0.0f);
        studio.autoMix = (bool) state.getProperty ("autoMix", true);
    }

    const juce::File f (state.getProperty ("samplePath", "").toString());
    if (f.existsAsFile())
        loadSample (f);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LivePlanetProcessor();
}

//==============================================================================
// Presets

int LivePlanetProcessor::getNumPrograms() { return lp::presets::count; }

const juce::String LivePlanetProcessor::getProgramName (int index)
{
    return lp::presets::all[juce::jlimit (0, lp::presets::count - 1, index)].name;
}

void LivePlanetProcessor::setCurrentProgram (int index)
{
    index = juce::jlimit (0, lp::presets::count - 1, index);
    const auto& preset = lp::presets::all[index];
    for (auto id : lp::presets::controlled)
    {
        auto* p = params.getParameter (id);
        if (p == nullptr) continue;
        float value = p->getDefaultValue();
        for (auto& [pid, v] : preset.values)
            if (juce::String (pid) == id)
                value = p->convertTo0to1 (v);
        p->beginChangeGesture();
        p->setValueNotifyingHost (value);
        p->endChangeGesture();
    }
    currentPreset = index;
}

//==============================================================================
// Recording the notes

juce::File LivePlanetProcessor::exportRecentNotes (int bars)
{
    std::vector<RecNote> notes;
    {
        const juce::SpinLock::ScopedLockType sl (recLock);
        notes = recorded;
    }
    if (notes.empty()) return {};

    double last = 0;
    for (auto& e : notes) last = std::max (last, e.beat);
    const double end = std::ceil ((last + 0.001) / 4.0) * 4.0;      // end of the bar
    const double start = std::max (std::floor (notes.front().beat / 4.0) * 4.0, end - bars * 4.0);

    constexpr int tpq = 960;
    juce::MidiMessageSequence seq;
    for (auto& e : notes)
    {
        if (e.beat < start || e.beat >= end) continue;
        const double ticks = (e.beat - start) * tpq;
        auto m = e.on ? juce::MidiMessage::noteOn (e.channel, e.note, juce::jlimit (0.05f, 1.0f, e.velocity))
                      : juce::MidiMessage::noteOff (e.channel, e.note);
        m.setTimeStamp (ticks);
        seq.addEvent (m);
    }
    seq.updateMatchedPairs();
    // notes still sounding at the end get closed there
    const double endTicks = (end - start) * tpq;
    for (int i = seq.getNumEvents(); --i >= 0;)
        if (auto* ev = seq.getEventPointer (i); ev->message.isNoteOn() && ev->noteOffObject == nullptr)
        {
            auto off = juce::MidiMessage::noteOff (ev->message.getChannel(), ev->message.getNoteNumber());
            off.setTimeStamp (endTicks);
            seq.addEvent (off);
        }
    // drop note-offs whose note-on was before the clip
    seq.updateMatchedPairs();
    for (int i = seq.getNumEvents(); --i >= 0;)
        if (auto* ev = seq.getEventPointer (i); ev->message.isNoteOff())
        {
            bool matched = false;
            for (int j = 0; j < i && ! matched; ++j)
                if (auto* on = seq.getEventPointer (j); on->noteOffObject == ev) matched = true;
            if (! matched) seq.deleteEvent (i, false);
        }
    if (seq.getNumEvents() == 0) return {};

    juce::MidiMessageSequence track;
    track.addEvent (juce::MidiMessage::textMetaEvent (3, "Loopwright"));
    track.addSequence (seq, 0.0);
    track.updateMatchedPairs();
    juce::MidiFile mf;
    mf.setTicksPerQuarterNote (tpq);
    mf.addTrack (track);

    juce::String cityName;
    {
        const juce::SpinLock::ScopedLockType sl (skyLock);
        cityName = city;
    }
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Loopwright");
    dir.createDirectory();
    auto file = dir.getChildFile (juce::File::createLegalFileName ("Loopwright - " + cityName + " - "
                                    + juce::String (lp::noteName (engine.uiKey.load())) + " "
                                    + lp::getScale ((lp::ScaleId) engine.uiScale.load()).name + ".mid"));
    file.deleteFile();
    juce::FileOutputStream os (file);
    if (! os.openedOk() || ! mf.writeTo (os)) return {};
    return file;
}

//==============================================================================
// Song mode

void LivePlanetProcessor::makeSong (bool full, bool newKey, int takes)
{
    lp::song::Options o;
    o.full = full;
    o.takes = takes;
    o.shiftKey = newKey;
    o.seed = (uint32_t) juce::Random::getSystemRandom().nextInt (1 << 30);
    o.style = (int) params.getRawParameterValue ("songstyle")->load() - 1;
    // a key you chose wins over the reference's (applied after the description, below)
    const int chosenKey = (int) params.getRawParameterValue ("songkey")->load() - 1;
    const int chosenMode = (int) params.getRawParameterValue ("songmode")->load();
    if (params.getRawParameterValue ("dawtempo")->load() > 0.5f && dawBpm.load() > 30.0)
        o.dawBpm = std::round (dawBpm.load() * 100.0) / 100.0;
    // "describe your song": genre, tempo, key, mood, instruments
    const auto pr = lp::song::parsePrompt (songPrompt);
    if (pr.any)
    {
        if (pr.genre != nullptr)
        {
            if (o.style < 0) o.style = (int) pr.genre->style; // the style switch still wins if you set it
            if (! songPromptIsAuto) // a description the plugin wrote never moves your tempo
            {
                o.genreLo = pr.genre->lo; o.genreHi = pr.genre->hi; o.genreBpm = pr.genre->typical;
            }
            for (auto* w : pr.genre->libraryWords) o.genreWords.add (w);
        }
        o.promptBpm = pr.bpm;
        o.promptKey = pr.key;
        o.promptMinor = pr.minor;
        o.promptMood = pr.mood;
        for (auto k : pr.instruments) o.wantKinds.push_back ((int) k);
        o.wish = pr.wish;
        if (pr.key >= 0) o.shiftKey = false;
        if (pr.sketch) o.full = false;
    }
    if (chosenKey >= 0 || chosenMode > 0)
    {
        const auto ref = songJob.getStatus().profile;
        o.promptKey = chosenKey >= 0 ? chosenKey : (ref.valid ? ref.key : 0);
        o.promptMinor = chosenMode > 0 ? chosenMode == 2 : (ref.valid ? ref.minor : true);
        o.shiftKey = false;
    }
    const int soundIdx = (int) params.getRawParameterValue ("sound")->load();
    o.mainSound = (soundIdx <= 0 || soundIdx >= kSampleChoice) ? lp::SoundId::Auto : (lp::SoundId) (soundIdx - 1);

    lp::song::RenderSettings rs;
    rs.nostalgia = params.getRawParameterValue ("nostalgia")->load();
    rs.space = params.getRawParameterValue ("space")->load();
    rs.warmth = params.getRawParameterValue ("warmth")->load();
    rs.echo = params.getRawParameterValue ("echo")->load();
    rs.sampleRate = 48000.0;
    songJob.makeSong (o, rs, engine.getTargetWeather(), params.getRawParameterValue ("usesamples")->load() > 0.5f,
                      params.getRawParameterValue ("samplesonly")->load() > 0.5f);
}

juce::Array<juce::File> LivePlanetProcessor::libraryFolders() const
{
    auto folders = lp::song::Library::defaultFolders();
    const auto list = lp::appFolder().getChildFile ("sample-folders.txt");
    juce::StringArray lines;
    lines.addLines (list.loadFileAsString());
    for (auto& l : lines)
        if (juce::File::isAbsolutePath (l.trim()) && juce::File (l.trim()).isDirectory())
            folders.addIfNotAlreadyThere (juce::File (l.trim()));
    return folders;
}

void LivePlanetProcessor::addLibraryFolder (const juce::File& f)
{
    const auto list = lp::appFolder().getChildFile ("sample-folders.txt");
    list.getParentDirectory().createDirectory();
    list.appendText (f.getFullPathName() + "\n");
    rescanLibrary();
}

// the song's section as you hear it (every track, with its mixer), for the voice helper to sing over
std::vector<lp::song::lyricmelody::Line> LivePlanetProcessor::writeVoiceLines (int sectionIndex, int count, const juce::String& lyrics, bool rap, juce::uint32 seed)
{
    const auto st = songJob.getStatus();
    if (! st.haveSong || sectionIndex < 0 || count < 1 || sectionIndex + count > (int) st.song.sections.size()) return {};
    const auto& a = st.song.sections[(size_t) sectionIndex];
    const auto& b = st.song.sections[(size_t) (sectionIndex + count - 1)];
    const int bars = b.startBar + b.bars - a.startBar;
    const auto lines = lp::song::lyricmelody::sungLines (lyrics);
    if (lines.isEmpty() || bars < 1) return {};
    return lp::song::lyricmelody::write (lines, bars, st.song.key, st.song.minor, rap, SongJob::songChords (st.song, st.tracks), a.startBar * 4.0, seed);
}

juce::var LivePlanetProcessor::writeVoiceMelodies (int sectionIndex, int count, const juce::String& lyrics, bool rap, int howMany)
{
    const auto st = songJob.getStatus();
    if (! st.haveSong || sectionIndex < 0 || count < 1 || sectionIndex + count > (int) st.song.sections.size()) return {};
    const auto& a = st.song.sections[(size_t) sectionIndex];
    const auto& b = st.song.sections[(size_t) (sectionIndex + count - 1)];
    const int bars = b.startBar + b.bars - a.startBar;
    const auto lines = lp::song::lyricmelody::sungLines (lyrics);
    if (lines.isEmpty() || bars < 1) return {};
    const auto chordList = SongJob::songChords (st.song, st.tracks);
    juce::Array<juce::var> out;
    const auto base = (juce::uint32) juce::Random::getSystemRandom().nextInt();
    for (int n = 0; n < juce::jlimit (1, 3, howMany); ++n)
    {
        const auto mel = lp::song::lyricmelody::write (lines, bars, st.song.key, st.song.minor, rap, chordList, a.startBar * 4.0,
                                                       base + (juce::uint32) n * 7919u);
        out.add (lp::song::lyricmelody::toScore (mel, st.song.bpm));
    }
    return out;
}

juce::File LivePlanetProcessor::renderSectionForVoice (int sectionIndex, int count)
{
    const auto st = songJob.getStatus();
    if (! st.haveSong || sectionIndex < 0 || count < 1 || sectionIndex + count > (int) st.song.sections.size()) return {};
    // (several sections in a row: one stretch, from the first's start to the last's end)
    auto sec = st.song.sections[(size_t) sectionIndex];
    for (int k = 1; k < count; ++k)
    {
        const auto& more = st.song.sections[(size_t) (sectionIndex + k)];
        sec.bars = more.startBar + more.bars - sec.startBar;
        sec.name += " + " + more.name;
    }
    juce::Array<juce::File> stems;
    for (auto& t : st.tracks) stems.add (t.stem);
    const double barSamples = 4.0 * 60.0 / st.song.bpm * StudioPlayer::fileRate;
    const int a = (int) std::round (sec.startBar * barSamples), len = (int) std::round (sec.bars * barSamples);
    // just the section, from a bar before it (so echoes and rooms are already ringing as it starts)
    const int warm = std::min (a, (int) std::round (barSamples));
    const auto all = StudioPlayer::bounce (stems, studio.mix, st.song.bpm, nullptr, a - warm, warm + len);
    if (all.getNumSamples() < warm + len || len <= 0) return {};
    juce::AudioBuffer<float> cut (2, len);
    for (int ch = 0; ch < 2; ++ch) cut.copyFrom (ch, 0, all, std::min (ch, all.getNumChannels() - 1), warm, len);
    // the same loudness as a mix, kept below clipping
    const float pk = std::max (cut.getMagnitude (0, 0, len), cut.getMagnitude (1, 0, len));
    if (pk > 0.0f) cut.applyGain (juce::jmin (4.0f, 0.89f / pk));
    const auto f = st.files.folder.getChildFile ("voice").getChildFile (juce::String (sec.name) + " (" + juce::String (sec.bars) + " bars).wav");
    f.getParentDirectory().createDirectory();
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), StudioPlayer::fileRate, 2, 24, {}, 0));
    if (w == nullptr || ! w->writeFromAudioSampleBuffer (cut, 0, len)) return {};
    return f;
}

bool LivePlanetProcessor::saveGuideTake (const juce::File& dest, double skipSeconds, double seconds, juce::String& message)
{
    recording = false;
    const int len = recPos.load();
    const int a = juce::jlimit (0, len, (int) std::round (skipSeconds * recRate));
    const int n = juce::jmin (len - a, (int) std::round (seconds * recRate));
    if (n < (int) (recRate * 1.0)) { message = "too short: keep going to the end of the section"; return false; }
    const float pk = std::max (recBuf.getMagnitude (0, a, n), recBuf.getMagnitude (1, a, n));
    if (pk < 0.003f)
    {
        message = "no sound came in: Options > Audio settings, pick your mic and untick Mute audio input";
        return false;
    }
    juce::AudioBuffer<float> take (2, n);
    for (int ch = 0; ch < 2; ++ch) take.copyFrom (ch, 0, recBuf, ch, a, n);
    take.applyGain (0.89f / pk);
    take.applyGainRamp (0, juce::jmin (n, (int) (0.005 * recRate)), 0.0f, 1.0f);
    dest.getParentDirectory().createDirectory();
    dest.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (dest), recRate, 2, 24, {}, 0));
    if (w == nullptr || ! w->writeFromAudioSampleBuffer (take, 0, n)) { message = "couldn't save your take"; return false; }
    return true;
}

bool LivePlanetProcessor::loadGrainSound (const juce::File& f)
{
    auto src = lp::grains::load (f);
    if (src == nullptr) return false;
    grainSource = src;
    grainSynth.setSource (src);
    return true;
}

// ---- recording into a track
bool LivePlanetProcessor::armRecord (int track, juce::String& why)
{
    if (! canRecordMic())
    {
        why = "Plug-ins get no mic in a DAW: record in your DAW and drop the file on the track, or record in the Loopwright app";
        return false;
    }
    const auto st = songJob.getStatus();
    if (! st.haveSong || track < 0 || track >= (int) st.tracks.size()) return false;
    const auto folder = st.files.folder.getChildFile ("voice");
    folder.createDirectory();
    trackTakeFile = folder.getChildFile ("take " + juce::Time::getCurrentTime().formatted ("%H.%M.%S") + ".wav").getNonexistentSibling (false);
    trackTakeRate = getSampleRate() > 0 ? getSampleRate() : 48000.0;
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (trackTakeFile), trackTakeRate, 2, 24, {}, 0));
    if (w == nullptr) { why = "Couldn't make a file to record into"; return false; }
    trackWriterThread.startThread();
    auto tw = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (w.release(), trackWriterThread, 1 << 17);
    {
        const juce::SpinLock::ScopedLockType sl (trackWriterLock);
        std::swap (trackWriter, tw);
    }
    tw.reset();
    trackRecording = false;
    trackTakeReady = false;
    armTrack = track;
    return true;
}

void LivePlanetProcessor::disarmRecord()
{
    armTrack = -1;
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> tw;
    { const juce::SpinLock::ScopedLockType sl (trackWriterLock); std::swap (trackWriter, tw); }
    tw.reset();
    if (! trackRecording.load() && ! trackTakeReady.load()) trackTakeFile.deleteFile(); // (nothing was recorded)
    trackRecording = false;
}

void LivePlanetProcessor::finishTrackTake()
{
    if (! trackTakeReady.exchange (false)) return;
    const int track = armTrack.load();
    // close the take's file (everything written so far is flushed)
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> tw;
    { const juce::SpinLock::ScopedLockType sl (trackWriterLock); std::swap (trackWriter, tw); }
    tw.reset();
    const auto take = trackTakeFile;
    const double start = trackRecStart.load();
    const auto st = songJob.getStatus();
    juce::String why;
    if (track >= 0) armRecord (track, why); // (still armed: the next play records again, into a new file)
    if (! st.haveSong || track < 0 || track >= (int) st.tracks.size()) return;
    const juce::String partName (st.tracks[(size_t) track].name);
    const bool isVocal = partName.startsWith ("vocal (");
    const auto name = partName.fromFirstOccurrenceOf ("(", false, false).upToLastOccurrenceOf (")", false, false);
    // what the track plays now, from bar 1 (the take goes into it)
    juce::File current = st.tracks[(size_t) track].stem;
    if (track < (int) st.song.parts.size() && juce::String (st.song.parts[(size_t) track].name) == partName && juce::File (st.song.parts[(size_t) track].samplePath).existsAsFile())
        current = juce::File (st.song.parts[(size_t) track].samplePath);
    auto* self = this;
    juce::Thread::launch ([self, take, start, current, name, isVocal, folder = st.files.folder.getChildFile ("voice")]
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        juce::AudioBuffer<float> rec;
        double recRate = 48000.0;
        if (std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (take)); rd != nullptr && rd->lengthInSamples > 0)
        {
            recRate = rd->sampleRate;
            rec.setSize (2, (int) rd->lengthInSamples);
            rd->read (&rec, 0, rec.getNumSamples(), 0, true, true);
        }
        auto say = [self] (const juce::String& m) { juce::MessageManager::callAsync ([self, m] { if (self->onRecordMessage) self->onRecordMessage (m); }); };
        if (rec.getNumSamples() < (int) (recRate * 0.2)) { take.deleteFile(); return; }
        if (rec.getMagnitude (0, rec.getNumSamples()) < 0.002f)
        {
            take.deleteFile();
            say (self->wrapperType == wrapperType_Standalone ? "Nothing came in: Options > Audio/MIDI Settings, pick your mic and turn off Mute audio input"
                                                              : "Nothing came in");
            return;
        }
        // the take at the song's rate
        if (std::abs (recRate - StudioPlayer::fileRate) > 0.5)
        {
            const double ratio = recRate / StudioPlayer::fileRate;
            juce::AudioBuffer<float> rs (2, (int) (rec.getNumSamples() / ratio));
            for (int c = 0; c < 2; ++c) { juce::WindowedSincInterpolator ip; ip.process (ratio, rec.getReadPointer (c), rs.getWritePointer (c), rs.getNumSamples()); }
            rec = std::move (rs);
        }
        // the track as it was, with the take put in where it was recorded (short fades at its edges)
        juce::AudioBuffer<float> old;
        if (std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (current)); rd != nullptr)
        {
            old.setSize (2, (int) rd->lengthInSamples);
            rd->read (&old, 0, old.getNumSamples(), 0, true, true);
        }
        const int at = std::max (0, (int) std::llround (start)), len = rec.getNumSamples();
        juce::AudioBuffer<float> out (2, std::max (old.getNumSamples(), at + len));
        out.clear();
        for (int c = 0; c < 2; ++c) if (old.getNumSamples() > 0) out.copyFrom (c, 0, old, c, 0, old.getNumSamples());
        const int xf = std::min (len / 4, (int) (0.005 * StudioPlayer::fileRate));
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < len; ++i)
            {
                const float w = xf > 0 ? std::min (1.0f, std::min ((float) i / xf, (float) (len - 1 - i) / xf)) : 1.0f;
                const float o = out.getSample (c, at + i);
                out.setSample (c, at + i, o * (1.0f - w) + rec.getSample (c, i) * w);
            }
        const auto file = folder.getChildFile (juce::File::createLegalFileName (name) + " rec.wav").getNonexistentSibling (false);
        juce::WavAudioFormat wav;
        bool ok = false;
        if (std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (file), StudioPlayer::fileRate, 2, 24, {}, 0)); w != nullptr)
            ok = w->writeFromAudioSampleBuffer (out, 0, out.getNumSamples());
        take.deleteFile();
        if (! ok) { say ("Couldn't save the recording"); return; }
        juce::MessageManager::callAsync ([self, file, name, isVocal]
        {
            self->songJob.addSungVocal (file, name, "recorded in Loopwright", isVocal, "recorded", true);
            if (self->onRecordMessage) self->onRecordMessage ("Recorded into " + name);
        });
    });
}

// ---- the Record button
void LivePlanetProcessor::startRecording (bool& startedPlay, juce::String& why)
{
    startedPlay = false;
    const int midiTrack = midiArmTrack.load(), audioTrack = armTrack.load();
    if (midiTrack < 0 && audioTrack < 0)
    {
        why = "Arm a track first: the I button on a MIDI track (for your keyboard), or the record button on an audio track";
        return;
    }
    if (midiTrack < 0 && audioTrack >= 0 && ! canRecordMic())
    {
        why = "Plug-ins get no audio input in a DAW: record audio in your DAW and drop the file on the track, or record in the Loopwright app. "
              "MIDI tracks record here: arm one with its I button.";
        return;
    }
    midiRecFifo.reset();
    midiRecHeld.clear();
    midiRecTrack = midiTrack;
    midiTakeEnded = false;
    recordOn = true;
    if (! songPlaying.load() && studio.length() > 0) { previewPlaying = true; startedPlay = true; }
}

void LivePlanetProcessor::stopRecording()
{
    if (! recordOn.exchange (false)) return;
    midiTakeEnded = false;
    finishMidiTake();
    if (trackRecording.load()) { trackRecording = false; trackTakeReady = true; } // (the audio take ends here too)
}

void LivePlanetProcessor::drainMidiTake()
{
    int s1, n1, s2, n2;
    midiRecFifo.prepareToRead (midiRecFifo.getNumReady(), s1, n1, s2, n2);
    for (int i = 0; i < n1; ++i) midiRecHeld.push_back (midiRecBuf[(size_t) (s1 + i)]);
    for (int i = 0; i < n2; ++i) midiRecHeld.push_back (midiRecBuf[(size_t) (s2 + i)]);
    midiRecFifo.finishedRead (n1 + n2);
}

void LivePlanetProcessor::finishMidiTake()
{
    drainMidiTake(); // (what's still waiting)
    const int track = midiRecTrack;
    auto events = std::move (midiRecHeld);
    midiRecHeld.clear();
    if (track < 0 || events.empty()) return;
    // note-ons paired with their note-offs (a note still held at the end lasts to the last event)
    double last = 0.0;
    for (auto& e : events) last = std::max (last, e.beat);
    std::vector<lp::song::Note> added;
    std::map<int, std::pair<double, float>> open;
    auto close = [&added] (int pitch, double from, float vel, double to)
    {
        added.push_back ({ from, std::max (0.0625, to - from), pitch, juce::jlimit (0.05f, 1.0f, vel) });
    };
    for (auto& e : events)
    {
        if (e.on)
        {
            if (auto it = open.find (e.note); it != open.end()) close (e.note, it->second.first, it->second.second, e.beat);
            open[e.note] = { e.beat, e.vel };
        }
        else if (auto it = open.find (e.note); it != open.end())
        {
            close (e.note, it->second.first, it->second.second, e.beat);
            open.erase (it);
        }
    }
    for (auto& [pitch, o] : open) close (pitch, o.first, o.second, std::max (last, o.first + 0.25));
    if (added.empty()) return;
    // the take goes onto what the track already has (overdub)
    const auto pv = songJob.partView (track);
    if (! pv.valid || ! pv.midi) return;
    auto notes = pv.notes;
    notes.insert (notes.end(), added.begin(), added.end());
    std::sort (notes.begin(), notes.end(), [] (const lp::song::Note& a, const lp::song::Note& b) { return a.beat < b.beat; });
    songJob.setNotes (track, std::move (notes));
    if (onRecordMessage) onRecordMessage ("Recorded " + juce::String ((int) added.size()) + (added.size() == 1 ? " note" : " notes") + " into " + pv.name);
}

// ---- an audio file dropped on the Arrangement
void LivePlanetProcessor::placeAudio (const juce::File& src, int track, int bar)
{
    const auto st = songJob.getStatus();
    if (! st.haveSong || ! src.existsAsFile()) return;
    const double bpm = songBpm.load() > 0.0 ? songBpm.load() : (st.song.bpm > 0.0 ? st.song.bpm : 120.0);
    const int at = (int) std::llround (juce::jmax (0, bar) * 4.0 * 60.0 / bpm * StudioPlayer::fileRate);
    // into an existing audio track: what it plays now (from bar 1), with the file put in at that bar
    juce::File current;
    juce::String name, about = "dropped in from your files";
    bool isVocal = false, replace = false;
    if (track >= 0 && track < (int) st.tracks.size())
    {
        const juce::String partName (st.tracks[(size_t) track].name);
        isVocal = partName.startsWith ("vocal (");
        name = partName.fromFirstOccurrenceOf ("(", false, false).upToLastOccurrenceOf (")", false, false);
        current = st.tracks[(size_t) track].stem;
        if (track < (int) st.song.parts.size() && juce::String (st.song.parts[(size_t) track].name) == partName
            && juce::File (st.song.parts[(size_t) track].samplePath).existsAsFile())
            current = juce::File (st.song.parts[(size_t) track].samplePath);
        replace = true;
    }
    else name = src.getFileNameWithoutExtension().substring (0, 40);
    auto* self = this;
    const auto folder = st.files.folder.getChildFile ("voice");
    juce::Thread::launch ([self, src, current, name, about, isVocal, replace, at, folder, bar]
    {
        auto say = [self] (const juce::String& m) { juce::MessageManager::callAsync ([self, m] { if (self->onRecordMessage) self->onRecordMessage (m); }); };
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        juce::AudioBuffer<float> in;
        double rate = StudioPlayer::fileRate;
        if (std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (src)); rd != nullptr && rd->lengthInSamples > 0)
        {
            if (rd->lengthInSamples > (juce::int64) (rd->sampleRate * 60 * 12)) { say ("That file is too long (12 minutes at most)"); return; }
            rate = rd->sampleRate;
            in.setSize (2, (int) rd->lengthInSamples);
            rd->read (&in, 0, in.getNumSamples(), 0, true, true);
            if (rd->numChannels == 1) in.copyFrom (1, 0, in, 0, 0, in.getNumSamples());
        }
        if (in.getNumSamples() == 0) { say ("Couldn't read that file"); return; }
        if (std::abs (rate - StudioPlayer::fileRate) > 0.5)
        {
            const double ratio = rate / StudioPlayer::fileRate;
            juce::AudioBuffer<float> rs (2, (int) (in.getNumSamples() / ratio));
            for (int c = 0; c < 2; ++c) { juce::WindowedSincInterpolator ip; ip.process (ratio, in.getReadPointer (c), rs.getWritePointer (c), rs.getNumSamples()); }
            in = std::move (rs);
        }
        juce::AudioBuffer<float> old;
        if (current.existsAsFile())
            if (std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (current)); rd != nullptr)
            {
                old.setSize (2, (int) rd->lengthInSamples);
                rd->read (&old, 0, old.getNumSamples(), 0, true, true);
            }
        const int len = in.getNumSamples();
        juce::AudioBuffer<float> out (2, std::max (old.getNumSamples(), at + len));
        out.clear();
        for (int c = 0; c < 2; ++c) if (old.getNumSamples() > 0) out.copyFrom (c, 0, old, c, 0, old.getNumSamples());
        const int xf = std::min (len / 4, (int) (0.005 * StudioPlayer::fileRate)); // (short fades where it meets what was there)
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < len; ++i)
            {
                const float w = xf > 0 ? std::min (1.0f, std::min ((float) i / xf, (float) (len - 1 - i) / xf)) : 1.0f;
                out.setSample (c, at + i, out.getSample (c, at + i) * (1.0f - w) + in.getSample (c, i) * w);
            }
        folder.createDirectory();
        const auto file = folder.getChildFile (juce::File::createLegalFileName (name) + " (dropped).wav").getNonexistentSibling (false);
        juce::WavAudioFormat wav;
        bool ok = false;
        if (std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (file), StudioPlayer::fileRate, 2, 24, {}, 0)); w != nullptr)
            ok = w->writeFromAudioSampleBuffer (out, 0, out.getNumSamples());
        if (! ok) { say ("Couldn't save it into the song"); return; }
        juce::MessageManager::callAsync ([self, file, name, about, isVocal, replace, bar]
        {
            self->songJob.addSungVocal (file, name, about, isVocal, "dropped", replace);
            if (self->onRecordMessage) self->onRecordMessage ((replace ? "Dropped into " : "New track: ") + name + ", from bar " + juce::String (bar + 1));
        });
    });
}
