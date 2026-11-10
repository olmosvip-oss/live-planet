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
    for (auto c : { home.getChildFile ("Splice"), juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Splice"),
                    juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Splice") })
        if (c.isDirectory()) f.add (c);
    return f;
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
        if (packsAt >= 0 && packsAt + 1 < parts.size()) e.pack = parts[packsAt + 1];
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

    // what it is
    const bool isLoop = hasToken (tokens, { "loop", "loops" }) || (e.bpm > 0.0 && ! hasToken (tokens, { "oneshot", "one", "shot", "shots", "hit" }));
    if (! isLoop)
    {
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
        if (hasToken (tokens, { "drum", "drums", "beat", "beats", "break", "breaks", "top", "tops", "groove", "kick", "hat", "hats", "perc" })) e.loop = LoopKind::Drums;
        else if (hasToken (tokens, { "bass", "808", "sub" }))                                                  e.loop = LoopKind::Bass;
        else if (hasToken (tokens, { "piano", "keys", "key", "rhodes", "ep", "organ", "wurli", "chords" }))   e.loop = LoopKind::Keys;
        else if (hasToken (tokens, { "guitar", "gtr", "guitars" }))                                           e.loop = LoopKind::Guitar;
        else if (hasToken (tokens, { "vox", "vocal", "vocals", "chop", "voice", "choir" }))                   e.loop = LoopKind::Vocal;
        else if (hasToken (tokens, { "strings", "string", "violin", "cello", "orchestral" }))                 e.loop = LoopKind::Strings;
        else if (hasToken (tokens, { "synth", "pad", "pads", "lead", "pluck", "arp", "bell", "bells", "melody", "melodic" })) e.loop = LoopKind::Synth;
        else                                                                                                  e.loop = LoopKind::Other;
    }

    // which grooves it seems made for
    auto tag = [&] (Style s, std::initializer_list<const char*> words) { if (hasToken (tokens, words)) e.styles |= 1u << (int) s; };
    tag (Style::Dusty, { "lofi", "lo", "dusty", "vinyl", "chill", "tape", "jazzy" });
    tag (Style::BoomBap, { "boombap", "boom", "hiphop", "hip", "soul", "dilla", "90s" });
    tag (Style::Trap, { "trap", "drill", "808", "rage", "plugg" });
    tag (Style::House, { "house", "deep", "disco", "garage", "techno", "dance" });
    tag (Style::Brushes, { "jazz", "brush", "brushes", "acoustic", "live" });
    tag (Style::Electronic, { "edm", "electronic", "future", "glitch", "idm", "synthwave", "pop" });
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
        entries.push_back (e);
    }
    return true;
}

LibraryPicks pickFromLibrary (const Library& lib, const Target& target, juce::uint32 seed,
                              std::function<int (const juce::String&, double)> loopLengthBars,
                              const juce::String& forceDrumPack)
{
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
        kitPacks.push_back ({ (float) (c[0] + c[1] + c[2] + c[3] + c[4] + c[5]) + rng.nextFloat() * 4.0f, pack });
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
        if (e.kind != SampleKind::Loop || e.loop == LoopKind::Drums || e.loop == LoopKind::Bass || e.key < 0 || e.bpm <= 0) continue;
        const bool sameKey = e.key == target.key && e.minor == target.minor;
        const bool relKey = e.key == relative && e.minor != target.minor;
        if (! sameKey && ! relKey) continue;
        const double ratio = e.bpm / target.bpm;
        double best = 1e9;
        for (double m : { 0.5, 1.0, 2.0 }) best = std::min (best, std::abs (ratio * m - 1.0));
        if (best > 0.06) continue;
        float score = (sameKey ? 2.0f : 1.0f) + ((e.styles & styleBit) ? 1.5f : 0.0f) + (float) (0.06 - best) * 20.0f + rng.nextFloat();
        loops.push_back ({ score, &e });
    }
    std::sort (loops.begin(), loops.end(), [] (auto& a, auto& b) { return a.first > b.first; });
    for (size_t i = 0; i < std::min<size_t> (loops.size(), 12); ++i)
    {
        const int bars = loopLengthBars ? loopLengthBars (loops[i].second->path, loops[i].second->bpm) : 0;
        if (bars > 0) { picks.loop = loops[i].second; picks.loopBars = bars; break; }
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
            for (double m : { 0.5, 1.0, 2.0 }) tempoOk = tempoOk || std::abs (e.bpm * m / best - 1.0) < 0.015;
            if (! tempoOk) continue;
            const bool tonal = e.loop != LoopKind::Drums;
            if (tonal)
            {
                if (e.key < 0) continue;
                const bool sameKey = e.key == target.key && e.minor == target.minor;
                const bool relKey = e.key == relative && e.minor != target.minor;
                if (! sameKey && ! relKey) continue;
            }
            auto& choice = byKind[e.loop];
            choice.kind = e.loop;
            if (choice.options.size() >= 24) continue;
            choice.options.push_back ({ &e, 0 });
        }
        for (auto& [kind, choice] : byKind)
        {
            // shuffle a little (seeded) so "write another" finds new sounds, then keep ones that are whole bars
            for (size_t i = choice.options.size(); i > 1; --i) std::swap (choice.options[i - 1], choice.options[(size_t) rng.nextInt ((int) i)]);
            if (kind == picks.loop->loop)
                for (size_t i = 0; i < choice.options.size(); ++i)
                    if (choice.options[i].first == picks.loop) std::swap (choice.options[0], choice.options[i]);
            std::vector<std::pair<const LibraryEntry*, int>> ok;
            for (auto& o : choice.options)
            {
                const int bars = loopLengthBars ? loopLengthBars (o.first->path, best) : 0;
                if (bars > 0) ok.push_back ({ o.first, bars });
                if (ok.size() >= 6) break;
            }
            choice.options = ok;
            if (! ok.empty()) picks.loopSet.push_back (choice);
        }
        // the main loop's kind first
        std::stable_sort (picks.loopSet.begin(), picks.loopSet.end(), [&] (auto& a, auto& b) { return a.kind == picks.loop->loop && b.kind != picks.loop->loop; });
    }
    return picks;
}

} // namespace lp::song
