#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

// A mastering-style peak limiter: it looks a little ahead and turns the whole mix down smoothly
// just before a peak would go over the ceiling, then lets it come back up. Unlike clipping or
// saturation, nothing gets distorted or flattened, so loud and quiet parts keep their balance.
namespace lp
{

class Limiter
{
public:
    void prepare (double sampleRate, float ceilingDb = -1.0f)
    {
        sr = sampleRate;
        look = std::max (8, (int) (0.004 * sr));            // 4 ms look-ahead
        bufL.assign ((size_t) look, 0.0f);
        bufR.assign ((size_t) look, 0.0f);
        qIdx.assign ((size_t) look + 2, 0);
        qVal.assign ((size_t) look + 2, 1.0f);
        qHead = qTail = 0;
        pos = 0;
        count = 0;
        gain = 1.0f;
        ceiling = juce::Decibels::decibelsToGain (ceilingDb);
        release = 1.0f - std::exp (-1.0f / (0.12f * (float) sr)); // 120 ms recovery
        attack = 1.0f - std::pow (0.002f, 1.0f / (float) look);    // all the way down within the look-ahead
    }

    // in place; `makeupDb` is applied before limiting (to bring the mix up to its loudness)
    void process (float* L, float* R, int n, float makeupDb = 0.0f)
    {
        const float makeup = juce::Decibels::decibelsToGain (makeupDb);
        for (int i = 0; i < n; ++i)
        {
            const float inL = L[i] * makeup, inR = R[i] * makeup;
            const float peak = std::max (std::abs (inL), std::abs (inR));
            const float need = peak > ceiling ? ceiling / peak : 1.0f;
            // the lowest gain any sample in the look-ahead needs (a sliding minimum)
            // (a fixed-size ring, so nothing is allocated while playing)
            const long now = count++;
            const int Q = (int) qIdx.size();
            while (qHead != qTail && qVal[(size_t) ((qTail - 1 + Q) % Q)] >= need) qTail = (qTail - 1 + Q) % Q;
            qIdx[(size_t) qTail] = now; qVal[(size_t) qTail] = need; qTail = (qTail + 1) % Q;
            while (qIdx[(size_t) qHead] < now - look) qHead = (qHead + 1) % Q;
            const float target = qVal[(size_t) qHead];
            gain += (target - gain) * (target < gain ? attack : release);
            const float outL = bufL[(size_t) pos], outR = bufR[(size_t) pos];
            bufL[(size_t) pos] = inL; bufR[(size_t) pos] = inR;
            pos = (pos + 1) % look;
            L[i] = juce::jlimit (-ceiling, ceiling, outL * gain);
            R[i] = juce::jlimit (-ceiling, ceiling, outR * gain);
        }
    }

    int latency() const { return look; }

private:
    double sr = 48000;
    int look = 192, pos = 0;
    long count = 0;
    std::vector<float> bufL, bufR, qVal;
    std::vector<long> qIdx;
    int qHead = 0, qTail = 0;
    float gain = 1.0f, ceiling = 0.89f, release = 0.0002f, attack = 0.03f;
};

} // namespace lp
