#include "Engine.h"

namespace lp
{

namespace
{
    constexpr int kChunk = 32;            // events are placed with ~0.7 ms precision
    constexpr float kTwoPi = 6.2831853f;

    inline float mtof (float note) { return 440.0f * std::pow (2.0f, (note - 69.0f) / 12.0f); }
    inline float cents (float c)   { return std::pow (2.0f, c / 1200.0f); }
    inline long floorDiv (long a, long b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
    inline float decayMul (float seconds, double sr)
    {
        return (float) std::exp (std::log (0.001) / (std::max (0.01f, seconds) * sr));
    }
}

Engine::Engine()
{
    builtIns[0] = sources::glass();
    builtIns[1] = sources::choir();
    builtIns[2] = sources::feltKeys();
    builtIns[3] = sources::musicBox();

    for (size_t i = 0; i < hann.size(); ++i)
        hann[i] = 0.5f - 0.5f * std::cos (kTwoPi * (float) i / (float) (hann.size() - 1));

    padGrains.resize (768);
    dropGrains.resize (512);
    pending.reserve (256);
    currentPad.reserve (16);
    scratchPad.reserve (16);
    melodyPool.reserve (64);
    scratchMelody.reserve (64);
    chordPcs.reserve (16);
    scratchPcs.reserve (16);
}

void Engine::prepare (double sampleRate)
{
    sr = sampleRate;
    wobbleL.prepare ((int) (0.02 * sr));
    wobbleR.prepare ((int) (0.02 * sr));
    echoL.prepare ((int) (3.0 * sr));
    echoR.prepare ((int) (3.0 * sr));
    shimmerL.prepare();
    shimmerR.prepare();
    reverb.setSampleRate (sr);
    reverb.reset();
    thunderFilter.setup (140.0f, 0.7f, (float) sr);
    echoFiltL.setup (3800.0f, 0.6f, (float) sr);
    echoFiltR.setup (3800.0f, 0.6f, (float) sr);
    rainFilt.setup (5200.0f, 0.5f, (float) sr);

    for (auto* b : { &padL, &padR, &dropL, &dropR, &revL, &revR, &shimFbL, &shimFbR })
        b->assign ((size_t) kChunk, 0.0f);

    for (auto& g : padGrains) g.active = false;
    for (auto& g : dropGrains) g.active = false;
    for (auto& v : padVoices) v.active = false;
    for (auto& v : dropVoices) v.active = false;
    pending.clear();
    currentPad.clear();
    currentChordId = 0xffffffffu;
    lastStep = -999999;
    bassEnv = bassTargetEnv = 0.0f;
    bassMidiNote = -1;
    thunderEnv = 0.0f;
}

const SourceSound& Engine::activeSource (const EngineParams& p) const
{
    if (p.sound == SoundId::UserSample)
        if (auto* u = userSource.load())
            if (u->data.size() > 2048)
                return *u;
    const int idx = p.sound == SoundId::UserSample ? 0 : (int) p.sound;
    return builtIns[(size_t) juce::jlimit (0, 3, idx)];
}

bool Engine::popVisual (VisualEvent& e)
{
    int s1, n1, s2, n2;
    visualFifo.prepareToRead (1, s1, n1, s2, n2);
    if (n1 + n2 == 0) return false;
    e = visualBuf[(size_t) (n1 > 0 ? s1 : s2)];
    visualFifo.finishedRead (1);
    return true;
}

void Engine::pushVisual (int note, float vel, int layer)
{
    int s1, n1, s2, n2;
    visualFifo.prepareToWrite (1, s1, n1, s2, n2);
    if (n1 + n2 == 0) return;
    visualBuf[(size_t) (n1 > 0 ? s1 : s2)] = { note, vel, layer };
    visualFifo.finishedWrite (1);
}

//==============================================================================
// Weather -> harmony

void Engine::computeHarmony (long bar, const EngineParams& p, const std::bitset<128>& held,
                             std::vector<int>& padNotes, int& bassNote, uint32_t& chordId)
{
    const Weather& w = current;
    keyRoot = p.keyRoot >= 0 ? p.keyRoot : (int) (citySeed % 12);

    if (p.scale >= 0)
        scaleId = (ScaleId) juce::jlimit (0, 4, p.scale);
    else if (! autoScaleSet || floorDiv (bar, 8) != autoScaleBlock)
    {
        // re-read the sky's mood every 8 bars so the scale never flickers
        autoScale = scaleForMood (w.mood());
        autoScaleSet = true;
        autoScaleBlock = floorDiv (bar, 8);
        scaleId = autoScale;
    }
    else
        scaleId = autoScale;

    const Scale& sc = getScale (scaleId);
    scratchPcs.clear();
    scratchMelody.clear();

    if (held.any())
    {
        // You are holding keys: your notes become the chord, the weather plays it.
        int lowest = -1;
        uint32_t h = 0x51ed27u;
        for (int n = 0; n < 128; ++n)
            if (held[(size_t) n])
            {
                if (lowest < 0) lowest = n;
                const int pc = n % 12;
                if (std::find (scratchPcs.begin(), scratchPcs.end(), pc) == scratchPcs.end())
                    scratchPcs.push_back (pc);
                h = hash3 (h, (uint32_t) n, 3);
            }
        chordId = h;
        bassNote = 33 + ((lowest % 12) - 9 + 12) % 12;
        for (int n = 72; n <= 96; ++n)
            if (std::find (scratchPcs.begin(), scratchPcs.end(), n % 12) != scratchPcs.end())
                scratchMelody.push_back (n);
        uiChordRoot = lowest % 12;
        uiChordMinor = -1;
    }
    else
    {
        const bool slow = w.windNorm() < 0.45f || w.snowNorm() > 0.3f;
        const int barsPerChord = slow ? 2 : 1;
        const long slot = floorDiv (bar, barsPerChord);
        const long cycle = floorDiv (slot, 4);
        const float tension = w.tension();
        const uint32_t bucket = tension < 0.33f ? 0u : (tension < 0.62f ? 1u : 2u);
        const auto& prog = pickProgression (tension, hash3 ((uint32_t) cycle, citySeed, bucket));
        int degree = prog.degrees[(size_t) (((slot % prog.len) + prog.len) % prog.len)];

        auto fifthOf = [&sc] (int d) { return scaleSemis (sc, d + 4) - scaleSemis (sc, d); };
        if (fifthOf (degree) == 6)
            degree = (degree + 5) % 7; // avoid the harsh diminished chord

        const int nTones = 3 + (int) std::lround (w.cloudNorm() * 3.0f); // clouds = thicker chords
        const bool sus = w.snowNorm() > 0.35f || w.fog;

        // stack thirds from the scale, keep only notes that don't clash
        const int rootSemis = scaleSemis (sc, degree);
        std::array<int, 8> tones {};
        int count = 0;
        auto addStep = [&] (int stepOffset, bool force)
        {
            const int semis = scaleSemis (sc, degree + stepOffset) - rootSemis;
            if (! force)
                for (int i = 0; i < count; ++i)
                    if ((((semis - tones[(size_t) i]) % 12) + 12) % 12 == 1)
                        return;
            tones[(size_t) count++] = semis;
        };
        addStep (0, true);
        addStep (sus ? 1 : 2, true);
        addStep (4, true);
        for (int ext : { 6, 8, 12, 10 })
            if (count < nTones) addStep (ext, false);

        for (int i = 0; i < count; ++i)
            scratchPcs.push_back ((keyRoot + rootSemis + tones[(size_t) i]) % 12);

        const int rootPc = (keyRoot + rootSemis) % 12;
        bassNote = 33 + (rootPc - 9 + 12) % 12;

        const bool minorThird = (scaleSemis (sc, degree + 2) - rootSemis) == 3;
        chordId = hash3 ((uint32_t) (keyRoot * 16 + (int) scaleId), (uint32_t) (degree * 8 + count), sus ? 1u : 0u);
        uiChordRoot = rootPc;
        uiChordMinor = sus ? 2 : (minorThird ? 1 : 0);

        // melody notes: the pentatonic scale of the key plus the chord tones
        static const int majPent[] = { 0, 2, 4, 7, 9 };
        static const int minPent[] = { 0, 3, 5, 7, 10 };
        const int* pent = sc.majorish ? majPent : minPent;
        for (int n = 72; n <= 96; ++n)
        {
            const int rel = (n - keyRoot + 120) % 12;
            bool ok = std::find (scratchPcs.begin(), scratchPcs.end(), n % 12) != scratchPcs.end();
            for (int i = 0; i < 5 && ! ok; ++i) ok = pent[i] == rel;
            if (ok) scratchMelody.push_back (n);
        }
    }

    uiKey = keyRoot;
    uiScale = (int) scaleId;

    // Voice the chord near the last one so the pads glide instead of jumping.
    padNotes.clear();
    const int m = (int) scratchPcs.size();
    if (m == 0) return;
    float bestCost = 1e9f;
    std::array<int, 12> cand {}, best {};
    int bestCount = 0;
    for (int inv = 0; inv < m; ++inv)
        for (int baseOct = 3; baseOct <= 5; ++baseOct)
            for (int spread = 0; spread < 2; ++spread)
            {
                int c = 0;
                int prev = 12 * (baseOct + 1) + scratchPcs[(size_t) inv];
                cand[(size_t) c++] = prev;
                for (int j = 1; j < m && c < 12; ++j)
                {
                    int note = prev + 1;
                    while (note % 12 != scratchPcs[(size_t) ((inv + j) % m)]) ++note;
                    cand[(size_t) c++] = note;
                    prev = note;
                }
                if (spread && c >= 3) cand[1] += 12;
                std::sort (cand.begin(), cand.begin() + c);
                if (cand[0] < 50 || cand[(size_t) c - 1] > 88) continue;

                float mean = 0;
                for (int i = 0; i < c; ++i) mean += (float) cand[(size_t) i];
                mean /= (float) c;
                float cost = std::abs (mean - 64.0f) * 0.5f;
                for (int i = 0; i < c; ++i)
                {
                    int dmin = 24;
                    for (auto pn : currentPad) dmin = std::min (dmin, std::abs (pn - cand[(size_t) i]));
                    cost += currentPad.empty() ? 0.0f : (float) dmin;
                }
                if (cost < bestCost) { bestCost = cost; best = cand; bestCount = c; }
            }
    for (int i = 0; i < bestCount; ++i) padNotes.push_back (best[(size_t) i]);
}

void Engine::changeChord (const std::vector<int>& padNotes, int bassNote, int offset, float strumMs, MidiOut& midi)
{
    // let go of notes that are not in the new chord; shared notes keep ringing
    for (auto& v : padVoices)
        if (v.active && v.stage != 2 && std::find (padNotes.begin(), padNotes.end(), v.note) == padNotes.end())
        {
            v.stage = 2;
            midi.add (offset, 1, v.note, 0.0f, false);
        }

    int idx = 0;
    for (auto n : padNotes)
    {
        const bool alreadyOn = std::find (currentPad.begin(), currentPad.end(), n) != currentPad.end();
        if (! alreadyOn && pending.size() < pending.capacity())
        {
            const float vel = 0.55f + 0.3f * h01 (hash3 ((uint32_t) n, citySeed, (uint32_t) idx));
            pending.push_back ({ (int) (idx * strumMs * 0.001 * sr), 0, n, vel });
            ++idx;
        }
    }
    currentPad = padNotes;
    melodyPool = scratchMelody;
    chordPcs = scratchPcs;

    // bass
    if (bassMidiNote >= 0) midi.add (offset, 3, bassMidiNote, 0.0f, false);
    bassMidiNote = bassNote;
    midi.add (offset, 3, bassNote, 0.7f, true);
    bassTargetFreq = mtof ((float) bassNote);
    if (bassEnv < 0.05f) bassFreq = bassTargetFreq;
    bassTargetEnv = 1.0f;
    pushVisual (bassNote, 0.7f, 2);
}

void Engine::startPad (int note, float vel, int offset, MidiOut& midi)
{
    Voice* v = nullptr;
    for (auto& c : padVoices) if (! c.active) { v = &c; break; }
    if (v == nullptr) // steal the quietest
    {
        v = &padVoices[0];
        for (auto& c : padVoices) if (c.env < v->env) v = &c;
    }
    const Weather& w = current;
    const float attack = 1.0f + w.snowNorm() * 1.5f + (1.0f - w.windNorm()) * 0.7f;
    *v = {};
    v->active = true;
    v->note = note;
    v->vel = vel;
    v->attackPerSample = 1.0f / (float) (attack * sr);
    v->releaseMul = decayMul (2.8f + w.snowNorm() * 2.0f, sr);
    v->layer = 0;
    v->pan = (h01 (hash3 ((uint32_t) note, citySeed, 9)) * 2.0f - 1.0f) * 0.55f;
    midi.add (offset, 1, note, vel, true);
    pushVisual (note, vel, 0);
}

void Engine::startDrop (int note, float vel, int offset, MidiOut& midi, float decaySec, bool withBell)
{
    Voice* v = nullptr;
    for (auto& c : dropVoices) if (! c.active) { v = &c; break; }
    if (v == nullptr)
    {
        v = &dropVoices[0];
        for (auto& c : dropVoices) if (c.env < v->env) v = &c;
    }
    *v = {};
    v->active = true;
    v->note = note;
    v->vel = vel;
    v->attackPerSample = 1.0f / (float) (0.004 * sr);
    v->releaseMul = decayMul (decaySec, sr);
    v->layer = 1;
    const float swing = std::sin (windLfo * 0.7f) * current.windNorm() * 0.5f;
    v->pan = juce::jlimit (-1.0f, 1.0f, (rng.nextFloat() * 2.0f - 1.0f) * 0.6f + swing);
    v->bellEnv = withBell ? 1.0f : 0.0f;
    midi.add (offset, 2, note, vel, true);
    if (pending.size() < pending.capacity())
        pending.push_back ({ (int) (0.2 * sr), 2, note, 0.0f });
    pushVisual (note, vel, 1);
}

void Engine::triggerThunder()
{
    thunderEnv = 1.0f;
    thunderFreq = 62.0f;
    pushVisual (0, 1.0f, 3);
}

//==============================================================================
// The 16th-note clock

void Engine::runStep (long step, int offset, const EngineParams& p, const std::bitset<128>& held, MidiOut& midi)
{
    const Weather& w = current;
    const int stepInBar = (int) (((step % 16) + 16) % 16);
    const long bar = floorDiv (step, 16);
    const uint32_t s = (uint32_t) step;

    const bool heldChanged = held != lastHeld;
    lastHeld = held;
    if (stepInBar == 0 || heldChanged || currentChordId == 0xffffffffu)
    {
        int bassNote = 36;
        uint32_t id = 0;
        computeHarmony (bar, p, held, scratchPad, bassNote, id);
        if (id != currentChordId && ! scratchPad.empty())
        {
            currentChordId = id;
            changeChord (scratchPad, bassNote, offset, 30.0f + w.windNorm() * 420.0f, midi);
        }
    }

    if (melodyPool.empty()) return;

    const float rainN = w.rainNorm(), snowN = w.snowNorm(), windN = w.windNorm();
    const bool eighth = (stepInBar % 2) == 0;
    const bool beat = (stepInBar % 4) == 0;

    // Rain = melody. Light drizzle is a few notes, a storm is a flurry.
    float prob = rainN * (eighth ? 0.55f : 0.32f);
    if (! w.thunder && w.cloudPct < 45.0f && rainN < 0.05f)
        prob += w.isDay ? (eighth ? 0.09f : 0.0f) : (beat ? 0.07f : 0.0f); // sun glints / starlight
    if (eighth) prob += snowN * 0.3f;
    if (w.fog) prob *= 0.6f;

    if (h01 (hash3 (s, citySeed, 77)) < prob)
    {
        // gentle random walk through the melody notes
        const int count = (int) melodyPool.size();
        int idx = 0;
        for (int i = 0; i < count; ++i)
            if (std::abs (melodyPool[(size_t) i] - lastDrop) < std::abs (melodyPool[(size_t) idx] - lastDrop))
                idx = i;
        const float r = h01 (hash3 (s, citySeed, 79));
        if (r < 0.12f) idx = (int) (h01 (hash3 (s, citySeed, 80)) * (float) count);
        else idx += (int) std::floor (r * 5.0f) - 2;
        idx = juce::jlimit (0, count - 1, idx);
        int note = melodyPool[(size_t) idx];

        if (beat) // land on a chord tone on the beat
            for (int d = 0; d < 6; ++d)
            {
                if (std::find (chordPcs.begin(), chordPcs.end(), (note + d) % 12) != chordPcs.end() && note + d <= 96) { note += d; break; }
                if (std::find (chordPcs.begin(), chordPcs.end(), (note - d) % 12) != chordPcs.end() && note - d >= 72) { note -= d; break; }
            }
        lastDrop = note;
        float vel = 0.35f + 0.45f * h01 (hash3 (s, citySeed, 78)) + (beat ? 0.1f : 0.0f);
        vel *= 1.0f - snowN * 0.35f;
        const float decay = 0.8f + snowN * 2.2f + (1.0f - rainN) * 0.8f;
        startDrop (note, vel, offset, midi, decay, true);
    }

    // Wind = soft rolling arpeggio notes from the chord, an octave up
    if (eighth && ! currentPad.empty() && h01 (hash3 (s, citySeed, 91)) < windN * 0.35f)
    {
        const int i = (int) (h01 (hash3 (s, citySeed, 92)) * (float) currentPad.size());
        startDrop (currentPad[(size_t) juce::jlimit (0, (int) currentPad.size() - 1, i)] + 12,
                   0.28f, offset, midi, 1.8f, false);
    }

    // Lightning = thunder boom plus a bright chord hit
    if (w.thunder && stepInBar == 0 && h01 (hash3 ((uint32_t) bar, citySeed, 5)) < 0.2f)
    {
        triggerThunder();
        for (auto n : currentPad)
            startDrop (n + 12, 0.45f, offset, midi, 2.6f, true);
    }
}

//==============================================================================

void Engine::process (float* L, float* R, int n, const EngineParams& p, const TransportInfo& t,
                      const std::bitset<128>& held, MidiOut& midi)
{
    const float a = 1.0f - std::exp (-(float) n / (float) sr / 4.0f); // ~4 s glide between skies
    current.glideTo (target, a);

    curBpm = t.bpm > 0.0 ? t.bpm : (double) (70.0f + current.windNorm() * 20.0f - current.snowNorm() * 8.0f);
    uiBpm = (float) curBpm;
    const double ppqPerSample = curBpm / 60.0 / sr;
    const double ppq = t.hostPlaying ? t.ppq : freePpq;

    for (int off = 0; off < n; off += kChunk)
    {
        const int len = std::min (kChunk, n - off);
        const double cur = ppq + off * ppqPerSample;
        const long step = (long) std::floor (cur * 4.0 + 1e-7);
        if (step != lastStep)
        {
            lastStep = step;
            runStep (step, off, p, held, midi);
        }

        for (size_t i = 0; i < pending.size();)
        {
            auto& e = pending[i];
            if (e.samplesLeft <= 0)
            {
                if (e.kind == 0) startPad (e.note, e.vel, off, midi);
                else if (e.kind == 2) midi.add (off, 2, e.note, 0.0f, false);
                pending[i] = pending.back();
                pending.pop_back();
            }
            else
            {
                e.samplesLeft -= len;
                ++i;
            }
        }

        renderChunk (L + off, R + off, len, p);
    }

    freePpq = ppq + n * ppqPerSample;
}

void Engine::spawnGrain (std::vector<Grain>& pool, const SourceSound& src, double startPos, double ratio,
                         int len, float gain, float pan)
{
    const int dataLen = (int) src.data.size();
    if (dataLen < 16 || len < 8) return;
    if ((double) len * ratio > dataLen - 4)
        len = (int) ((dataLen - 4) / ratio * 0.9);
    const double maxStart = dataLen - 3 - len * ratio;
    startPos = std::fmod (std::max (0.0, startPos), std::max (1.0, maxStart));

    for (auto& g : pool)
        if (! g.active)
        {
            g.active = true;
            g.data = src.data.data();
            g.dataLen = dataLen;
            g.pos = startPos;
            g.inc = ratio;
            g.len = len;
            g.age = 0;
            const float a = (juce::jlimit (-1.0f, 1.0f, pan) + 1.0f) * 0.25f * dsp::kPi;
            g.gl = std::cos (a) * gain;
            g.gr = std::sin (a) * gain;
            return;
        }
}

void Engine::renderChunk (float* outL, float* outR, int n, const EngineParams& p)
{
    const Weather& w = current;
    const float rainN = w.rainNorm(), windN = w.windNorm(), snowN = w.snowNorm();
    const auto fsr = (float) sr;

    std::fill (padL.begin(), padL.begin() + n, 0.0f);
    std::fill (padR.begin(), padR.begin() + n, 0.0f);
    std::fill (dropL.begin(), dropL.begin() + n, 0.0f);
    std::fill (dropR.begin(), dropR.begin() + n, 0.0f);

    const SourceSound& src = activeSource (p);
    const double rateFix = src.sampleRate / sr;
    const double srcLen = (double) src.data.size();
    const float spray = juce::jlimit (0.0f, 1.0f, p.texture + windN * 0.3f);
    const float padGrainSec = juce::jlimit (0.02f, 0.6f, p.grainMs * 0.001f * (1.0f + snowN * 0.8f));
    const int padGrainLen = (int) (padGrainSec * fsr);
    const int dropGrainLen = (int) (juce::jlimit (0.015f, 0.16f, padGrainSec * 0.35f) * fsr);
    constexpr float padOverlap = 6.0f, dropOverlap = 3.0f;
    const float padNorm = 0.32f / std::sqrt (padOverlap);
    const float dropNorm = 0.45f / std::sqrt (dropOverlap);

    // --- pads
    for (auto& v : padVoices)
    {
        if (! v.active) continue;
        if (v.stage == 0) { v.env += v.attackPerSample * (float) n; if (v.env >= 1.0f) { v.env = 1.0f; v.stage = 1; } }
        else if (v.stage == 2) { v.env *= std::pow (v.releaseMul, (float) n); if (v.env < 0.0003f) { v.active = false; continue; } }

        v.countdown -= n;
        const double interval = padGrainLen / padOverlap;
        while (v.countdown <= 0)
        {
            v.countdown += interval * (0.7 + 0.6 * rng.nextDouble());
            double ratio = mtof ((float) v.note) / src.rootHz * rateFix
                         * cents ((rng.nextFloat() * 2.0f - 1.0f) * (4.0f + spray * 14.0f));
            float gain = v.env * v.vel * p.padLevel * padNorm;
            if (rng.nextFloat() < p.sparkle * 0.3f) { ratio *= 2.0; gain *= 0.5f; }
            const double pos = (p.position + (rng.nextFloat() - 0.5f) * spray * 0.6f + 1.0f) * srcLen;
            const float pan = v.pan + (rng.nextFloat() * 2.0f - 1.0f) * (0.25f + 0.6f * spray);
            spawnGrain (padGrains, src, pos, ratio, padGrainLen, gain, pan);
        }
    }

    // --- drops (rain notes): grains plus a clear bell tone so every note rings true
    const float bellLevel = p.dropLevel * 0.16f * (1.0f - 0.5f * snowN);
    for (auto& v : dropVoices)
    {
        if (! v.active) continue;
        if (v.stage == 0) { v.env += v.attackPerSample * (float) n; if (v.env >= 1.0f) { v.env = 1.0f; v.stage = 2; } }
        else { v.env *= std::pow (v.releaseMul, (float) n); if (v.env < 0.0003f && v.bellEnv < 0.0003f) { v.active = false; continue; } }

        v.countdown -= n;
        const double interval = dropGrainLen / dropOverlap;
        while (v.countdown <= 0)
        {
            v.countdown += interval * (0.7 + 0.6 * rng.nextDouble());
            double ratio = mtof ((float) v.note) / src.rootHz * rateFix * cents ((rng.nextFloat() * 2.0f - 1.0f) * 6.0f);
            float gain = v.env * v.vel * p.dropLevel * dropNorm;
            if (rng.nextFloat() < p.sparkle * 0.35f) { ratio *= 2.0; gain *= 0.55f; }
            const double pos = (p.position + (rng.nextFloat() - 0.5f) * spray * 0.4f + 1.0f) * srcLen;
            spawnGrain (dropGrains, src, pos, ratio, dropGrainLen, gain, v.pan + (rng.nextFloat() - 0.5f) * 0.3f);
        }

        if (v.bellEnv > 0.0003f)
        {
            const float f = mtof ((float) v.note);
            const float inc = kTwoPi * f / fsr;
            const float mul = std::pow (v.releaseMul, 1.6f);
            const float a = (v.pan + 1.0f) * 0.25f * dsp::kPi;
            const float gl = std::cos (a) * bellLevel * v.vel, gr = std::sin (a) * bellLevel * v.vel;
            for (int i = 0; i < n; ++i)
            {
                const float ph = (float) v.bellPhase;
                const float s = (std::sin (ph) + 0.22f * std::sin (2.0f * ph) + 0.08f * std::sin (3.0f * ph)) * v.bellEnv;
                const float atk = std::min (1.0f, v.env * 1.5f + 0.3f);
                dropL[(size_t) i] += s * gl * atk;
                dropR[(size_t) i] += s * gr * atk;
                v.bellPhase += inc;
                if (v.bellPhase > 6.2831853) v.bellPhase -= 6.2831853;
                v.bellEnv *= mul;
            }
        }
    }

    // --- render grains
    auto renderPool = [this, n] (std::vector<Grain>& pool, std::vector<float>& bl, std::vector<float>& br)
    {
        constexpr float wScale = 1024.0f;
        for (auto& g : pool)
        {
            if (! g.active) continue;
            const float invLen = wScale / (float) g.len;
            for (int i = 0; i < n; ++i)
            {
                if (g.age >= g.len) { g.active = false; break; }
                const int i0 = (int) g.pos;
                const float fr = (float) (g.pos - i0);
                const float s = g.data[i0] + (g.data[i0 + 1] - g.data[i0]) * fr;
                const float v = s * hann[(size_t) ((float) g.age * invLen)];
                bl[(size_t) i] += v * g.gl;
                br[(size_t) i] += v * g.gr;
                g.pos += g.inc;
                ++g.age;
            }
        }
    };
    renderPool (padGrains, padL, padR);
    renderPool (dropGrains, dropL, dropR);

    // --- effects settings for this chunk
    const float warm = juce::jlimit (0.0f, 1.0f, p.warmth + (1.0f - w.warmthNorm()) * 0.12f);
    const float toneHz = 17000.0f * std::pow (0.2f, warm) * (w.isDay ? 1.0f : 0.8f);
    toneL.setup (toneHz, 0.6f, fsr);
    toneR.setup (toneHz, 0.6f, fsr);
    const float drive = 1.0f + warm * 1.6f;
    const float windHz = 380.0f + 950.0f * (0.5f + 0.5f * std::sin (windLfo));
    windFilt.setup (windHz, 1.4f, fsr);
    const float echoDelay = juce::jlimit (100.0f, (float) (2.9 * sr), (float) (0.75 * 60.0 / curBpm * sr));
    const float echoFb = 0.3f + 0.35f * p.echo;
    const float space = juce::jlimit (0.0f, 1.0f, p.space + w.humidityNorm() * 0.08f + (w.fog ? 0.15f : 0.0f));

    juce::Reverb::Parameters rp;
    rp.roomSize = 0.8f + 0.17f * space;
    rp.damping = 0.3f + warm * 0.35f;
    rp.wetLevel = 1.0f;
    rp.dryLevel = 0.0f;
    rp.width = 1.0f;
    if (std::abs (rp.roomSize - lastRoom) > 0.002f || std::abs (rp.damping - lastDamp) > 0.002f)
    {
        reverb.setParameters (rp);
        lastRoom = rp.roomSize;
        lastDamp = rp.damping;
    }

    const float bassGain = p.bassLevel * 0.3f;
    const float bassAtk = 1.0f - std::exp (-1.0f / (0.25f * fsr));
    const float bassGlide = 1.0f - std::exp (-1.0f / (0.08f * fsr));
    const float thunderMul = decayMul (3.5f, sr);
    const float rainLevel = p.atmos * rainN * 0.22f;
    const float crackleProb = rainN * 0.004f;
    const float windLevel = p.atmos * windN * 0.3f;
    const float wobbleDepth = warm * 0.0011f * fsr;
    const float wobbleInc = kTwoPi * 0.55f / fsr, flutterInc = kTwoPi * 6.3f / fsr;
    const float windInc = kTwoPi * (0.05f + 0.2f * windN) / fsr;
    const float shimmerAmt = p.shimmer * 0.3f;
    const float outGain = juce::Decibels::decibelsToGain (p.gainDb);

    for (int i = 0; i < n; ++i)
    {
        // bass: soft sine, glides between chord roots
        bassFreq += (bassTargetFreq - bassFreq) * bassGlide;
        bassEnv += (bassTargetEnv - bassEnv) * bassAtk;
        bassPhase += kTwoPi * bassFreq / fsr;
        if (bassPhase > 6.2831853) bassPhase -= 6.2831853;
        const float bass = ((float) std::sin (bassPhase) + 0.12f * (float) std::sin (2.0 * bassPhase)) * bassEnv * bassGain;

        // thunder rumble
        float thunder = 0.0f;
        if (thunderEnv > 0.0005f)
        {
            thunderFilter.tick (noise.next());
            thunderFreq *= 0.99997f;
            thunderPhase += kTwoPi * thunderFreq / fsr;
            thunder = (thunderFilter.lp * 3.0f + 0.5f * std::sin (thunderPhase)) * thunderEnv * 0.5f;
            thunderEnv *= thunderMul;
        }

        // real rain and wind noise
        float crack = noise.next() < (crackleProb * 2.0f - 1.0f) ? noise.next() * 6.0f : 0.0f;
        rainFilt.tick (noise.next() * 0.5f + crack);
        windFilt.tick (noise.next());
        windLfo += windInc;
        if (windLfo > kTwoPi) windLfo -= kTwoPi;
        const float wPan = 0.5f + 0.4f * std::sin (windLfo * 0.6f);
        const float rainS = rainFilt.bp * rainLevel;
        const float windS = windFilt.bp * windLevel;

        // echoes (mostly on the rain notes), bouncing left and right
        const float eL = echoL.read (echoDelay), eR = echoR.read (echoDelay);
        echoFiltL.tick (eL);
        echoFiltR.tick (eR);
        const float echoIn = (dropL[(size_t) i] + dropR[(size_t) i]) * 0.5f + (padL[(size_t) i] + padR[(size_t) i]) * 0.06f;
        echoL.push (echoIn + echoFiltR.lp * echoFb);
        echoR.push (echoFiltL.lp * echoFb);

        float xl = padL[(size_t) i] + dropL[(size_t) i] + bass + thunder + rainS + windS * (1.0f - wPan) + eL * p.echo * 0.8f;
        float xr = padR[(size_t) i] + dropR[(size_t) i] + bass + thunder + rainS + windS * wPan + eR * p.echo * 0.8f;

        // tape warmth: slow wobble, gentle darkening and soft saturation
        wobblePhase += wobbleInc; if (wobblePhase > kTwoPi) wobblePhase -= kTwoPi;
        flutterPhase += flutterInc; if (flutterPhase > kTwoPi) flutterPhase -= kTwoPi;
        const float wob = 0.004f * fsr + wobbleDepth * (0.8f * std::sin (wobblePhase) + 0.2f * std::sin (flutterPhase));
        wobbleL.push (xl);
        wobbleR.push (xr);
        xl = wobbleL.read (wob);
        xr = wobbleR.read (wob);
        toneL.tick (xl);
        toneR.tick (xr);
        xl = std::tanh (toneL.lp * drive) / drive;
        xr = std::tanh (toneR.lp * drive) / drive;

        padL[(size_t) i] = xl;  // reuse buffers for the reverb pass
        padR[(size_t) i] = xr;
        revL[(size_t) i] = xl + shimFbL[(size_t) i];
        revR[(size_t) i] = xr + shimFbR[(size_t) i];
    }

    reverb.processStereo (revL.data(), revR.data(), n);

    for (int i = 0; i < n; ++i)
    {
        const float rl = revL[(size_t) i], rr = revR[(size_t) i];
        // shimmer: the reverb tail is shifted up an octave and fed back in
        shimFbL[(size_t) i] = std::tanh (shimmerL.process (rl) * shimmerAmt);
        shimFbR[(size_t) i] = std::tanh (shimmerR.process (rr) * shimmerAmt);

        float yl = padL[(size_t) i] * (1.0f - 0.3f * space) + rl * space * 0.22f;
        float yr = padR[(size_t) i] * (1.0f - 0.3f * space) + rr * space * 0.22f;

        // remove DC, then a soft limiter so it never clips harshly
        dcL = yl - dcInL + 0.9995f * dcL; dcInL = yl;
        dcR = yr - dcInR + 0.9995f * dcR; dcInR = yr;
        outL[i] = std::tanh (dcL * outGain * 1.9f);
        outR[i] = std::tanh (dcR * outGain * 1.9f);
    }
}

} // namespace lp
