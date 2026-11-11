// Renders Live Planet to WAV files with no DAW needed.
// Usage: RenderDemo <output folder> [seconds per clip]
//   tour_XX_<sound>.wav : the same sky played by every built-in sound
//   city_XX_<city>.wav  : different skies in Auto mode (the weather picks the sound)
#include <juce_audio_formats/juce_audio_formats.h>
#include "../Source/engine/Engine.h"
#include "../Source/song/Song.h"
#include "../Source/song/Library.h"
#include "../Source/engine/SampleFx.h"
#include "../Source/song/Prompt.h"

namespace
{
lp::Weather makeSky (float temp, float rain, float snow, float wind, float cloud, float pressure,
                     float humidity, bool day, int code)
{
    lp::Weather w;
    w.temperatureC = temp; w.rainMm = rain; w.snowCm = snow; w.windKmh = wind; w.cloudPct = cloud;
    w.pressureHpa = pressure; w.humidityPct = humidity; w.isDay = day; w.weatherCode = code;
    w.applyCodeFlags();
    return w;
}

bool render (const juce::File& file, const char* city, const lp::Weather& sky, lp::SoundId sound, double seconds)
{
    const double sr = 48000.0;
    const int block = 512;
    lp::Engine engine;
    engine.prepare (sr);
    engine.setCitySeed (lp::hashString (city));
    engine.jumpToWeather (sky);

    lp::EngineParams p;
    p.sound = sound;

    const int total = (int) (seconds * sr);
    juce::AudioBuffer<float> out (2, total);
    out.clear();
    lp::MidiOut midi;
    std::bitset<128> held;
    lp::TransportInfo t;

    int notes = 0;
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = std::min (block, total - pos);
        midi.clear();
        engine.process (out.getWritePointer (0, pos), out.getWritePointer (1, pos), n, p, t, held, midi);
        for (int i = 0; i < midi.count; ++i)
            if (midi.events[(size_t) i].on) ++notes;
        lp::VisualEvent ve;
        while (engine.popVisual (ve)) {}
    }

    const int fade = (int) (1.5 * sr);
    for (int ch = 0; ch < 2; ++ch)
        out.applyGainRamp (ch, total - fade, fade, 1.0f, 0.0f);

    double sumSq = 0;
    bool bad = false;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < total; ++i)
        {
            const float v = out.getSample (ch, i);
            if (! std::isfinite (v)) bad = true;
            sumSq += v * v;
        }

    file.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream().release());
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), sr, 2, 24, {}, 0));
    if (writer == nullptr) return false;
    stream.release();
    writer->writeFromAudioSampleBuffer (out, 0, total);

    std::printf ("%-30s %-13s key %-2s %-10s %5.1f bpm  notes %4d  peak %4.2f  rms %6.1f dB %s\n",
                 file.getFileNameWithoutExtension().toRawUTF8(),
                 lp::profileFor ((lp::SoundId) engine.uiSound.load()).name,
                 lp::noteName (engine.uiKey.load()), lp::getScale ((lp::ScaleId) engine.uiScale.load()).name,
                 engine.uiBpm.load(), notes, out.getMagnitude (0, total),
                 10.0 * std::log10 (sumSq / (2.0 * total) + 1e-12), bad ? "NaN!" : "");
    return true;
}
}

// Renders a few seconds and returns the samples (both channels interleaved).
static std::vector<float> renderRaw (const lp::Weather& sky, const lp::EngineParams& p, double seconds,
                                     const lp::SourceSound* user = nullptr, bool play = true)
{
    lp::Engine engine;
    engine.prepare (48000.0);
    engine.setCitySeed (lp::hashString ("Tokyo"));
    engine.jumpToWeather (sky);
    if (user) engine.setUserSource (user);
    const int total = (int) (seconds * 48000.0), block = 512;
    std::vector<float> L ((size_t) total), R ((size_t) total), out;
    lp::MidiOut midi;
    std::bitset<128> held;
    lp::TransportInfo t;
    t.shouldPlay = play;
    for (int pos = 0; pos < total; pos += block)
    {
        midi.clear();
        engine.process (L.data() + pos, R.data() + pos, std::min (block, total - pos), p, t, held, midi);
        lp::VisualEvent ve;
        while (engine.popVisual (ve)) {}
    }
    out.reserve ((size_t) total * 2);
    for (int i = 0; i < total; ++i) { out.push_back (L[(size_t) i]); out.push_back (R[(size_t) i]); }
    return out;
}

static double rms (const std::vector<float>& v)
{
    double s = 0;
    for (auto x : v) s += (double) x * x;
    return std::sqrt (s / (double) std::max<size_t> (1, v.size()));
}

// Turns every knob from min to max on every sound and reports how much the sound changes.
static int knobTest()
{
    lp::Weather sky;
    sky.temperatureC = 15; sky.rainMm = 2.0f; sky.windKmh = 25; sky.cloudPct = 60; sky.pressureHpa = 1008;
    sky.humidityPct = 80; sky.isDay = true; sky.weatherCode = 61; sky.applyCodeFlags();

    struct K { const char* name; float lo, hi; float lp::EngineParams::* field; };
    const K knobs[] = {
        { "chords", 0, 1, &lp::EngineParams::padLevel },   { "melody", 0, 1, &lp::EngineParams::dropLevel },
        { "bass", 0, 1, &lp::EngineParams::bassLevel },    { "grain", 20, 400, &lp::EngineParams::grainMs },
        { "scatter", 0, 1, &lp::EngineParams::texture },   { "sparkle", 0, 1, &lp::EngineParams::sparkle },
        { "position", 0, 1, &lp::EngineParams::position }, { "warmth", 0, 1, &lp::EngineParams::warmth },
        { "echo", 0, 1, &lp::EngineParams::echo },         { "space", 0, 1, &lp::EngineParams::space },
        { "shimmer", 0, 1, &lp::EngineParams::shimmer },   { "atmos", 0, 1, &lp::EngineParams::atmos },
        { "output", -24, 6, &lp::EngineParams::gainDb },
        { "nostalgia", 0, 1, &lp::EngineParams::nostalgia },
    };
    std::printf ("%-13s", "sound");
    for (auto& k : knobs) std::printf ("%9s", k.name);
    std::printf ("%9s\n", "level");
    int dead = 0;
    for (int sId = 0; sId < lp::kNumBuiltIns; ++sId)
    {
        lp::EngineParams base;
        base.sound = (lp::SoundId) sId;
        std::printf ("%-13s", lp::profileFor ((lp::SoundId) sId).name);
        for (auto& k : knobs)
        {
            auto a = base, b = base;
            a.*(k.field) = k.lo;
            b.*(k.field) = k.hi;
            const auto ra = renderRaw (sky, a, 8.0), rb = renderRaw (sky, b, 8.0);
            std::vector<float> d (ra.size());
            for (size_t i = 0; i < ra.size(); ++i) d[i] = rb[i] - ra[i];
            const double change = 100.0 * rms (d) / std::max (1e-9, rms (ra) + rms (rb));
            if (change < 1.0) ++dead;
            std::printf ("%8.0f%%", change);
        }
        std::printf ("%7.1fdB\n", 20.0 * std::log10 (rms (renderRaw (sky, base, 8.0)) + 1e-9));
    }
    // silence when not asked to play
    lp::EngineParams p;
    const double quiet = rms (renderRaw (sky, p, 3.0, nullptr, false));
    std::printf ("\nstopped (DAW not playing): rms %.6f  %s\n", quiet, quiet < 1e-4 ? "silent - ok" : "NOT SILENT");
    std::printf ("knobs with under 1%% effect: %d\n", dead);
    return 0;
}

