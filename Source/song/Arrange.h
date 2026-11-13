#pragma once
#include "Song.h"

// Changing a song's structure: sections moved, made longer or shorter, added, removed, duplicated.
// Every new section comes from one of the song's sections (its "source"); every bar of it plays what
// that source played at the same place (a longer section keeps going from its start again). So drums,
// loops, written parts, the hook and the transition sounds all follow the new shape.
namespace lp::song::arrange
{

struct Item
{
    int src = 0;          // which section (of the song as it is now) it comes from
    int bars = 8;
    std::string name;     // its kind ("verse", "chorus", "drop"...): numbered again afterwards
};

inline std::string kindOf (const std::string& name)
{
    auto k = juce::String (name).trimCharactersAtEnd ("0123456789 ");
    return k.toStdString();
}

// the plan that matches a song as it is (no change)
inline std::vector<Item> planOf (const Song& s)
{
    std::vector<Item> p;
    for (size_t i = 0; i < s.sections.size(); ++i) p.push_back ({ (int) i, s.sections[i].bars, kindOf (s.sections[i].name) });
    return p;
}

inline Song rebuild (const Song& old, const std::vector<Item>& plan)
{
    Song s = old;
    s.sections.clear();
    // the new sections, numbered by kind (verse 1, verse 2...; intro / outro stay as they are)
    std::map<std::string, int> count, total;
    for (auto& it : plan) ++total[it.name];
    int bar = 0;
    for (auto& it : plan)
    {
        const auto& src = old.sections[(size_t) juce::jlimit (0, (int) old.sections.size() - 1, it.src)];
        Section sec;
        const int n = ++count[it.name];
        const bool numbered = it.name != "intro" && it.name != "outro" && (total[it.name] > 1 || juce::String (src.name).containsAnyOf ("0123456789"));
        sec.name = numbered ? it.name + " " + std::to_string (n) : it.name;
        sec.startBar = bar;
        sec.bars = std::max (1, it.bars);
        sec.energy = src.energy;
        s.sections.push_back (sec);
        bar += sec.bars;
    }
    const int newBars = bar;
    // every new bar -> the old bar it plays (and whether it's the source's first pass, for one-off sounds)
    std::vector<int> fromBar ((size_t) newBars, 0);
    std::vector<bool> firstPass ((size_t) newBars, true);
    for (size_t j = 0; j < plan.size(); ++j)
    {
        const auto& src = old.sections[(size_t) juce::jlimit (0, (int) old.sections.size() - 1, plan[j].src)];
        for (int k = 0; k < s.sections[j].bars; ++k)
        {
            const int nb = s.sections[j].startBar + k;
            fromBar[(size_t) nb] = src.startBar + k % std::max (1, src.bars);
            firstPass[(size_t) nb] = k < src.bars;
        }
    }
    const int oldBars = old.totalBars();
    for (auto& part : s.parts)
    {
        const auto& was = old.parts[(size_t) (&part - &s.parts[0])];
        // notes (drums, played parts, the hook): bar by bar
        part.notes.clear();
        for (int nb = 0; nb < newBars; ++nb)
        {
            const int ob = fromBar[(size_t) nb];
            for (auto n : was.notes)
                if (n.beat >= ob * 4.0 - 1e-6 && n.beat < ob * 4.0 + 4.0 - 1e-6)
                {
                    n.beat += (nb - ob) * 4.0;
                    part.notes.push_back (n);
                }
        }
        // transition sounds: once, where their section starts again (not repeated inside a longer section)
        part.fxHits.clear();
        for (int nb = 0; nb < newBars; ++nb)
        {
            if (! firstPass[(size_t) nb]) continue;
            const int ob = fromBar[(size_t) nb];
            for (auto h : was.fxHits)
                if (h.beat >= ob * 4.0 - 1e-6 && h.beat < ob * 4.0 + 4.0 - 1e-6)
                {
                    h.beat += (nb - ob) * 4.0;
                    part.fxHits.push_back (h);
                }
        }
        // loops: which bar of the loop plays where, and what was done to it
        if (! was.barSrc.empty())
        {
            part.barSrc.assign ((size_t) newBars, -1);
            for (int nb = 0; nb < newBars; ++nb)
            {
                const int ob = fromBar[(size_t) nb];
                if (ob < (int) was.barSrc.size()) part.barSrc[(size_t) nb] = was.barSrc[(size_t) ob];
            }
        }
        part.barFilter.clear();
        part.chopBars.clear(); part.stutterBars.clear(); part.gapBars.clear(); part.swellBars.clear();
        std::vector<int> starts;
        for (int nb = 0; nb < newBars; ++nb)
        {
            const int ob = fromBar[(size_t) nb];
            if (auto f = was.barFilter.find (ob); f != was.barFilter.end()) part.barFilter[nb] = f->second;
            if (was.chopBars.count (ob)) part.chopBars.insert (nb);
            if (was.stutterBars.count (ob)) part.stutterBars.insert (nb);
            // drop-outs and swells lead into the next section: only where a section still ends
            bool sectionEnd = false;
            for (auto& sec : s.sections) sectionEnd = sectionEnd || nb == sec.startBar + sec.bars - 1;
            if (sectionEnd && was.gapBars.count (ob)) part.gapBars.insert (nb);
            if (sectionEnd && was.swellBars.count (ob)) part.swellBars.insert (nb);
            for (int st : was.loopStartBars)
                if (st == ob && (starts.empty() || nb >= starts.back() + std::max (1, was.loopBars))) starts.push_back (nb);
        }
        if (was.barSrc.empty() || ! was.loopStartBars.empty()) part.loopStartBars = starts;
    }
    juce::ignoreUnused (oldBars);
    return s;
}

} // namespace lp::song::arrange
