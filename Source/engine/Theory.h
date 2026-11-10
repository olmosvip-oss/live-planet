#pragma once
#include <array>
#include <cstdint>
#include <cmath>
#include <juce_core/juce_core.h>

namespace lp
{

// Small, fast, repeatable random numbers: the same inputs always give the same
// answer, so a looped section of a song sounds the same every time round.
inline uint32_t hash3 (uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u ^ (c + 0x165667B1u) * 0xC2B2AE3Du;
    h ^= h >> 15; h *= 0x2C1B3C6Du;
    h ^= h >> 12; h *= 0x297A2D39u;
    h ^= h >> 15;
    return h;
}
inline float h01 (uint32_t h) { return (float) (h >> 8) * (1.0f / 16777216.0f); }

inline uint32_t hashString (const juce::String& s)
{
    uint32_t h = 2166136261u;
    for (auto c : s.toLowerCase())
        h = (h ^ (uint32_t) c) * 16777619u;
    return h;
}

inline const char* noteName (int pc)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return names[((pc % 12) + 12) % 12];
}

enum class ScaleId { Major, Lydian, Mixolydian, Dorian, Minor };

struct Scale
{
    const char* name;
    std::array<int, 7> steps;
    bool majorish;
};

inline const Scale& getScale (ScaleId id)
{
    static const Scale scales[] = {
        { "Major",      { 0, 2, 4, 5, 7, 9, 11 }, true  },
        { "Lydian",     { 0, 2, 4, 6, 7, 9, 11 }, true  },
        { "Mixolydian", { 0, 2, 4, 5, 7, 9, 10 }, true  },
        { "Dorian",     { 0, 2, 3, 5, 7, 9, 10 }, false },
        { "Minor",      { 0, 2, 3, 5, 7, 8, 10 }, false },
    };
    return scales[(int) id];
}

// Weather mood (0 = cold and grey, 1 = warm and sunny) picks the scale.
inline ScaleId scaleForMood (float mood)
{
    if (mood > 0.78f) return ScaleId::Lydian;
    if (mood > 0.58f) return ScaleId::Major;
    if (mood > 0.45f) return ScaleId::Mixolydian;
    if (mood > 0.30f) return ScaleId::Dorian;
    return ScaleId::Minor;
}

// Semitones of scale step `idx` (can go past 7 into higher octaves).
inline int scaleSemis (const Scale& s, int idx)
{
    int oct = idx >= 0 ? idx / 7 : -((-idx + 6) / 7);
    int i = idx - oct * 7;
    return s.steps[(size_t) i] + 12 * oct;
}

// Chord progressions as scale degrees (0 = I). Grouped by how tense they feel.
struct Progression { int len; int degrees[4]; };

inline const Progression& pickProgression (float tension, uint32_t h)
{
    static const Progression calm[] = {
        { 4, { 0, 4, 5, 3 } }, { 4, { 0, 5, 3, 4 } }, { 4, { 0, 3, 0, 4 } },
        { 4, { 3, 4, 0, 0 } }, { 4, { 0, 2, 3, 3 } }, { 2, { 0, 3 } },
    };
    static const Progression middle[] = {
        { 4, { 5, 3, 0, 4 } }, { 4, { 0, 5, 1, 4 } }, { 4, { 3, 0, 4, 5 } },
        { 4, { 0, 2, 5, 3 } }, { 4, { 5, 4, 3, 4 } }, { 2, { 0, 5 } },
    };
    static const Progression tense[] = {
        { 4, { 5, 3, 4, 2 } }, { 4, { 1, 4, 0, 5 } }, { 4, { 5, 6, 3, 4 } },
        { 4, { 3, 4, 2, 5 } }, { 4, { 5, 1, 2, 4 } }, { 2, { 5, 3 } },
    };
    const auto pick = h % 6;
    if (tension < 0.33f) return calm[pick];
    if (tension < 0.62f) return middle[pick];
    return tense[pick];
}

} // namespace lp