// Checks how your own samples are handled: pitch detection and staying near their own register.
static int sampleTest (const juce::File& outDir)
{
    outDir.createDirectory();
    const double sr = 44100.0;
    auto make = [sr] (bool pitched)
    {
        lp::SourceSound s;
        s.sampleRate = sr;
        s.data.resize ((size_t) (sr * 4.0));
        juce::Random rng (5);
        double ph = 0;
        for (size_t i = 0; i < s.data.size(); ++i)
        {
            const double t = (double) i / sr;
            if (pitched) // a sung "ooh" on A3 with vibrato
            {
                ph += 2.0 * juce::MathConstants<double>::pi * 220.0 * (1.0 + 0.006 * std::sin (2 * juce::MathConstants<double>::pi * 5.5 * t)) / sr;
                s.data[i] = (float) (0.6 * std::sin (ph) + 0.25 * std::sin (2 * ph) + 0.1 * std::sin (3 * ph) + 0.02 * (rng.nextFloat() - 0.5f));
            }
            else // a drum loop: kicks and hats
            {
                const double beat = std::fmod (t, 0.5);
                s.data[i] = (float) (std::sin (2 * juce::MathConstants<double>::pi * (50 + 120 * std::exp (-beat * 30)) * beat) * std::exp (-beat * 8)
                                     + (rng.nextFloat() - 0.5f) * std::exp (-std::fmod (t, 0.25) * 40) * 0.6);
            }
        }
        return s;
    };
    lp::Weather sky;
    sky.temperatureC = 16; sky.rainMm = 0.8f; sky.windKmh = 10; sky.cloudPct = 55; sky.isDay = true; sky.weatherCode = 61;
    for (int pitched = 1; pitched >= 0; --pitched)
    {
        auto s = make (pitched == 1);
        const double f0 = lp::sources::detectPitch (s.data, sr);
        std::printf ("%s sample: detected pitch %s\n", pitched ? "sung A3 (220 Hz)" : "drum loop",
                     f0 > 0 ? (juce::String (f0, 1) + " Hz").toRawUTF8() : "none (treated as texture)");
        for (int fixed = 0; fixed <= 1; ++fixed)
        {
            auto snd = s;
            if (fixed) { snd.rootHz = f0 > 0 ? f0 : 261.6256; snd.foldSemis = f0 > 0 ? 8.0f : 6.0f; }
            else       { snd.rootHz = 261.6256; snd.foldSemis = 0.0f; }
            lp::EngineParams p;
            p.sound = lp::SoundId::UserSample;
            const auto out = renderRaw (sky, p, 20.0, &snd);
            // where does the sound sit? (zero-crossing rate as a rough brightness / pitch measure)
            int zc = 0;
            for (size_t i = 2; i < out.size(); i += 2) if ((out[i] >= 0) != (out[i - 2] >= 0)) ++zc;
            std::printf ("   %-6s  brightness %5.0f crossings/s   level %5.1f dB\n", fixed ? "now" : "before",
                         zc / 20.0, 20.0 * std::log10 (rms (out) + 1e-9));
            juce::WavAudioFormat wav;
            auto file = outDir.getChildFile (juce::String (pitched ? "sample_sung_" : "sample_drums_") + (fixed ? "now" : "before") + ".wav");
            file.deleteFile();
            std::unique_ptr<juce::OutputStream> os (file.createOutputStream().release());
            std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), 48000.0, 2, 24, {}, 0));
            os.release();
            juce::AudioBuffer<float> buf (2, (int) out.size() / 2);
            for (int i = 0; i < buf.getNumSamples(); ++i) { buf.setSample (0, i, out[(size_t) i * 2]); buf.setSample (1, i, out[(size_t) i * 2 + 1]); }
            w->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
        }
    }
    return 0;
}

// How fast does a key you press make sound? (time until the note reaches half its loudness)
static int latencyTest()
{
    lp::Weather sky;
    std::printf ("%-13s %s\n", "sound", "response");
    for (int sId = 0; sId < lp::kNumBuiltIns; ++sId)
    {
        lp::Engine engine;
        engine.prepare (48000.0);
        engine.jumpToWeather (sky);
        lp::EngineParams p;
        p.sound = (lp::SoundId) sId;
        p.space = 0.0f; p.echo = 0.0f; p.atmos = 0.0f; p.nostalgia = 0.0f;
        const int block = 128, total = 48000;
        std::vector<float> L ((size_t) total), R ((size_t) total);
        lp::MidiOut midi;
        std::bitset<128> held;
        lp::KeyEvent key { 0, 64, 0.8f, true };
        for (int pos = 0; pos < total; pos += block)
        {
            lp::TransportInfo t;
            t.shouldPlay = false;          // the song is stopped: only your key plays
            if (pos == block * 4) { t.keys = &key; t.numKeys = 1; }   // press at ~10.7 ms
            midi.clear();
            engine.process (L.data() + pos, R.data() + pos, block, p, t, held, midi);
            lp::VisualEvent ve;
            while (engine.popVisual (ve)) {}
        }
        // loudness in 2 ms frames
        std::vector<double> env;
        for (int i = 0; i + 96 <= total; i += 96)
        {
            double e = 0;
            for (int j = 0; j < 96; ++j) e += (double) L[(size_t) (i + j)] * L[(size_t) (i + j)];
            env.push_back (std::sqrt (e / 96));
        }
        double peak = 0;
        for (auto e : env) peak = std::max (peak, e);
        int hit = -1;
        for (size_t i = 0; i < env.size(); ++i) if (env[i] >= peak * 0.5) { hit = (int) i; break; }
        const double ms = hit * 2.0 - block * 4 / 48.0;
        std::printf ("%-13s %5.0f ms %s\n", lp::profileFor ((lp::SoundId) sId).name, ms, peak < 1e-4 ? "(silent!)" : "");
    }
    return 0;
}

