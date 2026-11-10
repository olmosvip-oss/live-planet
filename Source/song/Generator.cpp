#include "Song.h"
#include "../engine/Theory.h"
#include "../engine/Profiles.h"
#include <map>

namespace lp::song
{
namespace
{
    struct Rng
    {
        juce::Random r;
        explicit Rng (uint32_t seed) : r ((juce::int64) seed * 7919 + 17) {}
        float f() { return r.nextFloat(); }
        int i (int n) { return r.nextInt (std::max (1, n)); }
        bool chance (float p) { return r.nextFloat() < p; }
    };

    // nearest scale degree (0..6) for a chord root, relative to the key
    int degreeOf (int rootPc, int key, const lp::Scale& sc)
    {
        const int rel = ((rootPc - key) % 12 + 12) % 12;
        int best = 0, bestD = 99;
        for (int d = 0; d < 7; ++d)
        {
            const int dist = std::min (std::abs (sc.steps[(size_t) d] - rel), 12 - std::abs (sc.steps[(size_t) d] - rel));
            if (dist < bestD) { bestD = dist; best = d; }
        }
        return best;
    }

    // chord tones (as semitones above the key) for a scale degree, stacked in thirds
    std::vector<int> chordTones (const lp::Scale& sc, int degree, int count)
    {
        std::vector<int> t;
        const int root = lp::scaleSemis (sc, degree);
        for (int k = 0; k < count; ++k)
            t.push_back (lp::scaleSemis (sc, degree + k * 2) - root + root);
        return t;
    }

    std::string titleFor (Rng& rng, bool minor)
    {
        static const char* calm[] = { "quiet", "golden", "paper", "slow", "soft", "late", "silver", "little", "faded", "warm", "distant", "sleepy" };
        static const char* dusk[] = { "blue", "midnight", "hollow", "grey", "lonely", "rain", "cold", "lost", "dim", "violet", "far", "pale" };
        static const char* noun[] = { "harbour", "window", "orchard", "lantern", "satellite", "porch", "comet", "meadow",
                                      "radio", "postcard", "lighthouse", "garden", "tide", "summer", "station", "kite" };
        const char* a = minor ? dusk[rng.i (12)] : calm[rng.i (12)];
        return std::string (a) + " " + noun[rng.i (16)];
    }

