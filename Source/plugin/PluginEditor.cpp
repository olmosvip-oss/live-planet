#include "PluginEditor.h"
#include "PlanetShader.h"
#include "BinaryData.h"
#include "GrassArt.h"
#include "../song/Prompt.h"

using namespace juce;
using lp::ui::font;

namespace
{
    constexpr int kW = 1120, kH = 760;

    Colour hsvCol (float h, float s, float v, float a = 1.0f)
    {
        return Colour::fromHSV (std::fmod (h + 10.0f, 1.0f), s, v, a);
    }

    struct RGB { float r, g, b; };
    RGB lerp (RGB a, RGB b, float t) { return { a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t }; }

    // a soft LED: glow plus a bright core
    void led (Graphics& g, Point<float> c, Colour col, float size, float glow)
    {
        ColourGradient halo (col.withAlpha (0.55f * glow), c.x, c.y, col.withAlpha (0.0f), c.x + size * 2.6f, c.y, true);
        g.setGradientFill (halo);
        g.fillEllipse (c.x - size * 2.6f, c.y - size * 2.6f, size * 5.2f, size * 5.2f);
        g.setColour (col);
        g.fillEllipse (c.x - size * 0.5f, c.y - size * 0.5f, size, size);
        g.setColour (Colours::white.withAlpha (0.6f * glow));
        g.fillEllipse (c.x - size * 0.22f, c.y - size * 0.3f, size * 0.4f, size * 0.4f);
    }

    // a shallow well cut into the body: shadow along the top, light catching the bottom edge
    void recess (Graphics& g, Rectangle<float> r, float radius, const lp::ui::Theme& th)
    {
        g.setColour (th.bodyDark.withAlpha (th.dark ? 0.7f : 0.45f));
        g.fillRoundedRectangle (r, radius);
        Graphics::ScopedSaveState ss (g);
        Path clip;
        clip.addRoundedRectangle (r, radius);
        g.reduceClipRegion (clip);
        ColourGradient top (Colours::black.withAlpha (th.dark ? 0.45f : 0.22f), 0, r.getY(), Colours::black.withAlpha (0.0f), 0, r.getY() + 12.0f, false);
        g.setGradientFill (top);
        g.fillRect (r.withHeight (12.0f));
        ColourGradient side (Colours::black.withAlpha (th.dark ? 0.25f : 0.12f), r.getX(), 0, Colours::black.withAlpha (0.0f), r.getX() + 8.0f, 0, false);
        g.setGradientFill (side);
        g.fillRect (r.withWidth (8.0f));
        g.setColour (Colours::white.withAlpha (th.dark ? 0.1f : 0.35f));
        g.drawLine (r.getX() + radius, r.getBottom() - 0.5f, r.getRight() - radius, r.getBottom() - 0.5f, 1.0f);
    }

    // a clean rounded button; the selected one turns dark like the knob faces
    void hardwareKey (Graphics& g, Rectangle<float> r, bool lit, bool)
    {
        g.setColour (Colours::black.withAlpha (0.12f));
        g.fillRoundedRectangle (r.translated (0.0f, 1.5f).expanded (0.5f), 8.0f);
        ColourGradient body (lit ? Colour (0xff33414c) : Colour (0xffffffff), r.getX(), r.getY(),
                             lit ? Colour (0xff121a20) : Colour (0xffe4e8ec), r.getX(), r.getBottom(), false);
        g.setGradientFill (body);
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (Colours::black.withAlpha (lit ? 0.3f : 0.08f));
        g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
    }
}

//==============================================================================

Font lp::ui::font (float size, Weight w, float kerning)
{
    static Typeface::Ptr light  = Typeface::createSystemTypefaceFor (BinaryData::SpaceGroteskLight_ttf,  BinaryData::SpaceGroteskLight_ttfSize);
    static Typeface::Ptr medium = Typeface::createSystemTypefaceFor (BinaryData::SpaceGroteskMedium_ttf, BinaryData::SpaceGroteskMedium_ttfSize);
    static Typeface::Ptr bold   = Typeface::createSystemTypefaceFor (BinaryData::SpaceGroteskBold_ttf,   BinaryData::SpaceGroteskBold_ttfSize);
    auto tf = w == Light ? light : (w == Bold ? bold : medium);
    return Font (FontOptions (tf).withHeight (size).withKerningFactor (kerning));
}

const lp::ui::Theme& lp::ui::theme (int i)
{
    static const Theme themes[] = {
        //  name        body          light         dark          ink           inkDim        line          dark?
        //  name    surface / light / shade                                   ink                 muted               line                dark   accent light / accent / deep                                 background top / bottom
        { "sky",    Colour (0xffe8ebee), Colour (0xfff7f8fa), Colour (0xffcfd5db), Colour (0xff1e2731), Colour (0xff7d8896), Colour (0x221e2731), false, Colour (0xff7fdcff), Colour (0xff27b4ea), Colour (0xff0b6f99), Colour (0xfff3f5f7), Colour (0xffdfe3e8) },
        { "mint",   Colour (0xffe8ecea), Colour (0xfff7f9f8), Colour (0xffcfd6d2), Colour (0xff1d2a26), Colour (0xff7a8a84), Colour (0x221d2a26), false, Colour (0xff8ef0c8), Colour (0xff2fcf95), Colour (0xff0d7d55), Colour (0xfff3f6f4), Colour (0xffdfe5e2) },
        { "amber",  Colour (0xffece9e5), Colour (0xfff9f8f6), Colour (0xffd8d2cb), Colour (0xff2b241d), Colour (0xff8a8076), Colour (0x222b241d), false, Colour (0xffffd28a), Colour (0xfff2a33a), Colour (0xffa0600f), Colour (0xfff7f5f2), Colour (0xffe6e1db) },
        { "violet", Colour (0xffeae9ee), Colour (0xfff8f7fa), Colour (0xffd3d0dc), Colour (0xff221f2d), Colour (0xff817c91), Colour (0x22221f2d), false, Colour (0xffc7b2ff), Colour (0xff8f6cf0), Colour (0xff4f31a8), Colour (0xfff5f4f8), Colour (0xffe2e0e8) },
    };
    return themes[jlimit (0, numThemes - 1, i)];
}

//==============================================================================
// Look and feel

LivePlanetLookAndFeel::LivePlanetLookAndFeel()
{
    setColour (TextEditor::highlightColourId, Colours::black.withAlpha (0.12f));
    setColour (PopupMenu::backgroundColourId, Colour (0xff1c1b19));
    setColour (PopupMenu::textColourId, Colours::white);
    setColour (BubbleComponent::backgroundColourId, Colour (0xff1c1b19));
    setDefaultSansSerifTypeface (font (14.0f).getTypefacePtr());
}

void LivePlanetLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos,
                                              float start, float end, Slider& s)
{
    const int size = (int) s.getProperties().getWithDefault ("size", 60);
    const bool enabled = s.isEnabled();
    const float alpha = enabled ? 1.0f : 0.4f;
    const bool small = size < 50;
    auto area = Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    const float labelH = small ? 30.0f : 20.0f;
    const float r = (float) size * 0.5f;
    const float blockH = r * 2.0f + 10.0f + labelH;
    const Point<float> c (area.getCentreX(), area.getCentreY() - blockH * 0.5f + r);
    const float angle = start + pos * (end - start);
    const auto accent = th->grass.withMultipliedAlpha (alpha);

    // soft shadow under the knob
    ColourGradient shadow (Colours::black.withAlpha (0.28f * alpha), c.x, c.y + r * 0.35f, Colours::black.withAlpha (0.0f), c.x, c.y + r * 1.45f, true);
    g.setGradientFill (shadow);
    g.fillEllipse (c.x - r * 1.25f, c.y - r * 0.9f, r * 2.5f, r * 2.5f);

    // polished light rim
    ColourGradient rim (Colours::white.withMultipliedAlpha (alpha), c.x - r * 0.4f, c.y - r, Colour (0xffaeb6be).withMultipliedAlpha (alpha), c.x + r * 0.4f, c.y + r, false);
    g.setGradientFill (rim);
    g.fillEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);
    g.setColour (Colours::black.withAlpha (0.12f * alpha));
    g.drawEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f, 1.0f);

    // dark face
    const float rf = r * (small ? 0.8f : 0.84f);
    ColourGradient face (Colour (0xff34424e).withMultipliedAlpha (alpha), c.x - rf * 0.5f, c.y - rf,
                         Colour (0xff0e151b).withMultipliedAlpha (alpha), c.x + rf * 0.5f, c.y + rf, false);
    g.setGradientFill (face);
    g.fillEllipse (c.x - rf, c.y - rf, rf * 2.0f, rf * 2.0f);

    // the arc, inside the face
    const float ra = rf * 0.8f, aw = std::max (2.0f, rf * 0.1f);
    Path track;
    track.addCentredArc (c.x, c.y, ra, ra, 0, start, end, true);
    g.setColour (Colours::black.withAlpha (0.45f * alpha));
    g.strokePath (track, PathStrokeType (aw, PathStrokeType::curved, PathStrokeType::rounded));
    if (pos > 0.002f)
    {
        Path arc;
        arc.addCentredArc (c.x, c.y, ra, ra, 0, start, angle, true);
        g.setColour (accent.withAlpha (0.25f * alpha));
        g.strokePath (arc, PathStrokeType (aw * 2.6f, PathStrokeType::curved, PathStrokeType::rounded));
        ColourGradient arcCol (th->grassDark.withMultipliedAlpha (alpha), c.x - ra, c.y + ra, th->grassLight.withMultipliedAlpha (alpha), c.x + ra, c.y - ra, false);
        g.setGradientFill (arcCol);
        g.strokePath (arc, PathStrokeType (aw, PathStrokeType::curved, PathStrokeType::rounded));
    }

    if (small)
    {
        const Point<float> tip (c.x + ra * std::sin (angle), c.y - ra * std::cos (angle));
        g.setColour (Colours::white.withAlpha (0.9f * alpha));
        g.fillEllipse (tip.x - 2.0f, tip.y - 2.0f, 4.0f, 4.0f);
    }
    else
    {
        // the value, right in the middle
        auto txt = s.getTextFromValue (s.getValue()).upToFirstOccurrenceOf (" ", false, false);
        g.setColour (Colours::white.withAlpha (0.92f * alpha));
        g.setFont (font (rf * (size > 90 ? 0.38f : 0.44f), lp::ui::Light));
        g.drawText (txt, Rectangle<float> (c.x - rf, c.y - rf * 0.35f, rf * 2.0f, rf * 0.7f), Justification::centred, false);
        auto unit = s.getTextFromValue (s.getValue()).fromFirstOccurrenceOf (" ", false, false);
        if (unit.isNotEmpty())
        {
            g.setColour (Colours::white.withAlpha (0.45f * alpha));
            g.setFont (font (rf * 0.2f, lp::ui::Medium));
            g.drawText (unit, Rectangle<float> (c.x - rf, c.y + rf * 0.32f, rf * 2.0f, rf * 0.3f), Justification::centred, false);
        }
    }

    // name (and value for the small ones)
    auto labels = Rectangle<float> (area.getX(), c.y + r + 8.0f, area.getWidth(), labelH);
    g.setFont (font (small ? 12.0f : 13.5f, lp::ui::Medium));
    g.setColour (th->ink.withMultipliedAlpha (alpha));
    g.drawText (s.getName(), labels.removeFromTop (small ? 15.0f : 18.0f), Justification::centred, false);
    if (small)
    {
        g.setFont (font (11.0f));
        g.setColour (th->inkDim.withMultipliedAlpha (alpha));
        g.drawText (s.getTextFromValue (s.getValue()), labels, Justification::centredTop, false);
    }
}