// Song mode round trip: make a reference song with a known tempo/key/shape, analyse it,
// then write a new song from what was learned.
static int songTest (const juce::File& outDir)
{
    using namespace lp::song;
    lp::Weather sky;
    sky.temperatureC = 14; sky.windKmh = 10; sky.cloudPct = 60; sky.rainMm = 0.6f; sky.isDay = true;

    Options o; o.seed = 11; o.mainSound = lp::SoundId::FeltPiano;
    const bool trapTest = std::getenv ("LP_TRAP_TEST") != nullptr;
    if (trapTest) { o.style = (int) Style::Trap; o.mainSound = lp::SoundId::Vapor; }
    Song truth = generate (Profile {}, sky, o);
    truth.bpm = trapTest ? 140 : 96; truth.title = "reference";
    std::printf ("REFERENCE (made here, so we know the truth)\n  %.0f bpm, %s %s, %d bars\n  sections:",
                 truth.bpm, lp::noteName (truth.key), truth.minor ? "minor" : "major", truth.totalBars());
    for (auto& sct : truth.sections) std::printf (" %s(%d)", sct.name.c_str(), sct.bars);
    // rebuild note times for the new tempo is not needed: beats are tempo-free
    Rendered refOut;
    auto t0 = juce::Time::getMillisecondCounterHiRes();
    if (! render (truth, {}, outDir.getChildFile ("reference"), refOut)) return 1;
    std::printf ("\n  rendered in %.1f s\n\n", (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0);

    // analyse its mix, as if you had dropped it on the plugin
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (refOut.mixFile));
    juce::AudioBuffer<float> b (2, (int) rd->lengthInSamples);
    rd->read (&b, 0, b.getNumSamples(), 0, true, true);
    std::vector<float> mono ((size_t) b.getNumSamples());
    for (int i = 0; i < b.getNumSamples(); ++i) mono[(size_t) i] = 0.5f * (b.getSample (0, i) + b.getSample (1, i));
    t0 = juce::Time::getMillisecondCounterHiRes();
    const Profile prof = analyse (mono.data(), (int) mono.size(), rd->sampleRate);
    std::printf ("ANALYSIS (%.1f s)\n  %.1f bpm, %s %s, density %.2f, brightness %.2f\n  sections:",
                 (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0, prof.bpm, lp::noteName (prof.key),
                 prof.minor ? "minor" : "major", prof.density, prof.brightness);
    for (auto& sct : prof.sections) std::printf (" %s@%d(%d)", sct.label.c_str(), sct.startBar, sct.bars);

    // a new song with a similar shape
    o.seed = 2024; o.mainSound = lp::SoundId::Rhodes;
    Song fresh = generate (prof, sky, o);
    std::printf ("\n\nNEW SONG \"%s\"\n  %.0f bpm, %s %s, %d bars (%.1f min)\n  sections:",
                 fresh.title.toRawUTF8(), fresh.bpm, lp::noteName (fresh.key), fresh.minor ? "minor" : "major",
                 fresh.totalBars(), fresh.lengthSeconds() / 60.0);
    for (auto& sct : fresh.sections) std::printf (" %s(%d)", sct.name.c_str(), sct.bars);
    std::printf ("\n  parts:");
    for (auto& pt : fresh.parts) std::printf (" %s[%s, %d notes]", pt.name.c_str(), lp::profileFor (pt.sound).name, (int) pt.notes.size());
    Rendered out;
    t0 = juce::Time::getMillisecondCounterHiRes();
    if (! render (fresh, {}, outDir.getChildFile ("new song"), out)) return 1;
    std::printf ("\n  rendered %d stems + mix + midi in %.1f s\n  %s\n", out.stems.size(),
                 (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0, out.folder.getFullPathName().toRawUTF8());
    return 0;
}

// Throws awkward audio at song mode to make sure nothing can crash it.
static int fuzzTest (const juce::File& outDir)
{
    using namespace lp::song;
    const double sr = 44100.0;
    juce::Random rng (3);
    struct Case { const char* name; double seconds; int kind; };
    const Case cases[] = { { "silence", 30, 0 }, { "white noise", 40, 1 }, { "sine 440", 20, 2 }, { "drum loop 140", 60, 3 },
                           { "drum loop 72", 45, 4 }, { "very short", 9, 3 }, { "clicks", 50, 5 }, { "long noisy song", 400, 6 } };
    for (auto& c : cases)
    {
        std::vector<float> x ((size_t) (c.seconds * sr));
        for (size_t i = 0; i < x.size(); ++i)
        {
            const double t = (double) i / sr;
            switch (c.kind)
            {
                case 0: x[i] = 0.0f; break;
                case 1: x[i] = rng.nextFloat() - 0.5f; break;
                case 2: x[i] = (float) std::sin (2 * juce::MathConstants<double>::pi * 440 * t) * 0.5f; break;
                case 3: { const double b = std::fmod (t, 60.0 / 140.0); x[i] = (float) (std::sin (2 * 3.14159 * 60 * b) * std::exp (-b * 12) + (rng.nextFloat() - 0.5f) * std::exp (-std::fmod (t, 30.0 / 140.0) * 50) * 0.4); break; }
                case 4: { const double b = std::fmod (t, 60.0 / 72.0); x[i] = (float) (std::sin (2 * 3.14159 * 55 * b) * std::exp (-b * 6)); break; }
                case 5: x[i] = (i % 22050 == 0) ? 1.0f : 0.0f; break;
                default: x[i] = (float) ((rng.nextFloat() - 0.5f) * 0.3 + std::sin (2 * 3.14159 * (220 + 110 * std::floor (t / 8.0)) * t) * 0.3 * (0.5 + 0.5 * std::sin (t * 0.2))); break;
            }
        }
        const auto prof = analyse (x.data(), (int) x.size(), sr);
        std::printf ("%-16s valid=%d bpm=%.1f key=%s%s sections=%d", c.name, prof.valid, prof.bpm, lp::noteName (prof.key), prof.minor ? "m" : "", (int) prof.sections.size());
        for (int full = 0; full <= 1; ++full)
        {
            Options o; o.seed = (uint32_t) (rng.nextInt (100000)); o.full = full == 1;
            const auto song = generate (prof, lp::Weather {}, o);
            if (full == 0)
            {
                Rendered r;
                const bool ok = render (song, {}, outDir.getChildFile (c.name), r);
                std::printf ("  sketch: %d bars rendered=%d", song.totalBars(), ok);
            }
            else std::printf ("  full: %d bars, %d parts", song.totalBars(), (int) song.parts.size());
        }
        std::printf ("\n");
    }
    return 0;
}

// One short song in every style, to hear the kits and grooves.
static int styleTour (const juce::File& outDir)
{
    using namespace lp::song;
    lp::Weather sky; sky.temperatureC = 16; sky.windKmh = 12; sky.cloudPct = 50;
    const lp::SoundId mains[] = { lp::SoundId::Rhodes, lp::SoundId::FeltPiano, lp::SoundId::Harp, lp::SoundId::Organ, lp::SoundId::Vibes, lp::SoundId::Marimba };
    for (int st = 0; st < kNumStyles; ++st)
    {
        Options o; o.style = st; o.full = false; o.seed = 100 + (uint32_t) st; o.mainSound = mains[st];
        auto song = generate (Profile {}, sky, o);
        Rendered r;
        const bool ok = render (song, {}, outDir.getChildFile (juce::String (st + 1) + " " + styleName ((Style) st)), r);
        std::printf ("%-11s %5.0f bpm  %s%s  %d bars  ok=%d  %s\n", styleName ((Style) st), song.bpm, lp::noteName (song.key),
                     song.minor ? "m" : "", song.totalBars(), ok, r.mixFile.getFullPathName().toRawUTF8());
    }
    return 0;
}

// Scans the sample folders, then writes a song that uses them.
static int libraryTest (const juce::File& outDir)
{
    using namespace lp::song;
    Library lib;
    lib.scan (Library::defaultFolders());
    std::printf ("found %d samples (%d kicks, %d snares, %d hats, %d usable loops)\n", lib.size(), lib.count (SampleKind::Kick),
                 lib.count (SampleKind::Snare), lib.count (SampleKind::Hat), lib.loopCount());
    for (auto& e : lib.all())
        std::printf ("  %-42s kind=%d loop=%d bpm=%.0f key=%s%s pack=%s\n", juce::File (e.path).getFileName().toRawUTF8(), (int) e.kind, (int) e.loop,
                     e.bpm, e.key >= 0 ? lp::noteName (e.key) : "-", e.minor ? "m" : "", e.pack.toRawUTF8());
    Target t { 90.0, 5, true, Style::Dusty };
    auto loopLen = [] (const juce::String& path, double bpm)
    {
        juce::AudioFormatManager fm; fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (juce::File (path)));
        if (! rd) return 0;
        const double bars = (double) rd->lengthInSamples / rd->sampleRate * bpm / 240.0;
        for (int b : { 1, 2, 4, 8, 16 }) if (std::abs (bars - b) / b < 0.015) return b;
        return 0;
    };
    auto picks = pickFromLibrary (lib, t, 7, loopLen);
    std::printf ("\npicks for 90 bpm F minor dusty: drum pack '%s', %d drum sounds, loop: %s (%d bars)\n", picks.drumPack.toRawUTF8(),
                 (int) picks.drums.size(), picks.loop ? juce::File (picks.loop->path).getFileName().toRawUTF8() : "none", picks.loopBars);
    Options o; o.forceBpm = picks.loop ? picks.loop->bpm : 0.0; o.forceKey = 5; o.forceMinor = true; o.style = 0; o.full = false; o.seed = 5;
    auto song = generate (Profile {}, lp::Weather {}, o);
    song.drumSamples = picks.drums;
    if (picks.loop)
    {
        Part loop; loop.name = "loop"; loop.kind = PartKind::Loop; loop.samplePath = picks.loop->path; loop.loopBars = loopLen (picks.loop->path, song.bpm);
        int k = -1; bool mi = false;
        {
            juce::AudioFormatManager fm; fm.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (juce::File (picks.loop->path)));
            juce::AudioBuffer<float> b (1, (int) rd->lengthInSamples); rd->read (&b, 0, b.getNumSamples(), 0, true, false);
            const bool clear = detectKey (b.getReadPointer (0), b.getNumSamples(), rd->sampleRate, k, mi);
            std::printf ("loop's key by ear: %s%s (clear=%d), name says %s%s, tune to F minor: %+d st\n", lp::noteName (std::max (0, k)), mi ? "m" : "", clear,
                         lp::noteName (picks.loop->key), picks.loop->minor ? "m" : "", tuneShift (k, mi, 5, true));
        }
        for (auto& sec : song.sections) if (sec.name.rfind ("chorus", 0) == 0) for (int b = sec.startBar; b + loop.loopBars <= sec.startBar + sec.bars; b += loop.loopBars) loop.loopStartBars.push_back (b);
        song.parts.push_back (loop);
    }
    Rendered r;
    const bool ok = render (song, {}, outDir, r);
    std::printf ("song at %.0f bpm, %d stems, rendered=%d -> %s\n", song.bpm, r.stems.size(), ok, r.mixFile.getFullPathName().toRawUTF8());
    return 0;
}

