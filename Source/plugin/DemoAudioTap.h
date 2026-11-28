#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>

// For recording tutorial videos with their sound (off unless the demo recorder switches it on): what the plugin
// plays is copied, block by block, into a ring the recorder empties into a WAV next to its frames.
namespace lp::demo
{
struct AudioTap
{
    static constexpr int size = 48000 * 30; // (30 s of room: emptied 60 times a second)
    std::atomic<bool> on { false };
    std::atomic<double> sampleRate { 48000.0 };
    juce::AbstractFifo fifo { size };
    juce::AudioBuffer<float> ring { 2, size };

    // (audio thread)
    void push (const float* L, const float* R, int n)
    {
        if (! on.load()) return;
        int s1, n1, s2, n2;
        fifo.prepareToWrite (n, s1, n1, s2, n2);
        if (n1 > 0) { ring.copyFrom (0, s1, L, n1); ring.copyFrom (1, s1, R, n1); }
        if (n2 > 0) { ring.copyFrom (0, s2, L + n1, n2); ring.copyFrom (1, s2, R + n1, n2); }
        fifo.finishedWrite (n1 + n2);
    }
    // (the recorder) everything waiting, into the file
    void drainInto (juce::AudioFormatWriter& w)
    {
        const int ready = fifo.getNumReady();
        if (ready <= 0) return;
        int s1, n1, s2, n2;
        fifo.prepareToRead (ready, s1, n1, s2, n2);
        if (n1 > 0) w.writeFromAudioSampleBuffer (ring, s1, n1);
        if (n2 > 0) w.writeFromAudioSampleBuffer (ring, s2, n2);
        fifo.finishedRead (n1 + n2);
    }
};
inline AudioTap& tap() { static AudioTap t; return t; }
} // namespace lp::demo
