#include "PluginProcessor.h"
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

LivePlanetProcessor::LivePlanetProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                                       // your mic (the standalone app's audio input, or a DAW's sidechain), for recording vocals
                                       .withInput ("Mic in", juce::AudioChannelSet::stereo(), true)),
      params (*this, nullptr, "LivePlanet", createLayout())
{
    engine.setCitySeed (citySeed.load());
    engine.jumpToWeather (getManualWeather());
    weatherService.requestCoordinates (city, lat, lon);
    startTimerHz (2);
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
    const auto in = layouts.getMainInputChannelSet();
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        && (in.isDisabled() || in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
}

void LivePlanetProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate);
    keyEvents.reserve (1024);
    studio.prepare (std::max (samplesPerBlock, 4096));
    const int maxBlock = std::max (samplesPerBlock, 4096);
    sampleFx.prepare (sampleRate, maxBlock);
    samplerFx.prepare (sampleRate, maxBlock);
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
    for (auto* v : { &fxL, &fxR, &sendL, &sendR }) v->assign ((size_t) maxBlock, 0.0f);
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
    for (const auto m : midi)
    {
        const auto msg = m.getMessage();
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
    songPlaying = playSong || preview;
    if (playSong || preview) t.shouldPlay = false; // the weather music steps aside while your song plays
    // your keys play the sample instrument (Play mode), or the built-in instrument
    const bool toSampler = samplerOn.load() && ! chordMode;
    t.keys = toSampler ? nullptr : keyEvents.data();
    t.numKeys = toSampler ? 0 : (int) keyEvents.size();
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
            pos = std::max (0.0, t.ppq) * 60.0 / sbpm * StudioPlayer::fileRate;
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
        if (looping)
        {
            const double toBeats = sbpm / 60.0 / StudioPlayer::fileRate;
            const auto& m = studio.mix[(size_t) juce::jlimit (0, StudioPlayer::maxTracks - 1, lt)];
            const float g = m.mute.load() ? 0.0f : juce::Decibels::decibelsToGain (m.gainDb.load());
            liveDrums.process (L, R, n, getSampleRate(), true, pos * toBeats, next * toBeats, g);
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

    // the sample instrument, through the Space knobs (warmth, echo, space, shimmer, nostalgia, atmosphere,
    // output), its level on the Melody knob, and the Texture knobs shaping how it plays
    if (samplerOn.load() && (size_t) n <= smpL.size())
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

    // a drum you clicked in the editor (when the loop isn't playing it already)
    if (! auditioned) liveDrums.process (L, R, n, getSampleRate(), false, 0.0, 0.0, 1.0f);

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
    k << "|pump " << studio.pumpDepth.load();
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
    {
        double bpm = 90.0;
        songJob.takeFinishedMix (bpm); // (the studio plays the tracks themselves now)
        const auto st = songJob.getStatus();
        if (st.mixInsertSerial != seenMixInserts)
        {
            // an undone delete: its track comes back, and so does a row in the mixer for it
            seenMixInserts = st.mixInsertSerial;
            studio.insertTrackMix (st.mixInsertAt);
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

juce::File LivePlanetProcessor::fileToDrag (int t)
{
    const auto st = songJob.getStatus();
    if (! st.haveSong || t < 0 || t >= (int) st.tracks.size()) return {};
    const auto stem = st.tracks[(size_t) t].stem;
    if (t >= StudioPlayer::maxTracks || ! studio.mix[(size_t) t].printsFx()) return stem;
    const std::lock_guard<std::mutex> lk (printLock);
    const auto& m = studio.mix[(size_t) t];
    // one file per sound of the track: the same settings drag the same file without recording again
    juce::String key;
    for (float v : { m.gainDb.load(), m.pan.load(), m.tone.load(), m.space.load(), m.echo.load(),
                     m.warmth.load(), m.nostalgia.load(), m.chorus.load() })
        key << juce::String (v, 3) << ",";
    key << stem.getLastModificationTime().toMilliseconds() << "," << songBpm << "," << studio.pumpDepth.load();
    // (a folder per sound, so the file keeps the track's plain name when it lands in Ableton)
    const auto printed = st.files.folder.getChildFile ("printed");
    const auto out = printed.getChildFile (juce::String::toHexString (key.hashCode64()).substring (0, 8))
                            .getChildFile (stem.getFileNameWithoutExtension() + " fx.wav");
    if (out.existsAsFile()) return out;
    for (auto& old : printed.findChildFiles (juce::File::findFiles, true, out.getFileName()))
        old.getParentDirectory().deleteRecursively(); // an older print of this track
    return StudioPlayer::printTrack (stem, m, songBpm, out, &studio) ? out : stem;
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
    // the song you made in this project, and each track's mixer, so reopening the project brings it back
    {
        const auto st = songJob.getStatus();
        if (st.haveSong)
        {
            state.setProperty ("songFolder", st.files.folder.getFullPathName(), nullptr);
            state.setProperty ("pump", studio.pumpDepth.load(), nullptr);
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

    if (const auto sp = state.getProperty ("samplerPath", "").toString(); sp.startsWith ("chops:"))
    {
        if (juce::File (sp.substring (6)).existsAsFile()) loadVocalChops (juce::File (sp.substring (6)));
    }
    else if (juce::File (sp).existsAsFile())
        loadSamplerInstrument (juce::File (sp));

    // the song this project was using: open it again, with its mixer as you left it
    const juce::File songFolder (state.getProperty ("songFolder", "").toString());
    if (songFolder.getChildFile ("song.json").existsAsFile())
    {
        {
            const juce::SpinLock::ScopedLockType sl (restoreLock);
            restoredMix = state.getChildWithName ("studioMix").createCopy();
            restoredMixFolder = songFolder;
        }
        songJob.openSong (songFolder);
        studio.pumpDepth = (float) state.getProperty ("pump", 0.0f);
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
