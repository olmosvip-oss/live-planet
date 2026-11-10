#pragma once
#include "Sources.h"
#include "Weather.h"

namespace lp
{

// How each sound is played and what space it lives in.
struct SoundProfile
{
    const char* name;
    bool  padStruck;     // chords are struck like keys/strings (true) or a cloud of grains (false)
    bool  dropStruck;    // melody notes struck (true) or grains (false)
    bool  bassStruck;    // bass note played with the sound itself
    float grainMul;      // grain length
    float scatterAdd;
    float attackMul;     // how slowly the chord fades in
    int   padOctave, dropOctave;
    float bell;          // clear bell tone under the melody
    float strumMs;       // how rolled the chords are
    float restrikeBeats; // struck sounds play the chord again every N beats
    float padGain, dropGain, sineBass;
    float reverb, echo, warmthAdd, brightness, shimmer;
};

inline const SoundProfile& profileFor (SoundId id)
{
    static const SoundProfile profiles[] = {
        //  name            pad    drop   bass   grain scat  atk   pOct dOct bell  strum  re   padG  drpG  sine  verb  echo  warm  brt   shim
        { "Glass",          false, false, false, 1.0f, 0.0f, 1.0f,  0,   0,  1.0f,   0.f, 0.f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.15f, 1.3f },
        { "Choir",          false, false, false, 2.2f,-0.1f, 1.8f,  0, -12,  0.0f,   0.f, 0.f, 1.1f, 1.2f, 0.8f, 1.35f,0.85f, 0.05f,0.85f, 1.4f },
        { "Felt Piano",     true,  true,  true,  1.0f, 0.0f, 1.0f,  0, -12,  0.0f,  22.f, 2.f, 1.0f, 1.0f, 0.3f, 0.85f,0.7f, 0.15f,0.9f,  0.4f },
        { "Music Box",      true,  true,  false, 1.0f, 0.0f, 1.0f, 12,   0,  0.0f, 140.f, 2.f, 1.45f,1.4f, 0.5f, 1.0f, 1.3f, 0.4f, 0.95f, 0.8f },
        { "Nylon Guitar",   true,  true,  true,  1.0f, 0.0f, 1.0f,  0, -12,  0.0f,  45.f, 1.f, 0.9f, 1.0f, 0.2f, 0.7f, 0.8f, 0.2f, 1.0f,  0.3f },
        { "Tape Strings",   false, false, false, 2.6f,-0.15f,2.2f,  0,   0,  0.25f,  0.f, 0.f, 1.0f, 1.15f,1.0f, 1.2f, 0.85f, 0.3f, 0.8f,  0.7f },
        { "Vapor Synth",    false, false, false, 1.3f, 0.25f,0.6f,  0,   0,  0.6f,   0.f, 0.f, 0.9f, 0.9f, 1.1f, 1.1f, 1.4f,-0.1f, 1.3f,  1.6f },
        { "Rhodes",         true,  true,  true,  1.0f, 0.0f, 1.0f,  0, -12,  0.0f,  30.f, 2.f, 0.55f,0.6f, 0.4f, 0.9f, 0.9f, 0.35f,0.8f,  0.4f },
        { "Kalimba",        true,  true,  false, 1.0f, 0.0f, 1.0f,  0,   0,  0.0f,  70.f, 1.f, 1.5f, 1.5f, 0.5f, 0.9f, 1.2f, 0.2f, 0.95f, 0.5f },
        { "Vibes",          true,  true,  false, 1.0f, 0.0f, 1.0f,  0, -12,  0.0f,  15.f, 4.f, 0.9f, 0.9f, 0.7f, 1.1f, 0.8f, 0.15f,0.9f,  0.7f },
        { "Ocarina",        false, false, false, 2.4f,-0.15f,1.6f,  0,   0,  0.15f,  0.f, 0.f, 1.0f, 0.9f, 0.9f, 1.25f,0.9f, 0.2f, 0.85f, 0.9f },
        { "Celesta",        true,  true,  false, 1.0f, 0.0f, 1.0f, 12,   0,  0.0f,  60.f, 2.f, 1.2f, 1.2f, 0.5f, 1.1f, 1.1f, 0.2f, 0.95f, 0.8f },
        { "Harp",           true,  true,  true,  1.0f, 0.0f, 1.0f,  0, -12,  0.0f, 120.f, 2.f, 1.0f, 1.1f, 0.3f, 1.0f, 0.9f, 0.15f,1.0f,  0.5f },
        { "Marimba",        true,  true,  false, 1.0f, 0.0f, 1.0f,  0, -12,  0.0f,  20.f, 1.f, 1.2f, 1.2f, 0.6f, 0.8f, 0.9f, 0.1f, 0.95f, 0.3f },
        { "Melodica",       false, false, false, 2.0f,-0.1f, 1.2f,  0,   0,  0.2f,   0.f, 0.f, 0.9f, 1.0f, 0.9f, 1.0f, 0.8f, 0.3f, 0.85f, 0.5f },
        { "Organ",          false, false, false, 2.6f,-0.2f, 1.0f,  0,   0,  0.1f,   0.f, 0.f, 0.85f,0.9f, 0.7f, 1.2f, 0.6f, 0.25f,0.85f, 0.6f },
        { "Cello",          false, false, false, 2.8f,-0.15f,2.0f,-12, -12,  0.0f,   0.f, 0.f, 1.1f, 1.0f, 0.8f, 1.15f,0.6f, 0.3f, 0.8f,  0.5f },
        { "Your Sample",    false, false, false, 1.6f,-0.1f, 1.2f,  0,   0,  0.35f,  0.f, 0.f, 1.0f, 1.0f, 1.0f, 1.0f, 0.9f, 0.25f,0.8f,  0.7f },
    };
    const int i = juce::jlimit (0, 17, (int) id);
    return profiles[i];
}

// Auto mode: the weather picks the instrument.
inline SoundId autoSoundFor (const Weather& w)
{
    if (w.thunder || w.windKmh > 40.0f)  return SoundId::Strings;   // storms are cinematic
    if (w.snowCm > 0.05f)                return w.isDay ? SoundId::MusicBox : SoundId::Choir;
    if (w.fog)                           return SoundId::Choir;
    if (w.rainMm > 2.5f)                 return SoundId::FeltPiano;
    if (w.rainMm > 1.0f)                 return SoundId::Glass;
    if (w.rainMm > 0.05f)                return SoundId::Rhodes;    // drizzle: dusty and soft
    if (! w.isDay)                       return w.temperatureC > 15.0f ? SoundId::Kalimba : (w.cloudPct < 30.0f ? SoundId::Celesta : SoundId::MusicBox);
    if (w.temperatureC > 24.0f)          return SoundId::Vapor;     // hot and bright
    if (w.cloudPct > 70.0f)              return w.temperatureC < 8.0f ? SoundId::Cello : SoundId::FeltPiano;
    if (w.temperatureC < 10.0f)          return SoundId::Ocarina;   // cool clear air
    if (w.cloudPct > 30.0f)              return w.temperatureC > 18.0f ? SoundId::Marimba : SoundId::Vibes;
    return SoundId::Nylon;                                          // mild and sunny
}

} // namespace lp