void LivePlanetLookAndFeel::drawButtonBackground (Graphics& g, Button& b, const Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (Colours::black.withAlpha (0.22f));
    g.fillRoundedRectangle (r.translated (0, 1.5f), 8.0f);
    ColourGradient pad (down ? th->body : th->bodyLight, 0, r.getY(), down ? th->bodyDark : th->body, 0, r.getBottom(), false);
    g.setGradientFill (pad);
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (Colours::white.withAlpha ((th->dark ? 0.12f : 0.4f) * (over ? 1.4f : 1.0f)));
    g.drawLine (r.getX() + 6, r.getY() + 0.5f, r.getRight() - 6, r.getY() + 0.5f, 1.0f);
}

void LivePlanetLookAndFeel::drawButtonText (Graphics& g, TextButton& b, bool, bool)
{
    g.setFont (font (13.5f));
    g.setColour (th->ink);
    g.drawText (b.getButtonText(), b.getLocalBounds(), Justification::centred, false);
}

Font LivePlanetLookAndFeel::getTextButtonFont (TextButton&, int) { return font (13.5f); }

void LivePlanetLookAndFeel::drawToggleButton (Graphics& g, ToggleButton& b, bool over, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const float alpha = b.isEnabled() ? 1.0f : 0.4f;
    recess (g, r, 8.0f, *th);
    if (over) { g.setColour (th->ink.withAlpha (0.15f)); g.drawRoundedRectangle (r, 8.0f, 1.0f); }
    const auto col = b.getToggleState() ? hsvCol (b.getName() == "thunder" ? 0.75f : 0.1f, 0.7f, 1.0f) : Colour (0xff4a4844);
    led (g, { r.getX() + 15.0f, r.getCentreY() }, col.withMultipliedAlpha (alpha), 6.0f, b.getToggleState() ? 1.0f : 0.0f);
    g.setFont (font (13.0f));
    g.setColour (th->ink.withMultipliedAlpha (alpha));
    g.drawText (b.getButtonText(), r.withTrimmedLeft (28.0f), Justification::centredLeft, false);
}

//==============================================================================
// Small parts

Segmented::Segmented (StringArray names, std::function<int()> get, std::function<void (int)> set, const lp::ui::Theme*& theme)
    : labels (std::move (names)), getter (std::move (get)), setter (std::move (set)), th (theme)
{
}

void Segmented::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    recess (g, r, 8.0f, *th);
    const int sel = getter();
    const int n = labels.size();
    for (int i = 0; i < n; ++i)
    {
        auto half = r.withWidth (r.getWidth() / (float) n).translated ((float) i * r.getWidth() / (float) n, 0.0f).reduced (2.0f);
        if (i == sel)
        {
            // a raised pad sitting in the well
            g.setColour (Colours::black.withAlpha (0.25f));
            g.fillRoundedRectangle (half.translated (0, 1.5f), 6.0f);
            ColourGradient pad (th->bodyLight, 0, half.getY(), th->body, 0, half.getBottom(), false);
            g.setGradientFill (pad);
            g.fillRoundedRectangle (half, 6.0f);
            g.setColour (Colours::white.withAlpha (th->dark ? 0.12f : 0.4f));
            g.drawLine (half.getX() + 5, half.getY() + 0.5f, half.getRight() - 5, half.getY() + 0.5f, 1.0f);
        }
        g.setFont (font (13.5f));
        g.setColour (i == sel ? th->ink : th->inkDim);
        g.drawText (labels[i], half, Justification::centred, false);
    }
}

void Segmented::mouseUp (const MouseEvent& e)
{
    setter (jlimit (0, labels.size() - 1, (int) (e.position.x / (float) getWidth() * (float) labels.size())));
    repaint();
}

Stepper::Stepper (RangedAudioParameter& p, const lp::ui::Theme*& theme) : param (p), th (theme) {}

void Stepper::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    recess (g, r, 8.0f, *th);
    g.setFont (font (13.5f));
    g.setColour (th->inkDim);
    g.drawText ("<", r.removeFromLeft (26.0f), Justification::centred, false);
    g.drawText (">", r.removeFromRight (26.0f), Justification::centred, false);
    g.setColour (th->ink);
    g.drawText (param.getCurrentValueAsText().toLowerCase(), r, Justification::centred, false);
}

void Stepper::mouseUp (const MouseEvent& e)
{
    const int n = param.getNumSteps();
    const int cur = roundToInt (param.getValue() * (float) (n - 1));
    const int next = (cur + (e.position.x < (float) getWidth() * 0.5f ? n - 1 : 1)) % n;
    param.beginChangeGesture();
    param.setValueNotifyingHost ((float) next / (float) (n - 1));
    param.endChangeGesture();
    repaint();
}

SoundKeys::SoundKeys (LivePlanetProcessor& p, const lp::ui::Theme*& theme) : proc (p), th (theme) {}

float SoundKeys::hueFor (int i)
{
    static const float hues[] = { 0.0f, 0.5f, 0.74f, 0.07f, 0.9f, 0.15f, 0.99f, 0.6f, 0.04f, 0.11f, 0.55f, 0.28f,
                                  0.82f, 0.45f, 0.09f, 0.34f, 0.66f, 0.02f };
    return hues[jlimit (0, 17, i)];
}

String SoundKeys::nameFor (int i)
{
    static const char* names[] = { "auto", "glass", "choir", "piano", "music box", "guitar", "strings", "vapor",
                                   "rhodes", "kalimba", "vibes", "ocarina", "celesta", "harp", "marimba", "melodica",
                                   "organ", "cello" };
    return names[jlimit (0, 17, i)];
}

Rectangle<float> SoundKeys::keyBounds (int i) const
{
    const float gap = 8.0f;
    constexpr int cols = 5, rows = 4;
    const float g2 = 5.0f;
    const float kw = ((float) getWidth() - g2 * (cols - 1)) / (float) cols;
    const float kh = ((float) getHeight() - g2 * (rows - 1) - 3.0f) / (float) rows;
    return { (float) (i % cols) * (kw + g2), (float) (i / cols) * (kh + g2), kw, kh };
}

void SoundKeys::paint (Graphics& g)
{
    const int selected = (int) proc.params.getRawParameterValue ("sound")->load();
    const int autoPick = proc.engine.uiSound.load() + 1;
    const float t = (float) (Time::getMillisecondCounterHiRes() * 0.001);
    for (int i = 0; i < 18; ++i)
    {
        auto r = keyBounds (i);
        const bool lit = i == selected;
        const bool picked = selected == 0 && i == autoPick;
        hardwareKey (g, r, lit, th->dark);
        const auto col = i == 0 ? hsvCol (t * 0.08f, 0.65f, 1.0f) : hsvCol (hueFor (i), 0.7f, 1.0f);
        const float glow = lit ? 1.0f : (picked ? 0.6f + 0.4f * std::sin (t * 4.0f) : 0.0f);
        led (g, { r.getX() + 9.0f, r.getCentreY() }, glow > 0.0f ? col : col.withMultipliedBrightness (0.35f), 4.0f, glow);
        g.setFont (font (11.0f));
        g.setColour (lit ? Colours::white : th->ink.withAlpha (picked ? 1.0f : 0.75f));
        g.drawFittedText (nameFor (i), r.withTrimmedLeft (15.0f).withTrimmedRight (2.0f).toNearestInt(), Justification::centred, 1, 0.75f);
    }
}

void SoundKeys::mouseUp (const MouseEvent& e)
{
    for (int i = 0; i < 18; ++i)
        if (keyBounds (i).contains (e.position))
            if (auto* p = proc.params.getParameter ("sound"))
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost (p->convertTo0to1 ((float) i));
                p->endChangeGesture();
                repaint();
            }
}

void PlayButton::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    const bool song = songMode && songMode();
    const bool on = song ? proc.previewPlaying.load() : proc.engine.uiRunning.load();
    const bool over = isMouseOver();
    g.setColour (Colours::white.withAlpha (over ? 0.2f : 0.12f));
    g.fillEllipse (r);
    g.setColour (Colours::white.withAlpha (over ? 0.55f : 0.35f));
    g.drawEllipse (r, 1.2f);
    const auto c = r.getCentre();
    g.setColour (Colours::white.withAlpha (0.92f));
    if (on)
        g.fillRoundedRectangle (c.x - 6.0f, c.y - 6.0f, 12.0f, 12.0f, 2.0f);
    else
    {
        Path tri;
        tri.addTriangle (c.x - 4.5f, c.y - 7.5f, c.x - 4.5f, c.y + 7.5f, c.x + 8.0f, c.y);
        g.fillPath (tri);
    }
}

void PlayButton::mouseUp (const MouseEvent&)
{
    if (songMode && songMode())
    {
        if (proc.studio.length() > 0) proc.previewPlaying = ! proc.previewPlaying.load();
        repaint();
        return;
    }
    if (auto* p = proc.params.getParameter ("play"))
    {
        // if the DAW is playing, the button can't stop it, so only toggle our own play
        const bool nowOn = p->getValue() > 0.5f;
        p->beginChangeGesture();
        p->setValueNotifyingHost (nowOn ? 0.0f : 1.0f);
        p->endChangeGesture();
    }
    repaint();
}

//==============================================================================

void PresetBrowser::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (Colours::white.withAlpha (0.45f));
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (th->ink.withAlpha (0.15f));
    g.drawRoundedRectangle (r, 8.0f, 1.0f);
    g.setFont (font (13.5f));
    g.setColour (th->inkDim);
    g.drawText ("<", r.removeFromLeft (28.0f), Justification::centred, false);
    g.drawText (">", r.removeFromRight (28.0f), Justification::centred, false);
    g.setColour (th->ink);
    const int i = proc.getCurrentProgram();
    g.drawText (String (i + 1) + "  " + proc.getProgramName (i), r, Justification::centred, false);
}

void PresetBrowser::mouseUp (const MouseEvent& e)
{
    const int n = proc.getNumPrograms(), cur = proc.getCurrentProgram();
    if (e.position.x < 28.0f)                         proc.setCurrentProgram ((cur + n - 1) % n);
    else if (e.position.x > (float) getWidth() - 28.0f) proc.setCurrentProgram ((cur + 1) % n);
    else
    {
        PopupMenu menu;
        for (int i = 0; i < n; ++i)
            menu.addItem (i + 1, proc.getProgramName (i), true, i == cur);
        menu.showMenuAsync (PopupMenu::Options().withTargetComponent (this).withMaximumNumColumns (2),
                            [this] (int result) { if (result > 0) { proc.setCurrentProgram (result - 1); repaint(); } });
    }
    repaint();
}

//==============================================================================

Rectangle<float> SampleView::waveArea() const { return getLocalBounds().toFloat().reduced (14.0f, 20.0f).withTrimmedRight (120.0f); }
Rectangle<float> SampleView::tuneDown() const  { return { (float) getWidth() - 122.0f, (float) getHeight() * 0.5f - 6.0f, 26.0f, 26.0f }; }
Rectangle<float> SampleView::tuneUp() const    { return { (float) getWidth() - 40.0f, (float) getHeight() * 0.5f - 6.0f, 26.0f, 26.0f }; }

