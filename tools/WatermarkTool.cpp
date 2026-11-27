// Finds the hidden tag in a song: whose Jordan voice license made its vocal.
//   Watermark --check song.wav            what's in it (wav, aiff, flac; mp3 on a Mac)
//   Watermark --id "buyer@email.com"       a buyer's tag, to compare with what --check finds
//   Watermark --embed in.wav out.wav "buyer@email.com"   (for testing)
#include "../Source/song/Watermark.h"
#include "../Source/song/VocalLevel.h"
#include "../Source/song/Stack.h"
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
        lp::song::vocallevel::process (b, rd->sampleRate);
        lp::watermark::addSpokenTag (b, rd->sampleRate, t, tr->sampleRate);
        lp::watermark::embed (b, rd->sampleRate, lp::watermark::idOf ("unlicensed"));
        const auto out = cwd.getChildFile (argv[4]);
        out.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (out), rd->sampleRate, (unsigned) b.getNumChannels(), 24, {}, 0));
        return w != nullptr && w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples()) ? 0 : 2;
    }
    if (cmd == "--level" && argc > 3) // (testing: a take evened out, as Loopwright does) --level in.wav out.wav
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        auto cwd = juce::File::getCurrentWorkingDirectory();
        std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (cwd.getChildFile (argv[2])));
        if (rd == nullptr) return 2;
        juce::AudioBuffer<float> b ((int) rd->numChannels, (int) rd->lengthInSamples);
        rd->read (&b, 0, b.getNumSamples(), 0, true, true);
        lp::song::vocallevel::process (b, rd->sampleRate);
        const auto out = cwd.getChildFile (argv[3]);
        out.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (out), rd->sampleRate, (unsigned) b.getNumChannels(), 24, {}, 0));
        return w != nullptr && w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples()) ? 0 : 2;
    }
    if (cmd == "--express" && argc > 4) // (testing) --express in.wav out.wav "t0,t1,slide;..."
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        auto cwd = juce::File::getCurrentWorkingDirectory();
        std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (cwd.getChildFile (argv[2])));
        if (rd == nullptr) return 2;
        juce::AudioBuffer<float> b (2, (int) rd->lengthInSamples);
        b.clear();
        rd->read (&b, 0, b.getNumSamples(), 0, true, rd->numChannels > 1);
        if (rd->numChannels == 1) b.copyFrom (1, 0, b, 0, 0, b.getNumSamples());
        std::vector<lp::song::stack::Bend> bends;
        for (auto& e : juce::StringArray::fromTokens (argv[4], ";", ""))
        {
            auto v = juce::StringArray::fromTokens (e, ",", "");
            if (v.size() >= 3) bends.push_back ({ v[0].getDoubleValue(), v[1].getDoubleValue(), v.size() > 3 ? v[3].getFloatValue() : 0.0f, v[2].getFloatValue() });
        }
        lp::song::stack::express (b, rd->sampleRate, bends);
        const auto out = cwd.getChildFile (argv[3]);
        out.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (out), rd->sampleRate, 2, 24, {}, 0));
        return w != nullptr && w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples()) ? 0 : 2;
    }
    if (cmd == "--piece" && argc > 6) // (testing Draw & sing's piece) --piece in.wav out.wav a0sec a1sec semis ratio
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        auto cwd = juce::File::getCurrentWorkingDirectory();
        std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (cwd.getChildFile (argv[2])));
        if (rd == nullptr) return 2;
        const double sr = rd->sampleRate;
        juce::AudioBuffer<float> take (2, (int) rd->lengthInSamples);
        take.clear();
        rd->read (&take, 0, take.getNumSamples(), 0, true, false);
        take.copyFrom (1, 0, take, 0, 0, take.getNumSamples());
        const int N = take.getNumSamples();
        const int a0 = (int) (juce::String (argv[4]).getDoubleValue() * sr), a1 = (int) (juce::String (argv[5]).getDoubleValue() * sr);
        const float semis = juce::String (argv[6]).getFloatValue();
        const double ratio = argc > 7 ? juce::String (argv[7]).getDoubleValue() : 1.0, ctx = 0.2, fadeIn = 0.006, tail = 0.006;
        const int cs = juce::jmax (0, a0 - (int) ((fadeIn + ctx) * sr)), ce = juce::jmin (N, a1 + (int) ((tail + ctx) * sr));
        const int inLen = ce - cs, outLen = juce::jmax (1, (int) std::round (inLen * ratio));
        juce::AudioBuffer<float> in (2, inLen), out (2, outLen);
        for (int ch = 0; ch < 2; ++ch) in.copyFrom (ch, 0, take, ch, cs, inLen);
        signalsmith::stretch::SignalsmithStretch<float> st;
        st.presetDefault (2, (float) sr);
        st.setTransposeSemitones (semis, (float) (8000.0 / sr));
        st.setFormantFactor (1.0f, true);
        const bool done = st.exact (in.getArrayOfReadPointers(), inLen, out.getArrayOfWritePointers(), outLen);
        std::printf ("exact: %d, in %d out %d\n", (int) done, inLen, outLen);
        const auto o = cwd.getChildFile (argv[3]);
        o.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (o), sr, 2, 24, {}, 0));
        return w != nullptr && w->writeFromAudioSampleBuffer (out, 0, out.getNumSamples()) ? 0 : 2;
    }
    std::printf ("Watermark --check song.wav | --id email | --embed in.wav out.wav email\n");
    return 1;
}
