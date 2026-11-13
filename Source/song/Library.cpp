#include "Library.h"
#include <regex>

namespace lp::song
{
namespace
{
    bool hasToken (const juce::StringArray& tokens, std::initializer_list<const char*> words)
    {
        for (auto& t : tokens)
            for (auto w : words)
                if (t == w || (std::strlen (w) > 3 && t.startsWith (w))) return true;
        return false;
    }

    int pitchClass (char letter, const juce::String& accidental)
    {
        static const int base[] = { 9, 11, 0, 2, 4, 5, 7 }; // a b c d e f g
        int pc = base[letter - 'a'];
        if (accidental == "#" || accidental == "s" || accidental == "sharp") ++pc;
        if (accidental == "b" || accidental == "flat") --pc;
        return (pc + 12) % 12;
    }
}

juce::Array<juce::File> Library::defaultFolders()
{
    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    juce::Array<juce::File> f;
    const auto docs = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    for (auto c : { home.getChildFile ("Splice"), juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Splice"),
                    docs.getChildFile ("Splice"), docs.getChildFile ("WAVS").getChildFile ("Samples") })
        if (c.isDirectory()) f.add (c);
    return f;
}

void Library::classify (LibraryEntry& e, const juce::String& fileName, const juce::String& path)
{
    auto tokensOf = [] (const juce::String& text)
    {
        juce::StringArray t;
        t.addTokens (text.toLowerCase().replaceCharacters ("-./\\ ()[]&+,", "____________"), "_", "");
        t.removeEmptyStrings();
        return t;
    };
    // the file's own name decides first; the folders only when the name says nothing
    const auto nameTokens = tokensOf (fileName), pathTokens = tokensOf (path);
    classifyKind (e, nameTokens);
    if (e.kind == SampleKind::Other || (e.kind == SampleKind::Loop && e.loop == LoopKind::Other))
    {
        LibraryEntry byPath = e;
        classifyKind (byPath, pathTokens);
        if (e.kind == SampleKind::Other || byPath.loop != LoopKind::Other) { e.kind = byPath.kind; e.loop = byPath.loop; }
    }
    // a one-shot that isn't a drum (a keys note, a synth stab, a vocal shot): playable as an instrument
    if (e.kind == SampleKind::Other && e.bpm <= 0.0)
    {
        const auto p = path.toLowerCase().removeCharacters (" _-");
        if (p.contains ("oneshot") || p.contains ("/shots/") || hasToken (pathTokens, { "stab", "stabs", "hit", "hits", "shot", "shots" }))
            e.kind = SampleKind::OneShot;
    }
    const auto& tokens = pathTokens;
    e.styles = 0;
    // which grooves it seems made for (the pack and folder names help here)
    auto tag = [&] (Style st, std::initializer_list<const char*> words) { if (hasToken (tokens, words)) e.styles |= 1u << (int) st; };
    tag (Style::Dusty, { "lofi", "lo", "dusty", "vinyl", "chill", "tape", "jazzy", "chillhop", "nostalgic", "vintage", "rnb", "neosoul" });
    tag (Style::BoomBap, { "boombap", "boom", "hiphop", "hip", "soul", "dilla", "90s", "rap", "funk", "soulful", "gospel" });
    tag (Style::Trap, { "trap", "drill", "808", "rage", "plugg", "phonk", "dark", "hard", "jersey" });
    tag (Style::House, { "house", "deep", "disco", "garage", "techno", "dance", "afro", "afrobeat", "afrobeats", "amapiano", "latin", "reggaeton", "dancehall" });
    tag (Style::Brushes, { "jazz", "brush", "brushes", "acoustic", "live", "folk", "indie", "cinematic", "ambient", "orchestral", "rock" });
    tag (Style::Electronic, { "edm", "electronic", "future", "glitch", "idm", "synthwave", "pop", "hyperpop", "kpop", "dubstep", "dnb", "electro" });
}

void Library::classifyKind (LibraryEntry& e, const juce::StringArray& tokens)
{
    e.kind = SampleKind::Other;
    e.loop = LoopKind::None;
    const bool isLoop = hasToken (tokens, { "loop", "loops" }) || (e.bpm > 0.0 && ! hasToken (tokens, { "oneshot", "one", "shot", "shots", "hit" }));
    auto fxKind = [&]() -> SampleKind
    {
        const bool rev = hasToken (tokens, { "reverse", "reversed", "rev", "rvrs", "reverb_swell" });
        if (hasToken (tokens, { "downlifter", "downlifters", "downsweep", "downer", "falldown" })) return SampleKind::Downlifter;
        if (hasToken (tokens, { "riser", "risers", "uplifter", "uplifters", "build", "buildup", "upsweep", "rise", "rising", "tension" })) return SampleKind::Riser;
        if (rev && hasToken (tokens, { "cymbal", "crash", "swell", "fx", "sfx", "reverb", "vox", "vocal", "piano", "snare" })) return SampleKind::ReverseFx;
        if (hasToken (tokens, { "subdrop", "sub_drop", "drop" }) && hasToken (tokens, { "sub", "subdrop", "808", "bass" })) return SampleKind::SubDrop;
        if (hasToken (tokens, { "impact", "impacts", "boom", "booms", "slam", "explosion", "hit_fx", "cinematic_hit" })) return SampleKind::Impact;
        if (hasToken (tokens, { "crash", "crashes", "cymbal", "cymbals" })) return SampleKind::Crash;
        if (hasToken (tokens, { "sweep", "sweeps", "whoosh", "whooshes", "swoosh" })) return SampleKind::Riser;
        return SampleKind::Other;
    };
    if (! isLoop)
    {
        if (const auto fx = fxKind(); fx != SampleKind::Other) { e.kind = fx; return; }
        if (hasToken (tokens, { "808" }))                                          e.kind = SampleKind::Bass808;
        else if (hasToken (tokens, { "kick", "kicks", "kck", "bd", "bassdrum" }))   e.kind = SampleKind::Kick;
        else if (hasToken (tokens, { "clap", "claps", "clp" }))                     e.kind = SampleKind::Clap;
        else if (hasToken (tokens, { "snare", "snares", "snr", "sd", "rimshot" }))  e.kind = SampleKind::Snare;
        else if (hasToken (tokens, { "open", "ohh", "openhat", "oh" }) && hasToken (tokens, { "hat", "hats", "hh", "hihat", "hihats", "openhat", "ohh" }))
                                                                                    e.kind = SampleKind::OpenHat;
        else if (hasToken (tokens, { "hat", "hats", "hh", "hihat", "hihats", "chh" })) e.kind = SampleKind::Hat;
        else if (hasToken (tokens, { "perc", "percs", "percussion", "shaker", "tamb", "tambourine", "rim", "conga", "bongo", "cowbell", "clave", "snap" }))
                                                                                    e.kind = SampleKind::Perc;
    }
    else
    {
        e.kind = SampleKind::Loop;
        auto is = [&] (std::initializer_list<const char*> w) { return hasToken (tokens, w); };
        if (is ({ "drum", "drums", "beat", "beats", "break", "breaks", "groove", "kick", "fill", "fills" }))           e.loop = LoopKind::Drums;
        else if (is ({ "perc", "percs", "percussion", "top", "tops", "shaker", "shakers", "hat", "hats", "conga", "congas",
                       "bongo", "bongos", "tambourine", "tamb", "clap", "claps", "rim", "timbale", "timbales" }))            e.loop = LoopKind::Perc;
        else if (is ({ "bass", "808", "sub", "bassline", "reese" }))                                                       e.loop = LoopKind::Bass;
        else if (is ({ "vox", "vocal", "vocals", "chop", "chops", "voice", "choir", "adlib", "adlibs", "hook", "acapella", "sing" })) e.loop = LoopKind::Vocal;
        else if (is ({ "brass", "horn", "horns", "trumpet", "trumpets", "trombone", "tuba", "flugelhorn" }))              e.loop = LoopKind::Brass;
        else if (is ({ "flute", "flutes", "sax", "saxophone", "clarinet", "woodwind", "woodwinds", "oboe", "recorder", "pan", "panflute", "ocarina", "whistle" }))
                                                                                                                          e.loop = LoopKind::Woodwind;
        else if (is ({ "marimba", "kalimba", "vibes", "vibraphone", "glock", "glockenspiel", "bell", "bells", "mallet", "mallets",
                       "celesta", "musicbox", "xylophone", "steelpan", "chime", "chimes" }))                               e.loop = LoopKind::Mallet;
        else if (is ({ "guitar", "gtr", "guitars", "nylon", "strum", "banjo", "ukulele", "uke", "mandolin", "sitar" })) e.loop = LoopKind::Guitar;
        else if (is ({ "piano", "keys", "rhodes", "ep", "organ", "wurli", "wurlitzer", "clav", "keyboard" }))              e.loop = LoopKind::Keys;
        else if (is ({ "acoustic", "electric" }))                                                                          e.loop = LoopKind::Guitar;
        else if (is ({ "chords", "chord" }))                                                                               e.loop = LoopKind::Keys;
        else if (is ({ "strings", "string", "violin", "violins", "cello", "viola", "orchestral", "orchestra", "harp", "pizzicato", "pizz" }))
                                                                                                                          e.loop = LoopKind::Strings;
        else if (is ({ "pad", "pads", "atmos", "atmosphere", "ambient", "ambience", "drone", "texture", "textures" }))   e.loop = LoopKind::Pad;
        else if (is ({ "fx", "sfx", "riser", "risers", "sweep", "impact", "noise", "foley", "transition", "downlifter", "uplifter" })) e.loop = LoopKind::FX;
        else if (is ({ "lead", "leads" }))                                                                                 e.loop = LoopKind::Lead;
        else if (is ({ "pluck", "plucks", "arp", "arps", "arpeggio", "arpeggiated" }))                                   e.loop = LoopKind::Pluck;
        else if (is ({ "synth", "synths", "melody", "melodic", "melodies", "songstarter", "starter", "sample", "riff" }))  e.loop = LoopKind::Synth;
        else                                                                                                              e.loop = LoopKind::Other;
    }

}

std::vector<const LibraryEntry*> Library::fxOfKind (SampleKind k) const
{
    std::vector<const LibraryEntry*> out;
    for (auto& e : entries)
    {
        if (e.kind == k) { out.push_back (&e); continue; }
        // "fx_riser_128bpm" style loops are risers too (we only use them to land on the downbeat)
        if (k == SampleKind::Riser && e.kind == SampleKind::Loop && e.loop == LoopKind::FX)
        {
            const auto n = juce::File (e.path).getFileName().toLowerCase();
            if (n.contains ("riser") || n.contains ("uplift") || n.contains ("build") || n.contains ("sweep")) out.push_back (&e);
        }
    }
    return out;
}

std::vector<const LibraryEntry*> Library::loopsOfKind (LoopKind k, double songBpm) const
{
    std::vector<const LibraryEntry*> out;
    for (auto& e : entries)
    {
        if (e.kind != SampleKind::Loop || e.loop != k || e.bpm <= 0) continue;
        bool ok = false;
        for (double m : { 0.5, 1.0, 2.0 }) ok = ok || std::abs (e.bpm * m / songBpm - 1.0) < 0.10;
        if (ok) out.push_back (&e);
    }
    return out;
}

LibraryEntry Library::parse (const juce::File& file, const juce::File& root)
{
    LibraryEntry e;
    e.path = file.getFullPathName();
    // the pack: Splice keeps each one in sounds/packs/<pack name>/...; otherwise the first folder
    {
        juce::StringArray parts;
        parts.addTokens (file.getParentDirectory().getRelativePathFrom (root).replaceCharacter ('\\', '/'), "/", "");
        parts.removeEmptyStrings();
        const int packsAt = parts.indexOf ("packs", true);
        // WAVS keeps Packs/<creator>/<pack>/...: the pack is "creator - pack"
        if (sourceOf (e.path) == Source::Wavs && packsAt >= 0 && packsAt + 2 < parts.size())
            e.pack = parts[packsAt + 1] + " - " + parts[packsAt + 2];
        else if (packsAt >= 0 && packsAt + 1 < parts.size()) e.pack = parts[packsAt + 1];
        else if (parts.size() > 0 && parts[0].equalsIgnoreCase ("sounds") && parts.size() > 1) e.pack = parts[1];
        else if (parts.size() > 0) e.pack = parts[0];
        else e.pack = file.getParentDirectory().getFileName();
    }

    const auto name = file.getFileNameWithoutExtension().toLowerCase();
    const auto where = file.getRelativePathFrom (root).toLowerCase();
    juce::StringArray tokens;
    tokens.addTokens (where.replaceCharacters ("-./\\ ()[]&+,", "____________"), "_", "");
    tokens.removeEmptyStrings();

    // tempo: "95bpm", "95_bpm" or a lone 2-3 digit number between 60 and 200
    {
        std::smatch m;
        const std::string n = name.toStdString();
        static const std::regex explicitBpm ("(\\d{2,3})\\s*_?bpm");
        if (std::regex_search (n, m, explicitBpm)) e.bpm = std::stod (m[1].str());
        else
            for (auto& t : tokens)
                if (t.containsOnly ("0123456789") && t.length() >= 2 && t.length() <= 3)
                {
                    const int v = t.getIntValue();
                    if (v >= 60 && v <= 200) { e.bpm = v; break; }
                }
    }

    // key: "fmin", "f#m", "ebmaj", "a_minor", "c#" (a key token, not a word)
    {
        static const std::regex keyRe ("^([a-g])(#|b|s|sharp|flat)?(m|min|minor|maj|major)?$");
        for (int i = 0; i < tokens.size(); ++i)
        {
            const auto t = tokens[i];
            std::smatch m;
            const std::string ts = t.toStdString();
            if (! std::regex_match (ts, m, keyRe)) continue;
            const juce::String acc (m[2].str()), qual (m[3].str());
            // a bare letter only counts right after a tempo or before "minor"/"major"
            const bool bare = acc.isEmpty() && qual.isEmpty();
            const juce::String next = i + 1 < tokens.size() ? tokens[i + 1] : juce::String();
            const juce::String prev = i > 0 ? tokens[i - 1] : juce::String();
            if (bare && ! (next == "minor" || next == "major" || next == "min" || next == "maj" || prev.containsOnly ("0123456789")))
                continue;
            if (ts == "b" || ts == "a") { if (! prev.containsOnly ("0123456789") && next != "minor" && next != "major") continue; }
            e.key = pitchClass (m[1].str()[0], acc);
            e.minor = qual.startsWith ("m") && ! qual.startsWith ("maj") ? true : (next == "minor" || next == "min");
            break;
        }
    }

    classify (e, file.getFileNameWithoutExtension(), where);
    return e;
}

void Library::scan (const juce::Array<juce::File>& folders, int maxFiles)
{
    entries.clear();
    for (auto& root : folders)
    {
        for (const auto& it : juce::RangedDirectoryIterator (root, true, "*.wav;*.aif;*.aiff;*.flac;*.mp3", juce::File::findFiles))
        {
            if ((int) entries.size() >= maxFiles) return;
            auto e = parse (it.getFile(), root);
            if (e.kind != SampleKind::Other) entries.push_back (std::move (e));
        }
    }
}

int Library::count (SampleKind k) const
{
    return (int) std::count_if (entries.begin(), entries.end(), [k] (auto& e) { return e.kind == k; });
}

int Library::loopCount() const
{
    return (int) std::count_if (entries.begin(), entries.end(), [] (auto& e) { return e.kind == SampleKind::Loop && e.loop != LoopKind::Drums && e.key >= 0 && e.bpm > 0; });
}

void Library::save (const juce::File& cache) const
{
    juce::Array<juce::var> list;
    for (auto& e : entries)
    {
        juce::Array<juce::var> row { e.path, e.pack, (int) e.kind, (int) e.loop, e.bpm, e.key, e.minor, (int) e.styles };
        list.add (row);
    }
    cache.getParentDirectory().createDirectory();
    cache.replaceWithText (juce::JSON::toString (list, true));
}

bool Library::load (const juce::File& cache)
{
    auto v = juce::JSON::parse (cache);
    if (! v.isArray()) return false;
    entries.clear();
    for (auto& row : *v.getArray())
    {
        if (! row.isArray() || row.size() < 8) continue;
        LibraryEntry e;
        e.path = row[0].toString(); e.pack = row[1].toString();
        e.kind = (SampleKind) (int) row[2]; e.loop = (LoopKind) (int) row[3];
        e.bpm = (double) row[4]; e.key = (int) row[5]; e.minor = (bool) row[6]; e.styles = (juce::uint32) (int) row[7];
        classify (e, juce::File (e.path).getFileNameWithoutExtension(), e.path);
        entries.push_back (e);
    }
    return true;
}

LibraryPicks pickFromLibrary (const Library& lib, const Target& target, juce::uint32 seed,
                              std::function<int (const juce::String&, double)> loopLengthBars,
                              const juce::String& forceDrumPack,
                              std::function<int (const juce::String&)> tuneFor,
                              std::function<float (const juce::String&)> soundsLike)
{
    auto like = [&] (const juce::String& path) { return soundsLike ? soundsLike (path) : 0.5f; };
    // in key matters most, then sounding like your reference (a little tuning is fine; a lot is a last resort)
    auto rank = [] (int tune, float lk) { return lk * 3.0f - (float) std::abs (tune) * 0.35f; };
    LibraryPicks picks;
    juce::Random rng ((juce::int64) seed * 31 + 7);
    const auto styleBit = 1u << (int) target.style;

    // drums: find the pack with the most of what we need, preferring this style
    std::map<juce::String, std::array<int, 6>> packs; // kick, snare, clap, hat, open, perc
    for (auto& e : lib.all())
    {
        int slot = e.kind == SampleKind::Kick ? 0 : e.kind == SampleKind::Snare ? 1 : e.kind == SampleKind::Clap ? 2
                 : e.kind == SampleKind::Hat ? 3 : e.kind == SampleKind::OpenHat ? 4 : e.kind == SampleKind::Perc ? 5 : -1;
        if (slot < 0) continue;
        packs[e.pack][(size_t) slot] += (e.styles & styleBit) ? 3 : 1;
    }
    juce::String bestPack;
    std::vector<std::pair<float, juce::String>> kitPacks;
    for (auto& [pack, c] : packs)
    {
        if (c[0] == 0 || (c[1] == 0 && c[2] == 0) || c[3] == 0) continue; // needs kick, snare or clap, hat
        // a full kit counts; a huge pack doesn't win just for being huge, so different kits get their turn
        float sc = 0;
        for (int v : c) sc += (float) std::min (v, 6);
        for (auto& w : target.genreWords) if (pack.containsIgnoreCase (w)) sc += 12.0f; // the genre you asked for
        kitPacks.push_back ({ sc + rng.nextFloat() * 14.0f, pack });
    }
    std::sort (kitPacks.begin(), kitPacks.end(), [] (auto& a, auto& b) { return a.first > b.first; });
    for (size_t i = 0; i < std::min<size_t> (8, kitPacks.size()); ++i) picks.drumPacks.add (kitPacks[i].second);
    if (forceDrumPack.isNotEmpty()) bestPack = forceDrumPack;
    else if (! kitPacks.empty()) bestPack = kitPacks.front().second;
    auto pickOne = [&] (SampleKind k) -> juce::String
    {
        std::vector<const LibraryEntry*> inPack, styled, any;
        for (auto& e : lib.all())
        {
            if (e.kind != k) continue;
            if (e.pack == bestPack) inPack.push_back (&e);
            if (e.styles & styleBit) styled.push_back (&e);
            any.push_back (&e);
        }
        auto& from = ! inPack.empty() ? inPack : (! styled.empty() ? styled : any);
        return from.empty() ? juce::String() : from[(size_t) rng.nextInt ((int) from.size())]->path;
    };
    if (bestPack.isNotEmpty())
    {
        picks.drumPack = bestPack;
        auto set = [&] (int note, SampleKind k, SampleKind fallback)
        {
            auto p = pickOne (k);
            if (p.isEmpty()) p = pickOne (fallback);
            if (p.isNotEmpty()) picks.drums[note] = p;
        };
        set (36, SampleKind::Kick, SampleKind::Kick);
        set (38, SampleKind::Snare, SampleKind::Clap);
        set (39, SampleKind::Clap, SampleKind::Snare);
        set (42, SampleKind::Hat, SampleKind::Hat);
        set (46, SampleKind::OpenHat, SampleKind::Hat);
        set (70, SampleKind::Perc, SampleKind::Hat);
        set (37, SampleKind::Perc, SampleKind::Snare);
        set (45, SampleKind::Perc, SampleKind::Kick);
    }

    // a melodic loop: same key (or its relative), tempo within 6%, whole bars
    std::vector<std::pair<float, const LibraryEntry*>> loops;
    const int relative = target.minor ? (target.key + 3) % 12 : (target.key + 9) % 12;
    for (auto& e : lib.all())
    {
        if (e.kind != SampleKind::Loop || e.loop == LoopKind::Drums || e.loop == LoopKind::Bass || e.bpm <= 0) continue;
        const bool sameKey = e.key == target.key && e.minor == target.minor;
        const bool relKey = e.key == relative && e.minor != target.minor;
        if (e.key >= 0 && ! sameKey && ! relKey && ! tuneFor) continue;
        const double ratio = e.bpm / target.bpm;
        double best = 1e9;
        for (double m : { 0.5, 1.0, 2.0 }) best = std::min (best, std::abs (ratio * m - 1.0));
        if (best > 0.10) continue;
        float score = (sameKey ? 2.0f : 1.0f) + ((e.styles & styleBit) ? 1.5f : 0.0f) + (float) (0.06 - best) * 20.0f + rng.nextFloat();
        for (auto& w : target.genreWords) if (e.path.containsIgnoreCase (w)) { score += 3.0f; break; }
        loops.push_back ({ score, &e });
    }
    std::sort (loops.begin(), loops.end(), [] (auto& a, auto& b) { return a.first > b.first; });
    // listen to the best few: the one that's really in key (or needs the least tuning) wins
    {
        float bestRank = -1e9f;
        for (size_t i = 0; i < std::min<size_t> (loops.size(), 16); ++i)
        {
            const int bars = loopLengthBars ? loopLengthBars (loops[i].second->path, loops[i].second->bpm) : 0;
            if (bars <= 0) continue;
            const int tune = tuneFor ? tuneFor (loops[i].second->path) : 0;
            if (tune == 99) continue;
            const float r = rank (tune, like (loops[i].second->path));
            if (r > bestRank) { bestRank = r; picks.loop = loops[i].second; picks.loopBars = bars; }
        }
    }

    // a whole set that plays together: everything at the main loop's exact tempo, same key family
    if (picks.loop != nullptr)
    {
        double best = picks.loop->bpm, bestDiff = 1e9;
        for (double m : { 0.5, 1.0, 2.0 })
            if (std::abs (picks.loop->bpm * m - target.bpm) < bestDiff) { bestDiff = std::abs (picks.loop->bpm * m - target.bpm); best = picks.loop->bpm * m; }
        picks.loopBpm = best;
        std::map<LoopKind, LoopChoice> byKind;
        for (auto& e : lib.all())
        {
            if (e.kind != SampleKind::Loop || e.bpm <= 0) continue;
            bool tempoOk = false;
            for (double m : { 0.5, 1.0, 2.0 }) tempoOk = tempoOk || std::abs (e.bpm * m / target.bpm - 1.0) < 0.10; // stretched to fit
            if (! tempoOk) continue;
            const bool tonal = e.loop != LoopKind::Drums && e.loop != LoopKind::Perc && e.loop != LoopKind::FX;
            if (tonal && ! tuneFor)
            {
                if (e.key < 0) continue;
                const bool sameKey = e.key == target.key && e.minor == target.minor;
                const bool relKey = e.key == relative && e.minor != target.minor;
                if (! sameKey && ! relKey) continue;
            }
            auto& choice = byKind[e.loop];
            choice.kind = e.loop;
            choice.options.push_back ({ &e, 0, 0 });
        }
        // best first: the name says the right key, same pack as the main loop (same vibe), the song's genre;
        // a seeded shuffle among equals so "write another" finds new sounds. Keep the top 40.
        for (auto& [kind, choice] : byKind)
        {
            std::vector<std::pair<float, LoopOption>> scored;
            for (auto& o : choice.options)
            {
                const auto& e = *o.entry;
                const bool nameFits = e.key >= 0 && ((e.key == target.key && e.minor == target.minor) || (e.key == relative && e.minor != target.minor));
                float sc = (nameFits ? 4.0f : 0.0f) + (e.pack == picks.loop->pack ? 2.5f : 0.0f)
                         + ((e.styles & styleBit) ? 1.5f : 0.0f) + rng.nextFloat() * 2.0f;
                for (auto& w : target.genreWords) if (e.path.containsIgnoreCase (w)) { sc += 3.0f; break; }
                scored.push_back ({ sc, o });
            }
            std::sort (scored.begin(), scored.end(), [] (auto& a, auto& b) { return a.first > b.first; });
            choice.options.clear();
            for (size_t i = 0; i < std::min<size_t> (40, scored.size()); ++i) choice.options.push_back (scored[i].second);
        }
        for (auto& [kind, choice] : byKind)
        {
            if (kind == picks.loop->loop)
                for (size_t i = 0; i < choice.options.size(); ++i)
                    if (choice.options[i].entry == picks.loop) std::swap (choice.options[0], choice.options[i]);
            // listen to each: keep ones that are whole bars and (for tonal loops) in key or close enough to tune
            std::vector<LoopOption> ok;
            for (auto& o : choice.options)
            {
                const int bars = loopLengthBars ? loopLengthBars (o.entry->path, o.entry->bpm) : 0;
                if (bars <= 0) continue;
                int tune = 0;
                if (kind != LoopKind::Drums && kind != LoopKind::Perc && kind != LoopKind::FX && tuneFor)
                {
                    tune = tuneFor (o.entry->path);
                    if (tune == 99) continue; // no clear key at all; any other key gets tuned into the song's
                }
                ok.push_back ({ o.entry, bars, tune, like (o.entry->path) });
                if (ok.size() >= 14) break;
            }
            // the ones that sound most like your reference (and need the least tuning) first; keep 5
            std::stable_sort (ok.begin(), ok.end(), [&] (const LoopOption& a, const LoopOption& b) { return rank (a.tune, a.like) > rank (b.tune, b.like); });
            if (ok.size() > 5) ok.resize (5);
            if (kind == picks.loop->loop)
                for (size_t i = 0; i < ok.size(); ++i)
                    if (ok[i].entry == picks.loop) std::swap (ok[0], ok[i]);
            choice.options = ok;
            if (! ok.empty()) picks.loopSet.push_back (choice);
        }
        // the main loop's kind first
        std::stable_sort (picks.loopSet.begin(), picks.loopSet.end(), [&] (auto& a, auto& b) { return a.kind == picks.loop->loop && b.kind != picks.loop->loop; });
    }
    return picks;
}

} // namespace lp::song