void SampleView::setParam (const char* id, float value)
{
    if (auto* p = proc.params.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (value));
}

void SampleView::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (Colour (0xf0060609));
    g.fillRect (b);
    g.setColour (Colours::white.withAlpha (0.1f));
    g.drawLine (b.getX(), b.getY(), b.getRight(), b.getY(), 1.0f);

    const auto peaks = proc.getSamplePeaks();
    auto w = waveArea();
    const float st = proc.params.getRawParameterValue ("smpStart")->load();
    const float en = proc.params.getRawParameterValue ("smpEnd")->load();
    const float x0 = w.getX() + st * w.getWidth(), x1 = w.getX() + en * w.getWidth();

    g.setFont (font (11.5f));
    g.setColour (Colours::white.withAlpha (0.5f));
    g.drawText ("your sample  -  drag the edges to choose the part that plays", Rectangle<float> (w.getX(), 3.0f, w.getWidth(), 14.0f), Justification::centredLeft, false);

    // highlighted region
    g.setColour (Colour (0xff9fcb55).withAlpha (0.12f));
    g.fillRect (Rectangle<float> (x0, w.getY(), x1 - x0, w.getHeight()));

    if (! peaks.empty())
    {
        const float mid = w.getCentreY(), half = w.getHeight() * 0.5f;
        for (size_t i = 0; i < peaks.size(); ++i)
        {
            const float x = w.getX() + (float) i / (float) peaks.size() * w.getWidth();
            const bool inside = x >= x0 && x <= x1;
            g.setColour (inside ? Colour (0xffe9f5d6).withAlpha (0.85f) : Colours::white.withAlpha (0.18f));
            const float h = std::max (1.0f, peaks[i] * half);
            g.fillRect (x, mid - h, std::max (1.0f, w.getWidth() / (float) peaks.size() - 0.5f), h * 2.0f);
        }
    }
    else
    {
        g.setColour (Colours::white.withAlpha (0.5f));
        g.drawText ("drop an audio file on the plugin to load your own sound", w, Justification::centred, false);
    }

    // edge handles
    for (auto x : { x0, x1 })
    {
        g.setColour (Colour (0xffb6e06a));
        g.drawLine (x, w.getY() - 4.0f, x, w.getBottom() + 4.0f, 2.0f);
        g.fillEllipse (x - 5.0f, w.getY() - 9.0f, 10.0f, 10.0f);
    }

    // tuning
    const float tune = proc.params.getRawParameterValue ("smpTune")->load();
    g.setColour (Colours::white.withAlpha (0.12f));
    g.fillEllipse (tuneDown());
    g.fillEllipse (tuneUp());
    g.setColour (Colours::white.withAlpha (0.85f));
    g.setFont (font (15.0f));
    g.drawText ("-", tuneDown(), Justification::centred, false);
    g.drawText ("+", tuneUp(), Justification::centred, false);
    auto label = Rectangle<float> (tuneDown().getRight(), tuneDown().getY() - 2.0f, tuneUp().getX() - tuneDown().getRight(), 30.0f);
    g.setFont (font (14.0f));
    g.drawText ((tune > 0.05f ? "+" : "") + String (roundToInt (tune)) + " st", label, Justification::centred, false);
    g.setFont (font (11.0f));
    g.setColour (Colours::white.withAlpha (0.5f));
    g.drawText ("tune", label.translated (0.0f, -22.0f), Justification::centred, false);
    g.drawFittedText (proc.getSampleInfo(), Rectangle<float> ((float) getWidth() - 126.0f, label.getBottom() + 2.0f, 116.0f, 26.0f).toNearestInt(),
                      Justification::centredTop, 2, 0.8f);
}

void SampleView::mouseDown (const MouseEvent& e)
{
    const float tune = proc.params.getRawParameterValue ("smpTune")->load();
    if (tuneDown().contains (e.position)) { setParam ("smpTune", jmax (-12.0f, (float) roundToInt (tune) - 1.0f)); repaint(); return; }
    if (tuneUp().contains (e.position))   { setParam ("smpTune", jmin (12.0f, (float) roundToInt (tune) + 1.0f)); repaint(); return; }

    auto w = waveArea();
    const float st = proc.params.getRawParameterValue ("smpStart")->load();
    const float en = proc.params.getRawParameterValue ("smpEnd")->load();
    const float x0 = w.getX() + st * w.getWidth(), x1 = w.getX() + en * w.getWidth();
    if (std::abs (e.position.x - x0) < 10.0f)      dragging = 0;
    else if (std::abs (e.position.x - x1) < 10.0f) dragging = 1;
    else if (e.position.x > x0 && e.position.x < x1) { dragging = 2; grabOffset = (e.position.x - w.getX()) / w.getWidth() - st; }
    else dragging = -1;
    for (auto id : { "smpStart", "smpEnd" })
        if (auto* p = proc.params.getParameter (id)) p->beginChangeGesture();
}

void SampleView::mouseDrag (const MouseEvent& e)
{
    auto w = waveArea();
    const float f = jlimit (0.0f, 1.0f, (e.position.x - w.getX()) / w.getWidth());
    const float st = proc.params.getRawParameterValue ("smpStart")->load();
    const float en = proc.params.getRawParameterValue ("smpEnd")->load();
    if (dragging == 0) setParam ("smpStart", jmin (f, en - 0.02f));
    else if (dragging == 1) setParam ("smpEnd", jmax (f, st + 0.02f));
    else if (dragging == 2)
    {
        const float len = en - st;
        const float ns = jlimit (0.0f, 1.0f - len, f - grabOffset);
        setParam ("smpStart", ns);
        setParam ("smpEnd", ns + len);
    }
    repaint();
}

void SampleView::mouseUp (const MouseEvent&)
{
    for (auto id : { "smpStart", "smpEnd" })
        if (auto* p = proc.params.getParameter (id)) p->endChangeGesture();
    dragging = -1;
}

//==============================================================================

void DragNotes::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    const bool any = proc.recordedNoteCount() > 0;
    const bool over = isMouseOver();
    g.setColour (Colours::white.withAlpha (any ? (over ? 0.2f : 0.12f) : 0.05f));
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (Colours::white.withAlpha (any ? 0.4f : 0.15f));
    g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
    // a little grip of dots
    g.setColour (Colours::white.withAlpha (any ? 0.85f : 0.35f));
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 2; ++j)
            g.fillEllipse (r.getX() + 14.0f + (float) j * 5.0f, r.getCentreY() - 6.0f + (float) i * 5.0f, 2.5f, 2.5f);
    g.setFont (font (13.0f));
    g.drawText (message.isNotEmpty() ? message : (any ? "drag notes into your song" : "notes appear here as it plays"),
                r.withTrimmedLeft (30.0f), Justification::centredLeft, false);
}

void DragNotes::mouseDrag (const MouseEvent& e)
{
    if (started || e.getDistanceFromDragStart() < 6) return;
    started = true;
    const auto file = proc.exportRecentNotes (8);
    if (! file.existsAsFile())
    {
        message = "press play first, then drag";
        repaint();
        return;
    }
    message = {};
    DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this);
}

//==============================================================================
// Song mode

namespace
{
    Colour sectionColour (const std::string& name)
    {
        if (name.rfind ("chorus", 0) == 0)     return Colour (0xffe8b04a);
        if (name.rfind ("pre-chorus", 0) == 0) return Colour (0xffc9d27a);
        if (name.rfind ("verse", 0) == 0)      return Colour (0xff9fcb55);
        if (name.rfind ("bridge", 0) == 0)     return Colour (0xffb7a3e0);
        return Colour (0xff8fb6cf); // intro, outro
    }

    String timeText (double seconds)
    {
        const int s = roundToInt (seconds);
        return String (s / 60) + ":" + String (s % 60).paddedLeft ('0', 2);
    }
}

SongPanel::SongPanel (LivePlanetProcessor& p, const lp::ui::Theme*& theme)
    : proc (p), th (theme),
      length ({ "full song", "sketch" }, [this] { return full ? 0 : 1; }, [this] (int i) { full = i == 0; }, theme)
{
    newKey.setToggleState (false, dontSendNotification); // stay in your song's key unless you ask
    addAndMakeVisible (newKey);
    addAndMakeVisible (length);
    style = std::make_unique<Stepper> (*p.params.getParameter ("songstyle"), theme);
    addAndMakeVisible (*style);
    addAndMakeVisible (useSamples);
    addAndMakeVisible (playWith);
    useSamplesAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (p.params, "usesamples", useSamples);
    playWithAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (p.params, "songplay", playWith);
    addAndMakeVisible (samplesOnly);
    samplesOnlyAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (p.params, "samplesonly", samplesOnly);
    addAndMakeVisible (dawTempo);
    dawTempo.setWantsKeyboardFocus (false);
    dawTempo.setTooltip ("write the song at your project's tempo so every loop lines up");
    dawTempoAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (p.params, "dawtempo", dawTempo);
    write.onClick = [this] { proc.songPrompt = promptBox.getText(); proc.makeSong (full, newKey.getToggleState()); repaint(); };
    addAndMakeVisible (write);

    promptBox.setTextToShowWhenEmpty ("describe it: dark trap 140 in f minor with piano + flute", lp::ui::theme (0).inkDim);
    promptBox.setFont (font (13.0f));
    promptBox.setIndents (10, 5);
    promptBox.setColour (TextEditor::backgroundColourId, Colours::transparentBlack);
    promptBox.setColour (TextEditor::outlineColourId, Colours::transparentBlack);
    promptBox.setColour (TextEditor::focusedOutlineColourId, Colours::transparentBlack);
    promptBox.setText (p.songPrompt, false);
    promptBox.onTextChange = [this] { proc.songPrompt = promptBox.getText(); repaint(); };
    promptBox.onReturnKey = [this] { promptBox.giveAwayKeyboardFocus(); if (write.isEnabled()) write.triggerClick(); };
    promptBox.onEscapeKey = [this] { promptBox.giveAwayKeyboardFocus(); };
    addAndMakeVisible (promptBox);
}

void SongPanel::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (47);
    promptBox.setBounds (r.removeFromTop (26));
    r.removeFromTop (5);
    auto row1 = r.removeFromTop (24);
    newKey.setBounds (row1.removeFromLeft (88));
    row1.removeFromLeft (4);
    dawTempo.setBounds (row1.removeFromLeft (96));
    row1.removeFromLeft (4);
    style->setBounds (row1);
    r.removeFromTop (4);
    auto row2 = r.removeFromTop (24);
    useSamples.setBounds (row2.removeFromLeft (124));
    row2.removeFromLeft (8);
    length.setBounds (row2);
    r.removeFromTop (4);
    auto row3 = r.removeFromTop (24);
    playWith.setBounds (row3.removeFromLeft (row3.getWidth() / 2 + 20));
    row3.removeFromLeft (8);
    samplesOnly.setBounds (row3);
    r.removeFromTop (5);
    write.setBounds (r.removeFromTop (27));
}