    float sectionEnergy (const std::string& label)
    {
        if (label == "intro") return 0.25f;
        if (label == "verse") return 0.5f;
        if (label == "pre-chorus") return 0.62f;
        if (label == "chorus") return 0.9f;
        if (label == "bridge") return 0.55f;
        return 0.3f; // outro
    }
}

Target chooseTarget (const Profile& ref, const lp::Weather& sky, const Options& o)
{
    Rng rng (o.seed);
    Target t {};
    if (ref.valid)
    {
        t.bpm = std::round (ref.bpm * (1.0 + (rng.f() - 0.5) * (o.shiftKey ? 0.08 : 0.03)));
        static const int shifts[] = { -2, -1, 1, 2, 3, -3 };
        t.key = (ref.key + (o.shiftKey ? shifts[rng.i (6)] : 0) + 12) % 12;
        t.minor = ref.minor;
    }
    else
    {
        t.bpm = std::round (70.0 + sky.windNorm() * 24.0 + rng.f() * 10.0);
        t.key = rng.i (12);
        t.minor = sky.mood() < 0.45f;
    }
    if (o.style >= 0)
        t.style = (Style) juce::jlimit (0, kNumStyles - 1, o.style);
    else
    {
        // the groove that fits the reference's tempo and how busy it is
        const float d = ref.valid ? ref.density : 0.35f;
        const double b = t.bpm;
        if ((d > 0.5f && (b >= 128.0 || b <= 80.0)) || (b >= 130.0 && b <= 160.0 && ref.valid && ref.minor)) t.style = Style::Trap;
        else if (b >= 116.0 && b < 132.0 && d > 0.35f)  t.style = Style::House;
        else if (b >= 84.0 && b < 104.0 && d > 0.35f)   t.style = Style::BoomBap;
        else if (d < 0.25f)                              t.style = t.minor ? Style::Brushes : Style::Dusty;
        else if (b >= 104.0)                             t.style = Style::Electronic;
        else                                             t.style = Style::Dusty;
    }
    if (t.style == Style::Trap && t.bpm < 100.0 && ! ref.valid) t.bpm *= 2.0; // trap counts in double time
    if (t.style == Style::House) t.bpm = juce::jlimit (116.0, 128.0, t.bpm);
    if (o.dawBpm > 0.0) t.bpm = o.dawBpm; // your project's tempo wins
    return t;
}

Song generate (const Profile& ref, const lp::Weather& sky, const Options& o)
{
    Rng rng (o.seed);
    Song song;

    // ---- tempo, key, mood, groove
    const auto target = chooseTarget (ref, sky, o);
    rng.f(); rng.f(); rng.f(); // keep the rest of the song independent of the target's dice
    song.bpm = o.forceBpm > 0.0 ? o.forceBpm : target.bpm;
    song.key = o.forceKey >= 0 ? o.forceKey : target.key;
    song.minor = o.forceKey >= 0 ? o.forceMinor : target.minor;
    song.style = target.style;
    const float bright = ref.valid ? ref.brightness : sky.warmthNorm();
    const auto scaleId = song.minor ? (bright > 0.55f ? lp::ScaleId::Dorian : lp::ScaleId::Minor)
                                    : (bright > 0.7f ? lp::ScaleId::Lydian : lp::ScaleId::Major);
    const auto& sc = lp::getScale (scaleId);
    const float density = ref.valid ? ref.density : 0.35f + sky.windNorm() * 0.3f;
    song.title = titleFor (rng, song.minor);

    // ---- structure
    struct Plan { std::string label; int bars; float energy; const RefSection* ref; };
    std::vector<Plan> plan;
    int refBars = 0;
    for (auto& s2 : ref.sections) refBars = std::max (refBars, s2.startBar + s2.bars);
    const bool shortRef = ref.valid && (ref.sections.size() < 4 || refBars < 32);
    if (shortRef)
    {
        const RefSection* main = &ref.sections.front();
        for (auto& s2 : ref.sections) if (s2.energy > main->energy) main = &s2;
        for (auto& [l, b] : std::vector<std::pair<std::string, int>> {
                 { "intro", 8 }, { "verse", 16 }, { "chorus", 16 }, { "verse", 16 }, { "chorus", 16 }, { "bridge", 8 }, { "chorus", 16 }, { "outro", 8 } })
            plan.push_back ({ l, b, sectionEnergy (l), main });
    }
    else if (ref.valid && ! ref.sections.empty())
    {
        float maxE = 0.01f;
        for (auto& s : ref.sections) maxE = std::max (maxE, s.energy);
        for (auto& s : ref.sections)
        {
            const int bars = juce::jlimit (4, 16, (int) std::round (s.bars / 4.0) * 4);
            plan.push_back ({ s.label, bars, 0.2f + 0.8f * s.energy / maxE, &s });
        }
    }
    else
    {
        for (auto& [l, b] : std::vector<std::pair<std::string, int>> {
                 { "intro", 8 }, { "verse", 16 }, { "pre-chorus", 4 }, { "chorus", 16 }, { "verse", 8 },
                 { "chorus", 16 }, { "bridge", 8 }, { "chorus", 16 }, { "outro", 8 } })
            plan.push_back ({ l, b, sectionEnergy (l), nullptr });
    }
    if (! o.full)
    {
        // a short sketch: intro, one verse, one chorus, outro
        std::vector<Plan> sketch;
        for (auto want : { "intro", "verse", "chorus", "outro" })
            for (auto& p : plan)
                if (p.label == want) { sketch.push_back (p); sketch.back().bars = std::min (p.bars, want == std::string ("intro") || want == std::string ("outro") ? 4 : 8); break; }
        if (sketch.size() >= 2) plan = sketch;
    }

    std::map<std::string, int> counts;
    int bar = 0;
    for (auto& p : plan)
    {
        const int n = ++counts[p.label];
        const bool numbered = p.label == "verse" || p.label == "chorus" || p.label == "pre-chorus" || p.label == "bridge";
        song.sections.push_back ({ numbered ? p.label + " " + std::to_string (n) : p.label, bar, p.bars, p.energy });
        bar += p.bars;
    }

    // ---- chord progressions: one per kind of section, borrowed loosely from the reference
    std::map<std::string, std::vector<int>> progressions;
    for (auto& p : plan)
    {
        if (progressions.count (p.label)) continue;
        std::vector<int> prog;
        if (p.ref != nullptr)
        {
            int last = 0;
            for (size_t b = 0; b < std::min<size_t> (8, p.ref->chordRoot.size()); ++b)
            {
                const int r = p.ref->chordRoot[b];
                last = r >= 0 ? degreeOf (r, (song.key + 12 - (o.shiftKey ? (song.key - ref.key + 12) % 12 : 0)) % 12, sc) : last;
                prog.push_back (last);
            }
            // a loop of 4 if the second half just repeats the first
            if (prog.size() == 8 && std::equal (prog.begin(), prog.begin() + 4, prog.begin() + 4)) prog.resize (4);
        }
        if (prog.size() < 2)
        {
            const auto& pp = lp::pickProgression (1.0f - p.energy * 0.6f, (uint32_t) rng.i (1000000));
            prog.assign (pp.degrees, pp.degrees + pp.len);
            if (prog.size() == 2) prog.insert (prog.end(), prog.begin(), prog.end());
        }
        // make it our own: swap some chords for close relatives
        static const int relative[7] = { 5, 3, 4, 1, 2, 0, 4 };
        for (auto& d : prog)
            if (rng.chance (o.shiftKey ? 0.35f : (p.label == "chorus" || p.label == "verse" ? 0.08f : 0.3f))) d = relative[(size_t) d];
        // avoid the harsh diminished chord
        for (auto& d : prog)
            if (lp::scaleSemis (sc, d + 4) - lp::scaleSemis (sc, d) == 6) d = (d + 5) % 7;
        if (prog.empty()) prog = { 0, 5, 3, 4 };
        progressions[p.label] = prog;
    }

    // ---- instruments
    const auto main = o.mainSound == lp::SoundId::Auto ? lp::autoSoundFor (sky) : o.mainSound;
    const bool mainStruck = lp::profileFor (main).padStruck;
    const lp::SoundId melodySound = mainStruck ? (song.minor ? lp::SoundId::Ocarina : lp::SoundId::Glass)
                                               : std::array<lp::SoundId, 4> { lp::SoundId::Rhodes, lp::SoundId::Kalimba, lp::SoundId::Vibes, lp::SoundId::MusicBox }[(size_t) rng.i (4)];
    const lp::SoundId counterSound = melodySound == lp::SoundId::Kalimba ? lp::SoundId::MusicBox : lp::SoundId::Kalimba;
    const lp::SoundId padSound = song.minor ? lp::SoundId::Choir : lp::SoundId::Strings;

    Part chords { "chords", PartKind::Chords, main, -2.0f, {} };
    Part melody { "melody", PartKind::Melody, melodySound, 0.0f, {} };
    Part counter { "sparkle", PartKind::Counter, counterSound, -7.0f, {} };
    Part pad { "pad", PartKind::Pad, padSound, -9.0f, {} };
    Part bass { "bass", PartKind::Bass, lp::SoundId::Glass, -3.0f, {} };
    Part drums { "drums", PartKind::Drums, lp::SoundId::Glass, -4.0f, {} };
    drums.variant = (int) song.style;
    bass.variant = song.style == Style::Trap ? 1 : 0;
    if (bass.variant == 1) bass.name = "808";

    // one melody idea per kind of section (2 bars on an 8th-note grid), reused with variations
    struct Motif { std::vector<int> slots; std::vector<int> steps; };
    std::map<std::string, Motif> motifs;
    auto motifFor = [&] (const std::string& label, float energy)
    {
        if (motifs.count (label)) return motifs[label];
        Motif m;
        const float busy = juce::jlimit (0.2f, 0.75f, 0.25f + density * 0.4f + (label == "chorus" ? -0.05f : 0.0f));
        for (int s = 0; s < 16; ++s)
        {
            const bool strong = s % 4 == 0;
            if (s == 0 || rng.chance (strong ? busy + 0.2f : busy * 0.6f)) m.slots.push_back (s);
        }
        int pos = 0;
        for (size_t k = 0; k < m.slots.size(); ++k)
        {
            int step = rng.i (5) - 2;              // small steps, like singing
            if (pos > 3) step = -std::abs (step);   // drift back to the middle
            if (pos < -3) step = std::abs (step);
            pos += step;
            m.steps.push_back (pos);
        }
        juce::ignoreUnused (energy);
        motifs[label] = m;
        return m;
    };

    std::vector<int> lastVoicing;
    for (size_t si = 0; si < song.sections.size(); ++si)
    {
        const auto& sec = song.sections[si];
        const auto& label = plan[si].label;
        const auto& prog = progressions[label];
        const float e = sec.energy;
        const bool isIntro = label == "intro", isOutro = label == "outro";
        const bool lastSection = si + 1 == song.sections.size();
        const float nextE = lastSection ? 0.0f : song.sections[si + 1].energy;
        const Motif motif = motifFor (label, e);
        const int melodyCentre = label == "chorus" ? 11 : (label == "bridge" ? 9 : 7); // scale steps above the key's middle note

        for (int b = 0; b < sec.bars; ++b)
        {
            const double t0 = (sec.startBar + b) * 4.0;
            const int degree = prog[(size_t) b % prog.size()];
            const int rootSemis = lp::scaleSemis (sc, degree);
            const int toneCount = e > 0.6f ? 4 : 3;
            const float fade = isOutro ? 1.0f - (float) b / (float) sec.bars * 0.6f : 1.0f;
            const float vj = (rng.f() - 0.5f) * 0.08f;

            // chords: voiced close to the previous chord so it flows
            std::vector<int> voicing;
            {
                std::vector<int> pcs;
                for (int k = 0; k < toneCount; ++k) pcs.push_back ((song.key + lp::scaleSemis (sc, degree + k * 2)) % 12);
                int bestCost = 1 << 20;
                for (int inv = 0; inv < toneCount; ++inv)
                    for (int base = 48; base <= 60; ++base)
                    {
                        if (base % 12 != pcs[(size_t) inv]) continue;
                        std::vector<int> v { base };
                        for (int k = 1; k < toneCount; ++k)
                        {
                            int nn = v.back() + 1;
                            while (nn % 12 != pcs[(size_t) ((inv + k) % toneCount)]) ++nn;
                            v.push_back (nn);
                        }
                        int cost = std::abs ((v.front() + v.back()) / 2 - 62) * 2;
                        for (size_t k = 0; k < v.size() && k < lastVoicing.size(); ++k) cost += std::abs (v[k] - lastVoicing[k]);
                        if (cost < bestCost) { bestCost = cost; voicing = v; }
                    }
                lastVoicing = voicing;
            }
            if (isIntro || isOutro || e < 0.35f)
                for (auto n : voicing) chords.notes.push_back ({ t0, 3.9, n, (0.5f + vj) * fade });
            else if (density < 0.45f)
                for (double off : { 0.0, 2.0 })
                    for (auto n : voicing) chords.notes.push_back ({ t0 + off, 1.9, n, 0.55f + vj + (off == 0 ? 0.08f : 0.0f) });
            else
                for (auto [off, len] : { std::pair { 0.0, 1.4 }, { 1.5, 1.4 }, { 3.0, 0.9 } })
                    for (auto n : voicing) chords.notes.push_back ({ t0 + off, len, n, 0.5f + vj + (off == 0 ? 0.12f : 0.0f) });

            // pad: long held chord on top in the big sections
            if ((label == "chorus" || label == "bridge") && e > 0.5f)
                for (auto n : voicing) pad.notes.push_back ({ t0, 4.0, n + 12, 0.45f });

            // bass
            const int bassNote = 36 + ((song.key + rootSemis) % 12);
            if (song.style == Style::Trap && (! isIntro || e > 0.4f))
            {
                const int n808 = bassNote - 12 + (bassNote - 12 < 29 ? 12 : 0); // deep but audible
                bass.notes.push_back ({ t0, 0.7, n808, 0.9f * fade });
                bass.notes.push_back ({ t0 + 2.5, 1.4, n808, 0.85f * fade });
                if (e > 0.6f && rng.chance (0.5f)) bass.notes.push_back ({ t0 + 3.5, 0.45, n808 + 12, 0.7f });
            }
            else if (song.style == Style::House && (! isIntro || e > 0.4f))
            {
                for (int k = 0; k < 4; ++k) bass.notes.push_back ({ t0 + k + 0.5, 0.4, bassNote, 0.8f * fade }); // off-beat house bass
            }
            else if (! isIntro || e > 0.4f)
            {
                if (density < 0.35f || isOutro)
                    bass.notes.push_back ({ t0, 3.5, bassNote, 0.75f * fade });
                else if (density < 0.65f)
                {
                    bass.notes.push_back ({ t0, 1.5, bassNote, 0.8f });
                    bass.notes.push_back ({ t0 + 2.5, 1.0, bassNote, 0.65f });
                    bass.notes.push_back ({ t0 + 3.5, 0.5, bassNote + (rng.chance (0.5f) ? 12 : 7), 0.55f });
                }
                else
                    for (int k = 0; k < 8; ++k)
                        bass.notes.push_back ({ t0 + k * 0.5, 0.45, bassNote + (k == 6 ? 7 : (k % 4 == 3 ? 12 : 0)), k % 2 == 0 ? 0.8f : 0.6f });
            }

            // melody: the section's idea, repeated with small changes, always landing on chord tones
            if (! isIntro && ! isOutro)
            {
                const int phrase = b / 2;
                const bool answer = phrase % 4 == 3;  // every 4th phrase answers instead of repeating
                const int half = b % 2;
                for (size_t k = 0; k < motif.slots.size(); ++k)
                {
                    const int slot = motif.slots[k];
                    if ((slot >= 8) != (half == 1)) continue;
                    const double beatPos = t0 + (slot % 8) * 0.5;
                    const int nextSlot = k + 1 < motif.slots.size() ? motif.slots[k + 1] : 16;
                    double len = std::min (2.0, (nextSlot - slot) * 0.5) * 0.92;
                    int deg = melodyCentre + motif.steps[k] + (answer ? (k + 1 == motif.slots.size() ? -melodyCentre % 7 : 1) : 0);
                    if (slot % 4 == 0) // strong beat: move to the nearest chord tone
                    {
                        int bestD = deg, bestDist = 99;
                        for (int c = -3; c <= 3; ++c)
                        {
                            const int cand = deg + c;
                            const int rel = ((cand - degree) % 7 + 7) % 7;
                            if ((rel == 0 || rel == 2 || rel == 4) && std::abs (c) < bestDist) { bestDist = std::abs (c); bestD = cand; }
                        }
                        deg = bestD;
                    }
                    const int pitch = 48 + song.key + lp::scaleSemis (sc, deg) - (song.key > 6 ? 12 : 0);
                    melody.notes.push_back ({ beatPos, len, juce::jlimit (55, 91, pitch), 0.6f + (slot % 4 == 0 ? 0.15f : 0.0f) + vj });
                }
            }

            // sparkle: a gentle arpeggio in the choruses
            if (label == "chorus" && e > 0.55f)
                for (int k = 0; k < 8; ++k)
                    counter.notes.push_back ({ t0 + k * 0.5, 0.45, voicing[(size_t) (k % (int) voicing.size())] + 24, k % 2 == 0 ? 0.5f : 0.38f });

            // drums: each style has its own groove
            const bool lastBar = b == sec.bars - 1;
            auto hit = [&] (double at, int note, float vel) { drums.notes.push_back ({ t0 + at, 0.1, note, juce::jlimit (0.05f, 1.0f, vel * fade) }); };
            const double swing = song.style == Style::BoomBap ? 0.09 : (song.style == Style::Dusty || song.style == Style::Brushes ? 0.06 : 0.0);
            auto sw = [swing] (double h) { return std::fmod (h, 1.0) == 0.5 ? h + swing : h; };
            if (isIntro && e < 0.35f)
            {
                if (b >= sec.bars / 2)
                    for (int k = 0; k < 4; ++k) hit (k, song.style == Style::Brushes ? 46 : 42, 0.3f);
            }
            else
            {
                const bool big = e > 0.6f;
                if (ref.valid && ref.hasGroove && ! (isIntro || isOutro))
                {
                    // the reference's groove (where its hits land), not its sound
                    const float lift = big ? 0.0f : 0.12f; // quieter sections keep only the strongest hits
                    for (int st = 0; st < 16; ++st)
                    {
                        const double at = st * 0.25;
                        const float lo = ref.grooveLow[(size_t) st], mi = ref.grooveMid[(size_t) st], hiS = ref.grooveHigh[(size_t) st];
                        if (lo > 0.5f + lift && ! (mi > lo && mi > 0.6f)) hit (at, 36, 0.6f + 0.4f * lo);
                        if (mi > 0.55f + lift && mi >= lo * 0.8f) hit (at, big ? 39 : 38, 0.55f + 0.4f * mi);
                        if (hiS > 0.3f + lift * 0.5f) hit (at, 42, 0.25f + 0.4f * hiS);
                    }
                    if (song.style == Style::Trap && big && rng.chance (0.5f)) // a roll now and then
                    {
                        const double rollAt = (1 + rng.i (3)) + 0.5;
                        for (double r2 = rollAt; r2 < rollAt + 0.5 - 1e-6; r2 += 0.125) hit (r2, 42, 0.35f + (float) (r2 - rollAt));
                    }
                }
                else
                switch (song.style)
                {
                    case Style::Trap:
                    {
                        // half-time snare, syncopated kicks, rolling hats
                        hit (0.0, 36, 1.0f);
                        if (rng.chance (0.6f)) hit (0.75, 36, 0.8f);
                        hit (2.5, 36, 0.85f);
                        if (big && rng.chance (0.5f)) hit (3.25, 36, 0.7f);
                        hit (2.0, 39, 0.95f);
                        for (double h = 0; h < 4.0; h += 0.5) hit (h, 42, std::fmod (h, 1.0) == 0.0 ? 0.6f : 0.4f);
                        // a hat roll somewhere in the bar
                        const double rollAt = (1 + rng.i (3)) + 0.5;
                        const double step = rng.chance (0.5f) ? 0.125 : 1.0 / 6.0;
                        for (double r = rollAt; r < rollAt + 0.5 - 1e-6; r += step) hit (r, 42, 0.35f + (float) (r - rollAt));
                        if (big && b % 2 == 1) hit (3.5, 46, 0.5f);
                        break;
                    }
                    case Style::House:
                        for (int k = 0; k < 4; ++k) hit (k, 36, 0.95f);
                        hit (1.0, 39, 0.8f); hit (3.0, 39, 0.8f);
                        for (int k = 0; k < 4; ++k) hit (k + 0.5, 46, big ? 0.55f : 0.4f);
                        if (big) for (double h = 0.25; h < 4.0; h += 0.5) hit (h, 42, 0.25f);
                        if (e > 0.45f) for (double h = 0; h < 4.0; h += 0.25) hit (h, 70, 0.18f + (std::fmod (h, 0.5) == 0.25 ? 0.08f : 0.0f));
                        break;
                    case Style::Brushes:
                        // soft kick, brushed snare sweeps, a ride pattern
                        hit (0.0, 36, 0.55f);
                        if (rng.chance (0.5f)) hit (2.5, 36, 0.4f);
                        hit (1.0, 38, 0.55f); hit (3.0, 38, 0.55f);
                        for (double h : { 0.0, 1.0, 1.0 + 2.0 / 3.0, 2.0, 3.0, 3.0 + 2.0 / 3.0 }) hit (h, 46, 0.35f);
                        break;
                    case Style::Electronic:
                        hit (0.0, 36, 0.95f); hit (2.5, 36, 0.8f);
                        if (big) hit (1.75, 36, 0.6f);
                        hit (1.0, 38, 0.85f); hit (3.0, 38, 0.85f);
                        for (double h = 0; h < 4.0; h += 0.25) hit (h, 42, std::fmod (h, 1.0) == 0.0 ? 0.5f : 0.28f);
                        if (rng.chance (0.4f)) hit (3.75, 37, 0.4f);
                        if (rng.chance (0.3f)) hit (1.5, 37, 0.35f);
                        break;
                    case Style::BoomBap:
                        hit (0.0, 36, 1.0f); hit (1.75, 36, 0.7f); hit (2.5, 36, 0.85f);
                        hit (1.0, 38, 0.95f); hit (3.0, 38, 0.95f);
                        for (double h = 0; h < 4.0; h += 0.5) hit (sw (h), 42, std::fmod (h, 1.0) == 0.0 ? 0.55f : 0.35f);
                        if (big && rng.chance (0.4f)) hit (3.75, 38, 0.3f);
                        break;
                    case Style::Dusty:
                    default:
                        hit (0.0, 36, 0.9f);
                        if (e > 0.45f) hit (density > 0.5f ? 2.5 : 2.0, 36, 0.75f);
                        if (big && density > 0.45f) hit (1.75, 36, 0.55f);
                        hit (1.0, big ? 39 : 38, 0.85f); hit (3.0, big ? 39 : 38, 0.85f);
                        for (double h = 0; h < 4.0; h += 0.5) hit (sw (h), 42, std::fmod (h, 1.0) == 0.0 ? 0.5f : 0.32f);
                        if (big) hit (2.5, 46, 0.4f);
                        if (e > 0.4f) hit (sw (3.5), 70, 0.3f);
                        break;
                }
                if (lastBar && ! lastSection && nextE > e) // a fill into a bigger section
                    for (int k = 0; k < 4; ++k)
                        hit (3.0 + k * 0.25, k < 2 ? 38 : 45, 0.6f + k * 0.08f);
            }
            if (b == 0 && label == "chorus")
                drums.notes.push_back ({ t0, 0.5, 46, 0.7f });
        }
    }
    // the last chord rings out
    if (! lastVoicing.empty())
    {
        const double end = song.totalBars() * 4.0;
        for (auto n : lastVoicing) chords.notes.push_back ({ end, 4.0, n, 0.4f });
        song.sections.back().bars += 1;
    }

    for (auto* part : { &chords, &melody, &counter, &pad, &bass, &drums })
        if (! part->notes.empty())
            song.parts.push_back (*part);
    return song;
}

} // namespace lp::song
