#pragma once
#include <juce_core/juce_core.h>
#include <regex>
#include "Song.h"
#include "Library.h"

// "Describe your song": reads a sentence like "dark trap 140 in F minor with piano and flute"
// and turns it into a genre, tempo, key, mood and the instruments to use. Runs offline, instantly.
namespace lp::song
{

struct Genre
{
    const char* name;
    std::vector<const char*> words; // what you might type
    Style style;
    double lo, hi, typical;         // usual tempo range
    std::vector<const char*> libraryWords; // words to look for in your sample folders
    int mood = 0;                   // -1 leans minor, +1 leans major
};

inline const std::vector<Genre>& genres()
{
    static const std::vector<Genre> g {
        { "drill",        { "drill", "uk drill", "ny drill" },                   Style::Trap,       138, 146, 142, { "drill" }, -1 },
        { "trap",         { "trap" },                                             Style::Trap,       130, 160, 140, { "trap" }, -1 },
        { "rage",         { "rage" },                                             Style::Trap,       145, 170, 155, { "rage" }, -1 },
        { "plugg",        { "plugg", "pluggnb" },                                 Style::Trap,       140, 160, 150, { "plugg" }, 1 },
        { "phonk",        { "phonk" },                                            Style::Trap,       125, 145, 132, { "phonk" }, -1 },
        { "lofi",         { "lofi", "lo-fi", "lo fi", "chillhop", "study" },      Style::Dusty,       70,  92,  82, { "lofi", "chill", "jazzy", "dusty" }, 0 },
        { "r&b",          { "rnb", "r&b", "r and b", "neo soul", "neosoul" },     Style::Dusty,       60, 100,  88, { "rnb", "neosoul", "soul" }, 0 },
        { "boom bap",     { "boom bap", "boombap", "90s hip hop", "old school" }, Style::BoomBap,     84,  98,  90, { "boombap", "hiphop", "soul", "90s" }, 0 },
        { "hip hop",      { "hip hop", "hiphop", "rap" },                         Style::BoomBap,     80, 100,  92, { "hiphop", "rap" }, 0 },
        { "soul",         { "soul", "gospel", "motown" },                         Style::BoomBap,     70, 100,  86, { "soul", "gospel" }, 1 },
        { "afrobeats",    { "afrobeats", "afrobeat", "afro", "afropop" },         Style::House,       98, 112, 105, { "afro", "afrobeats", "afrobeat" }, 1 },
        { "amapiano",     { "amapiano", "piano house" },                          Style::House,      108, 116, 112, { "amapiano", "afro" }, 0 },
        { "reggaeton",    { "reggaeton", "dembow", "latin" },                     Style::House,       88, 100,  95, { "reggaeton", "latin", "dembow" }, 0 },
        { "dancehall",    { "dancehall" },                                        Style::House,       90, 105,  98, { "dancehall" }, 0 },
        { "house",        { "house", "deep house", "tech house" },                Style::House,      118, 128, 124, { "house", "deep" }, 0 },
        { "techno",       { "techno" },                                           Style::House,      125, 140, 130, { "techno" }, -1 },
        { "disco",        { "disco", "nu disco", "funk" },                        Style::House,      110, 125, 118, { "disco", "funk" }, 1 },
        { "garage",       { "garage", "uk garage", "2 step", "2step" },           Style::House,      128, 136, 132, { "garage" }, 0 },
        { "drum & bass",  { "dnb", "drum and bass", "drum & bass", "jungle" },    Style::Electronic, 165, 178, 174, { "dnb", "jungle" }, -1 },
        { "dubstep",      { "dubstep" },                                          Style::Electronic, 138, 145, 140, { "dubstep" }, -1 },
        { "future bass",  { "future bass" },                                      Style::Electronic, 140, 165, 150, { "future" }, 1 },
        { "edm",          { "edm", "big room", "festival" },                      Style::Electronic, 124, 130, 128, { "edm" }, 1 },
        { "synthwave",    { "synthwave", "retrowave", "80s" },                    Style::Electronic,  80, 118, 100, { "synthwave", "retro", "80s" }, 0 },
        { "hyperpop",     { "hyperpop" },                                         Style::Electronic, 140, 170, 155, { "hyperpop" }, 1 },
        { "pop",          { "pop", "radio", "kpop", "k-pop" },                    Style::Electronic, 100, 125, 112, { "pop" }, 1 },
        { "jazz",         { "jazz", "jazzy", "bossa" },                           Style::Brushes,     80, 140, 100, { "jazz", "jazzy" }, 0 },
        { "ambient",      { "ambient", "cinematic", "film", "score" },            Style::Brushes,     60,  90,  72, { "ambient", "cinematic" }, 0 },
        { "indie",        { "indie", "rock", "folk", "acoustic", "country" },     Style::Brushes,     90, 130, 110, { "indie", "rock", "folk", "acoustic" }, 1 },
    };
    return g;
}

struct Prompt
{
    bool any = false;            // understood anything at all
    const Genre* genre = nullptr;
    double bpm = 0;              // typed tempo (0 = not said)
    int key = -1; bool minor = false; // typed key
    int mood = 0;                // -1 dark / sad, +1 happy / bright (when no key was typed)
    std::vector<LoopKind> instruments;
    bool sketch = false;
    juce::String summary;        // what was understood, to show back
};

inline Prompt parsePrompt (const juce::String& textIn)
{
    Prompt p;
    const juce::String text = " " + textIn.toLowerCase().replaceCharacters (",;!?.()", "       ") + " ";
    auto has = [&] (const char* w) { return text.contains (juce::String (" ") + w + " ") || text.contains (juce::String (" ") + w + "s "); };

    // genre: the longest phrase that matches wins ("deep house" over "house")
    size_t bestLen = 0;
    for (auto& g : genres())
        for (auto* w : g.words)
            if (has (w) && std::strlen (w) > bestLen) { p.genre = &g; bestLen = std::strlen (w); }

    const std::string s = text.toStdString();
    std::smatch m;
    // tempo: "140", "140bpm", "140 bpm"
    {
        static const std::regex bpmRe (" (\\d{2,3}) ?(bpm)? ");
        auto it = std::sregex_iterator (s.begin(), s.end(), bpmRe);
        for (; it != std::sregex_iterator(); ++it)
        {
            const int v = std::stoi ((*it)[1].str());
            if (v >= 50 && v <= 200) { p.bpm = v; break; }
        }
    }
    // key: "f minor", "f#m", "ab major", "in d", "key of c#"
    bool keyHadQuality = false;
    {
        auto pc = [] (char letter, const std::string& acc)
        {
            static const int base[7] = { 9, 11, 0, 2, 4, 5, 7 }; // a..g
            int k = base[letter - 'a'];
            if (acc == "#" || acc == "sharp" || acc == " sharp") ++k;
            if (acc == "b" || acc == "flat" || acc == " flat") --k;
            return (k + 12) % 12;
        };
        // "m" only when attached ("f#m", "am"); "in a" / "in b" only with a sharp or flat (so "in a dark mood" isn't A)
        static const std::regex withQuality (" ([a-g])(#| ?sharp|b| ?flat)?(?: ?(minor|major|min|maj)|(m)) ");
        static const std::regex inKey (" (?:in the key of|key of|key|in) ([c-g](?:#| ?sharp|b| ?flat)?|[ab](?:#| ?sharp|b| ?flat)) ");
        if (std::regex_search (s, m, withQuality) && s.find (" i am ") == std::string::npos)
        {
            p.key = pc (m[1].str()[0], m[2].str());
            p.minor = m[4].matched || (m[3].str() != "major" && m[3].str() != "maj");
            keyHadQuality = true;
        }
        else if (std::regex_search (s, m, inKey))
        {
            const std::string k = m[1].str();
            p.key = pc (k[0], k.substr (1));
            p.minor = false;
        }
    }
    // mood
    for (auto* w : { "dark", "sad", "moody", "emotional", "melancholy", "eerie", "sinister", "gloomy", "minor", "evil", "haunting", "lonely", "rainy", "night" })
        if (has (w)) p.mood = -1;
    for (auto* w : { "happy", "bright", "uplifting", "joyful", "sunny", "cheerful", "summer", "major", "feel good", "positive", "hopeful" })
        if (has (w)) p.mood = 1;
    if (p.mood == 0 && p.genre != nullptr) p.mood = p.genre->mood;
    if (p.key >= 0 && ! keyHadQuality && p.mood < 0) p.minor = true; // "sad ... in c#" = C# minor

    // instruments
    struct Inst { LoopKind kind; std::vector<const char*> words; };
    static const std::vector<Inst> insts {
        { LoopKind::Keys,     { "piano", "keys", "rhodes", "organ", "wurli", "electric piano" } },
        { LoopKind::Guitar,   { "guitar", "guitars", "acoustic guitar", "strum" } },
        { LoopKind::Strings,  { "strings", "violin", "cello", "orchestra", "orchestral", "harp" } },
        { LoopKind::Pad,      { "pad", "pads", "atmosphere", "ambient pad" } },
        { LoopKind::Synth,    { "synth", "synths" } },
        { LoopKind::Lead,     { "lead", "leads" } },
        { LoopKind::Pluck,    { "pluck", "plucks", "arp", "arps", "arpeggio" } },
        { LoopKind::Brass,    { "brass", "horn", "horns", "trumpet", "trombone" } },
        { LoopKind::Woodwind, { "flute", "flutes", "sax", "saxophone", "woodwind", "clarinet" } },
        { LoopKind::Mallet,   { "bells", "bell", "kalimba", "marimba", "vibraphone", "glockenspiel", "music box", "mallets" } },
        { LoopKind::Vocal,    { "vocal", "vocals", "vox", "voice", "choir", "chops" } },
        { LoopKind::Bass,     { "bass", "808", "808s", "bassline" } },
        { LoopKind::Perc,     { "percussion", "perc", "shaker", "congas", "bongos" } },
        { LoopKind::FX,       { "fx", "riser", "risers", "effects" } },
    };
    // (the "bass" in "drum and bass" is the genre, not an instrument)
    const juce::String instText = text.replace ("drum and bass", " ").replace ("drum & bass", " ");
    auto hasInst = [&] (const char* w) { return instText.contains (juce::String (" ") + w + " ") || instText.contains (juce::String (" ") + w + "s "); };
    for (auto& in : insts)
        for (auto* w : in.words)
            if (hasInst (w))
            {
                if (std::find (p.instruments.begin(), p.instruments.end(), in.kind) == p.instruments.end()) p.instruments.push_back (in.kind);
                break;
            }
    p.sketch = has ("sketch") || has ("short") || has ("idea");

    p.any = p.genre != nullptr || p.bpm > 0 || p.key >= 0 || p.mood != 0 || ! p.instruments.empty();
    // what was understood, said back
    juce::StringArray bits;
    if (p.genre != nullptr) bits.add (p.genre->name);
    if (p.bpm > 0) bits.add (juce::String (juce::roundToInt (p.bpm)) + " bpm");
    if (p.key >= 0) bits.add (juce::String (lp::noteName (p.key)) + (p.minor ? " minor" : " major"));
    else if (p.mood < 0) bits.add ("dark");
    else if (p.mood > 0) bits.add ("bright");
    juce::StringArray kinds;
    for (auto k : p.instruments) kinds.add (loopKindName (k));
    if (! kinds.isEmpty()) bits.add (kinds.joinIntoString (", "));
    p.summary = bits.joinIntoString ("  -  ");
    return p;
}

// The other way round: describes a song you dropped in, in words the box understands (and you can edit)
inline juce::String describeReference (const Profile& p)
{
    if (! p.valid) return {};
    Options o; o.shiftKey = false;
    const auto t = chooseTarget (p, lp::Weather {}, o);
    juce::StringArray bits;
    switch (t.style)
    {
        case Style::Trap:       bits.add (p.bpm >= 138 && p.bpm <= 146 && p.density > 0.6f ? "drill" : "trap"); break;
        case Style::House:      bits.add ("house"); break;
        case Style::BoomBap:    bits.add ("boom bap"); break;
        case Style::Electronic: bits.add (p.bpm >= 124 ? "edm" : "pop"); break;
        case Style::Brushes:    bits.add ("jazz"); break;
        case Style::Dusty:      bits.add (p.minor ? "r&b" : "lofi"); break;
    }
    // (no tempo here: the song follows your project's tempo; type one if you want a different one)
    bits.add (juce::String (lp::noteName (p.key)) + (p.minor ? " minor" : " major"));
    const bool trapish = t.style == Style::Trap || t.style == Style::Electronic;
    if (trapish && p.lowSustain > 0.62f) bits.add ("held 808");
    else if (p.lowSustain > 0.62f)       bits.add ("deep bass");
    else                                 bits.add ("punchy bass");
    if (p.sustain > 0.83f) bits.add (trapish ? "sustained pads and lead" : "warm pads and strings");
    else                   bits.add (p.minor ? "piano and plucks" : "keys and guitar");
    if (p.density > 0.5f) bits.add ("busy hats");
    if (p.tone.valid) bits.add (p.tone.rolloffHz < 600.0f ? "dark warm mix" : (p.tone.airDb > -18.0f ? "bright airy mix" : "smooth mix"));
    return bits.joinIntoString (", ");
}

} // namespace lp::song