void SongPanel::paint (Graphics& g)
{
    const auto st = proc.songJob.getStatus();
    auto z = dropZone().toFloat().reduced (1.0f);
    Path outline;
    outline.addRoundedRectangle (z, 10.0f);
    Path dashed;
    const float dashes[] = { 5.0f, 4.0f };
    PathStrokeType (1.2f).createDashedStroke (dashed, outline, dashes, 2);
    g.setColour (th->ink.withAlpha (0.35f));
    g.fillPath (dashed);

    auto in = z.reduced (12.0f, 4.0f);

    // the description box: an outline, and what was understood
    {
        auto pb = promptBox.getBounds().toFloat();
        g.setColour (promptBox.hasKeyboardFocus (true) ? th->ink.withAlpha (0.6f) : th->ink.withAlpha (0.25f));
        g.drawRoundedRectangle (pb.reduced (0.5f), 8.0f, 1.0f);
    }
    const auto understood = lp::song::parsePrompt (promptBox.getText());
    if (st.stage == SongJob::Stage::Listening)
    {
        g.setFont (font (13.5f));
        g.setColour (th->ink);
        g.drawText (st.message, in, Justification::centredLeft, true);
    }
    else if (st.haveReference)
    {
        const auto& pr = st.profile;
        g.setFont (font (13.5f, lp::ui::Medium));
        g.setColour (th->ink);
        g.drawText ("like: " + st.referenceName.toLowerCase(), in.removeFromTop (20.0f).withTrimmedRight (24.0f), Justification::centredLeft, true);
        g.setFont (font (12.5f));
        g.setColour (th->inkDim);
        g.drawText (String (roundToInt (pr.bpm)) + " bpm  -  " + lp::noteName (pr.key) + (pr.minor ? " minor" : " major")
                        + "  -  " + String ((int) pr.sections.size()) + " sections  -  " + timeText (pr.lengthSeconds),
                    in.removeFromTop (18.0f), Justification::centredLeft, true);
        g.drawText ("x", z.removeFromRight (26.0f).removeFromTop (26.0f), Justification::centred, false);
    }
    else
    {
        g.setFont (font (13.5f, lp::ui::Medium));
        g.setColour (th->ink);
        g.drawText ("drop a song you love here, or describe one below", in.removeFromTop (19.0f), Justification::centredLeft, false);
        g.setFont (font (12.0f));
        g.setColour (understood.any ? th->grass.darker (0.3f) : th->inkDim);
        g.drawText (understood.any ? "making: " + understood.summary : String ("it learns the feel, never the melody."), in.removeFromTop (17.0f),
                    Justification::centredLeft, true);
    }

    // status under the button
    auto status = getLocalBounds().toFloat().withTrimmedTop ((float) write.getBottom() + 6.0f);
    const bool busy = st.stage == SongJob::Stage::Writing || st.stage == SongJob::Stage::Rendering;
    if (busy)
    {
        auto bar = status.removeFromTop (5.0f).withTrimmedTop (1.0f);
        g.setColour (th->ink.withAlpha (0.12f));
        g.fillRoundedRectangle (bar, 2.0f);
        g.setColour (th->grass);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * st.progress), 2.0f);
        status.removeFromTop (2.0f);
    }
    auto msgLine = status.removeFromTop (16.0f);
    g.setFont (font (12.0f));
    g.setColour (st.stage == SongJob::Stage::Failed ? Colour (0xffb5482b) : th->inkDim);
    String msg = st.message.toLowerCase();
    const bool important = st.message.contains ("set your project") || st.message.contains (". no ");
    if (st.stage == SongJob::Stage::Done && st.usedSamples.isNotEmpty() && ! important) msg = st.usedSamples.toLowerCase();
    g.drawText (msg, msgLine, Justification::centredLeft, true);

    // your sample library
    auto lib = status.removeFromTop (16.0f);
    const String count = st.scanning ? String ("looking through your samples...")
                       : st.librarySounds > 0 ? String (st.librarySounds) + " samples, " + String (st.libraryLoops) + " loops"
                                              : String ("no sample library found yet");
    g.setColour (th->inkDim);
    const float cw = GlyphArrangement::getStringWidth (font (12.0f), count);
    g.drawText (count, lib.removeFromLeft (cw + 10.0f), Justification::centredLeft, false);
    g.setColour (th->ink);
    g.setFont (font (12.0f, lp::ui::Medium));
    scanLink = lib.removeFromLeft (40.0f);
    g.drawText ("scan", scanLink, Justification::centredLeft, false);
    folderLink = lib.removeFromLeft (80.0f);
    g.drawText ("add folder", folderLink, Justification::centredLeft, false);
}

void SongPanel::refresh()
{
    const auto st = proc.songJob.getStatus();
    const bool busy = st.stage == SongJob::Stage::Writing || st.stage == SongJob::Stage::Rendering || st.stage == SongJob::Stage::Listening;
    if (write.isEnabled() == busy) write.setEnabled (! busy);
    const String text = st.haveSong ? "write another" : "write a song";
    if (write.getButtonText() != text) write.setButtonText (text);
    repaint();
}

void SongPanel::mouseUp (const MouseEvent& e)
{
    if (scanLink.contains (e.position)) { proc.rescanLibrary(); repaint(); return; }
    if (folderLink.contains (e.position))
    {
        chooser = std::make_unique<FileChooser> ("Add a sample folder (Splice, packs, your own sounds)", File());
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories,
                              [this] (const FileChooser& fc) { if (fc.getResult().isDirectory()) proc.addLibraryFolder (fc.getResult()); });
        return;
    }
    if (! dropZone().contains (e.getPosition())) return;
    const auto st = proc.songJob.getStatus();
    if (st.haveReference && e.position.x > (float) getWidth() - 30.0f && e.position.y < 30.0f)
    {
        proc.songJob.clearReference();
        repaint();
        return;
    }
    chooser = std::make_unique<FileChooser> ("Choose a song for Live Planet to learn the feel of", File(),
                                             "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                          [this] (const FileChooser& fc)
                          {
                              if (fc.getResult().existsAsFile()) proc.songJob.setReference (fc.getResult());
                          });
}

void SongView::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (Colour (0xf0060609));
    g.fillRect (b);
    g.setColour (Colours::white.withAlpha (0.1f));
    g.drawLine (b.getX(), b.getY(), b.getRight(), b.getY(), 1.0f);
    const auto st = proc.songJob.getStatus();
    auto r = b.reduced (16.0f, 10.0f);
    auto head = r.removeFromTop (20.0f);

    struct Block { std::string name; int start, bars; float energy; };
    std::vector<Block> blocks;
    int totalBars = 0;
    String title;
    if (st.haveSong)
    {
        for (auto& s : st.song.sections) blocks.push_back ({ s.name, s.startBar, s.bars, s.energy });
        totalBars = st.song.totalBars();
        title = st.song.title + "  -  " + String (roundToInt (st.song.bpm)) + " bpm  -  " + lp::noteName (st.song.key)
              + (st.song.minor ? " minor" : " major") + "  -  " + timeText (st.song.lengthSeconds());
    }
    else if (st.haveReference)
    {
        float maxE = 0.01f;
        for (auto& s : st.profile.sections) maxE = std::max (maxE, s.energy);
        for (auto& s : st.profile.sections) blocks.push_back ({ s.label, s.startBar, s.bars, s.energy / maxE });
        for (auto& bl : blocks) totalBars = std::max (totalBars, bl.start + bl.bars);
        title = "the shape of \"" + st.referenceName.toLowerCase() + "\"";
    }

    g.setFont (font (13.0f));
    g.setColour (Colours::white.withAlpha (0.75f));
    const bool busy = st.stage == SongJob::Stage::Writing || st.stage == SongJob::Stage::Rendering || st.stage == SongJob::Stage::Listening;
    g.drawText (busy ? st.message.toLowerCase() : title, head, Justification::centredLeft, true);

    if (blocks.empty() || totalBars == 0)
    {
        g.setColour (Colours::white.withAlpha (0.45f));
        g.drawText ("drop a song you love on the right, or press \"write a song\" and the weather leads", r, Justification::centred, true);
    }
    else
    {
        auto lane = r.withTrimmedTop (6.0f);
        for (auto& bl : blocks)
        {
            const float x0 = lane.getX() + lane.getWidth() * (float) bl.start / (float) totalBars;
            const float x1 = lane.getX() + lane.getWidth() * (float) (bl.start + bl.bars) / (float) totalBars;
            const float h = lane.getHeight() * (0.35f + 0.65f * jlimit (0.0f, 1.0f, bl.energy));
            auto rect = Rectangle<float> (x0 + 1.0f, lane.getBottom() - h, x1 - x0 - 2.0f, h);
            const auto c = sectionColour (bl.name);
            ColourGradient grad (c.withAlpha (0.85f), 0, rect.getY(), c.withAlpha (0.35f), 0, rect.getBottom(), false);
            g.setGradientFill (grad);
            g.fillRoundedRectangle (rect, 4.0f);
            if (rect.getWidth() > 34.0f)
            {
                g.setFont (font (11.0f));
                g.setColour (Colour (0xff111111).withAlpha (0.85f));
                g.drawFittedText (String (bl.name), rect.reduced (4.0f, 3.0f).toNearestInt(), Justification::topLeft, 2, 0.7f);
            }
        }
    }
    if (st.haveSong && proc.studio.length() > 0 && proc.songIsPlaying())
    {
        const float frac = (float) jlimit (0.0, 1.0, proc.playheadPos.load() / (double) proc.studio.length());
        auto lane = r.withTrimmedTop (6.0f);
        const float x = lane.getX() + lane.getWidth() * frac;
        g.setColour (Colours::white);
        g.drawLine (x, lane.getY() - 4.0f, x, lane.getBottom(), 1.5f);
    }
    if (busy)
    {
        auto bar = Rectangle<float> (b.getX() + 16.0f, b.getBottom() - 6.0f, b.getWidth() - 32.0f, 3.0f);
        g.setColour (Colours::white.withAlpha (0.12f));
        g.fillRoundedRectangle (bar, 1.5f);
        g.setColour (Colour (0xff9fcb55));
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * st.progress), 1.5f);
    }
}

void FilePill::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    const bool ready = ! getFiles().isEmpty();
    const bool over = isMouseOver() && ready;
    g.setColour (Colours::white.withAlpha (ready ? (over ? 0.2f : 0.12f) : 0.04f));
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (Colours::white.withAlpha (ready ? 0.4f : 0.12f));
    g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
    g.setColour (Colours::white.withAlpha (ready ? 0.9f : 0.3f));
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 2; ++j)
            g.fillEllipse (r.getX() + 12.0f + (float) j * 5.0f, r.getCentreY() - 6.0f + (float) i * 5.0f, 2.5f, 2.5f);
    g.setFont (font (12.5f));
    g.drawText (label, r.withTrimmedLeft (26.0f), Justification::centredLeft, false);
}

void FilePill::mouseDrag (const MouseEvent& e)
{
    if (dragged || e.getDistanceFromDragStart() < 6) return;
    const auto files = getFiles();
    if (files.isEmpty()) return;
    dragged = true;
    DragAndDropContainer::performExternalDragDropOfFiles (files, false, this);
}

void FilePill::mouseUp (const MouseEvent&)
{
    if (! dragged)
    {
        const auto files = getFiles();
        if (! files.isEmpty()) File (files[0]).revealToUser(); // a click shows it in Finder
    }
    dragged = false;
}