// a 2-bar loop at 155 bpm (tone + a click each beat), fitted into a 120 bpm song: beats must land on 120's grid
int stretchTest (const juce::File& dir, int tune = 0)
{
    const double sr = 48000.0, srcBpm = 155.0, songBpm = 120.0;
    const int len = (int) std::round (8 * 60.0 / srcBpm * sr);
    juce::AudioBuffer<float> b (2, len);
    for (int i = 0; i < len; ++i)
    {
        const double beatPos = std::fmod (i / sr, 60.0 / srcBpm);
        float v = 0.3f * (float) std::sin (2 * juce::MathConstants<double>::pi * 440.0 * i / sr);
        if (beatPos < 0.004) v += 0.9f * (float) std::exp (-beatPos * 800.0);
        b.setSample (0, i, v); b.setSample (1, i, v);
    }
    dir.createDirectory();
    auto f = dir.getChildFile ("loop_155.wav");
    f.deleteFile();
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), sr, 2, 24, {}, 0));
        w->writeFromAudioSampleBuffer (b, 0, len);
    }
    lp::song::Song song;
    song.bpm = songBpm;
    song.sections.push_back ({ "verse", 0, 4, 0.6f });
    lp::song::Part part;
    part.name = "loop"; part.kind = lp::song::PartKind::Loop;
    part.samplePath = f.getFullPathName(); part.loopBars = 2; part.loopStartBars = { 0, 2 }; part.autoTune = tune;
    lp::song::RenderSettings rs;
    juce::AudioBuffer<float> out;
    if (! lp::song::renderPart (song, part, rs, out)) { std::printf ("render failed\n"); return 1; }
    // find clicks: peaks of the high-passed signal
    const double beat = 60.0 / songBpm * sr;
    double worst = 0; int found = 0;
    for (int k = 1; k < 15; ++k)
    {
        const int c = (int) std::round (k * beat);
        int bestI = c; float bestV = 0;
        for (int i = std::max (1, c - 2400); i < std::min (out.getNumSamples(), c + 2400); ++i)
        {
            if (std::abs (out.getSample (0, i)) > 0.7f) { bestI = i; bestV = 1; break; }
        }
        std::printf ("%.1f ", (bestI - c) / sr * 1000.0);
        worst = std::max (worst, std::abs (bestI - c) / sr * 1000.0);
        ++found;
    }
    // pitch: zero crossings over a click-free stretch in bar 2
    const int a = (int) (beat * 5.3), z = (int) (beat * 5.9);
    int crossings = 0;
    for (int i = a + 1; i < z; ++i) if ((out.getSample (0, i - 1) < 0) != (out.getSample (0, i) < 0)) ++crossings;
    const double hz = crossings / 2.0 / ((z - a) / sr);
    std::printf ("length %.3f s (want %.3f)  beats checked %d  worst beat offset %.1f ms  pitch %.1f Hz (want 440)\n",
                 out.getNumSamples() / sr, 16 * 60.0 / songBpm, found, worst, hz);
    juce::WavAudioFormat wav;
    auto of = dir.getChildFile ("loop_at_120.wav"); of.deleteFile();
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (of), sr, 2, 24, {}, 0));
    w->writeFromAudioSampleBuffer (out, 0, out.getNumSamples());
    return worst < 8.0 && std::abs (hz - 440.0) < 6.0 ? 0 : 1;
}

