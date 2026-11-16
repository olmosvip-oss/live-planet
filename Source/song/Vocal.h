#pragma once
// The Vocal chain: EQ, compress, saturate, de-ess, air & width, delay and reverb on a vocal track.
// It plays live (the studio runs it on the track as you listen, so every knob is heard at once), and the
// same processor records it into the track when you drag it out. Analyze listens to the vocal and sets the
// chain up, and says what it found in plain words.
#include <juce_audio_basics/juce_audio_basics.h>
#include <complex>
#include <atomic>
#include "Song.h"

namespace lp::song::vocal
{
struct Bq
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    inline float run (float x) { const float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    // new settings, keeping what's ringing in it (so turning a knob never clicks)
    void take (const Bq& o) { b0 = o.b0; b1 = o.b1; b2 = o.b2; a1 = o.a1; a2 = o.a2; }
    void reset() { z1 = z2 = 0; }
    // RBJ cookbook. type: 0 bell, 1 low shelf, 2 high shelf, 3 high-pass, 4 low-pass
    static Bq make (int type, double sr, double f, double q, double db)
    {
        Bq b;
        f = juce::jlimit (10.0, sr * 0.45, f);
        const double A = std::pow (10.0, db / 40.0), w = juce::MathConstants<double>::twoPi * f / sr;
        const double cw = std::cos (w), sw = std::sin (w), al = sw / (2.0 * q);
        double b0, b1, b2, a0, a1, a2;
        if (type == 0) { b0 = 1 + al * A; b1 = -2 * cw; b2 = 1 - al * A; a0 = 1 + al / A; a1 = -2 * cw; a2 = 1 - al / A; }
        else if (type == 3) { b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = (1 + cw) / 2; a0 = 1 + al; a1 = -2 * cw; a2 = 1 - al; }
        else if (type == 4) { b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = (1 - cw) / 2; a0 = 1 + al; a1 = -2 * cw; a2 = 1 - al; }
        else
        {
            const double sa = 2.0 * std::sqrt (A) * al, sg = type == 1 ? 1.0 : -1.0;
            b0 = A * ((A + 1) - sg * (A - 1) * cw + sa); b1 = sg * 2 * A * ((A - 1) - sg * (A + 1) * cw);
            b2 = A * ((A + 1) - sg * (A - 1) * cw - sa); a0 = (A + 1) + sg * (A - 1) * cw + sa;
            a1 = -sg * 2 * ((A - 1) + sg * (A + 1) * cw); a2 = (A + 1) + sg * (A - 1) * cw - sa;
        }
        b.b0 = (float) (b0 / a0); b.b1 = (float) (b1 / a0); b.b2 = (float) (b2 / a0); b.a1 = (float) (a1 / a0); b.a2 = (float) (a2 / a0);
        return b;
    }
    // how loud it makes a frequency, in dB (for drawing the curve)
    double dbAt (double f, double sr) const
    {
        const double w = juce::MathConstants<double>::twoPi * f / sr;
        const std::complex<double> z = std::polar (1.0, -w), z2 = z * z;
        const auto h = ((double) b0 + (double) b1 * z + (double) b2 * z2) / (1.0 + (double) a1 * z + (double) a2 * z2);
        return 20.0 * std::log10 (std::max (1e-9, std::abs (h)));
    }
};

// the chain's settings without its words (safe to copy on the audio thread: no memory is allocated)
struct Params
{
    bool on = false, eqOn = true, compOn = true, satOn = false, deessOn = true, airOn = true, delayOn = false, verbOn = true, delayPingPong = true;
    float lowCutHz = 90, mudDb = 0, presenceDb = 0, airDb = 0, compAmount = 0.4f, compSpeed = 0.5f, drive = 0.2f, satTone = 0, satMix = 1,
          deess = 0.35f, deessHz = 6500, width = 0.25f, focusHz = 220, delayMix = 0.18f, delayFeedback = 0.35f, delayTone = 0, delayDuck = 0.5f,
          verbSize = 0.5f, verbMix = 0.16f, verbPredelayMs = 20, verbTone = 0, verbDuck = 0.3f, mudHz = 300, presenceHz = 3000, airHz = 10000;
    int delayNote = 1;
    // every setting as text (a different chain makes a different print)
    juce::String fingerprint() const
    {
        juce::String k;
        for (bool b : { on, eqOn, compOn, satOn, deessOn, airOn, delayOn, verbOn, delayPingPong }) k << (int) b;
        for (float v : { lowCutHz, mudDb, presenceDb, airDb, compAmount, compSpeed, drive, satTone, satMix, deess, deessHz, width, focusHz,
                         delayMix, delayFeedback, delayTone, delayDuck, verbSize, verbMix, verbPredelayMs, verbTone, verbDuck, mudHz, presenceHz, airHz })
            k << "," << juce::String (v, 3);
        return k << "," << delayNote;
    }
    static Params of (const VocalChain& c)
    {
        Params p;
        p.on = c.on; p.eqOn = c.eqOn; p.compOn = c.compOn; p.satOn = c.satOn; p.deessOn = c.deessOn; p.airOn = c.airOn; p.delayOn = c.delayOn; p.verbOn = c.verbOn;
        p.delayPingPong = c.delayPingPong; p.lowCutHz = c.lowCutHz; p.mudDb = c.mudDb; p.presenceDb = c.presenceDb; p.airDb = c.airDb;
        p.compAmount = c.compAmount; p.compSpeed = c.compSpeed; p.drive = c.drive; p.satTone = c.satTone; p.satMix = c.satMix; p.deess = c.deess;
        p.deessHz = c.deessHz; p.width = c.width; p.focusHz = c.focusHz; p.delayMix = c.delayMix; p.delayFeedback = c.delayFeedback;
        p.delayTone = c.delayTone; p.delayDuck = c.delayDuck; p.verbSize = c.verbSize; p.verbMix = c.verbMix; p.verbPredelayMs = c.verbPredelayMs;
        p.verbTone = c.verbTone; p.verbDuck = c.verbDuck; p.delayNote = c.delayNote;
        p.mudHz = c.mudHz; p.presenceHz = c.presenceHz; p.airHz = c.airHz;
        return p;
    }
};

// the EQ module's filters (the same ones it plays through, so the drawn curve is the truth)
inline std::array<Bq, 5> eqFilters (const Params& c, double sr)
{
    return { Bq::make (3, sr, c.lowCutHz, 0.54, 0), Bq::make (3, sr, c.lowCutHz, 1.31, 0), // (24 dB / octave)
             Bq::make (0, sr, c.mudHz, 1.1, c.mudDb), Bq::make (0, sr, c.presenceHz, 0.9, c.presenceDb), Bq::make (2, sr, c.airHz, 0.7, c.airDb) };
}
inline double eqDbAt (const Params& c, double f)
{
    double db = 0;
    for (auto& b : eqFilters (c, 48000.0)) db += b.dbAt (f, 48000.0);
    return db;
}
// the compressor's curve: what comes out for what goes in (dB), with its soft knee
inline float compThresholdDb (const Params& c) { return -8.0f - 24.0f * c.compAmount; }
inline float compRatio (const Params& c) { return 1.5f + 4.5f * c.compAmount; }
inline float compCurve (const Params& c, float inDb)
{
    const float T = compThresholdDb (c), R = compRatio (c), W = 8.0f, over = inDb - T;
    if (2.0f * over < -W) return inDb;
    if (2.0f * std::abs (over) <= W) return inDb + (1.0f / R - 1.0f) * (over + W * 0.5f) * (over + W * 0.5f) / (2.0f * W);
    return T + over / R;
}
// the saturator's curve (unity for quiet sounds, rounding the loud ones)
inline float satCurve (const Params& c, float x)
{
    // (a sound at 0.12 comes out at the same level: quieter parts come up, louder round off,
    //  so driving it makes it denser and warmer, not quieter)
    const float k = 1.0f + 8.0f * c.drive, b = 0.12f * c.drive, tb = std::tanh (k * b), ref = 0.12f;
    const float y = (std::tanh (k * (x + b)) - tb) / (std::tanh (k * (ref + b)) - tb) * ref;
    return x + (y - x) * c.satMix;
}
inline double delaySeconds (const Params& c, double bpm)
{
    const double beat = 60.0 / std::max (40.0, bpm);
    return c.delayNote == 0 ? beat : c.delayNote == 2 ? beat * 0.5 : beat * 0.75;
}
inline double reverbSeconds (const Params& c) { return 0.5 + 4.2 * std::pow ((double) c.verbSize, 1.5); }

// the chain, as it plays: one per vocal track in the studio (and one for each print)
class Processor
{
public:
    std::atomic<float> compGrDb { 0.0f }, deessGrDb { 0.0f }; // how hard each is working right now (dB, <= 0)

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        for (auto* d : { &dblL, &dblR }) d->assign ((size_t) (0.05 * sr) + 4, 0.0f);
        for (auto* d : { &delL, &delR }) d->assign ((size_t) (2.6 * sr) + 4, 0.0f);
        pre.assign ((size_t) (0.25 * sr) + 4, 0.0f);
        static const double ms[8] = { 29.7, 37.1, 41.1, 43.7, 53.9, 59.3, 67.1, 73.3 };
        for (int i = 0; i < 8; ++i) { fdn[i].assign ((size_t) (ms[i] * 0.0015 * sr * 1.6) + 8, 0.0f); fdnW[i] = 0; damp[i] = 0; }
        reset();
    }
    void reset()
    {
        for (auto& f : eq) for (auto& b : f) b.reset();
        for (auto* d : { &dblL, &dblR, &delL, &delR, &pre }) std::fill (d->begin(), d->end(), 0.0f);
        for (auto& l : fdn) std::fill (l.begin(), l.end(), 0.0f);
        rms = 0; grSm = 0; grAvg = 0; envHi = envAll = 0; dGain = 1; duckEnv = 0; wobPh = 0;
        dbW = dlW = preW = 0;
        for (auto& b : satTilt) b.reset();
        for (auto& b : deHp) b.reset();
        sideLp.reset(); verbHp.reset(); dcL = dcR = dcInL = dcInR = 0; satIn = satOut = 0;
    }