void ThemeDots::paint (Graphics& g)
{
    const int cur = proc.themeIndex.load();
    const auto& th = lp::ui::theme (cur);
    for (int i = 0; i < lp::ui::numThemes; ++i)
    {
        const auto& t = lp::ui::theme (i);
        auto r = Rectangle<float> ((float) i * 22.0f + 4.0f, 4.0f, 14.0f, 14.0f);
        g.setColour (Colours::black.withAlpha (0.25f));
        g.fillEllipse (r.translated (0, 1.5f));
        ColourGradient gc (t.grassLight, r.getX(), r.getY(), t.grassDark, r.getRight(), r.getBottom(), false);
        g.setGradientFill (gc);
        g.fillEllipse (r);
        g.setColour (th.ink.withAlpha (i == cur ? 0.8f : 0.25f));
        g.drawEllipse (r.expanded (i == cur ? 2.5f : 0.0f), 1.0f);
    }
}

void ThemeDots::mouseUp (const MouseEvent& e)
{
    const int i = jlimit (0, lp::ui::numThemes - 1, (int) (e.position.x / 22.0f));
    proc.themeIndex = i;
    if (onChange) onChange();
}

//==============================================================================
// Editor

LivePlanetEditor::LivePlanetEditor (LivePlanetProcessor& p)
    : AudioProcessorEditor (p), proc (p),
      liveSwitch ({ "live", "manual" },
                  [&p] { return p.params.getRawParameterValue ("live")->load() > 0.5f ? 0 : 1; },
                  [&p] (int i) { if (auto* q = p.params.getParameter ("live")) { q->beginChangeGesture(); q->setValueNotifyingHost (i == 0 ? 1.0f : 0.0f); q->endChangeGesture(); } },
                  lnf.th),
      pageSwitch ({ "sound", "sky", "song" }, [this] { return page; }, [this] (int i) { setPage (i); }, lnf.th),
      keySwitch ({ "play", "chord" },
                 [&p] { return p.params.getRawParameterValue ("keymode")->load() > 0.5f ? 1 : 0; },
                 [&p] (int i) { if (auto* q = p.params.getParameter ("keymode")) { q->beginChangeGesture(); q->setValueNotifyingHost ((float) i); q->endChangeGesture(); } },
                 lnf.th),
      keys (p, lnf.th),
      playButton (p),
      presetBrowser (p, lnf.th),
      sampleView (p),
      dragNotes (p),
      songPanel (p, lnf.th),
      songView (p),
      studioView (p),
      pillMidi ("drag midi", [&p] { auto st = p.songJob.getStatus(); return st.haveSong ? StringArray (st.files.midiFile.getFullPathName()) : StringArray(); }),
      pillStems ("drag stems", [&p] { auto st = p.songJob.getStatus(); StringArray a; if (st.haveSong) for (auto& t : st.tracks) a.add (t.stem.getFullPathName()); return a; }),
      pillMix ("drag mix", [&p] { auto st = p.songJob.getStatus(); return st.haveSong ? StringArray (st.files.mixFile.getFullPathName()) : StringArray(); }),
      pillHelper ("locators", [&p] { auto st = p.songJob.getStatus(); return st.abletonSet.existsAsFile() ? StringArray (st.abletonSet.getFullPathName()) : StringArray(); }),
      pillFolder ("folder", [&p] { auto st = p.songJob.getStatus(); return st.haveSong ? StringArray (st.files.folder.getFullPathName()) : StringArray(); }),
      themeDots (p, [this] { applyTheme(); })
{
    setLookAndFeel (&lnf);
    setOpaque (true);
    startMs = Time::getMillisecondCounterHiRes();

    cityBox.setTextToShowWhenEmpty ("search any city...", lp::ui::theme (0).inkDim);
    cityBox.setFont (font (16.0f));
    cityBox.setIndents (24, 6);
    cityBox.setColour (TextEditor::backgroundColourId, Colours::transparentBlack);
    cityBox.setColour (TextEditor::outlineColourId, Colours::transparentBlack);
    cityBox.setColour (TextEditor::focusedOutlineColourId, Colours::transparentBlack);
    cityBox.onReturnKey = [this] { runSearch(); };
    cityBox.onEscapeKey = [this] { cityBox.setText ({}, false); cityBox.giveAwayKeyboardFocus(); };
    addAndMakeVisible (cityBox);

    addAndMakeVisible (liveSwitch);
    addAndMakeVisible (pageSwitch);
    addAndMakeVisible (keySwitch);
    addAndMakeVisible (keys);
    addAndMakeVisible (playButton);
    addAndMakeVisible (presetBrowser);
    addChildComponent (sampleView);
    addAndMakeVisible (dragNotes);
    addChildComponent (songPanel);
    addChildComponent (songView);
    addChildComponent (studioView);
    playButton.songMode = [this] { return page == 2; };
    for (auto* pill : { &pillMidi, &pillStems, &pillMix, &pillHelper, &pillFolder })
        addChildComponent (pill);
    addAndMakeVisible (themeDots);

    keyStepper = std::make_unique<Stepper> (*p.params.getParameter ("key"), lnf.th);
    scaleStepper = std::make_unique<Stepper> (*p.params.getParameter ("scale"), lnf.th);
    addAndMakeVisible (*keyStepper);
    addAndMakeVisible (*scaleStepper);

    loadButton.onClick = [this]
    {
        chooser = std::make_unique<FileChooser> ("Choose a sound for Live Planet to play", File(),
                                                 "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                              [this] (const FileChooser& fc)
                              {
                                  if (fc.getResult().existsAsFile()) proc.loadSample (fc.getResult());
                              });
    };
    addAndMakeVisible (loadButton);

    // knobs along the bottom, each ring its own colour
    addKnob ("pad", "chords", 0.86f, 64);
    addKnob ("drops", "melody", 0.78f, 64);
    addKnob ("bass", "bass", 0.68f, 64);
    addKnob ("grain", "grain", 0.08f, 124);
    addKnob ("texture", "scatter", 0.12f, 54);
    addKnob ("sparkle", "sparkle", 0.16f, 54);
    addKnob ("position", "position", 0.2f, 64);
    addKnob ("warmth", "warmth", 0.03f, 56);
    addKnob ("echo", "echo", 0.45f, 56);
    addKnob ("space", "space", 0.52f, 56);
    addKnob ("shimmer", "shimmer", 0.58f, 56);
    addKnob ("atmos", "atmos", 0.36f, 56);
    addKnob ("gain", "output", 0.0f, 56);
    addKnob ("nostalgia", "nostalgia", 0.1f, 56);
    knobById["gain"]->slider.getProperties().set ("hue", 0.95);

    // manual sky
    const char* skyIds[] = { "temp", "rain", "snow", "wind", "clouds", "pressure", "humidity" };
    const char* skyNames[] = { "temp", "rain", "snow", "wind", "clouds", "pressure", "humidity" };
    for (int i = 0; i < 7; ++i)
        addKnob (skyIds[i], skyNames[i], 0.5f + 0.03f * (float) i, 34);
    dayToggle.setName ("daylight");
    thunderToggle.setName ("thunder");
    addChildComponent (dayToggle);
    addChildComponent (thunderToggle);
    dayAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (p.params, "daylight", dayToggle);
    thunderAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (p.params, "thunder", thunderToggle);

    setSize (kW, kH);
    applyTheme();
    setPage (0);

    // only the city search box ever takes the keyboard, so your DAW's keys keep playing notes
    setWantsKeyboardFocus (false);
    for (auto* c : getChildren())
        if (c != &cityBox)
        {
            c->setWantsKeyboardFocus (false);
            c->setMouseClickGrabsKeyboardFocus (false);
        }
    addMouseListener (this, true);

    gl.setRenderer (this);
    gl.setComponentPaintingEnabled (true);
    gl.setContinuousRepainting (true);
    gl.attachTo (*this);

    timerCallback();
    startTimerHz (30);
}

LivePlanetEditor::~LivePlanetEditor()
{
    stopTimer();
    gl.detach();
    setLookAndFeel (nullptr);
}

LivePlanetEditor::Knob& LivePlanetEditor::addKnob (const char* id, const String& name, float hue, int size)
{
    auto k = std::make_unique<Knob>();
    k->slider.setName (name);
    k->slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    k->slider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    k->slider.setRotaryParameters (MathConstants<float>::pi * 1.2f, MathConstants<float>::pi * 2.8f, true);
    k->slider.getProperties().set ("hue", hue);
    k->slider.getProperties().set ("size", size);
    k->slider.setMouseDragSensitivity (240);
    k->attachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.params, id, k->slider);
    addAndMakeVisible (k->slider);
    auto* raw = k.get();
    knobById[id] = raw;
    knobs.push_back (std::move (k));
    return *raw;
}

void LivePlanetEditor::applyTheme()
{
    lnf.th = &lp::ui::theme (proc.themeIndex.load());
    cityBox.setColour (TextEditor::textColourId, lnf.th->ink);
    cityBox.setColour (CaretComponent::caretColourId, lnf.th->ink);
    cityBox.setTextToShowWhenEmpty ("search any city...", lnf.th->inkDim);
    cityBox.applyColourToAllText (lnf.th->ink);
    songPanel.promptBox.setColour (TextEditor::textColourId, lnf.th->ink);
    songPanel.promptBox.setColour (CaretComponent::caretColourId, lnf.th->ink);
    songPanel.promptBox.setTextToShowWhenEmpty ("describe it: dark trap 140 in f minor with piano + flute", lnf.th->inkDim);
    songPanel.promptBox.applyColourToAllText (lnf.th->ink);
    for (auto* c : getChildren()) c->repaint();
    repaint();
}

void LivePlanetEditor::setPage (int p)
{
    page = p;
    keys.setVisible (p == 0);
    loadButton.setVisible (p == 0);
    for (auto id : { "temp", "rain", "snow", "wind", "clouds", "pressure", "humidity" })
        knobById[id]->slider.setVisible (p == 1);
    dayToggle.setVisible (p == 1);
    thunderToggle.setVisible (p == 1);
    // song mode: its own panel, and the screen shows the song instead of the step lane
    songPanel.setVisible (p == 2);
    keyStepper->setVisible (p != 2);
    scaleStepper->setVisible (p != 2);
    keySwitch.setVisible (p != 2);
    songView.setVisible (p == 2);
    studioView.setVisible (p == 2 && proc.songJob.getStatus().haveSong);
    for (auto* pill : { &pillMidi, &pillStems, &pillMix, &pillHelper, &pillFolder })
        pill->setVisible (p == 2);
    dragNotes.setVisible (p != 2);
    pageSwitch.repaint();
    repaint();
}

void LivePlanetEditor::runSearch()
{
    const auto text = cityBox.getText().trim();
    if (text.isEmpty()) return;
    proc.searchCity (text);
    if (auto* live = proc.params.getParameter ("live"))
        live->setValueNotifyingHost (1.0f);
    cityBox.setText ({}, false);
    cityBox.giveAwayKeyboardFocus();
}

//==============================================================================

