// Renders Live Planet to WAV files with no DAW needed.
// Usage: RenderDemo <output folder> [seconds per clip]
//   tour_XX_<sound>.wav : the same sky played by every built-in sound
//   city_XX_<city>.wav  : different skies in Auto mode (the weather picks the sound)
#include <juce_audio_formats/juce_audio_formats.h>
#include "../Source/engine/Engine.h"

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

int main (int argc, char** argv)
{
    if (argc > 2 && juce::String (argv[1]) == "--samples")
        return sampleTest (juce::File (juce::String (argv[2])));
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
