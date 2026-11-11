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
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
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
}

bool LivePlanetProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void LivePlanetProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate);
    keyEvents.reserve (1024);
    studio.prepare (std::max (samplesPerBlock, 4096));
    const int maxBlock = std::max (samplesPerBlock, 4096);
    sampleFx.prepare (sampleRate, maxBlock);
    masterLimiter.prepare (sampleRate, -1.0f);
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
    const bool haveSong = studio.length() > 0;
    const bool preview = haveSong && previewPlaying.load();
    const bool playSong = haveSong && ! preview && t.hostPlaying && params.getRawParameterValue ("songplay")->load() > 0.5f;
    songPlaying = playSong || preview;
    if (playSong || preview) t.shouldPlay = false; // the weather music steps aside while your song plays
    t.keys = keyEvents.data();
    t.numKeys = (int) keyEvents.size();
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
        masterLimiter.process (L, R, n, songMakeupDb.load());
        const double next = pos + n * inc;
        playheadPos = next;
        if (preview)
        {
            if (next >= (double) studio.length()) { previewPlaying = false; previewPos = 0.0; }
            else previewPos = next;
        }
    }

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
        if (st.haveSong && st.version != loadedStudioVersion)
        {
            loadedStudioVersion = st.version;
            juce::Array<juce::File> stems;
            for (auto& t : st.tracks) stems.add (t.stem);
            // a new song (not just an edited track): every track starts with its own effects, by role,
            // and for songs made of your samples the master knobs start at zero (turn them up if you like)
            const bool newSong = st.files.folder != loadedSongFolder;
            loadedSongFolder = st.files.folder;
            bool anySamples = false;
            for (size_t i = 0; i < st.tracks.size() && i < (size_t) StudioPlayer::maxTracks; ++i)
            {
                auto& m = studio.mix[i];
                const auto& tr = st.tracks[i];
                m.sample = tr.sample;
                m.group = tr.group;
                anySamples = anySamples || tr.sample;
                if (newSong)
                {
                    m.space = tr.space; m.echo = tr.echo; m.warmth = tr.warmth; m.nostalgia = tr.nostalgia;
                    m.gainDb = 0.0f; m.pan = 0.0f; m.tone = 0.0f; m.mute = false; m.solo = false;
                }
            }
            if (newSong && anySamples)
                for (auto id : { "warmth", "echo", "space", "shimmer", "nostalgia", "atmos" })
                    if (auto* prm = params.getParameter (id)) prm->setValueNotifyingHost (0.0f);
            songBpm = st.song.bpm;
            songMakeupDb = st.song.playbackMakeupDb;
            studio.load (stems);
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
    state.setProperty ("theme", themeIndex.load(), nullptr);
    state.setProperty ("preset", currentPreset.load(), nullptr);
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

void LivePlanetProcessor::makeSong (bool full, bool newKey)
{
    lp::song::Options o;
    o.full = full;
    o.shiftKey = newKey;
    o.seed = (uint32_t) juce::Random::getSystemRandom().nextInt (1 << 30);
    o.style = (int) params.getRawParameterValue ("songstyle")->load() - 1;
    if (params.getRawParameterValue ("dawtempo")->load() > 0.5f && dawBpm.load() > 30.0)
        o.dawBpm = std::round (dawBpm.load() * 100.0) / 100.0;
    // "describe your song": genre, tempo, key, mood, instruments
    const auto pr = lp::song::parsePrompt (songPrompt);
    if (pr.any)
    {
        if (pr.genre != nullptr)
        {
            if (o.style < 0) o.style = (int) pr.genre->style; // the style switch still wins if you set it
            o.genreLo = pr.genre->lo; o.genreHi = pr.genre->hi; o.genreBpm = pr.genre->typical;
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
