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

int main (int argc, char** argv)
{
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
