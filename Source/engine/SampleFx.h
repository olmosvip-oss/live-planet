#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include "Dsp.h"

// The plugin's knobs, for your own samples (Splice loops and one-shots in a song).
// The same ideas as the engine's sound: tape warmth, a tempo echo, a big room with a
// shimmer that rises an octave, an old-record "nostalgia", a soft air bed, and the output level.
// Every knob at 0 = that effect is off.
namespace lp
{

struct SampleFxSettings
{
    float warmth = 0.4f, echo = 0.3f, space = 0.6f, shimmer = 0.35f, nostalgia = 0.0f, atmos = 0.2f, gainDb = 0.0f;
    double bpm = 120.0;
};

class SampleFx
{
public:
    void prepare (double sampleRate, int maxBlock)
    {
        sr = (float) sampleRate;
        echoL.prepare ((int) (3.0 * sampleRate));
        echoR.prepare ((int) (3.0 * sampleRate));
        wobL.prepare ((int) (0.05 * sampleRate));
        wobR.prepare ((int) (0.05 * sampleRate));
        shimL.prepare();
        shimR.prepare();
        reverb.setSampleRate (sampleRate);
        reverb.reset();
        echoFiltL.setup (3800.0f, 0.6f, sr);
        echoFiltR.setup (3800.0f, 0.6f, sr);
        hissFilt.setup (7000.0f, 0.5f, sr);
        airFilt.setup (900.0f, 0.5f, sr);
        for (auto* v : { &revL, &revR, &shimFbL, &shimFbR }) v->assign ((size_t) maxBlock + 16, 0.0f);
        lastRoom = -1.0f;
    }