    void process (float* L, float* R, int n, const Params& c, double bpm)
    {
        if (! c.on || n <= 0 || sr <= 0) { compGrDb = 0; deessGrDb = 0; return; }
        juce::ScopedNoDenormals noDenormals;
        const float fsr = (float) sr;

        // ---- 1 EQ
        if (c.eqOn)
        {
            const auto f = eqFilters (c, sr);
            for (int ch = 0; ch < 2; ++ch) for (int k = 0; k < 5; ++k) eq[ch][(size_t) k].take (f[(size_t) k]);
            for (int i = 0; i < n; ++i)
            {
                float l = L[i], r = R[i];
                for (int k = 0; k < 5; ++k) { l = eq[0][(size_t) k].run (l); r = eq[1][(size_t) k].run (r); }
                L[i] = l; R[i] = r;
            }
        }

        // ---- 2 compress: RMS detector, soft knee, speed sets attack and release, level made up by how hard it works
        float grPeak = 0.0f;
        if (c.compOn && c.compAmount > 0.005f)
        {
            const float aRms = std::exp (-1.0f / (0.004f * fsr));
            const float att = std::exp (-1.0f / ((0.022f - 0.019f * c.compSpeed) * fsr));
            const float rel = std::exp (-1.0f / ((0.28f - 0.21f * c.compSpeed) * fsr));
            const float avgK = std::exp (-1.0f / (1.2f * fsr));
            for (int i = 0; i < n; ++i)
            {
                const float x2 = 0.5f * (L[i] * L[i] + R[i] * R[i]);
                rms = aRms * rms + (1 - aRms) * x2;
                const float inDb = 10.0f * std::log10 (rms + 1e-12f);
                const float target = compCurve (c, inDb) - inDb;      // <= 0
                grSm = target < grSm ? att * grSm + (1 - att) * target : rel * grSm + (1 - rel) * target;
                if (inDb > -50.0f) grAvg = avgK * grAvg + (1 - avgK) * grSm;
                const float g = juce::Decibels::decibelsToGain (grSm - grAvg * 0.85f);
                L[i] *= g; R[i] *= g;
                grPeak = std::min (grPeak, grSm);
            }
        }
        compGrDb = grPeak;

        // ---- 3 saturate: a smooth curve (quiet stays clean, loud rounds off), then its tone
        if (c.satOn && c.drive > 0.005f)
        {
            const auto tilt = Bq::make (2, sr, 3000.0, 0.7, c.satTone * 6.0);
            satTilt[0].take (tilt); satTilt[1].take (tilt);
            // level matched as it plays (it gets thicker, not louder)
            const float k = std::exp (-1.0f / (0.3f * fsr));
            for (int i = 0; i < n; ++i)
            {
                const float inL = L[i], inR = R[i];
                float l = satCurve (c, inL), r = satCurve (c, inR);
                dcL = l - dcInL + 0.9995f * dcL; dcInL = l;
                dcR = r - dcInR + 0.9995f * dcR; dcInR = r;
                l = satTilt[0].run (dcL); r = satTilt[1].run (dcR);
                satIn = k * satIn + (1 - k) * (inL * inL + inR * inR);
                satOut = k * satOut + (1 - k) * (l * l + r * r);
                const float g = juce::jlimit (0.25f, 2.0f, std::sqrt ((satIn + 1e-9f) / (satOut + 1e-9f)));
                L[i] = l * g; R[i] = r * g;
            }
        }

        // ---- 4 de-ess: the S band split off; held back only while it jumps out of the voice
        float dePeak = 0.0f;
        if (c.deessOn && c.deess > 0.005f)
        {
            const auto hp = Bq::make (3, sr, c.deessHz, 0.7, 0);
            deHp[0].take (hp); deHp[1].take (hp);
            const float aHi = std::exp (-1.0f / (0.0006f * fsr)), rHi = std::exp (-1.0f / (0.045f * fsr));
            const float aAll = std::exp (-1.0f / (0.02f * fsr));
            const float gSm = std::exp (-1.0f / (0.0015f * fsr));
            for (int i = 0; i < n; ++i)
            {
                const float hl = deHp[0].run (L[i]), hr = deHp[1].run (R[i]);
                const float hi = std::max (std::abs (hl), std::abs (hr));
                const float all = std::max (std::abs (L[i]), std::abs (R[i]));
                envHi = hi > envHi ? aHi * envHi + (1 - aHi) * hi : rHi * envHi + (1 - rHi) * hi;
                envAll = aAll * envAll + (1 - aAll) * all;
                const float ratio = envHi / (envAll + 1e-6f);
                const float want = juce::jlimit (1.0f - 0.92f * c.deess, 1.0f, 1.0f - (ratio - 0.28f) * 2.6f * c.deess);
                dGain = gSm * dGain + (1 - gSm) * want;
                L[i] += hl * (dGain - 1.0f); R[i] += hr * (dGain - 1.0f);
                dePeak = std::min (dePeak, juce::Decibels::gainToDecibels (dGain));
            }
        }
        deessGrDb = dePeak;

        // ---- 5 air & width: a soft detuned double out to the sides; below Focus it all stays in the middle
        if (c.airOn)
        {
            const auto lpS = Bq::make (4, sr, c.focusHz, 0.7, 0);
            sideLp.take (lpS);
            const auto& hpD = Bq::make (3, sr, c.focusHz, 0.7, 0);
            dblHp[0].take (hpD); dblHp[1].take (hpD);
            const int dlen = (int) dblL.size();
            const float amt = 0.6f * c.width;
            for (int i = 0; i < n; ++i)
            {
                wobPh += 0.31f * juce::MathConstants<float>::twoPi / fsr;
                if (wobPh > juce::MathConstants<float>::twoPi) wobPh -= juce::MathConstants<float>::twoPi;
                const float mid = 0.5f * (L[i] + R[i]);
                dblL[(size_t) dbW] = mid; dblR[(size_t) dbW] = mid;
                auto tap = [&] (const std::vector<float>& d, float back)
                {
                    float p = (float) dbW - back; while (p < 0) p += (float) dlen;
                    const int a = (int) p; const float f = p - (float) a;
                    return d[(size_t) a] + (d[(size_t) ((a + 1) % dlen)] - d[(size_t) a]) * f;
                };
                const float wl = dblHp[0].run (tap (dblL, 0.0123f * fsr + 0.0007f * fsr * std::sin (wobPh)));
                const float wr = dblHp[1].run (tap (dblR, 0.0171f * fsr - 0.0007f * fsr * std::sin (wobPh * 1.3f)));
                if (++dbW >= dlen) dbW = 0;
                float l = L[i] + (wl - wr) * amt * 0.5f, r = R[i] - (wl - wr) * amt * 0.5f;
                // the low end in the middle
                const float side = 0.5f * (l - r), sideLow = sideLp.run (side), m2 = 0.5f * (l + r);
                L[i] = m2 + side - sideLow; R[i] = m2 - side + sideLow;
            }
        }

        // ---- 6 delay and 7 reverb: sends from here, ducked while the voice sings
        const bool del = c.delayOn && c.delayMix > 0.005f, verb = c.verbOn && c.verbMix > 0.005f;
        if (! del && ! verb) return;
        const float duckA = std::exp (-1.0f / (0.01f * fsr)), duckR = std::exp (-1.0f / (0.35f * fsr));
        // delay
        const int dLen = (int) delL.size();
        const float dT = (float) juce::jlimit (64.0, (double) dLen - 4.0, delaySeconds (c, bpm) * sr);
        const auto dLp = Bq::make (4, sr, 2500.0 * std::pow (4.0, c.delayTone + 1.0) / 2.0, 0.6, 0);
        const auto dHp = Bq::make (3, sr, 120.0 + 300.0 * std::max (0.0f, c.delayTone), 0.6, 0);
        for (int k = 0; k < 2; ++k) { delLp[k].take (dLp); delHp[k].take (dHp); }
        // reverb (an 8-line feedback network: smooth, no metallic ring)
        const double t60 = reverbSeconds (c);
        static const double ms[8] = { 29.7, 37.1, 41.1, 43.7, 53.9, 59.3, 67.1, 73.3 };
        float lenS[8], gLine[8];
        const double scale = 0.6 + 0.9 * c.verbSize;
        for (int k = 0; k < 8; ++k)
        {
            lenS[k] = (float) std::min ((double) fdn[k].size() - 4.0, ms[k] * 0.001 * sr * scale);
            gLine[k] = (float) std::pow (10.0, -3.0 * (lenS[k] / sr) / t60);
        }
        const float dampK = std::exp (-juce::MathConstants<float>::twoPi * (2200.0f * std::pow (5.0f, c.verbTone + 1.0f) / 2.2f) / fsr);
        verbHp.take (Bq::make (3, sr, 250.0, 0.7, 0));
        const int preLen = (int) pre.size();
        const int preD = juce::jlimit (1, preLen - 2, (int) (c.verbPredelayMs * 0.001f * fsr));
        for (int i = 0; i < n; ++i)
        {
            const float dryL = L[i], dryR = R[i];
            const float lvl = std::max (std::abs (dryL), std::abs (dryR));
            duckEnv = lvl > duckEnv ? duckA * duckEnv + (1 - duckA) * lvl : duckR * duckEnv + (1 - duckR) * lvl;
            const float singing = juce::jlimit (0.0f, 1.0f, duckEnv * 6.0f);
            float sendVerbL = dryL, sendVerbR = dryR;
            if (del)
            {
                auto rd = [&] (const std::vector<float>& d) { float p = (float) dlW - dT; while (p < 0) p += (float) dLen; const int a = (int) p; const float f = p - (float) a;
                                                              return d[(size_t) a] + (d[(size_t) ((a + 1) % dLen)] - d[(size_t) a]) * f; };
                const float eL = delHp[0].run (delLp[0].run (rd (delL))), eR = delHp[1].run (delLp[1].run (rd (delR)));
                const float fb = 0.85f * c.delayFeedback, in = 0.5f * (dryL + dryR);
                if (c.delayPingPong) { delL[(size_t) dlW] = in + eR * fb; delR[(size_t) dlW] = eL * fb; }
                else { delL[(size_t) dlW] = dryL + eL * fb; delR[(size_t) dlW] = dryR + eR * fb; }
                if (++dlW >= dLen) dlW = 0;
                const float g = c.delayMix * 1.25f * (1.0f - c.delayDuck * 0.85f * singing);
                L[i] += eL * g; R[i] += eR * g;
                sendVerbL += eL * 0.35f; sendVerbR += eR * 0.35f;
            }
            if (verb)
            {
                pre[(size_t) preW] = verbHp.run (0.5f * (sendVerbL + sendVerbR));
                int rp = preW - preD; if (rp < 0) rp += preLen;
                const float vin = pre[(size_t) rp];
                if (++preW >= preLen) preW = 0;
                float o[8];
                for (int k = 0; k < 8; ++k)
                {
                    auto& line = fdn[k];
                    const int len = (int) line.size();
                    float p = (float) fdnW[k] - lenS[k]; while (p < 0) p += (float) len;
                    const int a = (int) p; const float f = p - (float) a;
                    const float v = line[(size_t) a] + (line[(size_t) ((a + 1) % len)] - line[(size_t) a]) * f;
                    damp[k] = v + (damp[k] - v) * dampK;            // darker every time round
                    o[k] = damp[k] * gLine[k];
                }
                // a Hadamard mix of the 8 lines back into them
                float h[8];
                for (int k = 0; k < 8; ++k) h[k] = o[k];
                for (int sz = 1; sz < 8; sz <<= 1)
                    for (int s = 0; s < 8; s += sz * 2)
                        for (int j = s; j < s + sz; ++j) { const float a = h[j], b = h[j + sz]; h[j] = a + b; h[j + sz] = a - b; }
                for (int k = 0; k < 8; ++k)
                {
                    fdn[k][(size_t) fdnW[k]] = h[k] * 0.35355339f + vin * (k & 1 ? -0.5f : 0.5f);
                    if (++fdnW[k] >= (int) fdn[k].size()) fdnW[k] = 0;
                }
                const float wl = o[0] + o[2] + o[4] + o[6], wr = o[1] + o[3] + o[5] + o[7];
                const float g = c.verbMix * 1.7f * (1.0f - c.verbDuck * 0.8f * singing);
                L[i] += wl * g; R[i] += wr * g;
            }
        }
    }

private:
    double sr = 0;
    std::array<std::array<Bq, 5>, 2> eq;
    Bq satTilt[2], deHp[2], sideLp, dblHp[2], delLp[2], delHp[2], verbHp;
    float rms = 0, grSm = 0, grAvg = 0, envHi = 0, envAll = 0, dGain = 1, duckEnv = 0, wobPh = 0;
    float dcL = 0, dcR = 0, dcInL = 0, dcInR = 0, satIn = 0, satOut = 0;
    std::vector<float> dblL, dblR, delL, delR, pre;
    int dbW = 0, dlW = 0, preW = 0;
    std::array<std::vector<float>, 8> fdn;
    int fdnW[8] {};
    float damp[8] {};
};

