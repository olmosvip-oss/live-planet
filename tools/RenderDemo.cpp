// Renders Live Planet to WAV files for a few example skies, with no DAW needed.
// Usage: RenderDemo <output folder> [seconds]
#include <juce_audio_formats/juce_audio_formats.h>
#include "../Source/engine/Engine.h"

namespace
{
struct Scene
{
    const char* file;
    const char* city;
    lp::Weather w;
    lp::SoundId sound;
    float sparkle, warmth, shimmer;
};

lp::Weather makeSky (float temp, float rain, float snow, float wind, float cloud, float pressure,
                     float humidity, bool day, int code)
{
    lp::Weather w;
    w.temperatureC = temp; w.rainMm = rain; w.snowCm = snow; w.windKmh = wind; w.cloudPct = cloud;
    w.pressureHpa = pressure; w.humidityPct = humidity; w.isDay = day; w.weatherCode = code;
    w.applyCodeFlags();
    return w;
}
}

int main (int argc, char** argv)
{
    juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    const double seconds = argc > 2 ? juce::String (argv[2]).getDoubleValue() : 40.0;
    outDir.createDirectory();

    const Scene scenes[] = {
        { "01_tokyo_light_rain_glass",    "Tokyo",     makeSky (17, 0.8f, 0, 11, 70, 1010, 82, true, 61),  lp::SoundId::Glass,    0.3f, 0.35f, 0.35f },
        { "02_reykjavik_snow_night_choir","Reykjavik", makeSky (-3, 0, 1.2f, 9, 95, 1004, 88, false, 73),  lp::SoundId::Choir,    0.25f, 0.55f, 0.5f },
        { "03_sahara_clear_day_musicbox", "Tamanrasset", makeSky (34, 0, 0, 6, 5, 1018, 15, true, 0),      lp::SoundId::MusicBox, 0.45f, 0.25f, 0.4f },
        { "04_london_storm_feltkeys",     "London",    makeSky (11, 6.5f, 0, 42, 100, 992, 93, true, 95),  lp::SoundId::FeltKeys, 0.2f, 0.45f, 0.3f },
    };

    juce::WavAudioFormat wav;
    const double sr = 48000.0;
    const int block = 512;

    for (auto& sc : scenes)
    {
        lp::Engine engine;
        engine.prepare (sr);
        engine.setCitySeed (lp::hashString (sc.city));
        engine.jumpToWeather (sc.w);

        lp::EngineParams p;
        p.sound = sc.sound;
        p.sparkle = sc.sparkle;
        p.warmth = sc.warmth;
        p.shimmer = sc.shimmer;

        const int total = (int) (seconds * sr);
        juce::AudioBuffer<float> out (2, total);
        out.clear();
        lp::MidiOut midi;
        std::bitset<128> held;
        lp::TransportInfo t; // free-running clock at the weather's own tempo

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

        // fade the last two seconds
        const int fade = (int) (2.0 * sr);
        for (int ch = 0; ch < 2; ++ch)
            out.applyGainRamp (ch, total - fade, fade, 1.0f, 0.0f);

        float peak = out.getMagnitude (0, total);
        double sumSq = 0;
        bool bad = false;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < total; ++i)
            {
                const float v = out.getSample (ch, i);
                if (! std::isfinite (v)) bad = true;
                sumSq += v * v;
            }
        const double rmsDb = 10.0 * std::log10 (sumSq / (2.0 * total) + 1e-12);

        auto file = outDir.getChildFile (juce::String (sc.file) + ".wav");
        file.deleteFile();
        std::unique_ptr<juce::OutputStream> stream (file.createOutputStream().release());
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), sr, 2, 24, {}, 0));
        if (writer == nullptr) { std::printf ("could not write %s\n", file.getFullPathName().toRawUTF8()); return 1; }
        stream.release();
        writer->writeFromAudioSampleBuffer (out, 0, total);

        std::printf ("%-34s key %-2s %-10s %5.1f bpm  notes %4d  peak %5.2f  rms %6.1f dB %s\n",
                     sc.file, lp::noteName (engine.uiKey.load()), lp::getScale ((lp::ScaleId) engine.uiScale.load()).name,
                     engine.uiBpm.load(), notes, peak, rmsDb, bad ? "NaN!" : "");
    }
    return 0;
}