void LivePlanetEditor::timerCallback()
{
    const double now = Time::getMillisecondCounterHiRes();
    const double t = (now - startMs) * 0.001;
    const float dt = 1.0f / 30.0f;
    const int step = proc.engine.uiStep.load();

    lp::VisualEvent e;
    while (proc.engine.popVisual (e))
    {
        if (e.layer == 3) { flash = 1.0f; continue; }
        const float hue = (float) (((e.note % 12) * 7) % 12) / 12.0f;
        fx.push_back ({ hue, e.layer, t, Random::getSystemRandom().nextFloat(), e.layer == 0 ? 7.0 : (e.layer == 1 ? 2.4 : 3.0) });
        lane.push_back ({ step, e.note, e.layer, t, hue });
    }
    fx.erase (std::remove_if (fx.begin(), fx.end(), [t] (const NoteFx& f) { return t - f.born > f.life; }), fx.end());
    while (fx.size() > 24) fx.erase (fx.begin());
    // keep the orbit tidy: only the most recent few chord rings
    for (int rings = 0, i = (int) fx.size() - 1; i >= 0; --i)
        if (fx[(size_t) i].kind == 0 && ++rings > 5)
            fx.erase (fx.begin() + i);
    const double barSecs = 4.0 * 60.0 / std::max (40.0f, proc.engine.uiBpm.load());
    lane.erase (std::remove_if (lane.begin(), lane.end(), [t, barSecs] (const LaneDot& d) { return t - d.born > barSecs * 1.5; }), lane.end());
    if (step != lastUiStep) { lastUiStep = step; stepClock = t; }
    flash = std::max (0.0f, flash - dt * 2.2f);

    const auto status = proc.getSkyStatus();
    const lp::Weather w = proc.engine.getCurrentWeather();
    const lp::Weather real = proc.engine.getTargetWeather();
    dayAmt += ((w.isDay ? 1.0f : 0.0f) - dayAmt) * dt * 0.8f;

    const float tLat = (float) degreesToRadians (status.lat), tLon = (float) degreesToRadians (status.lon);
    if (! haveCity) { smoothLat = tLat; smoothLon = tLon; haveCity = true; }
    smoothLat += (tLat - smoothLat) * dt * 1.4f;
    smoothLon += std::remainder (tLon - smoothLon, MathConstants<float>::twoPi) * dt * 1.4f;

    // colours from the weather
    const float warm = w.warmthNorm(), cloud = w.cloudNorm();
    struct Pal { RGB a, b, c; };
    const Pal sunny { { 1.0f, 0.52f, 0.25f }, { 0.98f, 0.3f, 0.55f }, { 1.0f, 0.82f, 0.38f } };
    const Pal clear { { 0.3f, 0.6f, 1.0f }, { 0.5f, 0.36f, 1.0f }, { 1.0f, 0.62f, 0.3f } };
    const Pal rainy { { 0.16f, 0.8f, 0.95f }, { 0.22f, 0.32f, 0.98f }, { 1.0f, 0.6f, 0.32f } };
    const Pal snowy { { 0.66f, 0.74f, 1.0f }, { 0.62f, 0.42f, 1.0f }, { 0.85f, 1.0f, 1.0f } };
    const Pal storm { { 0.62f, 0.3f, 1.0f }, { 0.95f, 0.22f, 0.62f }, { 1.0f, 0.55f, 0.3f } };
    float ws[5] = {
        warm * warm * dayAmt * (1.0f - cloud) * 1.8f + 0.03f,
        0.35f + (1.0f - warm) * 0.2f,
        w.rainNorm() * 1.6f,
        w.snowNorm() * 1.8f,
        (w.thunder ? 1.6f : 0.0f) + std::max (0.0f, w.tension() - 0.6f) * 2.0f
    };
    float sum = 0; for (auto v : ws) sum += v;
    const Pal* pals[5] = { &sunny, &clear, &rainy, &snowy, &storm };
    RGB a { 0, 0, 0 }, b { 0, 0, 0 }, c { 0, 0, 0 };
    for (int i = 0; i < 5; ++i)
    {
        const float k = ws[i] / sum;
        a = { a.r + pals[i]->a.r * k, a.g + pals[i]->a.g * k, a.b + pals[i]->a.b * k };
        b = { b.r + pals[i]->b.r * k, b.g + pals[i]->b.g * k, b.b + pals[i]->b.b * k };
        c = { c.r + pals[i]->c.r * k, c.g + pals[i]->c.g * k, c.b + pals[i]->c.b * k };
    }
    const float night = (1.0f - dayAmt) * 0.35f;
    a = lerp (a, { 0.36f, 0.42f, 1.0f }, night);
    b = lerp (b, { 0.58f, 0.3f, 0.98f }, night);

    {
        const SpinLock::ScopedLockType sl (sceneLock);
        scene.w1[0] = warm; scene.w1[1] = w.rainNorm(); scene.w1[2] = w.snowNorm(); scene.w1[3] = w.windNorm();
        scene.w2[0] = cloud; scene.w2[1] = dayAmt; scene.w2[2] = flash; scene.w2[3] = w.tension();
        scene.w3[0] = w.fog ? 1.0f : 0.0f;
        scene.w3[1] = jlimit (0.0f, 1.0f, (0.45f - warm) * 2.5f) * (0.35f + 0.65f * (1.0f - dayAmt));
        scene.w3[2] = degreesToRadians (w.windDirDeg);
        scene.w3[3] = smoothLat;
        scene.cityLon = smoothLon;
        scene.palA[0] = a.r; scene.palA[1] = a.g; scene.palA[2] = a.b;
        scene.palB[0] = b.r; scene.palB[1] = b.g; scene.palB[2] = b.b;
        scene.palC[0] = c.r; scene.palC[1] = c.g; scene.palC[2] = c.b;
        scene.noteCount = (int) fx.size();
        for (size_t i = 0; i < fx.size(); ++i)
        {
            scene.notes[i * 4 + 0] = fx[i].hue;
            scene.notes[i * 4 + 1] = (float) fx[i].kind;
            scene.notes[i * 4 + 2] = (float) jlimit (0.0, 1.0, (t - fx[i].born) / fx[i].life);
            scene.notes[i * 4 + 3] = fx[i].seed;
        }
    }

    // words
    const bool usingLive = status.live && status.haveLive;
    cityText = status.live ? status.city : String ("your own sky");
    const String deg (CharPointer_UTF8 ("\xc2\xb0"));
    headerText = (status.live && status.country.isNotEmpty() ? status.country.toLowerCase() + "  \xc2\xb7  " : String())
               + real.describe().toLowerCase() + "  " + String (roundToInt (real.temperatureC)) + deg + "c  \xc2\xb7  wind "
               + String (roundToInt (real.windKmh)) + " km/h  \xc2\xb7  "
               + (real.snowCm > 0.05f ? "snow " + String (real.snowCm, 1) + " cm" : "rain " + String (real.rainMm, 1) + " mm");
    headerText = String (CharPointer_UTF8 (headerText.toRawUTF8()));
    if (usingLive)
    {
        const auto mins = (int) (Time::getCurrentTime() - status.fetchedAt).inMinutes();
        statusText = mins < 1 ? String ("live") : String (CharPointer_UTF8 ("live \xc2\xb7 ")) + String (mins) + " min ago";
    }
    else
        statusText = status.live ? "connecting" : "manual";
    if (status.busy) statusText = "checking the sky...";
    if (status.message.isNotEmpty()) statusText = status.message.toLowerCase();

    const int q = proc.engine.uiChordMinor.load();
    const String chord = q < 0 ? String ("your chord")
                               : String (lp::noteName (proc.engine.uiChordRoot.load())) + (q == 1 ? "m" : (q == 2 ? "sus2" : ""));
    nowText = String (lp::profileFor ((lp::SoundId) proc.engine.uiSound.load()).name).toLowerCase() + "  \xc2\xb7  "
            + String (lp::noteName (proc.engine.uiKey.load())) + " " + String (lp::getScale ((lp::ScaleId) proc.engine.uiScale.load()).name).toLowerCase()
            + "  \xc2\xb7  " + chord + "  \xc2\xb7  " + String (roundToInt (proc.engine.uiBpm.load())) + " bpm";
    nowText = String (CharPointer_UTF8 (nowText.toRawUTF8()));

    const bool manual = ! status.live;
    for (auto id : { "temp", "rain", "snow", "wind", "clouds", "pressure", "humidity" })
        knobById[id]->slider.setEnabled (manual);
    dayToggle.setEnabled (manual);
    thunderToggle.setEnabled (manual);

    // the screen is redrawn every frame anyway; refresh the text over it and the moving parts
    repaint (screen);
    repaint (infoCard);
    keys.repaint();
    liveSwitch.repaint();
    playButton.repaint();
    presetBrowser.repaint();
    dragNotes.repaint();
    // the sample editor replaces the step lane while your own sample is playing
    const bool sampleMode = page != 2 && (int) proc.params.getRawParameterValue ("sound")->load() == LivePlanetProcessor::kSampleChoice;
    if (page == 2)
    {
        const bool haveSong = proc.songJob.getStatus().haveSong;
        if (studioView.isVisible() != haveSong) studioView.setVisible (haveSong);
        if (haveSong) studioView.repaint();
        playButton.repaint();
        songPanel.refresh();
        songView.repaint();
        for (auto* pill : { &pillMidi, &pillStems, &pillMix, &pillHelper, &pillFolder })
            pill->repaint();
    }
    if (sampleView.isVisible() != sampleMode) sampleView.setVisible (sampleMode);
    if (sampleMode) sampleView.repaint();
}

//==============================================================================

