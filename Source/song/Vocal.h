#pragma once
// The Vocal chain: EQ, compress, saturate, de-ess, air & width, delay and reverb on a vocal track,
// recorded into the track (so the stems and the mix you drag out have it). Analyze listens to the
// vocal and sets the chain up, and says what it found in plain words.
#include <juce_audio_basics/juce_audio_basics.h>
#include <complex>
#include "Song.h"

namespace lp::song::vocal
{
struct Bq
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    inline float run (float x) { const float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
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

// the EQ module's four filters (the same ones it plays through, so the drawn curve is the truth)
inline std::array<Bq, 4> eqFilters (const VocalChain& c, double sr)
{
    return { Bq::make (3, sr, c.lowCutHz, 0.71, 0), Bq::make (0, sr, 300.0, 1.1, c.mudDb),
             Bq::make (0, sr, 3000.0, 0.9, c.presenceDb), Bq::make (2, sr, 10000.0, 0.7, c.airDb) };
}
inline double eqDbAt (const VocalChain& c, double f)
{
    double db = 0;
    for (auto& b : eqFilters (c, 48000.0)) db += b.dbAt (f, 48000.0);
    return db;
}

// plays the chain over a whole vocal track, in place
inline void apply (juce::AudioBuffer<float>& buf, const VocalChain& c, double sr, double bpm)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buf.getNumSamples();
    if (! c.on || n < 2) return;
    if (buf.getNumChannels() < 2) { buf.setSize (2, n, true); buf.copyFrom (1, 0, buf, 0, 0, n); }
    float* L = buf.getWritePointer (0);
    float* R = buf.getWritePointer (1);
    const float fsr = (float) sr;

    // 1 EQ
    if (c.eqOn)
        for (int ch = 0; ch < 2; ++ch)
        {
            auto f = eqFilters (c, sr);
            auto* d = buf.getWritePointer (ch);
            for (int i = 0; i < n; ++i) d[i] = f[3].run (f[2].run (f[1].run (f[0].run (d[i]))));
        }

    // 2 compress: an RMS-style detector on both sides, threshold and ratio from one amount, level made up
    if (c.compOn && c.compAmount > 0.01f)
    {
        const float thr = -10.0f - 22.0f * c.compAmount, ratio = 1.6f + 4.4f * c.compAmount;
        const float att = std::exp (-1.0f / (0.006f * fsr)), rel = std::exp (-1.0f / (0.14f * fsr));
        const float makeup = juce::Decibels::decibelsToGain (-thr * (1.0f - 1.0f / ratio) * 0.45f);
        float env = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float lvl = std::max (std::abs (L[i]), std::abs (R[i]));
            env = lvl > env ? att * env + (1 - att) * lvl : rel * env + (1 - rel) * lvl;
            const float db = juce::Decibels::gainToDecibels (env + 1e-9f);
            const float gr = db > thr ? (thr + (db - thr) / ratio) - db : 0.0f;
            const float g = juce::Decibels::decibelsToGain (gr) * makeup;
            L[i] *= g; R[i] *= g;
        }
    }

    // 3 saturate: a warm tube-ish curve (even a little drive thickens a vocal)
    if (c.satOn && c.drive > 0.01f)
    {
        const float k = 1.0f + 5.0f * c.drive, b = 0.08f * c.drive, off = std::tanh (k * b) / k, mix = 0.35f + 0.65f * c.drive;
        for (int i = 0; i < n; ++i)
        {
            L[i] = L[i] * (1.0f - mix) + (std::tanh (k * (L[i] + b)) / k - off) * mix;
            R[i] = R[i] * (1.0f - mix) + (std::tanh (k * (R[i] + b)) / k - off) * mix;
        }
        // (the small offset adds the even harmonics; take it back out)
        for (int ch = 0; ch < 2; ++ch)
        {
            auto hp = Bq::make (3, sr, 20.0, 0.7, 0);
            auto* d = buf.getWritePointer (ch);
            for (int i = 0; i < n; ++i) d[i] = hp.run (d[i]);
        }
    }

    // 4 de-ess: when the 5-10 kHz band (S, T, SH) jumps up, only that band is held back
    if (c.deessOn && c.deess > 0.01f)
    {
        auto hpL = Bq::make (3, sr, 5000.0, 0.7, 0), hpR = Bq::make (3, sr, 5000.0, 0.7, 0);
        const float att = std::exp (-1.0f / (0.001f * fsr)), rel = std::exp (-1.0f / (0.06f * fsr));
        const float slowRel = std::exp (-1.0f / (0.4f * fsr));
        float env = 0.0f, body = 0.0f;
        const float maxCut = 0.85f * c.deess;
        for (int i = 0; i < n; ++i)
        {
            const float hl = hpL.run (L[i]), hr = hpR.run (R[i]);
            const float s = std::max (std::abs (hl), std::abs (hr));
            const float full = std::max (std::abs (L[i]), std::abs (R[i]));
            env = s > env ? att * env + (1 - att) * s : rel * env + (1 - rel) * s;
            body = slowRel * body + (1 - slowRel) * full;
            // sibilant when the highs are a big part of the sound right now
            const float ratio = env / (body + 1e-6f);
            const float cut = juce::jlimit (0.0f, maxCut, (ratio - 0.35f) * 1.6f * c.deess);
            L[i] -= hl * cut; R[i] -= hr * cut;
        }
    }

