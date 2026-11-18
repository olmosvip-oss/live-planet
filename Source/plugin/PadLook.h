#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// How a pad looks (drum pads, chop pads): dark glass with its colour glowing up from the bottom, a key cap,
// a small light, and a soft glow and press when it's hit. lit: 0..1 (1 = just hit).
namespace lp::ui
{
inline void drawPad (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour col, float lit, const juce::String& name,
                     const juce::String& keyCap, const juce::String& sub, bool selected, const juce::Font& nameFont, const juce::Font& smallFont,
                     juce::Colour ink, juce::Colour inkDim)
{
    using namespace juce;
    const float rad = 16.0f;
    if (lit > 0.6f) r = r.reduced (1.5f * (lit - 0.6f) / 0.4f); // pressed in
    // its glow, outside, while it rings
    if (lit > 0.01f)
        for (int k = 1; k <= 4; ++k)
        {
            g.setColour (col.withAlpha (0.07f * lit * (float) (5 - k) / 4.0f));
            g.fillRoundedRectangle (r.expanded ((float) k * 3.0f), rad + (float) k * 3.0f);
        }
    // the glass
    g.setGradientFill (ColourGradient (Colour (0xff17171d), r.getX(), r.getY(), Colour (0xff0b0b0f), r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, rad);
    {
        Graphics::ScopedSaveState s (g);
        Path clip; clip.addRoundedRectangle (r, rad);
        g.reduceClipRegion (clip);
        // its colour rising from the bottom (brighter while it rings)
        g.setGradientFill (ColourGradient (col.withAlpha (0.20f + 0.55f * lit), r.getCentreX(), r.getBottom() + r.getHeight() * 0.15f,
                                           col.withAlpha (0.0f), r.getCentreX(), r.getY() + r.getHeight() * 0.15f, true));
        g.fillRect (r);
        // a thin light along the top edge
        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.10f), r.getX(), r.getY(), Colours::white.withAlpha (0.0f), r.getX(), r.getY() + 18.0f, false));
        g.fillRect (r.withHeight (18.0f));
    }
    g.setColour (selected ? col.withAlpha (0.95f) : lit > 0.01f ? col.withAlpha (0.35f + 0.6f * lit) : Colours::white.withAlpha (0.07f));
    g.drawRoundedRectangle (r.reduced (0.5f), rad, selected || lit > 0.3f ? 1.6f : 1.0f);

    auto in = r.reduced (14.0f, 12.0f);
    // the key cap
    if (keyCap.isNotEmpty())
    {
        auto cap = in.withSize (24.0f, 22.0f);
        g.setColour (Colours::white.withAlpha (0.05f + 0.1f * lit));
        g.fillRoundedRectangle (cap, 6.0f);
        g.setColour (Colours::white.withAlpha (0.14f));
        g.drawRoundedRectangle (cap.reduced (0.5f), 6.0f, 1.0f);
        g.setFont (smallFont);
        g.setColour (ink.withAlpha (0.85f));
        g.drawText (keyCap, cap, Justification::centred, false);
    }
    // its light
    {
        const auto led = Rectangle<float> (7.0f, 7.0f).withCentre ({ in.getRight() - 3.5f, in.getY() + 11.0f });
        g.setColour (col.withAlpha (0.25f + 0.75f * lit));
        g.fillEllipse (led);
        if (lit > 0.05f) { g.setColour (col.withAlpha (0.3f * lit)); g.fillEllipse (led.expanded (4.0f)); }
    }
    // its name, and a line under it (a note, a velocity)
    g.setFont (nameFont);
    g.setColour (ink.withAlpha (0.92f));
    auto bottom = in.removeFromBottom (sub.isNotEmpty() ? 34.0f : 18.0f);
    g.drawText (name, bottom.removeFromTop (18.0f), Justification::bottomLeft, true);
    if (sub.isNotEmpty())
    {
        g.setFont (smallFont);
        g.setColour (inkDim);
        g.drawText (sub, bottom, Justification::bottomLeft, true);
    }
}

// a pad with nothing on it: just a faint outline
inline void drawEmptyPad (juce::Graphics& g, juce::Rectangle<float> r)
{
    using namespace juce;
    g.setColour (Colours::white.withAlpha (0.018f));
    g.fillRoundedRectangle (r, 16.0f);
    g.setColour (Colours::white.withAlpha (0.045f));
    g.drawRoundedRectangle (r.reduced (0.5f), 16.0f, 1.0f);
}
} // namespace lp::ui