void LivePlanetEditor::paint (Graphics& g)
{
    const auto& th = *lnf.th;
    auto bounds = getLocalBounds().toFloat();

    {
        // a clean, soft-lit surface everywhere except the screen window
        Path surface;
        surface.addRectangle (bounds);
        surface.addRoundedRectangle (screen.toFloat(), 18.0f);
        surface.setUsingNonZeroWinding (false);
        ColourGradient bg (th.skyTop, 0, 0, th.skyBottom, 0, bounds.getBottom(), false);
        g.setGradientFill (bg);
        g.fillPath (surface);
        ColourGradient sheen (Colours::white.withAlpha (0.6f), bounds.getWidth() * 0.25f, 0.0f,
                              Colours::white.withAlpha (0.0f), bounds.getWidth() * 0.25f + 700.0f, 0.0f, true);
        g.setGradientFill (sheen);
        g.fillPath (surface);
    }

    if (shaderFailed)
    {
        g.setColour (Colour (0xff050608));
        g.fillRoundedRectangle (screen.toFloat(), 18.0f);
    }

    // raised cards: light top edge, soft shadow below, nothing else
    auto card = [&g, &th] (Rectangle<int> rect)
    {
        auto r = rect.toFloat();
        for (int i = 4; i >= 1; --i)
        {
            g.setColour (Colours::black.withAlpha (0.035f));
            g.fillRoundedRectangle (r.translated (0.0f, 1.0f + (float) i * 1.5f).expanded ((float) i * 0.8f), 18.0f + (float) i);
        }
        ColourGradient body (th.bodyLight, 0, r.getY(), th.body, 0, r.getBottom(), false);
        g.setGradientFill (body);
        g.fillRoundedRectangle (r, 18.0f);
        g.setColour (Colours::white);
        g.drawLine (r.getX() + 18.0f, r.getY() + 0.75f, r.getRight() - 18.0f, r.getY() + 0.75f, 1.2f);
        g.setColour (Colours::black.withAlpha (0.06f));
        g.drawRoundedRectangle (r.reduced (0.5f), 18.0f, 1.0f);
    };
    card (rightPlate);
    card (plateMix);
    card (plateTex);
    card (plateFx);
    card (infoCard);

    // a slim dark bezel around the planet's screen
    {
        Path ring;
        ring.addRoundedRectangle (bezel.toFloat(), 24.0f);
        ring.addRoundedRectangle (screen.toFloat(), 18.0f);
        ring.setUsingNonZeroWinding (false);
        for (int i = 4; i >= 1; --i)
        {
            g.setColour (Colours::black.withAlpha (0.05f));
            g.fillRoundedRectangle (bezel.toFloat().translated (0, 2.0f + (float) i * 2.0f).expanded ((float) i), 25.0f + (float) i);
        }
        ColourGradient bz (Colour (0xff2b333a), 0, (float) bezel.getY(), Colour (0xff10151a), 0, (float) bezel.getBottom(), false);
        g.setGradientFill (bz);
        g.fillPath (ring);
        g.setColour (Colours::white.withAlpha (0.18f));
        g.drawRoundedRectangle (bezel.toFloat().reduced (0.75f), 24.0f, 1.0f);
    }

    paintScreen (g);

    // info card: the name, presets, the city
    {
        auto r = infoCard.toFloat();
        auto in = r.reduced (18.0f, 14.0f);
        auto head = in.removeFromTop (24.0f);
        {
            const auto bold = font (21.0f, lp::ui::Bold, -0.01f), light = font (21.0f, lp::ui::Light, -0.01f);
            const float w1 = GlyphArrangement::getStringWidth (bold, "live");
            g.setColour (th.ink);
            g.setFont (bold);
            g.drawText ("live", head, Justification::centredLeft, false);
            g.setColour (th.inkDim.withAlpha (0.6f));
            g.drawLine (head.getX() + w1 + 7.0f, head.getY() + 3.0f, head.getX() + w1 + 7.0f, head.getBottom() - 3.0f, 1.2f);
            g.setColour (th.ink);
            g.setFont (light);
            g.drawText ("planet", head.withTrimmedLeft (w1 + 14.0f), Justification::centredLeft, false);
        }
        g.setFont (font (13.5f));
        g.setColour (th.inkDim);
        in.removeFromTop (22.0f); // the preset browser sits here

        const bool live = proc.params.getRawParameterValue ("live")->load() > 0.5f;
        const float pulse = 0.6f + 0.4f * (float) std::sin (Time::getMillisecondCounterHiRes() * 0.004);
        led (g, { r.getRight() - 22.0f, r.getY() + 25.0f }, live ? Colour (0xff3fd98a) : Colour (0xffffa23a), 7.0f, live ? pulse : 0.7f);
        g.setFont (font (12.5f));
        g.setColour (th.inkDim);
        g.drawText (statusText, Rectangle<float> (r.getRight() - 240.0f, r.getY() + 16.0f, 204.0f, 18.0f), Justification::centredRight, true);

        auto sr = cityBox.getBounds().toFloat();
        g.setColour (cityBox.hasKeyboardFocus (true) ? th.ink.withAlpha (0.7f) : th.ink.withAlpha (0.28f));
        g.drawLine (sr.getX(), sr.getBottom(), sr.getRight(), sr.getBottom(), 1.0f);
        const float ix = sr.getX() + 7.0f, iy = sr.getCentreY();
        g.setColour (th.inkDim);
        g.drawEllipse (ix - 5.0f, iy - 5.0f, 10.0f, 10.0f, 1.3f);
        g.drawLine (ix + 3.5f, iy + 3.5f, ix + 7.0f, iy + 7.0f, 1.3f);
    }

    // instrument / sky well inside the right slab
    {
        auto r = (page == 2 ? pageCard.withBottom (474) : pageCard).toFloat();
        recess (g, r, 14.0f, th);
        if (page == 2) {}
        else if (page == 0)
        {
            g.setFont (font (12.5f));
            g.setColour (th.inkDim);
            const auto smp = proc.getSampleName();
            g.drawText (smp.isEmpty() ? "or drop a sound here" : smp.toLowerCase() + String (CharPointer_UTF8 ("  \xc2\xb7  ")) + proc.getSampleInfo(),
                        loadButton.getBounds().translated (loadButton.getWidth() + 10, 0).withWidth (160), Justification::centredLeft, true);
        }
        else if (proc.params.getRawParameterValue ("live")->load() > 0.5f)
        {
            g.setFont (font (12.5f));
            g.setColour (th.inkDim);
            g.drawText ("switch to manual to make your own weather", r.reduced (14.0f, 6.0f).removeFromBottom (16.0f), Justification::centred, false);
        }
    }

    // small engraved titles on the slabs
    auto title = [&g, &th] (const String& text, Rectangle<int> plate)
    {
        g.setFont (font (12.0f, lp::ui::Medium, 0.12f));
        g.setColour (th.inkDim);
        g.drawText (text, plate.getX() + 18, plate.getY() + 12, 200, 16, Justification::centredLeft, false);
    };
    title ("mix", plateMix);
    title ("texture", plateTex);
    title ("space", plateFx);

    g.setFont (font (12.5f));
    g.setColour (th.inkDim);
    if (page != 2)
    g.drawText ("key", keyStepper->getBounds().translated (0, -19).withHeight (16), Justification::centredLeft, false);
    if (page != 2)
    g.drawText ("scale", scaleStepper->getBounds().translated (0, -19).withHeight (16), Justification::centredLeft, false);
    if (page != 2)
    g.drawText ("keyboard", keySwitch.getBounds().translated (0, -19).withHeight (16), Justification::centredLeft, false);
    g.drawText (String ("colour  ") + th.name, themeDots.getBounds().translated (themeDots.getWidth() + 6, 0).withWidth (140),
                Justification::centredLeft, false);
}

void LivePlanetEditor::paintScreen (Graphics& g)
{
    auto r = screen.toFloat();
    Graphics::ScopedSaveState ss (g);
    Path clip;
    clip.addRoundedRectangle (r, 22.0f);
    g.reduceClipRegion (clip);

    const Colour textCol (0xffe9e6f2);
    auto top = r.reduced (24.0f, 16.0f);
    g.setFont (font (28.0f));
    g.setColour (textCol);
    g.drawText (cityText.toLowerCase(), top.removeFromTop (36.0f), Justification::topLeft, false);
    g.setFont (font (13.5f));
    g.setColour (textCol.withAlpha (0.62f));
    g.drawText (headerText, top.removeFromTop (20.0f), Justification::topLeft, false);

    // the step lane along the bottom of the screen
    auto laneR = r.removeFromBottom (112.0f);
    g.setColour (Colours::black.withAlpha (0.45f));
    g.fillRect (laneR);
    g.setColour (Colours::white.withAlpha (0.1f));
    g.drawLine (laneR.getX(), laneR.getY(), laneR.getRight(), laneR.getY(), 1.0f);
    const float colW = laneR.getWidth() / 16.0f;
    const int step = proc.engine.uiStep.load();
    for (int i = 0; i < 16; ++i)
    {
        const float x = laneR.getX() + (float) i * colW;
        if (i > 0)
        {
            g.setColour (Colours::white.withAlpha (i % 4 == 0 ? 0.14f : 0.06f));
            g.drawLine (x, laneR.getY(), x, laneR.getBottom(), 1.0f);
        }
        g.setFont (font (11.0f));
        g.setColour (Colours::white.withAlpha (0.4f));
        g.drawText (String (i + 1), Rectangle<float> (x + 5.0f, laneR.getY() + 3.0f, 20.0f, 14.0f), Justification::topLeft, false);
        if (i == step)
        {
            g.setColour (Colours::white.withAlpha (0.06f));
            g.fillRect (x, laneR.getY(), colW, laneR.getHeight());
        }
    }
    const double t = (Time::getMillisecondCounterHiRes() - startMs) * 0.001;
    const double barSecs = 4.0 * 60.0 / std::max (40.0f, proc.engine.uiBpm.load());
    for (auto& d : lane)
    {
        const float age = (float) ((t - d.born) / (barSecs * 1.5));
        if (age > 1.0f) continue;
        const float x = laneR.getX() + ((float) d.step + 0.5f) * colW;
        const auto col = hsvCol (d.hue, 0.6f, 1.0f);
        const float fade = 1.0f - age;
        if (d.kind == 2)
        {
            g.setColour (col.withAlpha (0.5f * fade));
            g.fillRoundedRectangle (x - colW * 0.4f, laneR.getBottom() - 10.0f, colW * 0.8f, 4.0f, 2.0f);
            continue;
        }
        const float y = jmap ((float) jlimit (45, 100, d.note), 45.0f, 100.0f, laneR.getBottom() - 18.0f, laneR.getY() + 24.0f);
        const float sz = d.kind == 0 ? 5.0f : 4.0f;
        ColourGradient halo (col.withAlpha (0.6f * fade), x, y, col.withAlpha (0.0f), x + sz * 3.5f, y, true);
        g.setGradientFill (halo);
        g.fillEllipse (x - sz * 3.5f, y - sz * 3.5f, sz * 7.0f, sz * 7.0f);
        g.setColour (Colours::white.withAlpha (0.9f * fade));
        g.fillEllipse (x - sz * 0.5f, y - sz * 0.5f, sz, sz);
    }
    // playhead
    {
        const float frac = (float) jlimit (0.0, 1.0, (t - stepClock) / (barSecs / 16.0));
        const float x = laneR.getX() + ((float) step + frac) * colW;
        g.setColour (Colours::white.withAlpha (0.9f));
        g.drawLine (x, laneR.getY(), x, laneR.getBottom(), 1.5f);
        Path tri;
        tri.addTriangle (x - 5.0f, laneR.getY(), x + 5.0f, laneR.getY(), x, laneR.getY() + 6.0f);
        g.fillPath (tri);
    }

    // play status, next to the play button
    {
        const auto pb = playButton.getBounds().toFloat();
        String hint;
        if (page == 2)
            hint = {};
        else if (! proc.engine.uiRunning.load())
            hint = proc.hostPlaying.load() ? "" : "press play or start your song  -  your keys play notes any time";
        else if (! proc.hostPlaying.load() && proc.params.getRawParameterValue ("play")->load() > 0.5f)
            hint = "playing on its own";
        g.setFont (font (13.5f));
        g.setColour (textCol.withAlpha (0.7f));
        g.drawText (hint, Rectangle<float> (pb.getRight() + 12.0f, pb.getY(), 360.0f, pb.getHeight()), Justification::centredLeft, false);
    }

    // now playing, top right
    g.setFont (font (13.5f));
    g.setColour (textCol.withAlpha (0.8f));
    g.drawText (nowText, r.reduced (24.0f, 18.0f).removeFromTop (20.0f), Justification::topRight, false);

    // glass: inner shadow and a faint reflection
    ColourGradient shade (Colours::black.withAlpha (0.55f), r.getX(), r.getY(), Colours::black.withAlpha (0.0f), r.getX(), r.getY() + 26.0f, false);
    g.setGradientFill (shade);
    g.fillRect (r.withHeight (26.0f));
    ColourGradient glare (Colours::white.withAlpha (0.045f), r.getX(), r.getY(), Colours::white.withAlpha (0.0f), r.getX() + r.getWidth() * 0.5f, r.getBottom(), false);
    g.setGradientFill (glare);
    g.fillRect (r);
}

void LivePlanetEditor::mouseDown (const MouseEvent& e)
{
    if (e.eventComponent != &cityBox && ! cityBox.isParentOf (e.eventComponent) && cityBox.hasKeyboardFocus (true))
        cityBox.giveAwayKeyboardFocus();
    auto& pb = songPanel.promptBox;
    if (e.eventComponent != &pb && ! pb.isParentOf (e.eventComponent) && pb.hasKeyboardFocus (true))
        pb.giveAwayKeyboardFocus();
}