// key-finding accuracy: synthetic songs / loops in all 24 keys, with bright tones, drums, detuning
juce::AudioBuffer<float> keySong (int key, bool minor, double seconds, bool drums, bool bassy, juce::Random& rng, double& detuneCents, int type = 0)
{
    const double sr = 44100.0, bpm = 100.0 + rng.nextInt (40);
    const int n = (int) (seconds * sr);
    juce::AudioBuffer<float> b (1, n);
    b.clear();
    float* x = b.getWritePointer (0);
    detuneCents = rng.nextFloat() * 70.0 - 35.0;
    const double tune = std::pow (2.0, detuneCents / 1200.0);
    auto hz = [&] (int midi) { return 440.0 * std::pow (2.0, (midi - 69) / 12.0) * tune; };
    const int majS[7] = { 0, 2, 4, 5, 7, 9, 11 }, minS[7] = { 0, 2, 3, 5, 7, 8, 10 };
    const int* sc = minor ? minS : majS;
    // progressions by scale degree
    const int majP[4] = { 0, 4, 5, 3 }, minP[4] = { 0, 5, 2, 6 };
    const int* prog = minor ? minP : majP;
    const double bar = 240.0 / bpm;
    auto addTone = [&] (double f, double t0, double dur, float amp, int harmonics)
    {
        const int i0 = (int) (t0 * sr), i1 = std::min (n, (int) ((t0 + dur) * sr));
        for (int i = i0; i < i1; ++i)
        {
            const double t = (i - i0) / sr;
            double v = 0;
            for (int h = 1; h <= harmonics; ++h) if (f * h < 8000) v += std::sin (2 * juce::MathConstants<double>::pi * f * h * t) / h;
            x[i] += (float) (amp * v * std::exp (-t * 1.5) * std::min (1.0, t * 200));
        }
    };
    for (int barN = 0; barN * bar < seconds; ++barN)
    {
        const int deg = type == 1 ? (barN % 2 == 0 ? 0 : (minor ? 6 : 3)) : prog[barN % 4];
        const double t0 = barN * bar;
        if (type == 0 || type == 1)
            for (int k = 0; k < 3; ++k)
            {
                const int d = deg + 2 * k;
                const int pc = sc[d % 7] + 12 * (d / 7);
                addTone (hz (key + 60 + pc), t0, bar, 0.12f, 12);
            }
        if (type == 3)
            for (int k : { 0, 7, 12 }) addTone (hz (key + 48 + sc[deg] + k), t0, bar, 0.15f, 16);
        if (type == 2)
        {
            const int riff[8] = { 0, 0, 12, 7, 0, 10, 7, 3 };
            for (int q = 0; q < 8; ++q)
            {
                int iv = riff[q];
                if (iv == 3 && ! minor) iv = 4;
                if (iv == 10 && ! minor) iv = 9;
                addTone (hz (key + 36 + iv), t0 + q * bar / 8, bar / 8, 0.4f, 6);
            }
        }
        else if (type != 4)
            addTone (hz (key + 36 + sc[deg]), t0, bar, bassy ? 0.35f : 0.15f, 4);
        if (type == 0 || type == 4)
            for (int q = 0; q < 4; ++q)
            {
                static const int tune[8] = { 0, 2, 4, 1, 0, 4, 5, 2 };
                const int d = type == 4 ? tune[(barN * 4 + q) % 8] : rng.nextInt (7);
                addTone (hz (key + 72 + sc[d]), t0 + q * bar / 4, bar / 4, 0.12f, 5);
            }
        if (drums)
            for (int q = 0; q < 8; ++q)
            {
                const int i0 = (int) ((t0 + q * bar / 8) * sr);
                for (int i = 0; i < 4000 && i0 + i < n; ++i)
                {
                    const double t = i / sr;
                    float v = (float) (rng.nextFloat() * 2 - 1) * (float) std::exp (-t * 60) * 0.3f; // hat
                    if (q % 4 == 0) v += (float) std::sin (2 * juce::MathConstants<double>::pi * (50 + 120 * std::exp (-t * 30)) * t) * (float) std::exp (-t * 12) * 0.8f;
                    if (q % 4 == 2) v += (float) (rng.nextFloat() * 2 - 1) * (float) std::exp (-t * 20) * 0.5f;
                    x[i0 + i] += v;
                }
            }
    }
    return b;
}

