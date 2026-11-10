#pragma once
#include <initializer_list>
#include <utility>

// Factory presets. Each one only lists what differs from the defaults.
// sound: 0 = auto, 1 glass, 2 choir, 3 felt piano, 4 music box, 5 nylon guitar, 6 tape strings,
//        7 vapor synth, 8 rhodes, 9 kalimba, 10 vibes, 11 ocarina
// key:   0 = auto, 1..12 = C..B      scale: 0 = auto, 1 major, 2 lydian, 3 mixolydian, 4 dorian, 5 minor
namespace lp::presets
{

struct Preset
{
    const char* name;
    std::initializer_list<std::pair<const char*, float>> values;
};

inline const Preset all[] = {
    { "pure weather",          { } },
    { "rainy window",          { { "sound", 8 },  { "nostalgia", 0.5f },  { "echo", 0.45f }, { "space", 0.65f }, { "warmth", 0.55f } } },
    { "grandma's music box",   { { "sound", 4 },  { "nostalgia", 0.7f },  { "warmth", 0.6f }, { "drops", 0.55f }, { "position", 0.15f } } },
    { "4am drive",             { { "sound", 7 },  { "scale", 5 },         { "nostalgia", 0.3f }, { "echo", 0.6f }, { "bass", 0.6f } } },
    { "snow globe",            { { "sound", 1 },  { "sparkle", 0.7f },    { "shimmer", 0.7f }, { "nostalgia", 0.2f }, { "space", 0.75f } } },
    { "old film reel",         { { "sound", 6 },  { "nostalgia", 0.9f },  { "warmth", 0.7f }, { "drops", 0.4f } } },
    { "summer porch",          { { "sound", 5 },  { "scale", 1 },         { "nostalgia", 0.4f }, { "space", 0.45f } } },
    { "paper lanterns",        { { "sound", 9 },  { "echo", 0.55f },      { "space", 0.7f }, { "nostalgia", 0.25f } } },
    { "sunday morning",        { { "sound", 3 },  { "scale", 1 },         { "nostalgia", 0.35f }, { "position", 0.4f } } },
    { "lighthouse",            { { "sound", 2 },  { "space", 0.9f },      { "shimmer", 0.6f }, { "drops", 0.45f } } },
    { "cassette diary",        { { "sound", 8 },  { "nostalgia", 0.85f }, { "texture", 0.6f }, { "warmth", 0.6f } } },
    { "firefly field",         { { "sound", 9 },  { "sparkle", 0.6f },    { "drops", 0.85f }, { "scale", 2 } } },
    { "cloud nap",             { { "sound", 11 }, { "grain", 280 },       { "space", 0.8f }, { "drops", 0.45f }, { "nostalgia", 0.2f } } },
    { "vinyl sunset",          { { "sound", 10 }, { "nostalgia", 0.7f },  { "scale", 3 }, { "warmth", 0.55f } } },
    { "tidepools",             { { "sound", 1 },  { "position", 0.7f },   { "texture", 0.55f }, { "atmos", 0.45f } } },
    { "attic toys",            { { "sound", 4 },  { "nostalgia", 0.6f },  { "sparkle", 0.5f }, { "echo", 0.45f } } },
    { "moonlit lake",          { { "sound", 10 }, { "space", 0.85f },     { "shimmer", 0.5f }, { "scale", 4 } } },
    { "faded polaroid",        { { "sound", 3 },  { "nostalgia", 0.75f }, { "warmth", 0.7f }, { "position", 0.55f } } },
    { "northern lights",       { { "sound", 2 },  { "shimmer", 0.9f },    { "sparkle", 0.4f }, { "space", 0.85f } } },
    { "lullaby",               { { "sound", 4 },  { "scale", 1 },         { "drops", 0.5f }, { "pad", 0.6f }, { "space", 0.7f }, { "nostalgia", 0.3f } } },
    { "train window",          { { "sound", 5 },  { "echo", 0.5f },       { "nostalgia", 0.5f }, { "texture", 0.45f } } },
    { "dream arcade",          { { "sound", 7 },  { "nostalgia", 0.5f },  { "sparkle", 0.5f }, { "scale", 2 } } },
    { "forest hut",            { { "sound", 11 }, { "nostalgia", 0.4f },  { "atmos", 0.5f }, { "warmth", 0.5f } } },
    { "static radio",          { { "sound", 6 },  { "nostalgia", 1.0f },  { "space", 0.4f } } },
    { "bloom",                 { { "sound", 3 },  { "position", 0.85f },  { "grain", 300 }, { "space", 0.75f } } },
    { "glass garden",          { { "sound", 1 },  { "nostalgia", 0.1f },  { "scale", 2 }, { "shimmer", 0.5f } } },
    { "wind chimes",           { { "sound", 9 },  { "position", 0.0f },   { "sparkle", 0.8f }, { "pad", 0.45f } } },
    { "hazy afternoon",        { { "sound", 8 },  { "warmth", 0.6f },     { "space", 0.5f }, { "nostalgia", 0.45f } } },
    { "distant choir",         { { "sound", 2 },  { "nostalgia", 0.5f },  { "echo", 0.5f }, { "pad", 0.6f } } },
    { "rain on tin roof",      { { "sound", 0 },  { "atmos", 0.7f },      { "nostalgia", 0.4f }, { "echo", 0.4f } } },
};

constexpr int count = (int) (sizeof (all) / sizeof (all[0]));

// the settings a preset controls (everything else, like the city, is left alone)
inline const char* const controlled[] = { "sound", "key", "scale", "pad", "drops", "bass", "grain", "texture",
                                          "sparkle", "position", "warmth", "echo", "space", "shimmer", "atmos",
                                          "nostalgia" };

} // namespace lp::presets