    // bus: your samples (processed in place). send: what feeds the echo and the room (drums send less).
    void process (float* busL, float* busR, const float* sendL, const float* sendR, int n, const SampleFxSettings& s)
    {
        if ((size_t) n > revL.size()) return;
        const float warm = juce::jlimit (0.0f, 1.0f, s.warmth), nost = juce::jlimit (0.0f, 1.0f, s.nostalgia);
        const float space = juce::jlimit (0.0f, 1.0f, s.space), echoAmt = juce::jlimit (0.0f, 1.0f, s.echo);
        const float toneHz = 20000.0f * std::pow (0.25f, warm) * std::pow (0.35f, nost);
        if (std::abs (toneHz - lastTone) > 1.0f)
        {
            toneL.setup (toneHz, 0.6f, sr); toneR.setup (toneHz, 0.6f, sr);
            hpL.setup (25.0f + nost * 190.0f, 0.6f, sr); hpR.setup (25.0f + nost * 190.0f, 0.6f, sr);
            lastTone = toneHz;
        }
        const float drive = 1.0f + warm * 1.8f + nost * 1.2f;
        const float satMix = std::min (1.0f, warm * 1.5f + nost);
        const float echoDelay = juce::jlimit (100.0f, 2.9f * sr, (float) (0.75 * 60.0 / std::max (40.0, s.bpm) * sr)); // dotted 8th
        const float echoFb = 0.25f + 0.4f * echoAmt;
        const float wobbleDepth = (warm * 0.0009f + nost * 0.0024f) * sr;
        const float shimmerAmt = std::min (0.38f, s.shimmer * 0.32f);
        const float hissLevel = nost * nost * 0.012f;
        const float crackleRate = nost * 0.0007f;
        const float airLevel = s.atmos * 0.035f;
        const float outGain = juce::Decibels::decibelsToGain (s.gainDb);

        juce::Reverb::Parameters rp;
        rp.roomSize = 0.72f + 0.25f * space;
        rp.damping = 0.3f + warm * 0.35f;
        rp.wetLevel = 1.0f; rp.dryLevel = 0.0f; rp.width = 1.0f;
        if (std::abs (rp.roomSize - lastRoom) > 0.002f || std::abs (rp.damping - lastDamp) > 0.002f)
        {
            reverb.setParameters (rp);
            lastRoom = rp.roomSize; lastDamp = rp.damping;
        }

        for (int i = 0; i < n; ++i)
        {
            // echo, bouncing left and right
            const float eL = echoL.read (echoDelay), eR = echoR.read (echoDelay);
            echoFiltL.tick (eL); echoFiltR.tick (eR);
            echoL.push ((sendL[i] + sendR[i]) * 0.5f + echoFiltR.lp * echoFb);
            echoR.push (echoFiltL.lp * echoFb);
            float xl = busL[i] + eL * echoAmt * 0.9f;
            float xr = busR[i] + eR * echoAmt * 0.9f;

            // tape: slow wobble, darker tone, soft saturation
            if (wobbleDepth > 0.01f)
            {
                wobPhase += 0.55f * 6.2831853f / sr; if (wobPhase > 6.2831853f) wobPhase -= 6.2831853f;
                fltPhase += 6.3f * 6.2831853f / sr;  if (fltPhase > 6.2831853f) fltPhase -= 6.2831853f;
                const float d = 0.003f * sr + wobbleDepth * (0.8f * std::sin (wobPhase) + 0.2f * std::sin (fltPhase));
                wobL.push (xl); wobR.push (xr);
                xl = wobL.read (d); xr = wobR.read (d);
            }
            toneL.tick (xl); toneR.tick (xr);
            xl = toneL.lp; xr = toneR.lp;
            xl += (std::tanh (xl * drive) / drive - xl) * satMix;
            xr += (std::tanh (xr * drive) / drive - xr) * satMix;
            busL[i] = xl; busR[i] = xr;
            revL[(size_t) i] = sendL[i] + eL * echoAmt * 0.5f + shimFbL[(size_t) i];
            revR[(size_t) i] = sendR[i] + eR * echoAmt * 0.5f + shimFbR[(size_t) i];
        }

        reverb.processStereo (revL.data(), revR.data(), n);

        for (int i = 0; i < n; ++i)
        {
            const float rl = revL[(size_t) i], rr = revR[(size_t) i];
            shimFbL[(size_t) i] = std::tanh (shimL.process (rl) * shimmerAmt);
            shimFbR[(size_t) i] = std::tanh (shimR.process (rr) * shimmerAmt);
            float yl = busL[i] + rl * space * space * 0.3f;
            float yr = busR[i] + rr * space * space * 0.3f;

            if (nost > 0.001f)
            {
                // an old record: thinner lows, narrower, a slow sway, hiss and crackle
                hpL.tick (yl); hpR.tick (yr);
                yl += (hpL.hp - yl) * nost; yr += (hpR.hp - yr) * nost;
                const float mid = (yl + yr) * 0.5f, side = (yl - yr) * 0.5f * (1.0f - 0.5f * nost);
                swayPhase += 0.31f * 6.2831853f / sr; if (swayPhase > 6.2831853f) swayPhase -= 6.2831853f;
                const float sway = 1.0f - nost * 0.1f * (0.5f + 0.5f * std::sin (swayPhase) * std::sin (swayPhase * 0.37f));
                yl = (mid + side) * sway; yr = (mid - side) * sway;
                hissFilt.tick (noise.next());
                float pop = 0.0f;
                if (noise.next() * 0.5f + 0.5f < crackleRate) pop = noise.next() * (0.05f + 0.25f * std::abs (noise.next())) * nost;
                crackleHp = 0.6f * (crackleHp + pop - crackleLast); crackleLast = pop;
                yl += hissFilt.lp * hissLevel + crackleHp;
                yr += hissFilt.lp * hissLevel * 0.9f + crackleHp * 0.8f;
            }
            if (airLevel > 0.0001f)
            {
                // a soft air bed that follows the music (silent when it is)
                env += (std::abs (busL[i]) + std::abs (busR[i]) - env) * 0.0005f;
                airFilt.tick (noise.next());
                const float a = airFilt.bp * airLevel * std::min (1.0f, env * 8.0f);
                yl += a; yr += a * 0.85f;
            }
            busL[i] = yl * outGain;
            busR[i] = yr * outGain;
        }
    }

private:
    float sr = 48000.0f;
    dsp::Delay echoL, echoR, wobL, wobR;
    dsp::SVF echoFiltL, echoFiltR, toneL, toneR, hpL, hpR, hissFilt, airFilt;
    dsp::OctaveUp shimL, shimR;
    dsp::Noise noise;
    juce::Reverb reverb;
    std::vector<float> revL, revR, shimFbL, shimFbR;
    float lastRoom = -1.0f, lastDamp = -1.0f, lastTone = -1.0f;
    float wobPhase = 0, fltPhase = 0, swayPhase = 0, crackleHp = 0, crackleLast = 0, env = 0;
};

} // namespace lp