int keyTest()
{
    juce::Random rng (7);
    int songExact = 0, songNotes = 0, loopExact = 0, loopNotes = 0, loopUnclear = 0, total = 0, loopsTotal = 0;
    int missBy[5] {}, unclearBy[5] {}, badBy[5] {};
    int decEar[3] {}, decName[3] {}, decBadName[3] {};
    for (int trial = 0; trial < 2; ++trial)
        for (int key = 0; key < 12; ++key)
            for (int mi = 0; mi < 2; ++mi)
            {
                ++total;
                double det;
                auto song = keySong (key, mi == 1, 40.0, true, true, rng, det);
                auto prof = lp::song::analyse (song.getReadPointer (0), song.getNumSamples(), 44100.0);
                const bool sameNotes = lp::song::tuneShift (prof.key, prof.minor, key, mi == 1) == 0;
                songNotes += sameNotes;
                songExact += prof.key == key && prof.minor == (mi == 1);
                if (! sameNotes) std::printf ("  song miss: %s%s heard %s%s (detune %.0f c)\n", lp::noteName (key), mi ? "m" : "", lp::noteName (prof.key), prof.minor ? "m" : "", det);
                for (int type = 0; type < 5; ++type)
                {
                    ++loopsTotal;
                    auto loop = keySong (key, mi == 1, 8.0, trial == 1, false, rng, det, type);
                    int k = -1; bool m = false;
                    lp::song::KeyReading rd;
                    const bool clear = lp::song::detectKey (loop.getReadPointer (0), loop.getNumSamples(), 44100.0, k, m, &rd);
                    // the decision: song in the same key, and song 2 semitones up (loop must be moved +2)
                    for (int songOff : { 0, 2 })
                    {
                        const int want = songOff;
                        const int sk = (key + songOff) % 12;
                        for (int named = 0; named < 3; ++named)
                        {
                            // named 2: the label is wrong (says 2 semitones up): want the ear to catch it, else a wrong tuning
                            const int nameKey = named == 0 ? -1 : (named == 1 ? key : (key + 2) % 12);
                            const int t = lp::song::loopTune (rd, clear, nameKey, mi == 1, sk, mi == 1);
                            auto& c = named == 0 ? decEar : (named == 1 ? decName : decBadName);
                            if (t == 99 || std::abs (t) > 3) ++c[1];      // skipped (harmless)
                            else if (t == want) ++c[0];                    // right
                            else { ++c[2]; ++badBy[type]; }               // wrong tuning applied
                        }
                    }
                    if (! clear) { ++loopUnclear; ++unclearBy[type]; continue; }
                    const bool ln = lp::song::tuneShift (k, m, key, mi == 1) == 0;
                    loopNotes += ln;
                    loopExact += k == key && m == (mi == 1);
                    if (! ln) ++missBy[type];
                }
            }
    std::printf ("songs: right notes %d/%d, exact key %d/%d\nloops: right notes %d/%d, exact %d/%d, unclear %d\n",
                 songNotes, total, songExact, total, loopNotes, loopsTotal, loopExact, loopsTotal, loopUnclear);
    const char* names[5] = { "full", "two-chord vamp", "bass riff", "power chords", "melody only" };
    for (int t = 0; t < 5; ++t) std::printf ("  %-15s wrong %d  unclear %d  wrong tuning applied %d (of %d)\n", names[t], missBy[t], unclearBy[t], badBy[t], total * 4);
    std::printf ("tuning decisions, ear only:  right %d  skipped %d  WRONG %d\n", decEar[0], decEar[1], decEar[2]);
    std::printf ("tuning decisions, with name: right %d  skipped %d  WRONG %d\n", decName[0], decName[1], decName[2]);
    std::printf ("with a WRONG label:          right %d  skipped %d  WRONG %d\n", decBadName[0], decBadName[1], decBadName[2]);
    return 0;
}

int fxTest()
{
    const int sr = 48000, n = 512, blocks = 400;
    for (int mode = 0; mode < 3; ++mode)
    {
        lp::SampleFx fx; fx.prepare (sr, n);
        lp::SampleFxSettings s;
        if (mode == 0) { s.warmth = s.echo = s.space = s.shimmer = s.nostalgia = s.atmos = 0; }
        if (mode == 2) { s.warmth = s.echo = s.space = s.shimmer = s.nostalgia = s.atmos = 1; s.gainDb = 6; }
        double err = 0, sig = 0, peak = 0; bool finite = true;
        float L[n], R[n], sl[n], sr2[n];
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < n; ++i) { const int k = b * n + i; L[i] = R[i] = sl[i] = sr2[i] = 0.5f * std::sin (k * 0.02f) * ((k / 6000) % 2); }
            float inL[n]; std::copy (L, L + n, inL);
            fx.process (L, R, sl, sr2, n, s);
            for (int i = 0; i < n; ++i)
            {
                finite = finite && std::isfinite (L[i]) && std::isfinite (R[i]);
                err += (L[i] - inL[i]) * (L[i] - inL[i]); sig += inL[i] * inL[i];
                peak = std::max (peak, (double) std::abs (L[i]));
            }
        }
        std::printf ("%s: difference from dry %.1f%%  peak %.2f  finite %s\n", mode == 0 ? "all knobs 0 " : mode == 1 ? "defaults    " : "all knobs max",
                     100.0 * std::sqrt (err / sig), peak, finite ? "yes" : "NO");
    }
    return 0;
}

