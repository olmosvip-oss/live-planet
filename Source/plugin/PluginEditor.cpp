#include "PluginEditor.h"
#include "PlanetShader.h"
#include "BinaryData.h"
#include "GrassArt.h"

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

    // a dark hardware key with a soft shadow
    void hardwareKey (Graphics& g, Rectangle<float> r, bool lit, bool dark)
    {
        g.setColour (Colours::black.withAlpha (dark ? 0.5f : 0.22f));
        g.fillRoundedRectangle (r.translated (0.0f, 2.5f).expanded (0.5f), 7.0f);
        ColourGradient body (lit ? Colour (0xff5d5b56) : Colour (0xff2d2c29), r.getX(), r.getY(),
                             lit ? Colour (0xff3f3d39) : Colour (0xff171614), r.getX(), r.getBottom(), false);
        g.setGradientFill (body);
        g.fillRoundedRectangle (r, 7.0f);
        g.setColour (Colours::white.withAlpha (lit ? 0.18f : 0.08f));
        g.drawLine (r.getX() + 6.0f, r.getY() + 1.0f, r.getRight() - 6.0f, r.getY() + 1.0f, 1.0f);
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
        //  name   bark body / light / dark                                    ink                 inkDim              line                dark   grass light / base / dark                                   sky top / bottom
        { "spring", Colour (0xffdcd9d2), Colour (0xffeeece7), Colour (0xffbdb9b0), Colour (0xff2c2b27), Colour (0xff7a766c), Colour (0x222c2b27), false, Colour (0xff9fcb55), Colour (0xff5b8a2e), Colour (0xff27461a), Colour (0xffeef3f6), Colour (0xffc8dbe8) },
        { "moss",   Colour (0xffd6d4cd), Colour (0xffe9e7e2), Colour (0xffb5b1a8), Colour (0xff2a2a26), Colour (0xff74716a), Colour (0x222a2a26), false, Colour (0xff7d9a4c), Colour (0xff45612c), Colour (0xff1c2c13), Colour (0xffe6ebe8), Colour (0xffbfcfca) },
        { "autumn", Colour (0xffdcd5cc), Colour (0xffeee9e2), Colour (0xffbeb5a8), Colour (0xff2f2a24), Colour (0xff7e7466), Colour (0x222f2a24), false, Colour (0xffe8b04a), Colour (0xffb0652a), Colour (0xff5a2a12), Colour (0xfff5eee4), Colour (0xffe4d3bc) },
        { "frost",  Colour (0xffdedfe0), Colour (0xffeff0f1), Colour (0xffbfc2c5), Colour (0xff2a2d31), Colour (0xff747a82), Colour (0x222a2d31), false, Colour (0xffd8ece4), Colour (0xff86a99d), Colour (0xff3b5a52), Colour (0xfff2f6fa), Colour (0xffd0e0ee) },
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
    const float hue = (float) (double) s.getProperties().getWithDefault ("hue", 0.5);
    const bool enabled = s.isEnabled();
    const float alpha = enabled ? 1.0f : 0.35f;
    const bool small = size < 50;

    auto area = Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    const float labelH = small ? 30.0f : 44.0f;
    const float r = (float) size * 0.5f;
    const float ringPad = small ? 5.0f : 8.0f;
    // knob and its labels sit together, centred in the cell
    const float blockH = (r + ringPad) * 2.0f + 8.0f + labelH;
    const Point<float> c (area.getCentreX(), area.getCentreY() - blockH * 0.5f + r + ringPad);
    auto labels = Rectangle<float> (area.getX(), c.y + r + ringPad + 8.0f, area.getWidth(), labelH);
    const float angle = start + pos * (end - start);

    // coloured value ring on the body
    const float ringR = r + ringPad;
    Path track;
    track.addCentredArc (c.x, c.y, ringR, ringR, 0, start, end, true);
    g.setColour (th->ink.withAlpha (0.1f * alpha));
    g.strokePath (track, PathStrokeType (2.0f, PathStrokeType::curved, PathStrokeType::rounded));
    if (pos > 0.002f)
    {
        Path arc;
        arc.addCentredArc (c.x, c.y, ringR, ringR, 0, start, angle, true);
        // the season's own green, from deep at the start to fresh at the tip
        ignoreUnused (hue);
        ColourGradient col (th->grassDark.withMultipliedAlpha (alpha), c.x - ringR, c.y + ringR,
                            th->grass.brighter (0.15f).withMultipliedAlpha (alpha), c.x + ringR, c.y - ringR, false);
        auto soft = col;
        soft.multiplyOpacity (0.22f);
        g.setGradientFill (soft);
        g.strokePath (arc, PathStrokeType (6.0f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setGradientFill (col);
        g.strokePath (arc, PathStrokeType (2.5f, PathStrokeType::curved, PathStrokeType::rounded));
    }

    // shadow on the slab
    ColourGradient shadow (Colours::black.withAlpha (0.38f * alpha), c.x, c.y + r * 0.45f,
                           Colours::black.withAlpha (0.0f), c.x, c.y + r * 1.5f, true);
    g.setGradientFill (shadow);
    g.fillEllipse (c.x - r * 1.3f, c.y - r * 0.85f, r * 2.6f, r * 2.6f);

    // a little grass topiary ball (rendered once per size and season, then reused)
    {
        auto key = std::make_pair (th, size);
        auto it = balls.find (key);
        if (it == balls.end())
        {
            const int pad = 6;
            lp::art::Grass gr { th->grassLight, th->grass, th->grassDark };
            auto img = lp::art::makeTopiary (size + pad * 2, size + pad * 2,
                                             { { (float) pad, (float) pad, (float) size, (float) size, (float) size * 0.5f } },
                                             gr, (juce::uint32) size * 31u, displayScale, 0.35f);
            it = balls.emplace (key, img).first;
        }
        const float pad = 6.0f;
        g.setOpacity (alpha);
        g.drawImage (it->second, Rectangle<float> (c.x - r - pad, c.y - r - pad, r * 2.0f + pad * 2.0f, r * 2.0f + pad * 2.0f));
        g.setOpacity (1.0f);
        // round it off: soft highlight on top, shade underneath
        ColourGradient hi (Colours::white.withAlpha (0.22f * alpha), c.x - r * 0.3f, c.y - r * 0.5f,
                           Colours::white.withAlpha (0.0f), c.x - r * 0.3f + r, c.y - r * 0.5f, true);
        g.setGradientFill (hi);
        g.fillEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);
        ColourGradient lo (Colours::black.withAlpha (0.0f), c.x, c.y, Colours::black.withAlpha (0.35f * alpha), c.x, c.y + r, false);
        g.setGradientFill (lo);
        g.fillEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);
    }

    // indicator line
    const float sn = std::sin (angle), cs = -std::cos (angle);
    const float pw = size > 90 ? 5.0f : 3.5f;
    const Line<float> peg (c.x + sn * r * 0.4f, c.y + cs * r * 0.4f, c.x + sn * r * 0.86f, c.y + cs * r * 0.86f);
    g.setColour (Colour (0xff2a2a22).withAlpha (0.7f * alpha));
    g.drawLine (peg, pw + 2.0f);
    g.setColour (th->bodyLight.withAlpha (alpha));
    g.drawLine (peg, pw);

    // name and value
    g.setFont (font (small ? 12.5f : 15.0f));
    g.setColour (th->ink.withMultipliedAlpha (alpha));
    g.drawText (s.getName(), labels.removeFromTop (small ? 15.0f : 21.0f), Justification::centred, false);
    g.setFont (font (small ? 11.5f : 13.5f));
    g.setColour (th->inkDim.withMultipliedAlpha (alpha));
    g.drawText (s.getTextFromValue (s.getValue()), labels, Justification::centredTop, false);
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

Segmented::Segmented (String a, String b, std::function<int()> get, std::function<void (int)> set, const lp::ui::Theme*& theme)
    : getter (std::move (get)), setter (std::move (set)), th (theme)
{
    labels[0] = a;
    labels[1] = b;
}

void Segmented::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    recess (g, r, 8.0f, *th);
    const int sel = getter();
    for (int i = 0; i < 2; ++i)
    {
        auto half = r.withWidth (r.getWidth() * 0.5f).translated (i * r.getWidth() * 0.5f, 0.0f).reduced (2.0f);
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
    setter (e.position.x < (float) getWidth() * 0.5f ? 0 : 1);
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
    static const float hues[] = { 0.0f, 0.5f, 0.74f, 0.07f, 0.9f, 0.15f, 0.99f, 0.6f, 0.04f, 0.11f, 0.55f, 0.28f };
    return hues[jlimit (0, 11, i)];
}

String SoundKeys::nameFor (int i)
{
    static const char* names[] = { "auto", "glass", "choir", "piano", "music box", "guitar", "strings", "vapor",
                                   "rhodes", "kalimba", "vibes", "ocarina" };
    return names[jlimit (0, 11, i)];
}

Rectangle<float> SoundKeys::keyBounds (int i) const
{
    const float gap = 8.0f;
    const float kw = ((float) getWidth() - gap * 3.0f) / 4.0f;
    const float kh = ((float) getHeight() - gap * 2.0f - 3.0f) / 3.0f;
    return { (float) (i % 4) * (kw + gap), (float) (i / 4) * (kh + gap), kw, kh };
}

void SoundKeys::paint (Graphics& g)
{
    const int selected = (int) proc.params.getRawParameterValue ("sound")->load();
    const int autoPick = proc.engine.uiSound.load() + 1;
    const float t = (float) (Time::getMillisecondCounterHiRes() * 0.001);
    for (int i = 0; i < 12; ++i)
    {
        auto r = keyBounds (i);
        const bool lit = i == selected;
        const bool picked = selected == 0 && i == autoPick;
        hardwareKey (g, r, lit, th->dark);
        const auto col = i == 0 ? hsvCol (t * 0.08f, 0.65f, 1.0f) : hsvCol (hueFor (i), 0.7f, 1.0f);
        const float glow = lit ? 1.0f : (picked ? 0.6f + 0.4f * std::sin (t * 4.0f) : 0.0f);
        led (g, { r.getX() + 12.0f, r.getCentreY() }, glow > 0.0f ? col : col.withMultipliedBrightness (0.35f), 5.0f, glow);
        g.setFont (font (12.5f));
        g.setColour (Colour (0xffeeebe4).withAlpha (lit || picked ? 1.0f : 0.7f));
        g.drawFittedText (nameFor (i), r.withTrimmedLeft (20.0f).withTrimmedRight (4.0f).toNearestInt(), Justification::centred, 1, 0.8f);
    }
}

void SoundKeys::mouseUp (const MouseEvent& e)
{
    for (int i = 0; i < 12; ++i)
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
    const bool on = proc.engine.uiRunning.load();
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
      liveSwitch ("live", "manual",
                  [&p] { return p.params.getRawParameterValue ("live")->load() > 0.5f ? 0 : 1; },
                  [&p] (int i) { if (auto* q = p.params.getParameter ("live")) { q->beginChangeGesture(); q->setValueNotifyingHost (i == 0 ? 1.0f : 0.0f); q->endChangeGesture(); } },
                  lnf.th),
      pageSwitch ("instrument", "sky", [this] { return page; }, [this] (int i) { setPage (i); }, lnf.th),
      keys (p, lnf.th),
      playButton (p),
      presetBrowser (p, lnf.th),
      sampleView (p),
      dragNotes (p),
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
    addAndMakeVisible (cityBox);

    addAndMakeVisible (liveSwitch);
    addAndMakeVisible (pageSwitch);
    addAndMakeVisible (keys);
    addAndMakeVisible (playButton);
    addAndMakeVisible (presetBrowser);
    addChildComponent (sampleView);
    addAndMakeVisible (dragNotes);
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
    addKnob ("pad", "chords", 0.86f, 52);
    addKnob ("drops", "melody", 0.78f, 52);
    addKnob ("bass", "bass", 0.68f, 52);
    addKnob ("grain", "grain", 0.08f, 112);
    addKnob ("texture", "scatter", 0.12f, 44);
    addKnob ("sparkle", "sparkle", 0.16f, 44);
    addKnob ("position", "position", 0.2f, 52);
    addKnob ("warmth", "warmth", 0.03f, 44);
    addKnob ("echo", "echo", 0.45f, 44);
    addKnob ("space", "space", 0.52f, 44);
    addKnob ("shimmer", "shimmer", 0.58f, 44);
    addKnob ("atmos", "atmos", 0.36f, 44);
    addKnob ("gain", "output", 0.0f, 44);
    addKnob ("nostalgia", "nostalgia", 0.1f, 44);
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
        statusText = mins < 1 ? "live" : "live \xc2\xb7 " + String (mins) + " min ago";
        statusText = String (CharPointer_UTF8 (statusText.toRawUTF8()));
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
    const bool sampleMode = (int) proc.params.getRawParameterValue ("sound")->load() == LivePlanetProcessor::kSampleChoice;
    if (sampleView.isVisible() != sampleMode) sampleView.setVisible (sampleMode);
    if (sampleMode) sampleView.repaint();
}

//==============================================================================

void LivePlanetEditor::paint (Graphics& g)
{
    const auto& th = *lnf.th;
    auto bounds = getLocalBounds().toFloat();

    // the grass topiary is rendered once per season and size, then reused
    const float scale = lnf.displayScale;
    if (topiaryTheme != lnf.th || topiary.getWidth() != roundToInt ((float) getWidth() * scale))
    {
        lp::art::Grass gr { th.grassLight, th.grass, th.grassDark };
        const float W = (float) getWidth(), H = (float) getHeight();
        topiary = lp::art::makeTopiary (getWidth(), getHeight(), {
                        { 14.0f, 30.0f, W - 28.0f, H - 44.0f, 34.0f },   // the main block
                        { 520.0f, 10.0f, 210.0f, 70.0f, 30.0f },          // bumps along the top
                        { 860.0f, 16.0f, 170.0f, 60.0f, 26.0f },
                        { 70.0f, 8.0f, 64.0f, 64.0f, 32.0f },
                        { 1040.0f, 520.0f, 70.0f, 120.0f, 30.0f },
                    }, gr, 1234u, scale, 0.16f);
        topiaryTheme = lnf.th;
        // leave the screen window empty so the planet shows through
        topiary.clear (screen.toFloat().transformedBy (AffineTransform::scale (scale)).getSmallestIntegerContainer(), Colours::transparentBlack);
        if (auto* dump = std::getenv ("LP_DUMP_TOPIARY"))
        {
            File f (dump);
            f.deleteFile();
            FileOutputStream os (f);
            PNGImageFormat().writeImageToStream (topiary, os);
        }
    }

    {
        // sky everywhere except the screen window
        Path skyArea;
        skyArea.addRectangle (bounds);
        skyArea.addRectangle (screen.toFloat());
        skyArea.setUsingNonZeroWinding (false);
        ColourGradient sky (th.skyTop, 0, 0, th.skyBottom, 0, bounds.getBottom(), false);
        g.setGradientFill (sky);
        g.fillPath (skyArea);
        ColourGradient sun (Colours::white.withAlpha (0.7f), bounds.getRight() * 0.82f, 0.0f,
                            Colours::white.withAlpha (0.0f), bounds.getRight() * 0.82f + 420.0f, 0.0f, true);
        g.setGradientFill (sun);
        g.fillPath (skyArea);

        // soft contact shadow, then the topiary
        g.setColour (Colours::black.withAlpha (0.18f));
        g.fillEllipse (40.0f, bounds.getBottom() - 26.0f, bounds.getWidth() - 80.0f, 30.0f);
        g.setOpacity (1.0f); // drawImage uses the current colour's opacity
        g.drawImage (topiary, bounds);
    }

    if (shaderFailed)
    {
        g.setColour (Colour (0xff050608));
        g.fillRoundedRectangle (screen.toFloat(), 22.0f);
    }

    // a slab of birch bark resting on the grass
    auto bark = [&g, &th] (Rectangle<int> rect, int seed)
    {
        auto r = rect.toFloat();
        for (int i = 3; i >= 1; --i)
        {
            g.setColour (Colours::black.withAlpha (0.09f));
            g.fillRoundedRectangle (r.translated (0.0f, 2.0f + (float) i * 2.0f).expanded ((float) i), 16.0f + (float) i);
        }
        ColourGradient body (th.bodyLight, 0, r.getY(), th.bodyDark, 0, r.getBottom(), false);
        body.addColour (0.35, th.body);
        g.setGradientFill (body);
        g.fillRoundedRectangle (r, 16.0f);
        {
            Graphics::ScopedSaveState ss (g);
            Path clip;
            clip.addRoundedRectangle (r, 16.0f);
            g.reduceClipRegion (clip);
            Random rng (seed);
            const int marks = (int) (r.getWidth() * r.getHeight() / 2600.0f);
            for (int i = 0; i < marks; ++i)
            {
                const float w = 5.0f + rng.nextFloat() * 26.0f, h = 1.0f + rng.nextFloat() * 2.2f;
                g.setColour (th.ink.withAlpha (0.05f + rng.nextFloat() * 0.13f));
                g.fillRoundedRectangle (r.getX() + rng.nextFloat() * r.getWidth(), r.getY() + rng.nextFloat() * r.getHeight(), w, h, h * 0.5f);
            }
        }
        g.setColour (Colours::white.withAlpha (0.7f));
        g.drawLine (r.getX() + 16.0f, r.getY() + 0.75f, r.getRight() - 16.0f, r.getY() + 0.75f, 1.2f);
        g.setColour (Colours::black.withAlpha (0.12f));
        g.drawRoundedRectangle (r.reduced (0.5f), 16.0f, 1.0f);
    };
    bark (rightPlate, 3);
    bark (plateMix, 5);
    bark (plateTex, 8);
    bark (plateFx, 13);

    // black glass bezel around the screen
    {
        Path ring;
        ring.addRoundedRectangle (bezel.toFloat(), 30.0f);
        ring.addRoundedRectangle (screen.toFloat(), 22.0f);
        ring.setUsingNonZeroWinding (false);
        for (int i = 3; i >= 1; --i)
        {
            g.setColour (Colours::black.withAlpha (0.1f));
            g.fillRoundedRectangle (bezel.toFloat().translated (0, 3.0f + (float) i * 2.0f).expanded ((float) i), 31.0f + (float) i);
        }
        ColourGradient bz (Colour (0xff222224), 0, (float) bezel.getY(), Colour (0xff0a0a0b), 0, (float) bezel.getBottom(), false);
        g.setGradientFill (bz);
        g.fillPath (ring);
        ColourGradient lip (Colours::white.withAlpha (0.25f), 0, (float) bezel.getY(), Colours::white.withAlpha (0.02f), 0, (float) bezel.getBottom(), false);
        g.setGradientFill (lip);
        g.drawRoundedRectangle (bezel.toFloat().reduced (0.75f), 30.0f, 1.2f);
    }

    paintScreen (g);

    // info card: a pane of pale sky glass
    {
        auto r = infoCard.toFloat();
        for (int i = 3; i >= 1; --i)
        {
            g.setColour (Colours::black.withAlpha (0.08f));
            g.fillRoundedRectangle (r.translated (0, 2.0f + (float) i * 2.0f).expanded ((float) i), 16.0f + (float) i);
        }
        ColourGradient glass (th.skyTop.brighter (0.05f), 0, r.getY(), th.skyBottom, 0, r.getBottom(), false);
        g.setGradientFill (glass);
        g.fillRoundedRectangle (r, 16.0f);
        g.setColour (Colours::white.withAlpha (0.85f));
        g.drawLine (r.getX() + 16.0f, r.getY() + 0.75f, r.getRight() - 16.0f, r.getY() + 0.75f, 1.2f);
        g.setColour (Colours::black.withAlpha (0.1f));
        g.drawRoundedRectangle (r.reduced (0.5f), 16.0f, 1.0f);

        auto in = r.reduced (18.0f, 14.0f);
        auto head = in.removeFromTop (24.0f);
        const float cx = head.getX() + 9.0f, cy = head.getCentreY();
        ColourGradient orb (th.grassLight, cx - 5, cy - 5, th.grass, cx + 5, cy + 5, false);
        g.setGradientFill (orb);
        g.fillEllipse (cx - 7, cy - 7, 14, 14);
        g.setColour (th.ink.withAlpha (0.6f));
        g.drawEllipse (cx - 11.0f, cy - 3.5f, 22.0f, 7.0f, 1.0f);
        g.setFont (font (19.0f, lp::ui::Light, -0.01f));
        g.setColour (th.ink);
        g.drawText ("liveplanet", head.withTrimmedLeft (26.0f), Justification::centredLeft, false);
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
        auto r = pageCard.toFloat();
        recess (g, r, 14.0f, th);
        if (page == 0)
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
    g.drawText ("key", keyStepper->getBounds().translated (0, -19).withHeight (16), Justification::centredLeft, false);
    g.drawText ("scale", scaleStepper->getBounds().translated (0, -19).withHeight (16), Justification::centredLeft, false);
    g.drawText (String ("season  ") + th.name, themeDots.getBounds().translated (themeDots.getWidth() + 6, 0).withWidth (140),
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
        if (! proc.engine.uiRunning.load())
            hint = proc.hostPlaying.load() ? "" : "press play, start your song, or hold a key";
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

void LivePlanetEditor::paintOverChildren (Graphics& g)
{
    if (dragging)
    {
        g.setColour (Colours::black.withAlpha (0.25f));
        g.fillAll();
        g.setColour (Colours::white);
        g.setFont (font (26.0f));
        g.drawText ("drop to let the weather play this sound", getLocalBounds(), Justification::centred);
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
    keyStepper->setBounds (rx + 14, 438, sw, 30);
    scaleStepper->setBounds (rx + 14 + sw + 12, 438, sw, 30);
    playButton.setBounds (screen.getX() + 18, screen.getBottom() - 112 - 54, 42, 42);
    dragNotes.setBounds (screen.getRight() - 18 - 210, screen.getBottom() - 112 - 50, 210, 34);
    sampleView.setBounds (screen.getX(), screen.getBottom() - 112, screen.getWidth(), 112);
    presetBrowser.setBounds (rx + 18, 56 + 40, rw - 36, 28);

    // three bark slabs of knobs
    const int py = 490, ph = 236;
    plateMix = { 40, py, 236, ph };
    plateTex = { 290, py, 390, ph };
    plateFx  = { 694, py, 386, ph };

    // mix: three knobs in a gentle arc, the season picker underneath
    k ("pad")->setBounds   (plateMix.getX() + 6,   py + 34, 74, 132);
    k ("drops")->setBounds (plateMix.getX() + 81,  py + 20, 74, 132);
    k ("bass")->setBounds  (plateMix.getX() + 156, py + 34, 74, 132);
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
    for (auto& f : files)
        if (proc.loadSample (File (f)))
            break;
    repaint();
}

AudioProcessorEditor* LivePlanetProcessor::createEditor()
{
    return new LivePlanetEditor (*this);
}