// plays the chain over a whole track (the same processor as live, so a print sounds like what you heard)
inline void apply (juce::AudioBuffer<float>& buf, const VocalChain& chain, double sr, double bpm)
{
    if (! chain.on || buf.getNumSamples() < 2) return;
    if (buf.getNumChannels() < 2) { buf.setSize (2, buf.getNumSamples(), true); buf.copyFrom (1, 0, buf, 0, 0, buf.getNumSamples()); }
    Processor p;
    p.prepare (sr);
    const auto params = Params::of (chain);
    for (int pos = 0; pos < buf.getNumSamples(); pos += 512)
        p.process (buf.getWritePointer (0, pos), buf.getWritePointer (1, pos), std::min (512, buf.getNumSamples() - pos), params, bpm);
}

// Analyze: listens to the vocal and sets the chain up for it. Returns the chain (turned on) with
// `analysis` saying what it found, one finding per line.
inline VocalChain analyze (const juce::AudioBuffer<float>& buf, double sr, VocalChain c)
{
    const int n = buf.getNumSamples();
    c.on = true;
    if (n < (int) (0.5 * sr)) { c.analysis = "Too short to read: the chain is on with its usual settings."; return c; }
    // a mono copy, then the level of each band over 50 ms blocks, counting only where the voice is singing
    std::vector<float> mono ((size_t) n);
    for (int i = 0; i < n; ++i) mono[(size_t) i] = 0.5f * (buf.getSample (0, i) + buf.getSample (buf.getNumChannels() > 1 ? 1 : 0, i));
    auto bandEnergy = [&] (std::vector<Bq> fs)
    {
        std::vector<double> e;
        const int blk = (int) (0.05 * sr);
        double acc = 0; int k = 0;
        for (int i = 0; i < n; ++i)
        {
            float x = mono[(size_t) i];
            for (auto& f : fs) x = f.run (x);
            acc += (double) x * x;
            if (++k == blk) { e.push_back (acc / blk); acc = 0; k = 0; }
        }
        return e;
    };
    const auto full = bandEnergy ({});
    const auto low = bandEnergy ({ Bq::make (4, sr, 100.0, 0.7, 0) });
    const auto mud = bandEnergy ({ Bq::make (3, sr, 200.0, 0.7, 0), Bq::make (4, sr, 450.0, 0.7, 0) });
    const auto pres = bandEnergy ({ Bq::make (3, sr, 2000.0, 0.7, 0), Bq::make (4, sr, 5000.0, 0.7, 0) });
    const auto sib = bandEnergy ({ Bq::make (3, sr, 5500.0, 0.7, 0), Bq::make (4, sr, 10000.0, 0.7, 0) });
    const auto air = bandEnergy ({ Bq::make (3, sr, 10000.0, 0.7, 0) });
    double loud = 0;
    for (double e : full) loud = std::max (loud, e);
    std::vector<size_t> singing;
    for (size_t i = 0; i < full.size(); ++i) if (full[i] > loud * 0.01 && full[i] > 1e-9) singing.push_back (i);
    if (singing.size() < 6) { c.analysis = "Hardly any singing found: the chain is on with its usual settings."; return c; }
    auto share = [&] (const std::vector<double>& band) { double a = 0, b = 0; for (auto i : singing) { a += band[i]; b += full[i]; } return a / std::max (1e-12, b); };
    auto dbOf = [] (double r) { return 10.0 * std::log10 (std::max (1e-9, r)); };
    const double lowS = dbOf (share (low)), mudS = dbOf (share (mud)), presS = dbOf (share (pres)), airS = dbOf (share (air));
    // how much S sounds stick out: the loudest sibilant moments against the typical ones
    std::vector<double> sibR;
    for (auto i : singing) sibR.push_back (sib[i] / std::max (1e-12, full[i]));
    std::sort (sibR.begin(), sibR.end());
    const double sibPeak = dbOf (sibR[(size_t) (sibR.size() * 0.97)]), sibTyp = dbOf (sibR[sibR.size() / 2]);
    // how much the level jumps around: loud vs. quiet singing
    std::vector<double> lv;
    for (auto i : singing) lv.push_back (dbOf (full[i]));
    std::sort (lv.begin(), lv.end());
    const double range = lv[(size_t) (lv.size() * 0.95)] - lv[(size_t) (lv.size() * 0.2)];

    juce::StringArray found;
    c.eqOn = true;
    // rumble
    if (lowS > -14.0) { c.lowCutHz = 120.0f; found.add ("Low rumble under the voice: cut below 120 Hz."); }
    else { c.lowCutHz = 85.0f; found.add ("Clean low end: a gentle cut below 85 Hz."); }
    // mud (a vocal usually has about a quarter of its energy at 200-450 Hz)
    if (mudS > -5.0) { c.mudDb = (float) juce::jlimit (-8.0, -2.0, -(mudS + 5.0) * 1.5 - 2.0); found.add ("A bit boxy around 300 Hz: pulled down " + juce::String (-c.mudDb, 1) + " dB."); }
    else c.mudDb = 0.0f;
    // presence
    if (presS < -16.0) { c.presenceDb = (float) juce::jlimit (1.0, 6.0, (-16.0 - presS) * 0.8 + 1.0); found.add ("Words sit back: +" + juce::String (c.presenceDb, 1) + " dB at 3 kHz to bring them forward."); }
    else c.presenceDb = 1.0f;
    // air
    if (airS < -32.0) { c.airDb = (float) juce::jlimit (1.0, 6.0, (-32.0 - airS) * 0.4 + 1.5); found.add ("Little top end: +" + juce::String (c.airDb, 1) + " dB of air from 10 kHz."); }
    else c.airDb = 1.0f;
    // sibilance
    c.deessOn = true;
    if (sibPeak - sibTyp > 12.0) { c.deess = 0.6f; found.add ("Sharp S sounds: the de-esser holds them back."); }
    else if (sibPeak - sibTyp > 8.0) c.deess = 0.4f;
    else { c.deess = 0.25f; found.add ("S sounds are smooth already: light de-essing."); }
    // dynamics
    c.compOn = true;
    if (range > 20.0) { c.compAmount = 0.65f; found.add ("Loud and quiet parts far apart (" + juce::String (range, 0) + " dB): firmer compression to even it out."); }
    else if (range > 12.0) { c.compAmount = 0.45f; found.add ("Some jumps in level: medium compression."); }
    else { c.compAmount = 0.25f; found.add ("Steady level already: light compression."); }
    c.analysis = found.joinIntoString ("\n");
    return c;
}
} // namespace lp::song::vocal