void LivePlanetEditor::paintOverChildren (Graphics& g)
{
    if (dragging)
    {
        g.setColour (Colours::black.withAlpha (0.25f));
        g.fillAll();
        g.setColour (Colours::white);
        g.setFont (font (26.0f));
        g.drawText (page == 2 ? "drop to learn the feel of this song" : "drop to let the weather play this sound", getLocalBounds(), Justification::centred);
    }
}

void LivePlanetEditor::resized()
{
    auto k = [this] (const char* id) { return &knobById[id]->slider; };

    bezel = { 40, 56, 680, 412 };
    screen = bezel.reduced (10);

    // right column: sky glass card, then a bark slab with the controls
    const int rx = 744, rw = 336;
    infoCard = { rx, 56, rw, 118 };
    cityBox.setBounds (rx + 18, 56 + 118 - 44, rw - 36, 30);
    rightPlate = { rx, 186, rw, 292 };
    liveSwitch.setBounds (rx + 14, 198, 140, 30);
    pageSwitch.setBounds (rx + rw - 14 - 170, 198, 170, 30);
    pageCard = { rx + 12, 236, rw - 24, 172 };
    auto pc = pageCard.reduced (12, 12);
    keys.setBounds (pc.removeFromTop (114));
    pc.removeFromTop (8);
    loadButton.setBounds (pc.removeFromTop (26).withWidth (112));
    {
        auto sp = pageCard.reduced (6, 8);
        const int cw = sp.getWidth() / 4;
        const char* top[] = { "temp", "rain", "snow", "wind" };
        const char* bottom[] = { "clouds", "pressure", "humidity" };
        for (int i = 0; i < 4; ++i) k (top[i])->setBounds (sp.getX() + i * cw, sp.getY(), cw, 74);
        for (int i = 0; i < 3; ++i) k (bottom[i])->setBounds (sp.getX() + i * cw, sp.getY() + 80, cw, 74);
        auto tg = Rectangle<int> (sp.getX() + 3 * cw, sp.getY() + 84, cw, 64);
        dayToggle.setBounds (tg.removeFromTop (28).reduced (2, 0));
        tg.removeFromTop (6);
        thunderToggle.setBounds (tg.removeFromTop (28).reduced (2, 0));
    }
    const int sw = (rw - 28 - 12) / 2;
    const int sw3 = (rw - 28 - 16) / 3;
    keyStepper->setBounds (rx + 14, 438, sw3, 30);
    scaleStepper->setBounds (rx + 14 + sw3 + 8, 438, sw3, 30);
    keySwitch.setBounds (rx + 14 + 2 * (sw3 + 8), 438, sw3, 30);
    playButton.setBounds (screen.getX() + 18, screen.getBottom() - 112 - 54, 42, 42);
    dragNotes.setBounds (screen.getRight() - 18 - 210, screen.getBottom() - 112 - 50, 210, 34);
    sampleView.setBounds (screen.getX(), screen.getBottom() - 112, screen.getWidth(), 112);
    songView.setBounds (screen.getX(), screen.getBottom() - 112, screen.getWidth(), 112);
    studioView.setBounds (screen.getX() + 10, screen.getY() + 10, screen.getWidth() - 20, screen.getHeight() - 112 - 60);
    songPanel.setBounds (pageCard.getX() + 12, pageCard.getY() + 8, pageCard.getWidth() - 24, 468 - pageCard.getY() - 8);
    {
        int px = screen.getX() + 70;
        const int pw = (screen.getRight() - 14 - px) / 5;
        for (auto* pill : { &pillMidi, &pillStems, &pillMix, &pillHelper, &pillFolder })
        {
            pill->setBounds (px, screen.getBottom() - 112 - 50, pw - 6, 34);
            px += pw;
        }
    }
    presetBrowser.setBounds (rx + 18, 56 + 40, rw - 36, 28);

    // three bark slabs of knobs
    const int py = 490, ph = 236;
    plateMix = { 40, py, 236, ph };
    plateTex = { 290, py, 390, ph };
    plateFx  = { 694, py, 386, ph };

    // mix: three knobs in a gentle arc, the season picker underneath
    k ("pad")->setBounds   (plateMix.getX() + 4,   py + 24, 76, 140);
    k ("drops")->setBounds (plateMix.getX() + 80,  py + 24, 76, 140);
    k ("bass")->setBounds  (plateMix.getX() + 156, py + 24, 76, 140);
    themeDots.setBounds (plateMix.getX() + 16, py + ph - 38, lp::ui::numThemes * 22 + 4, 24);

    // texture: the big grain knob in the middle, small ones around it
    k ("texture")->setBounds  (plateTex.getX() + 8,   py + 24, 100, 104);
    k ("sparkle")->setBounds  (plateTex.getX() + 8,   py + 128, 100, 104);
    k ("grain")->setBounds    (plateTex.getX() + 108, py + 14, 176, 218);
    k ("position")->setBounds (plateTex.getX() + 284, py + 70, 100, 110);

    // space: a 3 x 2 grid
    const char* fx[] = { "warmth", "echo", "space", "shimmer", "nostalgia", "atmos", "gain" };
    const int cw = (plateFx.getWidth() - 16) / 4;
    for (int i = 0; i < 7; ++i)
    {
        const int row = i / 4, col = i % 4;
        const int x = plateFx.getX() + 8 + col * cw + (row == 1 ? cw / 2 : 0); // second row sits between the first
        k (fx[i])->setBounds (x, py + 26 + row * 104, cw, 104);
    }

    lnf.displayScale = (float) jmax (1.0, Desktop::getInstance().getDisplays().getPrimaryDisplay() != nullptr
                                              ? Desktop::getInstance().getDisplays().getPrimaryDisplay()->scale : 1.0);

    const SpinLock::ScopedLockType sl (sceneLock);
    scene.size[0] = (float) getWidth();
    scene.size[1] = (float) getHeight();
    scene.screen[0] = (float) screen.getX();
    scene.screen[1] = (float) (getHeight() - screen.getBottom());
    scene.screen[2] = (float) screen.getWidth();
    scene.screen[3] = (float) screen.getHeight();
    scene.planet[0] = (float) screen.getWidth() * 0.62f;
    scene.planet[1] = 112.0f + ((float) screen.getHeight() - 112.0f) * 0.47f;
    scene.planet[2] = 116.0f;
}

//==============================================================================
// OpenGL

void LivePlanetEditor::newOpenGLContextCreated()
{
    using namespace juce::gl;
    shader = std::make_unique<OpenGLShaderProgram> (gl);
    const bool ok = shader->addVertexShader (OpenGLHelpers::translateVertexShaderToV3 (lp::shader::vertex))
                 && shader->addFragmentShader (OpenGLHelpers::translateFragmentShaderToV3 (lp::shader::fragment))
                 && shader->link();
    if (! ok)
    {
        std::fprintf (stderr, "Live Planet shader error:\n%s\n", shader->getLastError().toRawUTF8());
        shader.reset();
        shaderFailed = true;
        MessageManager::callAsync ([safe = Component::SafePointer<Component> (this)] { if (safe) safe->repaint(); });
        return;
    }
    const GLfloat quad[] = { -1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f };
    glGenBuffers (1, &vbo);
    glBindBuffer (GL_ARRAY_BUFFER, vbo);
    glBufferData (GL_ARRAY_BUFFER, sizeof (quad), quad, GL_STATIC_DRAW);
    glBindBuffer (GL_ARRAY_BUFFER, 0);
}

void LivePlanetEditor::renderOpenGL()
{
    using namespace juce::gl;
    SceneState sc;
    {
        const SpinLock::ScopedLockType sl (sceneLock);
        sc = scene;
    }
    const float s = (float) gl.getRenderingScale();
    glViewport (0, 0, roundToInt (sc.size[0] * s), roundToInt (sc.size[1] * s));
    glDisable (GL_DEPTH_TEST);
    glClearColor (0.02f, 0.02f, 0.03f, 1.0f);
    glClear (GL_COLOR_BUFFER_BIT);
    if (shader == nullptr) return;

    // only the screen window needs the planet
    const int sx = roundToInt (sc.screen[0] * s), sy = roundToInt (sc.screen[1] * s);
    const int sw = roundToInt (sc.screen[2] * s), sh = roundToInt (sc.screen[3] * s);
    glViewport (sx, sy, sw, sh);

    shader->use();
    const auto prog = shader->getProgramID();
    auto loc = [prog] (const char* n) { return glGetUniformLocation (prog, n); };
    const float t = (float) ((Time::getMillisecondCounterHiRes() - startMs) * 0.001);

    glUniform2f (loc ("uRes"), (float) sw, (float) sh);
    glUniform2f (loc ("uOrigin"), (float) sx, (float) sy);
    glUniform1f (loc ("uTime"), t);
    glUniform1f (loc ("uScale"), s);
    glUniform4f (loc ("uPlanet"), sc.planet[0] * s, sc.planet[1] * s, sc.planet[2] * s, 0.0f);
    glUniform4fv (loc ("uW1"), 1, sc.w1);
    glUniform4fv (loc ("uW2"), 1, sc.w2);
    glUniform4fv (loc ("uW3"), 1, sc.w3);
    glUniform1f (loc ("uCityLon"), sc.cityLon);
    glUniform3fv (loc ("uPalA"), 1, sc.palA);
    glUniform3fv (loc ("uPalB"), 1, sc.palB);
    glUniform3fv (loc ("uPalC"), 1, sc.palC);
    glUniform4fv (loc ("uNotes"), 24, sc.notes);
    glUniform1f (loc ("uNoteCount"), (float) sc.noteCount);
    const float noPanels[12] {};
    glUniform4fv (loc ("uPanels"), 3, noPanels);
    glUniform1f (loc ("uPanelRadius"), 1.0f);

    glBindBuffer (GL_ARRAY_BUFFER, vbo);
    const auto pos = (GLuint) glGetAttribLocation (prog, "position");
    glEnableVertexAttribArray (pos);
    glVertexAttribPointer (pos, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glDrawArrays (GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray (pos);
    glBindBuffer (GL_ARRAY_BUFFER, 0);
    glViewport (0, 0, roundToInt (sc.size[0] * s), roundToInt (sc.size[1] * s));
}

void LivePlanetEditor::openGLContextClosing()
{
    using namespace juce::gl;
    shader.reset();
    if (vbo != 0) glDeleteBuffers (1, &vbo);
    vbo = 0;
}

//==============================================================================

bool LivePlanetEditor::isInterestedInFileDrag (const StringArray& files)
{
    for (auto& f : files)
        if (File (f).hasFileExtension ("wav;aif;aiff;flac;mp3;ogg"))
            return true;
    return false;
}

void LivePlanetEditor::filesDropped (const StringArray& files, int, int)
{
    dragging = false;
    if (page == 2) // song mode: the dropped song becomes the reference
    {
        if (! files.isEmpty()) proc.songJob.setReference (File (files[0]));
        repaint();
        return;
    }
    for (auto& f : files)
        if (proc.loadSample (File (f)))
            break;
    repaint();
}

AudioProcessorEditor* LivePlanetProcessor::createEditor()
{
    return new LivePlanetEditor (*this);
}
