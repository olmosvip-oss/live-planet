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
        look = std::max (1, (int) (0.003 * sr));           // 3 ms look-ahead
        bufL.assign ((size_t) look, 0.0f);
        bufR.assign ((size_t) look, 0.0f);
        pos = 0;
        gain = 1.0f;
        ceiling = juce::Decibels::decibelsToGain (ceilingDb);
        release = std::exp (-1.0f / (0.12f * (float) sr));  // 120 ms recovery
        attack = 1.0f - std::exp (-1.0f / (0.0015f * (float) sr));
        holdLeft = 0;
    }

    // in place; `makeupDb` is applied before limiting (to bring the mix up to its loudness)
    void process (float* L, float* R, int n, float makeupDb = 0.0f)
    {
        const float makeup = juce::Decibels::decibelsToGain (makeupDb);
        for (int i = 0; i < n; ++i)
        {
            const float inL = L[i] * makeup, inR = R[i] * makeup;
            // the gain this incoming sample needs, and hold it for the look-ahead so the peak is caught in time
            const float peak = std::max (std::abs (inL), std::abs (inR));
            const float need = peak > ceiling ? ceiling / peak : 1.0f;
            if (need < target) { target = need; holdLeft = look; }
            else if (holdLeft > 0) --holdLeft;
            else target = 1.0f - (1.0f - target) * release;
            gain = target < gain ? gain + (target - gain) * attack * 4.0f : target;
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
    int look = 144, pos = 0, holdLeft = 0;
    std::vector<float> bufL, bufR;
    float gain = 1.0f, target = 1.0f, ceiling = 0.89f, release = 0.9999f, attack = 0.5f;
};

} // namespace lp