int promptTest()
{
    const char* tries[] = { "dark trap 140 in F minor with piano and flute", "chill lofi beat with rhodes and vinyl vibes",
                            "happy afrobeats with guitar, bells and vocal chops", "uk drill in a dark mood, 808s and strings",
                            "deep house 124 Am with pads", "make me something like 90s boom bap in Eb major with horns",
                            "drum and bass with a reese bass", "sad r&b, key of c#, 90 bpm, guitar", "I am making a pop song in the key of Bb" };
    for (auto* t : tries)
    {
        auto p = lp::song::parsePrompt (t);
        std::printf ("%-62s -> %s\n", t, p.summary.toRawUTF8());
    }
    // a C minor reference + a bright genre: the song must stay in C minor
    lp::song::Profile ref; ref.valid = true; ref.bpm = 124; ref.key = 0; ref.minor = true;
    auto p = lp::song::parsePrompt ("rock indie pop");
    lp::song::Options o; o.shiftKey = false; o.promptMood = p.mood; o.dawBpm = 90;
    if (p.genre != nullptr) { o.style = (int) p.genre->style; o.genreLo = p.genre->lo; o.genreHi = p.genre->hi; o.genreBpm = p.genre->typical; }
    const auto t = lp::song::chooseTarget (ref, lp::Weather {}, o);
    std::printf ("C minor reference + \"rock indie pop\" -> %s %s at %.0f bpm\n", lp::noteName (t.key), t.minor ? "minor" : "major", t.bpm);
    return t.key == 0 && t.minor ? 0 : 1;
}

// tone match: the same reference made dark (lo-fi) and bright; does each new song move toward it?
static int matchTest (const juce::File& outDir, const juce::File& refFile)
{
    using namespace lp::song;
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (refFile));
    if (rd == nullptr) { std::printf ("can't read reference\n"); return 1; }
    const double sr = rd->sampleRate;
    const int n = (int) std::min<juce::int64> (rd->lengthInSamples, (juce::int64) (sr * 120));
    juce::AudioBuffer<float> b (2, n);
    rd->read (&b, 0, n, 0, true, true);
    lp::Weather sky; sky.temperatureC = 14; sky.windKmh = 10; sky.cloudPct = 60;
    auto toneStr = [] (const Tone& t)
    {
        juce::String r; r << "rolloff " << juce::roundToInt (t.rolloffHz) << " Hz, air " << juce::String (t.airDb, 1) << " dB, level " << juce::String (t.rmsDb, 1) << " dB";
        return r;
    };
    int fails = 0;
    for (int variant = 0; variant < 2; ++variant)
    {
        // make the reference dark (lo-fi, -3 dB/oct-ish above 1.5 kHz) or bright (+6 dB air)
        std::vector<float> mono ((size_t) n);
        float z1 = 0, z2 = 0;
        for (int i = 0; i < n; ++i)
        {
            const float x = 0.5f * (b.getSample (0, i) + b.getSample (1, i));
            z1 += 0.18f * (x - z1); z2 += 0.18f * (z1 - z2); // two gentle low-passes (~1.5 kHz)
            mono[(size_t) i] = variant == 0 ? z2 : x + 2.0f * (x - z2);
        }
        const auto prof = analyse (mono.data(), n, sr);
        Options o; o.seed = 77; o.mainSound = lp::SoundId::Rhodes;
        Song song = generate (prof, sky, o);
        song.refTone = prof.tone;
        Song plain = song; plain.refTone = Tone {};
        Rendered withMatch, without;
        if (! render (song, {}, outDir.getChildFile (variant == 0 ? "dark" : "bright"), withMatch)) return 1;
        if (! render (plain, {}, outDir.getChildFile (variant == 0 ? "dark-plain" : "bright-plain"), without)) return 1;
        auto measure = [&] (const Rendered& r)
        {
            auto& m = *r.mixAudio;
            std::vector<float> mm ((size_t) m.getNumSamples());
            for (int i = 0; i < m.getNumSamples(); ++i) mm[(size_t) i] = 0.5f * (m.getSample (0, i) + m.getSample (1, i));
            return measureTone (mm.data(), (int) mm.size(), 48000.0);
        };
        const auto a = measure (without), c = measure (withMatch);
        auto dist = [] (const Tone& x, const Tone& y) { float d = 0; for (int i = 1; i < 9; ++i) d += std::abs (x.bandsDb[(size_t) i] - y.bandsDb[(size_t) i]); return d / 8.0f; };
        std::printf ("%s reference: %s\n  new song, no match:   %s   (tone distance %.1f dB)\n  new song, matched:    %s   (tone distance %.1f dB)\n",
                     variant == 0 ? "DARK" : "BRIGHT", toneStr (prof.tone).toRawUTF8(), toneStr (a).toRawUTF8(), dist (a, prof.tone),
                     toneStr (c).toRawUTF8(), dist (c, prof.tone));
        std::printf ("  eq:");
        for (float v : withMatch.matchEqDb) std::printf (" %+.1f", v);
        std::printf ("\n");
        if (dist (c, prof.tone) >= dist (a, prof.tone)) ++fails;
    }
    return fails;
}

