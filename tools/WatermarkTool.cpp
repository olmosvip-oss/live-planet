// Finds the hidden tag in a song: whose Jordan voice license made its vocal.
//   Watermark --check song.wav            what's in it (wav, aiff, flac; mp3 on a Mac)
//   Watermark --id "buyer@email.com"       a buyer's tag, to compare with what --check finds
//   Watermark --embed in.wav out.wav "buyer@email.com"   (for testing)
#include "../Source/song/Watermark.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cstdio>

static bool readMono44 (const juce::File& f, std::vector<float>& out)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (f));
    if (rd == nullptr) return false;
    const int n = (int) juce::jmin<juce::int64> (rd->lengthInSamples, (juce::int64) 44100 * 60 * 20);
    juce::AudioBuffer<float> b ((int) rd->numChannels, n);
    rd->read (&b, 0, n, 0, true, true);
    std::vector<float> mono ((size_t) n, 0.0f);
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < n; ++i) mono[(size_t) i] += b.getSample (c, i) / (float) b.getNumChannels();
    if (std::abs (rd->sampleRate - 44100.0) < 1.0) { out = std::move (mono); return true; }
    const double ratio = rd->sampleRate / 44100.0;
    out.assign ((size_t) (n / ratio), 0.0f);
    juce::WindowedSincInterpolator ip;
    ip.process (ratio, mono.data(), out.data(), (int) out.size());
    return true;
}

int main (int argc, char** argv)
{
    const juce::String cmd = argc > 1 ? argv[1] : "";
    if (cmd == "--id" && argc > 2) { std::printf ("%s\n", lp::watermark::hexOf (lp::watermark::idOf (argv[2])).toRawUTF8()); return 0; }
    if (cmd == "--check" && argc > 2)
    {
        std::vector<float> x;
        if (! readMono44 (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]), x)) { std::printf ("Can't read that file\n"); return 2; }
        const auto r = lp::watermark::scan (x);
        if (r.cycles < 1) { std::printf ("Too short to check (needs at least 9 seconds of vocal)\n"); return 2; }
        std::printf (r.found ? "FOUND tag %s (confidence %.0f%%)\n" : "No tag found (best guess %s, confidence %.0f%%)\n",
                     lp::watermark::hexOf (r.id).toRawUTF8(), r.confidence * 100.0f);
        return r.found ? 0 : 1;
    }
    if (cmd == "--embed" && argc > 4)
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        const auto in = juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]);
        std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (in));
        if (rd == nullptr) { std::printf ("Can't read that file\n"); return 2; }
        juce::AudioBuffer<float> b ((int) rd->numChannels, (int) rd->lengthInSamples);
        rd->read (&b, 0, b.getNumSamples(), 0, true, true);
        lp::watermark::embed (b, rd->sampleRate, lp::watermark::idOf (argv[4]));
        const auto out = juce::File::getCurrentWorkingDirectory().getChildFile (argv[3]);
        out.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (out), rd->sampleRate, (unsigned) b.getNumChannels(), 24, {}, 0));
        if (w == nullptr || ! w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples())) { std::printf ("Can't write\n"); return 2; }
        std::printf ("tagged with %s\n", lp::watermark::hexOf (lp::watermark::idOf (argv[4])).toRawUTF8());
        return 0;
    }
    if (cmd == "--spoken" && argc > 4) // (preview: the unlicensed version) --spoken in.wav tag.wav out.wav
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        auto cwd = juce::File::getCurrentWorkingDirectory();
        std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (cwd.getChildFile (argv[2]))), tr (fm.createReaderFor (cwd.getChildFile (argv[3])));
        if (rd == nullptr || tr == nullptr) { std::printf ("Can't read\n"); return 2; }
        juce::AudioBuffer<float> b ((int) rd->numChannels, (int) rd->lengthInSamples), t (1, (int) tr->lengthInSamples);
        rd->read (&b, 0, b.getNumSamples(), 0, true, true);
        tr->read (&t, 0, t.getNumSamples(), 0, true, false);
        lp::watermark::addSpokenTag (b, rd->sampleRate, t, tr->sampleRate);
        lp::watermark::embed (b, rd->sampleRate, lp::watermark::idOf ("unlicensed"));
        const auto out = cwd.getChildFile (argv[4]);
        out.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (out), rd->sampleRate, (unsigned) b.getNumChannels(), 24, {}, 0));
        return w != nullptr && w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples()) ? 0 : 2;
    }
    std::printf ("Watermark --check song.wav | --id email | --embed in.wav out.wav email\n");
    return 1;
}