    // 5 air & width: a little detuned double, out to the sides (low end stays in the middle)
    if (c.airOn && c.width > 0.01f)
    {
        juce::AudioBuffer<float> dbl (2, n);
        const int dl = (int) (0.011 * sr), dr = (int) (0.017 * sr);
        float ph = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            ph += 0.35f * juce::MathConstants<float>::twoPi / fsr;
            const float wob = 0.0006f * fsr * std::sin (ph);
            auto tap = [&] (const float* d, float back) { const float p = (float) i - back; const int a = (int) std::floor (p); const float f = p - (float) a;
                                                          return a >= 0 && a + 1 < n ? d[a] + (d[a + 1] - d[a]) * f : 0.0f; };
            dbl.setSample (0, i, tap (L, (float) dl + wob));
            dbl.setSample (1, i, tap (R, (float) dr - wob));
        }
        auto hpL = Bq::make (3, sr, 300.0, 0.7, 0), hpR = Bq::make (3, sr, 300.0, 0.7, 0);
        const float amt = 0.45f * c.width;
        for (int i = 0; i < n; ++i)
        {
            const float side = 0.5f * (hpL.run (dbl.getSample (0, i)) - hpR.run (dbl.getSample (1, i)));
            L[i] += side * amt * 2.0f; R[i] -= side * amt * 2.0f;
        }
    }

    // 6 delay: in time with the song, bouncing left / right, darker each repeat
    std::vector<float> verbInL ((size_t) n), verbInR ((size_t) n);
    for (int i = 0; i < n; ++i) { verbInL[(size_t) i] = L[i]; verbInR[(size_t) i] = R[i]; }
    if (c.delayOn && c.delayMix > 0.01f)
    {
        const double beat = 60.0 / std::max (40.0, bpm);
        const double t = c.delayNote == 0 ? beat : c.delayNote == 2 ? beat * 0.5 : beat * 0.75;
        const int d = juce::jlimit (100, (int) (2.5 * sr), (int) (t * sr));
        std::vector<float> bl ((size_t) d, 0.0f), br ((size_t) d, 0.0f);
        auto lpL = Bq::make (4, sr, 4500.0, 0.6, 0), lpR = Bq::make (4, sr, 4500.0, 0.6, 0);
        int w = 0;
        for (int i = 0; i < n; ++i)
        {
            const float eL = bl[(size_t) w], eR = br[(size_t) w];
            const float in = 0.5f * (L[i] + R[i]);
            bl[(size_t) w] = in + lpR.run (eR) * 0.45f;
            br[(size_t) w] = lpL.run (eL) * 0.45f;
            L[i] += eL * c.delayMix * 1.3f; R[i] += eR * c.delayMix * 1.3f;
            verbInL[(size_t) i] += eL * 0.3f; verbInR[(size_t) i] += eR * 0.3f;
            if (++w >= d) w = 0;
        }
    }

    // 7 reverb: a plate-like room, its low end kept out so the vocal stays clear
    if (c.verbOn && c.verbMix > 0.01f)
    {
        juce::Reverb room;
        room.setSampleRate (sr);
        juce::Reverb::Parameters rp;
        rp.roomSize = 0.45f + 0.5f * c.verbSize; rp.damping = 0.55f - 0.25f * c.verbSize; rp.wetLevel = 1.0f; rp.dryLevel = 0.0f; rp.width = 1.0f;
        room.setParameters (rp);
        auto hpL = Bq::make (3, sr, 250.0, 0.7, 0), hpR = Bq::make (3, sr, 250.0, 0.7, 0);
        for (int i = 0; i < n; ++i) { verbInL[(size_t) i] = hpL.run (verbInL[(size_t) i]); verbInR[(size_t) i] = hpR.run (verbInR[(size_t) i]); }
        room.processStereo (verbInL.data(), verbInR.data(), n);
        const float wet = 0.9f * c.verbMix;
        for (int i = 0; i < n; ++i) { L[i] += verbInL[(size_t) i] * wet; R[i] += verbInR[(size_t) i] * wet; }
    }
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