// writes a song from a reference file, the way the plugin does (coded sounds; no sample library)
static int makeFromReference (const juce::File& refFile, const juce::File& outDir, int seed)
{
    using namespace lp::song;
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (refFile));
    if (rd == nullptr) { std::printf ("can't read reference\n"); return 1; }
    const int n = (int) std::min<juce::int64> (rd->lengthInSamples, (juce::int64) (rd->sampleRate * 600));
    juce::AudioBuffer<float> b (2, n);
    rd->read (&b, 0, n, 0, true, true);
    std::vector<float> mono ((size_t) n);
    for (int i = 0; i < n; ++i) mono[(size_t) i] = 0.5f * (b.getSample (0, i) + b.getSample (1, i));
    const auto hint0 = Library::parse (refFile, refFile.getParentDirectory());
    auto prof = analyse (mono.data(), n, rd->sampleRate, hint0.bpm >= 60.0 && hint0.bpm <= 200.0 ? hint0.bpm : 0.0);
    std::printf ("heard: %.1f bpm, %s %s, %d sections, density %.2f, sustain %.2f, 808 held %.2f\n", prof.bpm, lp::noteName (prof.key), prof.minor ? "minor" : "major",
                 (int) prof.sections.size(), prof.density, prof.sustain, prof.lowSustain);
    for (auto* row : { &prof.grooveLow, &prof.grooveMid, &prof.grooveHigh })
    {
        std::printf ("  %s ", row == &prof.grooveLow ? "kick " : row == &prof.grooveMid ? "snare" : "hats ");
        for (float v : *row) std::printf ("%c", v > 0.66f ? '#' : v > 0.4f ? '+' : v > 0.2f ? '.' : ' ');
        std::printf ("|\n");
    }
    if (std::getenv ("LP_ANALYSE_ONLY")) return 0;
    const auto hint = Library::parse (refFile, refFile.getParentDirectory());
    if (prof.valid && hint.bpm >= 60.0 && hint.bpm <= 200.0) prof.bpm = hint.bpm;
    if (prof.valid && hint.key >= 0) { prof.key = hint.key; prof.minor = hint.minor; }
    std::printf ("with the name's hints: %.1f bpm, %s %s\n", prof.bpm, lp::noteName (prof.key), prof.minor ? "minor" : "major");
    std::printf ("its sound: rolloff %d Hz, air %.1f dB, level %.1f dB, crest %.1f dB\n", juce::roundToInt (prof.tone.rolloffHz),
                 prof.tone.airDb, prof.tone.rmsDb, prof.tone.crestDb);
    lp::Weather sky; sky.temperatureC = 16; sky.windKmh = 8; sky.cloudPct = 40;
    Options o; o.seed = (uint32_t) seed; o.shiftKey = false; o.full = true;
    o.dawBpm = prof.bpm; // as if your project were set to the reference's tempo
    Song song = generate (prof, sky, o);
    song.refTone = prof.tone;
    std::printf ("new song \"%s\": %.0f bpm, %s %s, style %s, %.1f min\n  sections:", song.title.toRawUTF8(), song.bpm, lp::noteName (song.key),
                 song.minor ? "minor" : "major", styleName (song.style), song.lengthSeconds() / 60.0);
    for (auto& sct : song.sections) std::printf (" %s(%d)", sct.name.c_str(), sct.bars);
    std::printf ("\n  parts:");
    for (auto& p : song.parts) std::printf (" %s[%s]", p.name.c_str(), p.kind == PartKind::Drums ? styleName ((Style) p.variant) : lp::profileFor (p.sound).name);
    std::printf ("\n");
    Rendered out;
    if (! render (song, {}, outDir, out)) return 1;
    std::vector<float> mm ((size_t) out.mixAudio->getNumSamples());
    for (int i = 0; i < out.mixAudio->getNumSamples(); ++i) mm[(size_t) i] = 0.5f * (out.mixAudio->getSample (0, i) + out.mixAudio->getSample (1, i));
    const auto t = measureTone (mm.data(), (int) mm.size(), 48000.0);
    std::printf ("new song's sound: rolloff %d Hz, air %.1f dB, level %.1f dB\n  tone match eq:", juce::roundToInt (t.rolloffHz), t.airDb, t.rmsDb);
    for (float v : out.matchEqDb) std::printf (" %+.1f", v);
    std::printf ("\nmix: %s\n", out.mixFile.getFullPathName().toRawUTF8());
    return 0;
}

int main (int argc, char** argv)
{
    if (argc > 2 && juce::String (argv[1]) == "--library")
        return libraryTest (juce::File (juce::String (argv[2])));
    if (argc > 2 && juce::String (argv[1]) == "--styles")
        return styleTour (juce::File (juce::String (argv[2])));
    if (argc > 2 && juce::String (argv[1]) == "--fuzz")
        return fuzzTest (juce::File (juce::String (argv[2])));
    if (argc > 2 && juce::String (argv[1]) == "--song")
        return songTest (juce::File (juce::String (argv[2])));
    if (argc > 1 && juce::String (argv[1]) == "--latency")
        return latencyTest();
    if (argc > 2 && juce::String (argv[1]) == "--samples")
        return sampleTest (juce::File (juce::String (argv[2])));
    if (argc > 2 && juce::String (argv[1]) == "--stretch")
        return stretchTest (juce::File (argv[2]), argc > 3 ? juce::String (argv[3]).getIntValue() : 0);
    if (argc > 3 && juce::String (argv[1]) == "--make")
        return makeFromReference (juce::File (argv[2]), juce::File (argv[3]), argc > 4 ? juce::String (argv[4]).getIntValue() : 1);
    if (argc > 3 && juce::String (argv[1]) == "--match")
        return matchTest (juce::File (argv[2]), juce::File (argv[3]));
    if (argc > 1 && juce::String (argv[1]) == "--prompt")
        return promptTest();
    if (argc > 1 && juce::String (argv[1]) == "--fx")
        return fxTest();
    if (argc > 1 && juce::String (argv[1]) == "--keys")
        return keyTest();
    if (argc > 1 && juce::String (argv[1]) == "--knobs")
        return knobTest();

    juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    const double seconds = argc > 2 ? juce::String (argv[2]).getDoubleValue() : 20.0;
    outDir.createDirectory();

    // 1. Sound tour: one sky, every instrument
    const auto tourSky = makeSky (18, 1.0f, 0, 14, 60, 1011, 75, true, 61);
    for (int i = 0; i < lp::kNumBuiltIns; ++i)
    {
        const auto id = (lp::SoundId) i;
        const auto name = juce::String (lp::profileFor (id).name).toLowerCase().replaceCharacter (' ', '_');
        if (! render (outDir.getChildFile (juce::String::formatted ("tour_%02d_", i + 1) + name + ".wav"),
                      "Tokyo", tourSky, id, seconds))
            return 1;
    }

    // 2. Cities in Auto mode
    struct City { const char* file; const char* name; lp::Weather sky; };
    const City cities[] = {
        { "city_1_tokyo_light_rain",     "Tokyo",       makeSky (17, 0.8f, 0, 11, 70, 1010, 82, true, 61) },
        { "city_2_reykjavik_snow_night", "Reykjavik",   makeSky (-3, 0, 1.2f, 9, 95, 1004, 88, false, 73) },
        { "city_3_sahara_hot_sun",       "Tamanrasset", makeSky (34, 0, 0, 6, 5, 1018, 15, true, 0) },
        { "city_4_london_thunderstorm",  "London",      makeSky (11, 6.5f, 0, 42, 100, 992, 93, true, 95) },
        { "city_5_los_angeles_sunny",    "Los Angeles", makeSky (21, 0, 0, 9, 10, 1016, 45, true, 1) },
        { "city_6_paris_heavy_rain",     "Paris",       makeSky (12, 3.5f, 0, 18, 100, 1003, 90, true, 63) },
        { "city_7_kyoto_clear_night",    "Kyoto",       makeSky (9, 0, 0, 4, 15, 1020, 60, false, 0) },
    };
    for (auto& c : cities)
        if (! render (outDir.getChildFile (juce::String (c.file) + ".wav"), c.name, c.sky, lp::SoundId::Auto, seconds * 1.5))
            return 1;
    return 0;
}
