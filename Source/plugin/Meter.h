#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include "Peak.h"

// Level meters like a DAW's: two thin bars (left, right), green, turning yellow near the top and red
// when it clips, with a small line that holds the last peak for a moment.
namespace lp::ui
{

struct MeterBallistics
{
    float level = 0.0f, hold = 0.0f;
    juce::uint32 holdUntil = 0;
    // fast up, smooth fall (about 20 dB a second), the peak line holds for 1.2 s
    void update (float peak)
    {
        const auto now = juce::Time::getMillisecondCounter();
        level = std::max (peak, level * 0.80f);
        if (peak >= hold || now > holdUntil) { hold = peak; holdUntil = now + 1200; }
    }
};

// where a level sits on the meter: -60 dB at the bottom, +6 dB at the top (0 dB a little below it)
inline float meterPos (float gain)
{
    const float db = juce::Decibels::gainToDecibels (gain, -60.0f);
    return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f);
}

inline void drawMeterBar (juce::Graphics& g, juce::Rectangle<float> r, const MeterBallistics& m)
{
    g.setColour (juce::Colour (0xff141416));
    g.fillRect (r);
    const float h = r.getHeight();
    const float lv = meterPos (m.level) * h;
    // the colours by height: green, then yellow from -6 dB, red above 0 dB
    const float yYellow = meterPos (juce::Decibels::decibelsToGain (-6.0f)) * h, yRed = meterPos (1.0f) * h;
    auto seg = [&] (float from, float to, juce::Colour c)
    {
        const float a = std::min (from, lv), b = std::min (to, lv);
        if (b > a) { g.setColour (c); g.fillRect (r.getX(), r.getBottom() - b, r.getWidth(), b - a); }
    };
    seg (0.0f, yYellow, juce::Colour (0xff3ee05a));
    seg (yYellow, yRed, juce::Colour (0xffe8d23a));
    seg (yRed, h, juce::Colour (0xffff4a3d));
    if (m.hold > 0.001f)
    {
        const float y = r.getBottom() - meterPos (m.hold) * h;
        g.setColour (m.hold >= 1.0f ? juce::Colour (0xffff4a3d) : juce::Colour (0xffd8f5dc));
        g.fillRect (r.getX(), y - 1.0f, r.getWidth(), 1.5f);
    }
}

// a stereo meter: two bars side by side
inline void drawStereoMeter (juce::Graphics& g, juce::Rectangle<float> r, const MeterBallistics& l, const MeterBallistics& rr)
{
    const float w = (r.getWidth() - 1.0f) * 0.5f;
    drawMeterBar (g, r.withWidth (w), l);
    drawMeterBar (g, r.withTrimmedLeft (w + 1.0f), rr);
}

} // namespace lp::ui
