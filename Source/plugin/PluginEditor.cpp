#include <typeinfo>
#include "PluginEditor.h"
#include "License.h"
#include "BinaryData.h"
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

    // a quiet inset area: a little darker than the card, a hairline border
    void recess (Graphics& g, Rectangle<float> r, float radius, const lp::ui::Theme& th)
    {
        g.setColour (th.bodyDark);
        g.fillRoundedRectangle (r, radius);
        g.setColour (th.line);
        g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
    }

    // a chip: dark when off, light (with dark text) when selected, like a filter chip
    void hardwareKey (Graphics& g, Rectangle<float> r, bool lit, bool)
    {
        const auto& th = lp::ui::theme (0);
        g.setColour (lit ? Colour (0xffe9e9ec) : th.bodyLight);
        g.fillRoundedRectangle (r, 6.0f);
        if (! lit)
        {
            g.setColour (th.line);
            g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
        }
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

Font lp::ui::mono (float size, bool bold, float kerning)
{
    static Typeface::Ptr reg = Typeface::createSystemTypefaceFor (BinaryData::SpaceMonoRegular_ttf, BinaryData::SpaceMonoRegular_ttfSize);
    static Typeface::Ptr bld = Typeface::createSystemTypefaceFor (BinaryData::SpaceMonoBold_ttf, BinaryData::SpaceMonoBold_ttfSize);
    return Font (FontOptions (bold ? bld : reg).withHeight (size).withKerningFactor (kerning));
}

const lp::ui::Theme& lp::ui::theme (int i)
{
    static const Theme themes[] = {
        //  name      card                 raised               page                 ink                  muted                line                 dark  accent light         accent               accent deep          page top             page bottom
        { "violet", Colour (0xff1b1b1d), Colour (0xff232326), Colour (0xff121213), Colour (0xfff2f2f3), Colour (0xff8b8b92), Colour (0xff2c2c30), true, Colour (0xffb9a2ff), Colour (0xff8b5cf6), Colour (0xff5b34c9), Colour (0xff17161b), Colour (0xff101011) },
        { "mint",   Colour (0xff1a1c1b), Colour (0xff222624), Colour (0xff111312), Colour (0xfff1f3f2), Colour (0xff88918d), Colour (0xff2b302e), true, Colour (0xff8ef0c8), Colour (0xff2fcf95), Colour (0xff11865a), Colour (0xff141917), Colour (0xff0f1110) },
        { "amber",  Colour (0xff1d1b19), Colour (0xff262320), Colour (0xff131211), Colour (0xfff4f1ee), Colour (0xff958b81), Colour (0xff302c28), true, Colour (0xffffcf8a), Colour (0xfff2a33a), Colour (0xffa86212), Colour (0xff1a1714), Colour (0xff111010) },
        { "sky",    Colour (0xff1a1c1e), Colour (0xff222528), Colour (0xff111213), Colour (0xfff0f3f5), Colour (0xff87909a), Colour (0xff2a2e33), true, Colour (0xff8fd8ff), Colour (0xff35a8e6), Colour (0xff17699a), Colour (0xff14181b), Colour (0xff0f1112) },
    };
    return themes[jlimit (0, numThemes - 1, i)];
}

//==============================================================================
// Look and feel

LivePlanetLookAndFeel::LivePlanetLookAndFeel()
{
    setColour (TextEditor::highlightColourId, Colours::black.withAlpha (0.12f));
    setDefaultSansSerifTypeface (font (14.0f).getTypefacePtr());
    applyThemeColours();
}

// labels and drop-downs (the settings window's): the plugin's own font
// (only inside the standalone's Audio/MIDI settings: everything else keeps the fonts it sets itself)
static bool inAudioSettings (Component& c)
{
    for (auto* p = c.getParentComponent(); p != nullptr; p = p->getParentComponent())
        if (String (typeid (*p).name()).contains ("AudioDeviceSelector")) return true;
    return false;
}
Font LivePlanetLookAndFeel::getLabelFont (Label& l) { return inAudioSettings (l) ? lp::ui::font (14.0f) : LookAndFeel_V4::getLabelFont (l); }
Font LivePlanetLookAndFeel::getComboBoxFont (ComboBox& c) { return inAudioSettings (c) ? lp::ui::font (14.0f) : LookAndFeel_V4::getComboBoxFont (c); }

void LivePlanetLookAndFeel::applyThemeColours()
{
    // (a touch see-through: the menu windows then get their rounded corners)
    const auto panel = th->bodyDark.interpolatedWith (Colour (0xff101014), 0.55f);
    setColour (PopupMenu::backgroundColourId, panel.withAlpha (0.985f));
    setColour (PopupMenu::textColourId, th->ink);
    setColour (PopupMenu::headerTextColourId, th->grassLight);
    setColour (PopupMenu::highlightedBackgroundColourId, th->grass.withAlpha (0.28f));
    setColour (PopupMenu::highlightedTextColourId, Colours::white);
    setColour (AlertWindow::backgroundColourId, panel.withAlpha (0.985f));
    setColour (AlertWindow::textColourId, th->ink);
    setColour (AlertWindow::outlineColourId, th->line);
    // text boxes inside dialogs (the license code): a visible field
    setColour (TextEditor::backgroundColourId, Colours::black.withAlpha (0.35f));
    setColour (TextEditor::textColourId, th->ink);
    setColour (TextEditor::outlineColourId, th->line.brighter (0.4f));
    setColour (TextEditor::focusedOutlineColourId, th->grass);
    setColour (CaretComponent::caretColourId, th->grass);
    setColour (TooltipWindow::backgroundColourId, panel.withAlpha (0.97f));
    setColour (TooltipWindow::textColourId, th->ink);
    setColour (TooltipWindow::outlineColourId, th->line);
    setColour (BubbleComponent::backgroundColourId, panel);
    setColour (ComboBox::backgroundColourId, Colours::transparentBlack);
    // the standalone app's Audio/MIDI settings window (JUCE's own) in the plugin's look too
    setColour (ResizableWindow::backgroundColourId, th->bodyDark.interpolatedWith (Colour (0xff0d0d11), 0.6f));
    setColour (DocumentWindow::textColourId, th->ink);
    setColour (ComboBox::outlineColourId, th->line.brighter (0.3f));
    setColour (ComboBox::textColourId, th->ink);
    setColour (ComboBox::arrowColourId, th->grassLight);
    setColour (ComboBox::focusedOutlineColourId, th->grass);
    setColour (Label::textColourId, th->ink.withAlpha (0.75f));
    setColour (ListBox::backgroundColourId, Colours::black.withAlpha (0.28f));
    setColour (ListBox::outlineColourId, th->line);
    setColour (ListBox::textColourId, th->ink);
    setColour (ToggleButton::textColourId, th->ink);
    setColour (ToggleButton::tickColourId, Colours::white);
    setColour (ToggleButton::tickDisabledColourId, th->line.brighter (0.6f));
    setColour (ScrollBar::thumbColourId, th->grass.withAlpha (0.5f));
    setColour (TextButton::buttonColourId, th->bodyDark.brighter (0.15f));
    setColour (TextButton::textColourOffId, th->ink);
}

void LivePlanetLookAndFeel::drawCallOutBoxBackground (CallOutBox&, Graphics& g, const Path& path, Image&)
{
    g.setColour (Colours::black.withAlpha (0.35f));
    g.fillPath (path, AffineTransform::translation (0.0f, 3.0f));
    g.setColour (th->bodyDark.interpolatedWith (Colour (0xff101014), 0.55f));
    g.fillPath (path);
    g.setColour (Colours::white.withAlpha (0.12f));
    g.strokePath (path, PathStrokeType (1.0f));
}

void LivePlanetLookAndFeel::drawPopupMenuBackground (Graphics& g, int w, int h)
{
    auto r = Rectangle<float> (0.0f, 0.0f, (float) w, (float) h);
    g.setColour (findColour (PopupMenu::backgroundColourId));
    g.fillRoundedRectangle (r.reduced (0.5f), 10.0f);
    g.setColour (Colours::white.withAlpha (0.09f));
    g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);
}

Font LivePlanetLookAndFeel::getPopupMenuFont() { return font (13.5f, lp::ui::Medium); }

void LivePlanetLookAndFeel::getIdealPopupMenuItemSize (const String& text, bool isSeparator, int, int& idealWidth, int& idealHeight)
{
    if (isSeparator) { idealWidth = 50; idealHeight = 11; return; }
    idealHeight = 26;
    idealWidth = (int) GlyphArrangement::getStringWidth (getPopupMenuFont(), text) + 52;
}

void LivePlanetLookAndFeel::drawPopupMenuItem (Graphics& g, const Rectangle<int>& area, bool isSeparator, bool isActive, bool isHighlighted,
                                               bool isTicked, bool hasSubMenu, const String& text, const String& shortcutKeyText,
                                               const Drawable*, const Colour* textColour)
{
    auto r = area.toFloat().reduced (5.0f, 1.0f);
    if (isSeparator)
    {
        g.setColour (Colours::white.withAlpha (0.08f));
        g.fillRect (r.getX() + 6.0f, r.getCentreY(), r.getWidth() - 12.0f, 1.0f);
        return;
    }
    if (isHighlighted && isActive)
    {
        g.setColour (findColour (PopupMenu::highlightedBackgroundColourId));
        g.fillRoundedRectangle (r, 7.0f);
    }
    auto tr = r.reduced (12.0f, 0.0f);
    if (isTicked)
    {
        // a dot in the accent colour for the one that's on
        g.setColour (th->grassLight);
        g.fillEllipse (tr.getX(), r.getCentreY() - 3.0f, 6.0f, 6.0f);
    }
    tr.removeFromLeft (14.0f);
    if (hasSubMenu)
    {
        Path arrow;
        const float x = r.getRight() - 14.0f, y = r.getCentreY();
        arrow.startNewSubPath (x - 3.0f, y - 4.0f); arrow.lineTo (x + 1.0f, y); arrow.lineTo (x - 3.0f, y + 4.0f);
        g.setColour (th->inkDim);
        g.strokePath (arrow, PathStrokeType (1.4f));
        tr.removeFromRight (16.0f);
    }
    g.setFont (getPopupMenuFont());
    const auto base = textColour != nullptr ? *textColour : th->ink;
    g.setColour (! isActive ? th->inkDim.withAlpha (0.5f) : (isHighlighted ? Colours::white : base));
    g.drawFittedText (text, tr.toNearestInt(), Justification::centredLeft, 1);
    if (shortcutKeyText.isNotEmpty())
    {
        g.setFont (lp::ui::mono (10.0f, false, 0.1f));
        g.setColour (th->inkDim);
        g.drawText (shortcutKeyText, tr, Justification::centredRight, true);
    }
}

void LivePlanetLookAndFeel::drawPopupMenuSectionHeader (Graphics& g, const Rectangle<int>& area, const String& name)
{
    g.setFont (lp::ui::mono (9.5f, true, 0.16f));
    g.setColour (th->grassLight);
    g.drawFittedText (name.toUpperCase(), area.reduced (17, 0).withTrimmedTop (6), Justification::centredLeft, 1);
}

void LivePlanetLookAndFeel::fillTextEditorBackground (Graphics& g, int w, int h, TextEditor& ed)
{
    const auto c = ed.findColour (TextEditor::backgroundColourId);
    if (c.getAlpha() == 0) return;
    g.setColour (c);
    g.fillRoundedRectangle (Rectangle<float> (0.0f, 0.0f, (float) w, (float) h), 12.0f);
}

void LivePlanetLookAndFeel::drawTextEditorOutline (Graphics& g, int w, int h, TextEditor& ed)
{
    const auto c = ed.findColour (ed.hasKeyboardFocus (true) ? TextEditor::focusedOutlineColourId : TextEditor::outlineColourId);
    if (c.getAlpha() == 0 || ! ed.isEnabled()) return;
    g.setColour (c);
    g.drawRoundedRectangle (Rectangle<float> (0.5f, 0.5f, (float) w - 1.0f, (float) h - 1.0f), 12.0f, ed.hasKeyboardFocus (true) ? 1.5f : 1.0f);
}

Font LivePlanetLookAndFeel::getAlertWindowTitleFont() { return font (19.0f, lp::ui::Medium, -0.01f); }
Font LivePlanetLookAndFeel::getAlertWindowMessageFont() { return font (14.0f); }
Font LivePlanetLookAndFeel::getAlertWindowFont() { return font (13.5f); }

void LivePlanetLookAndFeel::drawAlertBox (Graphics& g, AlertWindow& alert, const Rectangle<int>& textArea, TextLayout& layout)
{
    auto r = alert.getLocalBounds().toFloat();
    g.setColour (findColour (AlertWindow::backgroundColourId));
    g.fillRoundedRectangle (r.reduced (0.5f), 14.0f);
    g.setColour (Colours::white.withAlpha (0.1f));
    g.drawRoundedRectangle (r.reduced (0.5f), 14.0f, 1.0f);
    // a thin accent line along the top, and no big icon: just the words
    g.setColour (th->grass);
    g.fillRoundedRectangle (Rectangle<float> (r.getX() + 24.0f, r.getY() + 1.0f, 36.0f, 3.0f), 1.5f);
    layout.draw (g, textArea.withX (24).withWidth (alert.getWidth() - 48).toFloat());
}

void LivePlanetLookAndFeel::drawTooltip (Graphics& g, const String& text, int width, int height)
{
    auto r = Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);
    g.setColour (findColour (TooltipWindow::backgroundColourId));
    g.fillRoundedRectangle (r.reduced (0.5f), 7.0f);
    g.setColour (Colours::white.withAlpha (0.1f));
    g.drawRoundedRectangle (r.reduced (0.5f), 7.0f, 1.0f);
    AttributedString s;
    s.setJustification (Justification::centredLeft);
    s.append (text, font (12.5f), th->ink);
    TextLayout tl;
    tl.createLayoutWithBalancedLineLengths (s, (float) width - 24.0f);
    tl.draw (g, r.reduced (12.0f, 8.0f));
}

Rectangle<int> LivePlanetLookAndFeel::getTooltipBounds (const String& tipText, Point<int> screenPos, Rectangle<int> parentArea)
{
    AttributedString s;
    s.append (tipText, font (12.5f));
    TextLayout tl;
    tl.createLayoutWithBalancedLineLengths (s, 340.0f);
    const int w = (int) tl.getWidth() + 26, h = (int) tl.getHeight() + 18;
    return Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 16,
                           screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 8) : screenPos.y + 20, w, h)
        .constrainedWithin (parentArea);
}

void LivePlanetLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos,
                                              float start, float end, Slider& s)
{
    // a flat dark dial: a thin track, the value as a bright arc, the number in the middle, the name underneath
    const int size = (int) s.getProperties().getWithDefault ("size", 60);
    const float alpha = s.isEnabled() ? 1.0f : 0.35f;
    const bool small = size < 50;
    auto area = Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    const float labelH = small ? 30.0f : 20.0f;
    const float r = std::min ((float) size * 0.5f, std::min (area.getWidth() * 0.5f - 4.0f, (area.getHeight() - labelH - 10.0f) * 0.5f));
    const float blockH = r * 2.0f + 10.0f + labelH;
    const Point<float> c (area.getCentreX(), area.getCentreY() - blockH * 0.5f + r);
    const float angle = start + pos * (end - start);

    g.setColour (th->bodyLight.withMultipliedAlpha (alpha));
    g.fillEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);
    g.setColour (th->line.withMultipliedAlpha (alpha));
    g.drawEllipse (c.x - r + 0.5f, c.y - r + 0.5f, r * 2.0f - 1.0f, r * 2.0f - 1.0f, 1.0f);
    const float ra = r * 0.78f, aw = std::max (2.0f, r * 0.09f);
    Path track;
    track.addCentredArc (c.x, c.y, ra, ra, 0, start, end, true);
    g.setColour (Colours::white.withAlpha (0.08f * alpha));
    g.strokePath (track, PathStrokeType (aw, PathStrokeType::curved, PathStrokeType::rounded));
    if (pos > 0.002f)
    {
        Path arc;
        arc.addCentredArc (c.x, c.y, ra, ra, 0, start, angle, true);
        g.setColour (th->grass.withMultipliedAlpha (alpha));
        g.strokePath (arc, PathStrokeType (aw, PathStrokeType::curved, PathStrokeType::rounded));
    }
    const Point<float> tip (c.x + ra * std::sin (angle), c.y - ra * std::cos (angle));
    g.setColour (Colours::white.withAlpha (alpha));
    g.fillEllipse (tip.x - aw * 0.75f, tip.y - aw * 0.75f, aw * 1.5f, aw * 1.5f);
    if (! small)
    {
        auto txt = s.getTextFromValue (s.getValue()).upToFirstOccurrenceOf (" ", false, false);
        g.setColour (th->ink.withMultipliedAlpha (alpha));
        g.setFont (lp::ui::mono (r * (size > 90 ? 0.34f : 0.36f)));
        g.drawText (txt, Rectangle<float> (c.x - r, c.y - r * 0.3f, r * 2.0f, r * 0.6f), Justification::centred, false);
    }

    auto labels = Rectangle<float> (area.getX(), c.y + r + 8.0f, area.getWidth(), labelH + 14.0f);
    g.setFont (lp::ui::mono (10.5f, false, 0.12f));
    g.setColour (th->inkDim.withMultipliedAlpha (alpha));
    g.drawText (s.getName().toUpperCase(), labels.removeFromTop (14.0f), Justification::centred, false);
    if (! small)
    {
        g.setFont (font (12.0f, lp::ui::Medium));
        g.setColour (th->ink.withMultipliedAlpha (alpha * 0.75f));
        g.drawText (s.getTextFromValue (s.getValue()).toLowerCase(), labels.removeFromTop (16.0f), Justification::centred, false);
    }
    if (small)
    {
        g.setFont (lp::ui::mono (10.5f));
        g.setColour (th->ink.withMultipliedAlpha (alpha * 0.8f));
        g.drawText (s.getTextFromValue (s.getValue()), labels, Justification::centredTop, false);
    }
}

void LivePlanetLookAndFeel::drawButtonBackground (Graphics& g, Button& b, const Colour&, bool over, bool down)
{
    // the main action is a solid accent pill; the rest are quiet dark pills
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const bool primary = (bool) b.getProperties().getWithDefault ("primary", false);
    const float rad = r.getHeight() * 0.5f;
    if ((bool) b.getProperties().getWithDefault ("ai", false))
    {
        // sleek: dark glass with a fine edge that fades from the theme colour into a soft rose; a touch lighter on hover
        const float a = b.isEnabled() ? 1.0f : 0.45f;
        auto body = r.reduced (1.5f);
        const float rr = body.getHeight() * 0.5f;
        g.setColour (Colours::white.withAlpha (a * (down ? 0.05f : over ? 0.10f : 0.06f)));
        g.fillRoundedRectangle (body, rr);
        ColourGradient sheen (Colours::white.withAlpha (0.07f * a), 0.0f, body.getY(), Colours::white.withAlpha (0.0f), 0.0f, body.getCentreY(), false);
        g.setGradientFill (sheen);
        g.fillRoundedRectangle (body.reduced (1.0f), rr - 1.0f);
        ColourGradient edge (th->grassLight.withAlpha ((over ? 0.95f : 0.7f) * a), body.getX(), body.getCentreY(),
                             Colour (0xfff08aa8).withAlpha ((over ? 0.85f : 0.55f) * a), body.getRight(), body.getCentreY(), false);
        g.setGradientFill (edge);
        g.drawRoundedRectangle (body.reduced (0.5f), rr, 1.2f);
        return;
    }
    if ((bool) b.getProperties().getWithDefault ("glass", false))
    {
        // on glass: a clear pill with a fine light rim (faint when it can't be used)
        const float en = b.isEnabled() ? 1.0f : 0.35f;
        if (primary)
        {
            // picked: the theme colour, softly, under the glass
            g.setColour (th->grass.withAlpha (en * (over ? 0.85f : 0.7f)));
            g.fillRoundedRectangle (r, rad);
            g.setColour (Colours::white.withAlpha (0.25f * en));
            g.drawRoundedRectangle (r.reduced (0.5f), rad, 1.0f);
            return;
        }
        g.setColour (Colours::white.withAlpha (en * (down ? 0.04f : over ? 0.11f : 0.06f)));
        g.fillRoundedRectangle (r, rad);
        g.setColour (Colours::white.withAlpha (en * (over ? 0.22f : 0.13f)));
        g.drawRoundedRectangle (r.reduced (0.5f), rad, 1.0f);
        return;
    }
    if (primary)
    {
        // the main action: a solid pill in the theme colour (paler when it can't be used yet)
        g.setColour ((b.isEnabled() ? th->grass : th->grass.withSaturation (0.2f)).brighter (over ? 0.12f : 0.0f).darker (down ? 0.15f : 0.0f));
        g.fillRoundedRectangle (r, rad);
        return;
    }
    g.setColour (th->bodyLight.brighter (over ? 0.08f : 0.0f).darker (down ? 0.1f : 0.0f));
    g.fillRoundedRectangle (r, rad);
    if (b.getToggleState()) // switched on (mute, solo, print fx, your rating): tinted
    {
        const bool warn = (bool) b.getProperties().getWithDefault ("warn", false);
        g.setColour ((warn ? Colour (0xfff2a33a) : th->grass).withAlpha (0.4f));
        g.fillRoundedRectangle (r, rad);
    }
    g.setColour (th->line);
    g.drawRoundedRectangle (r.reduced (0.5f), rad, 1.0f);
}

void LivePlanetLookAndFeel::drawButtonText (Graphics& g, TextButton& b, bool, bool)
{
    const bool primary = (bool) b.getProperties().getWithDefault ("primary", false);
    if ((bool) b.getProperties().getWithDefault ("ai", false))
    {
        // a sparkle, then the words
        const auto f = font (b.getHeight() >= 34 ? 15.0f : 13.5f, lp::ui::Medium);
        const float tw = GlyphArrangement::getStringWidth (f, b.getButtonText());
        const float s = f.getHeight() * 0.36f, gap = 7.0f;
        const float x0 = ((float) b.getWidth() - (tw + 2.0f * s + gap)) * 0.5f, cy = (float) b.getHeight() * 0.5f;
        Path star;
        const float cx = x0 + s;
        star.startNewSubPath (cx, cy - s);
        star.quadraticTo (cx, cy, cx + s, cy);
        star.quadraticTo (cx, cy, cx, cy + s);
        star.quadraticTo (cx, cy, cx - s, cy);
        star.quadraticTo (cx, cy, cx, cy - s);
        star.closeSubPath();
        g.setGradientFill (ColourGradient (th->grassLight, cx - s, cy - s, Colour (0xfff08aa8), cx + s, cy + s, false));
        g.fillPath (star);
        g.fillEllipse (cx + s * 0.9f, cy - s * 1.15f, s * 0.45f, s * 0.45f);
        g.setColour (Colours::white.withAlpha (b.isEnabled() ? 0.95f : 0.5f));
        g.setFont (f);
        g.drawText (b.getButtonText(), Rectangle<float> (x0 + 2.0f * s + gap, 0.0f, tw + 4.0f, (float) b.getHeight()), Justification::centredLeft, false);
        return;
    }
    g.setFont (font (primary ? 15.0f : 13.0f, lp::ui::Medium));
    g.setColour ((primary ? Colours::white : th->ink).withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.35f));
    g.drawText (b.getButtonText(), b.getLocalBounds(), Justification::centred, false);
}

Font LivePlanetLookAndFeel::getTextButtonFont (TextButton&, int) { return font (13.0f, lp::ui::Medium); }

void LivePlanetLookAndFeel::drawToggleButton (Graphics& g, ToggleButton& b, bool over, bool)
{
    // a settings row: the name on the left, a switch on the right
    auto r = b.getLocalBounds().toFloat();
    const float alpha = b.isEnabled() ? 1.0f : 0.4f;
    const bool on = b.getToggleState();
    auto sw = r.removeFromRight (34.0f).withSizeKeepingCentre (30.0f, 17.0f);
    g.setColour ((on ? th->grass : Colour (0xff3a3a40)).withMultipliedAlpha (alpha));
    g.fillRoundedRectangle (sw, sw.getHeight() * 0.5f);
    const float d = sw.getHeight() - 4.0f;
    g.setColour (Colours::white.withAlpha (alpha));
    g.fillEllipse (on ? sw.getRight() - d - 2.0f : sw.getX() + 2.0f, sw.getY() + 2.0f, d, d);
    g.setFont (font (13.5f, lp::ui::Medium));
    g.setColour ((over ? th->ink : th->ink.withAlpha (0.88f)).withMultipliedAlpha (alpha));
    g.drawText (b.getButtonText(), r, Justification::centredLeft, false);
}

//==============================================================================
// Small parts

Segmented::Segmented (StringArray names, std::function<int()> get, std::function<void (int)> set, const lp::ui::Theme*& theme)
    : labels (std::move (names)), getter (std::move (get)), setter (std::move (set)), th (theme)
{
}

void Segmented::paint (Graphics& g)
{
    // a pill with the chosen side lit (like "trending | fresh")
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (th->bodyDark);
    g.fillRoundedRectangle (r, 7.0f);
    g.setColour (th->line);
    g.drawRoundedRectangle (r.reduced (0.5f), 7.0f, 1.0f);
    const int sel = getter();
    const int n = labels.size();
    for (int i = 0; i < n; ++i)
    {
        auto half = r.withWidth (r.getWidth() / (float) n).translated ((float) i * r.getWidth() / (float) n, 0.0f).reduced (3.0f);
        if (i == sel)
        {
            g.setColour (Colour (0xffe9e9ec));
            g.fillRoundedRectangle (half, 5.0f);
        }
        g.setFont (font (12.5f, lp::ui::Medium));
        g.setColour (i == sel ? Colour (0xff141416) : th->inkDim);
        g.drawText (labels[i], half, Justification::centred, false);
    }
}

void Segmented::mouseUp (const MouseEvent& e)
{
    setter (jlimit (0, labels.size() - 1, (int) (e.position.x / (float) getWidth() * (float) labels.size())));
    repaint();
}

Rectangle<float> ChoiceChips::chip (int i) const
{
    const auto names = param.getAllValueStrings();
    float x = 64.0f;
    for (int k = 0; k < i; ++k)
        x += GlyphArrangement::getStringWidth (lp::ui::mono (10.5f, false, 0.14f), names[k].toUpperCase()) + 28.0f + 8.0f;
    return { x, 1.0f, GlyphArrangement::getStringWidth (lp::ui::mono (10.5f, false, 0.14f), names[i].toUpperCase()) + 28.0f, (float) getHeight() - 2.0f };
}

void ChoiceChips::paint (Graphics& g)
{
    const auto names = param.getAllValueStrings();
    const int sel = roundToInt (param.getValue() * (float) (names.size() - 1));
    g.setFont (lp::ui::mono (10.5f, false, 0.14f));
    g.setColour (th->inkDim);
    g.drawText ("STYLE", Rectangle<float> (0.0f, 0.0f, 60.0f, (float) getHeight()), Justification::centredLeft, false);
    for (int i = 0; i < names.size(); ++i)
    {
        auto r = chip (i);
        const bool on = i == sel;
        g.setColour (on ? Colour (0xffe9e9ec) : th->bodyLight);
        g.fillRoundedRectangle (r, 6.0f);
        if (! on) { g.setColour (th->line); g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f); }
        g.setColour (on ? Colour (0xff141416) : th->ink.withAlpha (0.8f));
        g.drawText (names[i].toUpperCase(), r, Justification::centred, false);
    }
}

void ChoiceChips::mouseUp (const MouseEvent& e)
{
    const auto names = param.getAllValueStrings();
    for (int i = 0; i < names.size(); ++i)
        if (chip (i).contains (e.position))
        {
            param.beginChangeGesture();
            param.setValueNotifyingHost ((float) i / (float) std::max (1, names.size() - 1));
            param.endChangeGesture();
            repaint();
        }
}

Stepper::Stepper (RangedAudioParameter& p, const lp::ui::Theme*& theme) : param (p), th (theme) {}

void Stepper::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    recess (g, r, 7.0f, *th);
    g.setFont (lp::ui::mono (11.0f));
    g.setColour (th->inkDim);
    g.drawText ("<", r.removeFromLeft (24.0f), Justification::centred, false);
    g.drawText (">", r.removeFromRight (24.0f), Justification::centred, false);
    g.setColour (th->ink);
    g.drawText (param.getCurrentValueAsText().toUpperCase(), r, Justification::centred, false);
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
        g.setColour (lit ? Colour (0xffe9e9ec) : th->bodyLight);
        g.fillRoundedRectangle (r, 6.0f);
        if (! lit) { g.setColour (picked ? th->grass.withAlpha (0.7f) : th->line); g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f); }
        const auto col = i == 0 ? th->grass : hsvCol (hueFor (i), 0.55f, 1.0f);
        g.setColour (col.withAlpha (lit || picked ? 1.0f : 0.45f));
        g.fillEllipse (r.getX() + 7.0f, r.getCentreY() - 2.5f, 5.0f, 5.0f);
        juce::ignoreUnused (t);
        g.setFont (lp::ui::mono (9.5f, false, 0.06f));
        g.setColour (lit ? Colour (0xff141416) : th->ink.withAlpha (picked ? 1.0f : 0.7f));
        g.drawFittedText (nameFor (i).toUpperCase(), r.withTrimmedLeft (14.0f).withTrimmedRight (2.0f).toNearestInt(), Justification::centred, 1, 0.75f);
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
    const auto& th = lp::ui::theme (proc.themeIndex.load());
    g.setColour (th.grass.brighter (over ? 0.15f : 0.0f));
    g.fillEllipse (r);
    const auto c = r.getCentre();
    g.setColour (Colours::white);
    if (on)
        g.fillRoundedRectangle (c.x - 6.0f, c.y - 6.0f, 12.0f, 12.0f, 2.0f);
    else
    {
        Path tri;
        tri.addTriangle (c.x - 4.5f, c.y - 7.5f, c.x - 4.5f, c.y + 7.5f, c.x + 8.0f, c.y);
        g.fillPath (tri);
    }
}

void PlayButton::toggle()
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
    recess (g, r, r.getHeight() * 0.5f, *th);
    g.setFont (lp::ui::mono (11.0f));
    g.setColour (th->inkDim);
    g.drawText ("<", r.removeFromLeft (28.0f), Justification::centred, false);
    g.drawText (">", r.removeFromRight (28.0f), Justification::centred, false);
    const int i = proc.getCurrentProgram();
    g.drawText (String (i + 1).paddedLeft ('0', 2), r.removeFromLeft (26.0f), Justification::centredLeft, false);
    g.setFont (font (13.0f, lp::ui::Medium));
    g.setColour (th->ink);
    g.drawText (proc.getProgramName (i), r, Justification::centredLeft, false);
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

// the song's key, like Ableton's: a note and Major / Minor ("Auto" follows your reference)
static void setUpKeyBoxes (LivePlanetProcessor& p, ComboBox& key, ComboBox& mode,
                           std::unique_ptr<AudioProcessorValueTreeState::ComboBoxAttachment>& keyAtt,
                           std::unique_ptr<AudioProcessorValueTreeState::ComboBoxAttachment>& modeAtt)
{
    key.addItemList (StringArray { "Auto key", "C", "C#/Db", "D", "D#/Eb", "E", "F", "F#/Gb", "G", "G#/Ab", "A", "A#/Bb", "B" }, 1);
    mode.addItemList (StringArray { "Auto", "Major", "Minor" }, 1);
    keyAtt = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (p.params, "songkey", key);
    modeAtt = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (p.params, "songmode", mode);
    for (auto* b : { &key, &mode })
    {
        b->setWantsKeyboardFocus (false);
        b->setMouseClickGrabsKeyboardFocus (false);
        auto prev = b->onChange;
        b->onChange = [&p, prev] { if (prev) prev(); MessageManager::callAsync ([&p] { p.applyChosenKey(); }); };
    }
}

SongPanel::SongPanel (LivePlanetProcessor& p, const lp::ui::Theme*& theme)
    : proc (p), th (theme),
      length ({ "Full song", "Sketch", "3 takes" }, [this] { return takes3 ? 2 : (full ? 0 : 1); },
              [this] (int i) { full = i != 1; takes3 = i == 2; }, theme),
      soundsFrom ({ "My samples", "Mine + built-in", "Built-in" },
                  [this]
                  {
                      const bool lib = proc.params.getRawParameterValue ("usesamples")->load() > 0.5f;
                      const bool only = proc.params.getRawParameterValue ("samplesonly")->load() > 0.5f;
                      return lib && only ? 0 : (lib ? 1 : (only ? 0 : 2));
                  },
                  [this] (int i)
                  {
                      auto set = [this] (const char* id, bool on) { if (auto* q = proc.params.getParameter (id)) q->setValueNotifyingHost (on ? 1.0f : 0.0f); };
                      set ("usesamples", i <= 1);
                      set ("samplesonly", i == 0);
                  }, theme)
{
    newKey.setToggleState (false, dontSendNotification); // stay in your song's key unless you ask
    addAndMakeVisible (newKey);
    addAndMakeVisible (length);
    style = std::make_unique<ChoiceChips> (*p.params.getParameter ("songstyle"), theme);
    newKey.setButtonText ("New key (move away from the reference)");
    dawTempo.setButtonText ("Follow my DAW's tempo");
    useSamples.setButtonText ("Use my sample library");
    samplesOnly.setButtonText ("Samples only");
    playWith.setButtonText ("Play along with my DAW");
    write.getProperties().set ("primary", true);
    setUpKeyBoxes (p, keyBox, modeBox, keyAtt, modeAtt);
    addAndMakeVisible (keyBox);
    addAndMakeVisible (modeBox);
    addAndMakeVisible (*style);
    addChildComponent (useSamples);  // (the two switches live on behind "Sounds")
    addAndMakeVisible (soundsFrom);
    soundsFrom.setTooltip ("Where the song's sounds come from. My samples: only loops and sounds from your own sample folders. "
                           "Mine + built-in: your samples, filled out with the plugin's built-in instruments. Built-in: only the plugin's own instruments.");
    addAndMakeVisible (playWith);
    useSamplesAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (p.params, "usesamples", useSamples);
    playWithAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (p.params, "songplay", playWith);
    addChildComponent (samplesOnly);
    samplesOnlyAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (p.params, "samplesonly", samplesOnly);
    addAndMakeVisible (dawTempo);
    for (auto* h : { &hTempo, &hSounds, &hPlay, &hNewKey, &hLength }) addAndMakeVisible (*h);
    dawTempo.setWantsKeyboardFocus (false);
    dawTempo.setTooltip ("write the song at your project's tempo so every loop lines up");
    dawTempoAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (p.params, "dawtempo", dawTempo);
    write.onClick = [this]
    {
        if (! lp::license::canWrite()) { showActivate ("ended"); return; }
        proc.songPrompt = promptBox.getText();
        proc.songPromptIsAuto = autoDescription.isNotEmpty() && promptBox.getText().trim() == autoDescription;
        proc.makeSong (full, newKey.getToggleState(), takes3 ? 3 : 1);
        repaint();
    };
    addAndMakeVisible (write);

    promptBox.setTextToShowWhenEmpty ("describe it: dark trap 140 in f minor with piano + flute", lp::ui::theme (0).inkDim);
    promptBox.setFont (font (14.5f));
    promptBox.setIndents (18, 13);
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
    hero = Rectangle<int> (0, 0, r.getWidth() - 364, 290);
    settings = Rectangle<int> (hero.getRight() + 16, 0, r.getWidth() - hero.getRight() - 16, 290);
    // the describe box and the big button along the bottom of the hero
    auto bottom = hero.reduced (24, 22).removeFromBottom (48);
    write.setBounds (bottom.removeFromRight (156));
    bottom.removeFromRight (10);
    promptBox.setBounds (bottom);
    // the settings, one row each
    auto s = settings.reduced (22, 18);
    s.removeFromTop (40);
    {
        auto kr = s.removeFromTop (30);
        kr.removeFromLeft (64); // "Key"
        modeBox.setBounds (kr.removeFromRight (92));
        kr.removeFromRight (8);
        keyBox.setBounds (kr);
    }
    s.removeFromTop (6);
    dawTempo.setBounds (s.removeFromTop (25));
    s.removeFromTop (3);
    {
        auto sr = s.removeFromTop (28);
        sr.removeFromLeft (64); // "Sounds"
        soundsFrom.setBounds (sr);
    }
    s.removeFromTop (4);
    for (auto* t : { &playWith, &newKey })
        t->setBounds (s.removeFromTop (25));
    s.removeFromTop (6);
    length.setBounds (s.removeFromTop (28));
    // the "?"s: right after each setting's name
    auto after = [] (juce::Component& c, const String& text) { return c.getX() + (int) GlyphArrangement::getStringWidth (font (13.5f, lp::ui::Medium), text) + 8; };
    hTempo.setTopLeftPosition (after (dawTempo, dawTempo.getButtonText()), dawTempo.getY() + (dawTempo.getHeight() - 16) / 2);
    hPlay.setTopLeftPosition (after (playWith, playWith.getButtonText()), playWith.getY() + (playWith.getHeight() - 16) / 2);
    hNewKey.setTopLeftPosition (after (newKey, newKey.getButtonText()), newKey.getY() + (newKey.getHeight() - 16) / 2);
    hSounds.setTopLeftPosition (soundsFrom.getX() - 64 + (int) GlyphArrangement::getStringWidth (font (13.5f, lp::ui::Medium), "Sounds") + 6, soundsFrom.getY() + 6);
    hLength.setTopLeftPosition (length.getRight() - 8, length.getY() - 8);
    style->setBounds (0, hero.getBottom() + 16, r.getWidth(), 34);
}

void SongPanel::paint (Graphics& g)
{
    const auto st = proc.songJob.getStatus();
    const auto& t = *th;

    // ---- hero: a deep glow in the accent colour, like cover art
    {
        auto h = hero.toFloat();
        Graphics::ScopedSaveState ss (g);
        Path clip;
        clip.addRoundedRectangle (h, 14.0f);
        g.reduceClipRegion (clip);
        g.setColour (t.bodyDark);
        g.fillRect (h);
        ColourGradient glow (t.grass.withAlpha (0.55f), h.getX() + h.getWidth() * 0.78f, h.getY() + h.getHeight() * 0.18f,
                             t.grass.withAlpha (0.0f), h.getX() + h.getWidth() * 0.78f + 420.0f, h.getY() + h.getHeight() * 0.18f, true);
        g.setGradientFill (glow);
        g.fillRect (h);
        ColourGradient glow2 (t.grassLight.withAlpha (0.22f), h.getX() + h.getWidth() * 0.98f, h.getBottom(),
                              t.grassLight.withAlpha (0.0f), h.getX() + h.getWidth() * 0.98f + 300.0f, h.getBottom(), true);
        g.setGradientFill (glow2);
        g.fillRect (h);
        ColourGradient fade (Colours::black.withAlpha (0.55f), h.getX(), 0, Colours::black.withAlpha (0.0f), h.getX() + h.getWidth() * 0.65f, 0, false);
        g.setGradientFill (fade);
        g.fillRect (h);
        g.setColour (Colours::white.withAlpha (0.025f));
        for (float y = h.getY(); y < h.getBottom(); y += 3.0f) g.drawHorizontalLine ((int) y, h.getX(), h.getRight());
    }
    g.setColour (t.line);
    g.drawRoundedRectangle (hero.toFloat().reduced (0.5f), 14.0f, 1.0f);

    auto in = hero.toFloat().reduced (26.0f, 22.0f);
    const bool busy = st.stage == SongJob::Stage::Writing || st.stage == SongJob::Stage::Rendering || st.stage == SongJob::Stage::Listening;
    auto tag = [&] (const String& text, Rectangle<float> at)
    {
        g.setFont (lp::ui::mono (10.0f, false, 0.14f));
        const float w = GlyphArrangement::getStringWidth (g.getCurrentFont(), text) + 22.0f;
        auto c = at.withWidth (w);
        g.setColour (Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (c, c.getHeight() * 0.5f);
        g.setColour (Colours::white.withAlpha (0.85f));
        g.drawText (text, c, Justification::centred, false);
    };
    if (st.haveReference && st.stage != SongJob::Stage::Listening)
    {
        const auto& pr = st.profile;
        tag ("REFERENCE", in.removeFromTop (22.0f));
        in.removeFromTop (14.0f);
        g.setFont (font (34.0f, lp::ui::Medium, -0.02f));
        g.setColour (t.ink);
        g.drawText (st.referenceName, in.removeFromTop (44.0f).withTrimmedRight (30.0f), Justification::centredLeft, true);
        g.setFont (lp::ui::mono (11.5f, false, 0.12f));
        g.setColour (Colours::white.withAlpha (0.65f));
        g.drawText (String (roundToInt (pr.bpm)) + " BPM  \xc2\xb7  " + String (lp::noteName (pr.key)) + (pr.minor ? " MINOR" : " MAJOR")
                        + "  \xc2\xb7  " + String ((int) pr.sections.size()) + " SECTIONS  \xc2\xb7  " + timeText (pr.lengthSeconds),
                    in.removeFromTop (22.0f), Justification::centredLeft, false);
        auto x = Rectangle<float> ((float) hero.getRight() - 46.0f, (float) hero.getY() + 18.0f, 26.0f, 26.0f);
        g.setColour (Colours::black.withAlpha (0.35f));
        g.fillEllipse (x);
        g.setColour (Colours::white.withAlpha (0.8f));
        g.drawLine (x.getX() + 9.0f, x.getY() + 9.0f, x.getRight() - 9.0f, x.getBottom() - 9.0f, 1.4f);
        g.drawLine (x.getRight() - 9.0f, x.getY() + 9.0f, x.getX() + 9.0f, x.getBottom() - 9.0f, 1.4f);
    }
    else
    {
        tag (busy ? "LISTENING" : "START HERE", in.removeFromTop (22.0f));
        in.removeFromTop (14.0f);
        g.setFont (font (34.0f, lp::ui::Medium, -0.02f));
        g.setColour (t.ink);
        g.drawText ("Drop a song you love", in.removeFromTop (44.0f), Justification::centredLeft, false);
        g.setFont (font (14.5f));
        g.setColour (Colours::white.withAlpha (0.6f));
        g.drawText ("It learns the feel, never the melody. Or just describe the song below.", in.removeFromTop (22.0f), Justification::centredLeft, false);
    }
    // what the stem split heard in the reference: each part's level
    if (st.haveReference && st.profile.hasStems && st.stage != SongJob::Stage::Listening)
    {
        in.removeFromTop (18.0f);
        auto row = in.removeFromTop (34.0f);
        const char* names[] = { "DRUMS", "BASS", "VOCALS", "MUSIC" };
        float top = -100.0f;
        for (float v : st.profile.stemDb) top = std::max (top, v);
        for (int i = 0; i < 4; ++i)
        {
            auto cell = row.removeFromLeft (124.0f);
            g.setFont (lp::ui::mono (9.5f, false, 0.14f));
            g.setColour (Colours::white.withAlpha (0.55f));
            g.drawText (names[i], cell.removeFromTop (14.0f), Justification::centredLeft, false);
            const bool heard = i != 2 || st.profile.vocalsHeard;
            const float lvl = heard ? jlimit (0.05f, 1.0f, 1.0f + (st.profile.stemDb[(size_t) i] - top) / 18.0f) : 0.0f;
            for (int k = 0; k < 10; ++k)
            {
                auto seg = Rectangle<float> (cell.getX() + (float) k * 10.0f, cell.getY() + 6.0f, 7.0f, 10.0f);
                g.setColour ((float) k / 10.0f < lvl ? t.grassLight.withAlpha (0.95f) : Colours::white.withAlpha (0.1f));
                g.fillRoundedRectangle (seg, 1.5f);
            }
        }
    }
    // what's happening: progress, or what the description will make
    in.removeFromTop (14.0f);
    auto line = in.removeFromTop (18.0f);
    const auto understood = lp::song::parsePrompt (promptBox.getText());
    if (busy)
    {
        auto bar = line.removeFromBottom (3.0f).withWidth (260.0f);
        g.setColour (Colours::white.withAlpha (0.12f));
        g.fillRoundedRectangle (bar, 1.5f);
        g.setColour (t.grass);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * st.progress), 1.5f);
        g.setFont (lp::ui::mono (10.5f, false, 0.1f));
        g.setColour (Colours::white.withAlpha (0.75f));
        g.drawText (st.message.toUpperCase(), in.removeFromTop (18.0f), Justification::centredLeft, true);
    }
    else
    {
        String msg = st.message;
        const bool important = msg.contains ("set your project") || msg.contains (". no ") || st.stage == SongJob::Stage::Failed;
        if (understood.any && ! important) msg = "Making: " + understood.summary;
        g.setFont (lp::ui::mono (10.5f, false, 0.1f));
        g.setColour (st.stage == SongJob::Stage::Failed ? Colour (0xffff7a59) : (understood.any && ! important ? t.grassLight : Colours::white.withAlpha (0.6f)));
        g.drawText (msg.toUpperCase(), line, Justification::centredLeft, true);
    }
    // the describe box: a dark pill
    {
        auto pb = promptBox.getBounds().toFloat();
        g.setColour (Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (pb, pb.getHeight() * 0.5f);
        g.setColour (promptBox.hasKeyboardFocus (true) ? t.grass.withAlpha (0.8f) : Colours::white.withAlpha (0.14f));
        g.drawRoundedRectangle (pb.reduced (0.5f), pb.getHeight() * 0.5f, 1.0f);
    }

    // ---- settings card
    {
        auto c = settings.toFloat();
        g.setColour (t.body);
        g.fillRoundedRectangle (c, 12.0f);
        g.setColour (t.line);
        g.drawRoundedRectangle (c.reduced (0.5f), 12.0f, 1.0f);
        auto r = c.reduced (22.0f, 18.0f);
        g.setFont (font (19.0f, lp::ui::Medium, -0.01f));
        g.setColour (t.ink);
        {
            auto title = r.removeFromTop (26.0f);
            g.drawText ("Settings", title, Justification::centredLeft, false);
            // the license (only in builds that need one): trial days left, or activate
            licenseLink = {};
            if (lp::license::required())
            {
                const auto owner = lp::license::activatedTo();
                const int left = lp::license::trialDaysLeft();
                const String txt = owner.isNotEmpty() ? String ("LICENSED") : (left > 0 ? "TRIAL: " + String (left) + " DAYS LEFT" : String ("ACTIVATE"));
                g.setFont (lp::ui::mono (10.0f, false, 0.12f));
                licenseLink = title.removeFromRight (GlyphArrangement::getStringWidth (g.getCurrentFont(), txt) + 6.0f);
                g.setColour (owner.isNotEmpty() ? t.inkDim : Colour (0xfff2a33a));
                g.drawText (txt, licenseLink, Justification::centredRight, false);
                g.setFont (font (19.0f, lp::ui::Medium, -0.01f));
            }
        }
        g.setFont (font (13.5f, lp::ui::Medium));
        g.setColour (t.ink.withAlpha (0.88f));
        g.drawText ("Key", Rectangle<float> (r.getX(), (float) keyBox.getY(), 60.0f, (float) keyBox.getHeight()), Justification::centredLeft, false);
        g.drawText ("Sounds", Rectangle<float> (r.getX(), (float) soundsFrom.getY(), 60.0f, (float) soundsFrom.getHeight()), Justification::centredLeft, false);
        // the library along the bottom
        auto lib = c.reduced (22.0f, 16.0f).removeFromBottom (16.0f);
        g.setFont (lp::ui::mono (10.0f, false, 0.1f));
        const String count = st.scanning ? String ("SCANNING...")
                           : st.listening > 0 ? String (st.librarySounds) + String::fromUTF8 (" SAMPLES \xc2\xb7 LISTENING TO ") + String (st.listening)
                           : st.librarySounds > 0 ? String (st.librarySounds) + " SAMPLES"
                                                  : String ("NO LIBRARY YET");
        // every word measured; SONGS keeps its place at the right end, and if the row is short of room
        // the sample count goes first, then "ADD FOLDER" becomes "+ FOLDER"
        const auto& f = g.getCurrentFont();
        auto w = [&f] (const String& x) { return GlyphArrangement::getStringWidth (f, x); };
        constexpr float gap = 14.0f;
        const String songs ("SONGS");
        songsLink = lib.removeFromRight (w (songs) + 4.0f);
        g.setColour (t.inkDim);
        g.drawText (songs, songsLink, Justification::centredRight, false);
        lib.removeFromRight (gap);
        String folderText ("ADD FOLDER");
        const float links = w ("SCAN") + w (folderText) + w ("SOURCES") + 2.0f * gap;
        const bool showCount = w (count) + gap + links <= lib.getWidth();
        if (! showCount && links > lib.getWidth()) folderText = "+ FOLDER";
        if (showCount)
        {
            g.setColour (t.inkDim);
            g.drawText (count, lib.removeFromLeft (w (count) + gap), Justification::centredLeft, false);
        }
        g.setColour (t.ink);
        scanLink = lib.removeFromLeft (w ("SCAN") + gap);
        g.drawText ("SCAN", scanLink, Justification::centredLeft, false);
        folderLink = lib.removeFromLeft (w (folderText) + gap);
        g.drawText (folderText, folderLink, Justification::centredLeft, false);
        sourcesLink = lib.removeFromLeft (w ("SOURCES") + 4.0f);
        g.setColour (t.grassLight);
        g.drawText ("SOURCES", sourcesLink, Justification::centredLeft, false);

    }
}

void SongPanel::showActivate (const String& message)
{
    // over the whole plugin window
    auto* top = lp::ui::overlayHost (this);
    if (top == nullptr) return;
    SafePointer<SongPanel> self (this);
    licensePanel = std::make_unique<LicensePanel> ([self]
    {
        MessageManager::callAsync ([self] { if (self != nullptr) { self->licensePanel.reset(); self->repaint(); } });
    }, message);
    top->addAndMakeVisible (*licensePanel);
    licensePanel->setBounds (top->getLocalBounds());
    licensePanel->toFront (true);
    licensePanel->focusCode();
}

void SongPanel::refresh()
{
    const auto st = proc.songJob.getStatus();
    // a new reference: describe it in the box (unless you've typed your own words there)
    if (st.haveReference && st.profile.valid && st.referenceName != describedRef && st.stage != SongJob::Stage::Listening)
    {
        describedRef = st.referenceName;
        const auto now = promptBox.getText().trim();
        if (now.isEmpty() || now == autoDescription)
        {
            autoDescription = lp::song::describeReference (st.profile);
            promptBox.setText (autoDescription, false);
            proc.songPrompt = autoDescription;
        }
    }
    const bool busy = st.stage == SongJob::Stage::Writing || st.stage == SongJob::Stage::Rendering || st.stage == SongJob::Stage::Listening;
    if (write.isEnabled() == busy) write.setEnabled (! busy);
    const String text = st.haveSong ? "Write another" : "Write a song";
    if (write.getButtonText() != text) write.setButtonText (text);
    repaint();
}

void SongPanel::mouseUp (const MouseEvent& e)
{
    if (scanLink.contains (e.position)) { proc.rescanLibrary(); repaint(); return; }
    if (sourcesLink.contains (e.position)) { if (onShowSources) onShowSources(); return; }
    if (songsLink.contains (e.position))
    {
        // like a DAW's File menu: your recent songs (newest first), any song folder, and storage
        struct Recent { File folder; Time when; };
        std::vector<Recent> recent;
        for (const auto& f : SongJob::songsFolder().findChildFiles (File::findDirectories, false))
            if (const auto j = f.getChildFile ("song.json"); j.existsAsFile())
                recent.push_back ({ f, j.getLastModificationTime() });
        std::sort (recent.begin(), recent.end(), [] (const Recent& a, const Recent& b) { return a.when > b.when; });
        if (recent.size() > 15) recent.resize (15);

        const auto open = proc.songJob.getStatus();
        const bool busy = open.stage == SongJob::Stage::Listening || open.stage == SongJob::Stage::Writing || open.stage == SongJob::Stage::Rendering;
        const auto today = Time::getCurrentTime();
        PopupMenu recentMenu;
        for (size_t i = 0; i < recent.size(); ++i)
        {
            const auto& r = recent[i];
            const auto days = (int) ((today - r.when).inDays());
            const String when = days < 1 && r.when.getDayOfMonth() == today.getDayOfMonth() ? "today"
                              : days < 2 ? "yesterday" : r.when.formatted ("%b %e").trim();
            const bool isOpen = open.haveSong && open.files.folder == r.folder;
            recentMenu.addItem (PopupMenu::Item (r.folder.getFileName() + "   ·   " + when).setID (100 + (int) i).setTicked (isOpen).setEnabled (! busy && ! isOpen));
        }
        if (recent.empty()) recentMenu.addItem (PopupMenu::Item ("No songs yet").setEnabled (false));

        PopupMenu m;
        m.addSectionHeader ("SONGS");
        m.addSubMenu ("Open Recent Song", recentMenu, ! recent.empty());
        m.addItem (PopupMenu::Item ("Open Song...").setID (2).setEnabled (! busy));
        m.addItem (PopupMenu::Item (open.haveSong ? "Rename \"" + open.song.title + "\"..." : String ("Rename Song...")).setID (3).setEnabled (open.haveSong && ! busy));
        m.addSeparator();
        m.addItem (1, "Storage & cleanup...");
        m.addSeparator();
        m.addItem (PopupMenu::Item (lp::voice::helperCanRunHere() ? "Voice page (experimental)"
                                                                   : "Voice page (experimental, Apple Silicon Macs only)").setID (4).setTicked ((bool) lp::setting ("voiceLab", false)));
        SafePointer<SongPanel> self (this);
        m.showMenuAsync (PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (songsLink.toNearestInt())), [self, recent] (int id)
        {
            if (self == nullptr || id == 0) return;
            if (id >= 100 && id - 100 < (int) recent.size()) { self->proc.songJob.openSong (recent[(size_t) (id - 100)].folder); return; }
            if (id == 2)
            {
                self->chooser = std::make_unique<FileChooser> ("Open a Loopwright song (its folder)", SongJob::songsFolder());
                self->chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories | FileBrowserComponent::canSelectFiles,
                                            [self] (const FileChooser& fc)
                {
                    if (self == nullptr) return;
                    auto f = fc.getResult();
                    if (f.existsAsFile()) f = f.getParentDirectory(); // (song.json or any file inside it)
                    if (f.getChildFile ("song.json").existsAsFile()) self->proc.songJob.openSong (f);
                });
                return;
            }
            if (id == 4)
            {
                // the experimental Voice page: a fifth tab (it needs the voice helper running on this computer)
                lp::setSetting ("voiceLab", ! (bool) lp::setting ("voiceLab", false));
                if (auto* ed = self->findParentComponentOfClass<LivePlanetEditor>()) { ed->setPage (LivePlanetEditor::Write); ed->repaint(); }
                return;
            }
            if (id == 3)
            {
                const auto title = self->proc.songJob.getStatus().song.title;
                auto* w = new AlertWindow ("Rename song", "A new name for \"" + title + "\". Its folder and files get the new name too.",
                                           MessageBoxIconType::NoIcon, self.getComponent());
                w->addTextEditor ("name", title);
                w->addButton ("Rename", 1, KeyPress (KeyPress::returnKey));
                w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
                if (auto* ed = w->getTextEditor ("name")) { ed->setInputRestrictions (60); ed->selectAll(); }
                w->enterModalState (true, ModalCallbackFunction::create ([self, w] (int r)
                {
                    if (r == 1 && self != nullptr) self->proc.songJob.renameSong (w->getTextEditorContents ("name"));
                }), true);
                if (auto* ed = w->getTextEditor ("name")) ed->grabKeyboardFocus();
                return;
            }
            if (id == 1)
            {
                auto* top = lp::ui::overlayHost (self.getComponent());
                if (top == nullptr) return;
                self->storagePanel = std::make_unique<StoragePanel> (self->proc.songJob.getStatus().files.folder, [self]
                {
                    MessageManager::callAsync ([self] { if (self != nullptr) { self->storagePanel.reset(); self->songsSizeAt = 0; self->repaint(); } });
                });
                top->addAndMakeVisible (*self->storagePanel);
                self->storagePanel->setBounds (top->getLocalBounds());
                self->storagePanel->toFront (true);
            }
        });
        return;
    }
    if (licenseLink.contains (e.position))
    {
        showActivate ({});
        return;
    }
    if (folderLink.contains (e.position))
    {
        chooser = std::make_unique<FileChooser> ("Add a sample folder (Splice, packs, your own sounds)", File());
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories,
                              [this] (const FileChooser& fc) { if (fc.getResult().isDirectory()) proc.addLibraryFolder (fc.getResult()); });
        return;
    }
    if (! dropZone().contains (e.getPosition())) return;
    const auto st = proc.songJob.getStatus();
    if (st.haveReference && e.position.x > (float) hero.getRight() - 50.0f && e.position.y < (float) hero.getY() + 48.0f)
    {
        proc.songJob.clearReference();
        repaint();
        return;
    }
    chooser = std::make_unique<FileChooser> ("Choose a song for Loopwright to learn the feel of", File(),
                                             "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                          [this] (const FileChooser& fc)
                          {
                              if (fc.getResult().existsAsFile()) proc.songJob.setReference (fc.getResult());
                          });
}

SongView::SongView (LivePlanetProcessor& p) : proc (p)
{
    for (auto* b : { &addBtn, &dupBtn, &removeBtn, &undoBtn, &rebuildBtn, &layerBtn })
    {
        b->setWantsKeyboardFocus (false);
        b->setMouseClickGrabsKeyboardFocus (false);
        addChildComponent (b);
    }
    layerBtn.getProperties().set ("primary", true);
    layerBtn.setTooltip ("An instrument played by the AI over the sections you picked (shift-click for more): drums, bass, guitar, keys, strings...");
    layerBtn.onClick = [this]
    {
        std::vector<int> pk (picked.begin(), picked.end());
        if (pk.empty() && selected >= 0) pk.push_back (selected);
        if (pk.empty()) for (int i = 0; i < (int) draft.size(); ++i) pk.push_back (i); // (none picked: the whole song)
        if (pk.empty() || dirty || waiting || ! onLayer) return;
        const int a = *std::min_element (pk.begin(), pk.end()), b = *std::max_element (pk.begin(), pk.end());
        onLayer (a, b - a + 1); // (from the first picked to the last, in one stretch)
    };
    rebuildBtn.getProperties().set ("primary", true);
    addBtn.setTooltip ("add a section (a verse, a chorus, a drop...) after the one you picked");
    dupBtn.setTooltip ("play the section you picked twice");
    removeBtn.setTooltip ("take the section you picked out");
    rebuildBtn.setTooltip ("record the song in its new shape: every track follows it");
    addBtn.onClick = [this] { showAddMenu(); };
    dupBtn.onClick = [this]
    {
        if (selected < 0 || selected >= (int) draft.size()) return;
        draft.insert (draft.begin() + selected + 1, draft[(size_t) selected]);
        ++selected; pickOnly (selected); dirty = true; lastShapeEdit = Time::getMillisecondCounter(); repaint();
    };
    removeBtn.onClick = [this]
    {
        // every section picked comes out (one always stays)
        std::vector<int> out (picked.begin(), picked.end());
        if (out.empty() && selected >= 0) out.push_back (selected);
        std::sort (out.rbegin(), out.rend());
        bool any = false;
        for (int i : out)
            if (i >= 0 && i < (int) draft.size() && draft.size() > 1) { draft.erase (draft.begin() + i); any = true; }
        if (! any) return;
        selected = draft.empty() ? -1 : std::min (out.back(), (int) draft.size() - 1);
        pickOnly (selected);
        dirty = true; lastShapeEdit = Time::getMillisecondCounter(); repaint();
    };
    setWantsKeyboardFocus (true);
    undoBtn.onClick = [this] { dirty = false; version = -1; refresh(); };
    rebuildBtn.onClick = [this]
    {
        if (! dirty) return;
        proc.songJob.rearrange (draft);
        dirty = false; waiting = true; repaint();
    };
}

void SongView::refresh()
{
    const auto st = proc.songJob.getStatus();
    // follow the song as it is, unless you're shaping a new structure
    if (st.haveSong && st.version != version && ! dirty)
    {
        version = st.version;
        draft = lp::song::arrange::planOf (st.song);
        waiting = false;
        if (selected >= (int) draft.size()) selected = -1;
        for (auto it = picked.begin(); it != picked.end();) it = *it >= (int) draft.size() ? picked.erase (it) : std::next (it);
    }
    // your new shape is made by itself a moment after you stop changing it (not while you're still dragging)
    if (dirty && ! waiting && drag == Drag::None && Time::getMillisecondCounter() - lastShapeEdit > 1100)
        rebuildBtn.onClick();
    const bool edit = st.haveSong;
    for (auto* b : { &addBtn, &dupBtn, &removeBtn }) if (b->isVisible() != edit) b->setVisible (edit);
    if (layerBtn.isVisible()) layerBtn.setVisible (false); // (the Studio shows its own "AI layer" button, by the title)
    layerBtn.setEnabled ((selected >= 0 || ! picked.empty()) && ! dirty && ! waiting);
    dupBtn.setEnabled (selected >= 0);
    removeBtn.setEnabled ((selected >= 0 || ! picked.empty()) && draft.size() > 1);
    removeBtn.setButtonText (picked.size() > 1 ? "remove " + String ((int) picked.size()) : String ("remove"));
    if (rebuildBtn.isVisible()) rebuildBtn.setVisible (false); // (it rebuilds by itself now)
    if (undoBtn.isVisible() != (edit && dirty)) undoBtn.setVisible (edit && dirty);
    repaint();
}

void SongView::resized()
{
    auto head = getLocalBounds().reduced (8, 4).removeFromTop (24);
    if (ruler)
    {
        // the Studio: the section tools in a row from the left, over the sections they work on
        for (auto* b : { &addBtn, &dupBtn, &removeBtn, &undoBtn, &rebuildBtn })
        {
            const int w = b == &rebuildBtn ? 120 : b == &addBtn ? 92 : b == &layerBtn ? 104 : 84;
            b->setBounds (head.removeFromLeft (w).withSizeKeepingCentre (w, 24));
            head.removeFromLeft (6);
        }
        return;
    }
    for (auto* b : { &rebuildBtn, &undoBtn, &removeBtn, &dupBtn, &addBtn, &layerBtn })
    {
        if (b == &layerBtn && ! ruler) continue;
        const int w = b == &rebuildBtn ? 120 : b == &addBtn ? 92 : b == &layerBtn ? 104 : 80;
        b->setBounds (head.removeFromRight (w).withSizeKeepingCentre (w, 24));
        head.removeFromRight (6);
    }
}

Rectangle<float> SongView::lane() const
{
    auto r = getLocalBounds().toFloat().reduced (8.0f, 4.0f);
    r.removeFromTop (22.0f);
    if (ruler) return r.withTrimmedTop (4.0f); // (every section full height)
    auto l = r.withTrimmedTop (8.0f);
    return l.withTrimmedTop (std::max (0.0f, l.getHeight() - 96.0f));
}

int SongView::totalDraftBars() const
{
    int t = 0;
    for (auto& it : draft) t += it.bars;
    return std::max (1, t);
}

Rectangle<float> SongView::blockRect (int i) const
{
    const auto l = lane();
    int start = 0;
    for (int k = 0; k < i; ++k) start += draft[(size_t) k].bars;
    const float total = (float) totalDraftBars();
    if (ruler && view != nullptr)
    {
        // (zoomed: the bars in view, lined up with the lanes)
        const int tb = totalDraftBars();
        const float x0 = view->xOf (start, l.getX(), l.getWidth(), tb), x1 = view->xOf (start + draft[(size_t) i].bars, l.getX(), l.getWidth(), tb);
        return { x0 + 1.0f, l.getY(), x1 - x0 - 2.0f, l.getHeight() };
    }
    const float x0 = l.getX() + l.getWidth() * (float) start / total, x1 = l.getX() + l.getWidth() * (float) (start + draft[(size_t) i].bars) / total;
    return { x0 + 2.0f, l.getY(), x1 - x0 - 4.0f, l.getHeight() };
}

// "verse 2": numbered the way the song will name it
String SongView::shownName (int i) const
{
    int n = 0, total = 0;
    for (int k = 0; k < (int) draft.size(); ++k)
        if (draft[(size_t) k].name == draft[(size_t) i].name) { ++total; if (k <= i) ++n; }
    const auto& nm = draft[(size_t) i].name;
    return String (nm) + (total > 1 && nm != "intro" && nm != "outro" ? " " + String (n) : String());
}

void SongView::showAddMenu()
{
    const auto st = proc.songJob.getStatus();
    if (! st.haveSong) return;
    // each kind the song has, plus a drop (made from a chorus)
    std::vector<std::pair<std::string, int>> kinds;
    for (int i = 0; i < (int) st.song.sections.size(); ++i)
    {
        const auto k = lp::song::arrange::kindOf (st.song.sections[(size_t) i].name);
        bool have = false;
        for (auto& [n, src] : kinds) if (n == k) { have = true; src = i; }
        if (! have) kinds.push_back ({ k, i });
    }
    int chorusSrc = -1;
    for (auto& [n, src] : kinds) if (n == "chorus") chorusSrc = src;
    bool haveDrop = false;
    for (auto& [n, src] : kinds) haveDrop = haveDrop || n == "drop";
    if (! haveDrop && chorusSrc >= 0) kinds.push_back ({ "drop", chorusSrc });
    PopupMenu m;
    m.addSectionHeader (selected >= 0 ? "add after " + shownName (selected) : String ("add at the end"));
    for (size_t i = 0; i < kinds.size(); ++i) m.addItem ((int) i + 1, String (kinds[i].first));
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&addBtn), [this, kinds, st] (int r)
    {
        if (r <= 0) return;
        const auto& [name, src] = kinds[(size_t) r - 1];
        lp::song::arrange::Item it { src, st.song.sections[(size_t) src].bars, name };
        const int at = selected >= 0 ? selected + 1 : (int) draft.size();
        draft.insert (draft.begin() + at, it);
        selected = at; dirty = true; lastShapeEdit = Time::getMillisecondCounter(); repaint();
    });
}

void SongView::mouseMove (const MouseEvent& e)
{
    bool edge = false;
    for (int i = 0; i < (int) draft.size(); ++i)
    {
        const auto r = blockRect (i);
        if (std::abs (e.position.x - r.getRight()) < 7.0f && e.position.y >= r.getY()) edge = true;
    }
    setMouseCursor (edge ? MouseCursor::LeftRightResizeCursor : MouseCursor::NormalCursor);
}

void SongView::mouseDown (const MouseEvent& e)
{
    if (! proc.songJob.getStatus().haveSong || waiting) return;
    drag = Drag::None;
    // right-click a section: make it different (hit harder, a breakdown, a build into what comes next)
    if (e.mods.isPopupMenu())
    {
        for (int i = 0; i < (int) draft.size(); ++i)
        {
            const auto r = blockRect (i);
            if (e.position.y < r.getY() || e.position.x < r.getX() || e.position.x > r.getRight()) continue;
            const String name = draft[(size_t) i].name;
            PopupMenu m;
            m.addSectionHeader (name.toUpperCase());
            m.addItem (1, "Hit harder (everything plays, crash, busier hats)");
            m.addItem (2, "Breakdown (drums and bass drop out, the rest muffled)");
            m.addItem (3, "Build into the next section (snare roll, rising filter, a beat of silence)");
            if (dirty) m.addSectionHeader ("your new shape is rebuilt first");
            m.showMenuAsync (PopupMenu::Options(), [this, i] (int r)
            {
                if (r < 1 || r > 3) return;
                // a shape you changed but haven't rebuilt: rebuild it first, then change the section (in that order)
                if (dirty)
                {
                    proc.songJob.rearrange (draft);
                    dirty = false; waiting = true; repaint();
                }
                proc.songJob.varySection (i, r - 1);
            });
            selected = i;
            pickOnly (i);
            repaint();
            return;
        }
        return;
    }
    setWantsKeyboardFocus (true); // (the window turns it off for everything but text boxes; Delete needs it here)
    grabKeyboardFocus();
    const bool more = e.mods.isShiftDown() || e.mods.isCommandDown();
    for (int i = 0; i < (int) draft.size(); ++i)
    {
        const auto r = blockRect (i);
        if (e.position.y < r.getY()) continue;
        if (more && e.position.x >= r.getX() && e.position.x <= r.getRight())
        {
            // shift-click: add it to the sections picked (or take it out again)
            if (picked.count (i) > 0) picked.erase (i); else picked.insert (i);
            selected = picked.empty() ? -1 : i;
            refresh();
            return;
        }
        if (std::abs (e.position.x - r.getRight()) < 7.0f) { drag = Drag::Resize; dragIndex = i; selected = i; break; }
        if (e.position.x >= r.getX() && e.position.x <= r.getRight()) { drag = Drag::Move; dragIndex = i; selected = i; break; }
    }
    if (drag == Drag::None) selected = -1;
    pickOnly (selected);
    dragStartX = e.position.x;
    pxPerBarAtStart = lane().getWidth() / (float) (ruler && view != nullptr ? view->shown (totalDraftBars()) : totalDraftBars());
    if (dragIndex >= 0 && dragIndex < (int) draft.size()) dragOrigBars = draft[(size_t) dragIndex].bars;
    refresh();
}

void SongView::mouseDrag (const MouseEvent& e)
{
    if (drag == Drag::Resize && dragIndex >= 0)
    {
        // whole bars, at least one, at most 64
        const int bars = jlimit (1, 64, dragOrigBars + (int) std::round ((e.position.x - dragStartX) / pxPerBarAtStart));
        if (bars != draft[(size_t) dragIndex].bars) { draft[(size_t) dragIndex].bars = bars; dirty = true; lastShapeEdit = Time::getMillisecondCounter(); }
        repaint();
    }
    else if (drag == Drag::Move && dragIndex >= 0 && e.getDistanceFromDragStartX() != 0)
    {
        // move it past the neighbour whose middle you've crossed
        if (dragIndex + 1 < (int) draft.size() && e.position.x > blockRect (dragIndex + 1).getCentreX())
        {
            std::swap (draft[(size_t) dragIndex], draft[(size_t) dragIndex + 1]);
            ++dragIndex; selected = dragIndex; pickOnly (selected); dirty = true; lastShapeEdit = Time::getMillisecondCounter();
        }
        else if (dragIndex > 0 && e.position.x < blockRect (dragIndex - 1).getCentreX())
        {
            std::swap (draft[(size_t) dragIndex], draft[(size_t) dragIndex - 1]);
            --dragIndex; selected = dragIndex; pickOnly (selected); dirty = true; lastShapeEdit = Time::getMillisecondCounter();
        }
        repaint();
    }
}

void SongView::mouseUp (const MouseEvent&) { drag = Drag::None; refresh(); }

void SongView::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w)
{
    const auto l = lane();
    if (ruler && view != nullptr && view->wheel (e, w, l.getX(), l.getWidth(), totalDraftBars())) return;
    Component::mouseWheelMove (e, w);
}

void SongView::mouseMagnify (const MouseEvent& e, float scale)
{
    const auto l = lane();
    if (ruler && view != nullptr && scale > 0.0f)
        view->zoom (1.0 / scale, view->barAt (e.position.x, l.getX(), l.getWidth(), totalDraftBars()), totalDraftBars());
}

// delete or backspace: the sections picked come out
bool SongView::keyPressed (const KeyPress& k)
{
    if ((k == KeyPress::deleteKey || k == KeyPress::backspaceKey) && proc.songJob.getStatus().haveSong && ! waiting)
    {
        removeBtn.onClick();
        refresh();
        return true;
    }
    return false;
}

void SongView::paint (Graphics& g)
{
    const auto& th = lp::ui::theme (proc.themeIndex.load());
    auto b = getLocalBounds().toFloat();
    const auto st = proc.songJob.getStatus();
    auto r = b.reduced (8.0f, 4.0f);
    auto head = r.removeFromTop (22.0f);

    struct Block { String name; int start, bars; float energy; };
    std::vector<Block> blocks;
    int totalBars = 0;
    String title;
    if (st.haveSong)
    {
        // the structure you're shaping (or the song's own)
        int start = 0;
        for (int i = 0; i < (int) draft.size(); ++i)
        {
            const int src = jlimit (0, std::max (0, (int) st.song.sections.size() - 1), draft[(size_t) i].src);
            const float e = st.song.sections.empty() ? 0.5f : st.song.sections[(size_t) src].energy;
            blocks.push_back ({ shownName (i), start, draft[(size_t) i].bars, e });
            start += draft[(size_t) i].bars;
        }
        totalBars = start;
        const double secs = totalBars * 4.0 * 60.0 / std::max (40.0, st.song.bpm);
        title = st.song.title + "  \xc2\xb7  " + String (roundToInt (st.song.bpm)) + " BPM  \xc2\xb7  " + lp::noteName (st.song.key)
              + (st.song.minor ? " MINOR" : " MAJOR") + "  \xc2\xb7  " + timeText (secs) + "  \xc2\xb7  " + String (totalBars) + " BARS"
              + (dirty ? "  \xc2\xb7  NEW SHAPE: MAKING IT IN A MOMENT" : waiting ? "  \xc2\xb7  REBUILDING..." : "  \xc2\xb7  RIGHT-CLICK A SECTION TO CHANGE IT");
    }
    else if (st.haveReference)
    {
        float maxE = 0.01f;
        for (auto& s : st.profile.sections) maxE = std::max (maxE, s.energy);
        for (auto& s : st.profile.sections) blocks.push_back ({ String (s.label), s.startBar, s.bars, s.energy / maxE });
        for (auto& bl : blocks) totalBars = std::max (totalBars, bl.start + bl.bars);
        title = "THE SHAPE OF YOUR REFERENCE";
    }
    const bool busy = st.stage == SongJob::Stage::Writing || st.stage == SongJob::Stage::Rendering || st.stage == SongJob::Stage::Listening;
    g.setFont (lp::ui::mono (10.5f, false, 0.12f));
    g.setColour (dirty ? th.grassLight : th.inkDim);
    if (! ruler || busy || dirty || waiting) // (the Studio's ruler: just the sections and their buttons)
        g.drawText (String (CharPointer_UTF8 ((busy ? st.message : title).toUpperCase().toRawUTF8())),
                    ruler ? head.withTrimmedLeft (500.0f) : head.withTrimmedRight (st.haveSong ? 330.0f : 0.0f), ruler ? Justification::centredRight : Justification::centredLeft, true);

    if (blocks.empty() || totalBars == 0)
    {
        g.setFont (font (14.0f));
        g.setColour (th.inkDim);
        g.drawText ("Your song's sections appear here: intro, verses, choruses, the bridge...", r, Justification::centred, true);
    }
    else
    {
        const auto ln = lane();
        Graphics::ScopedSaveState clipToLane (g);
        if (ruler) g.reduceClipRegion (ln.expanded (0.0f, 8.0f).toNearestInt());
        const bool zoomView = ruler && view != nullptr && st.haveSong;
        for (size_t i = 0; i < blocks.size(); ++i)
        {
            const auto& bl = blocks[i];
            const float x0 = zoomView ? view->xOf (bl.start, ln.getX(), ln.getWidth(), totalBars) - 1.0f : ln.getX() + ln.getWidth() * (float) bl.start / (float) totalBars;
            const float x1 = zoomView ? view->xOf (bl.start + bl.bars, ln.getX(), ln.getWidth(), totalBars) + 1.0f : ln.getX() + ln.getWidth() * (float) (bl.start + bl.bars) / (float) totalBars;
            if (x1 < ln.getX() - 4.0f || x0 > ln.getRight() + 4.0f) continue;
            const float h = ruler ? ln.getHeight() : ln.getHeight() * (0.4f + 0.6f * jlimit (0.0f, 1.0f, bl.energy));
            auto rect = Rectangle<float> (x0 + 2.0f, ln.getBottom() - h, x1 - x0 - 4.0f, h);
            const String n (bl.name);
            const bool big = n.startsWith ("chorus") || n.startsWith ("drop");
            const auto c = big ? th.grass : (n.startsWith ("verse") ? th.grass.withSaturation (0.35f).withBrightness (0.55f)
                                           : (n.startsWith ("bridge") ? th.grassLight.withAlpha (0.55f) : Colour (0xff3a3a40)));
            g.setColour (c.withMultipliedAlpha (0.9f));
            g.fillRoundedRectangle (rect, 6.0f);
            if ((picked.count ((int) i) > 0 || (int) i == selected) && st.haveSong)
            {
                g.setColour (Colours::white);
                g.drawRoundedRectangle (rect.reduced (0.5f), 6.0f, 2.0f);
                g.fillRoundedRectangle (Rectangle<float> (rect.getRight() - 4.0f, rect.getCentreY() - 10.0f, 3.0f, 20.0f), 1.5f); // the handle to resize
            }
            if (rect.getWidth() > 40.0f)
            {
                g.setFont (lp::ui::mono (9.5f, false, 0.1f));
                g.setColour (Colours::white.withAlpha (0.9f));
                // (zoomed in: the name stays in view while its section is)
                auto tr = ruler ? rect.withLeft (jmax (rect.getX(), ln.getX())).withRight (jmin (rect.getRight(), ln.getRight())) : rect;
                g.drawFittedText (n.toUpperCase() + (st.haveSong ? (ruler ? "  " : "\n") + String (bl.bars) + (ruler ? "" : " BARS") : String()),
                                  tr.reduced (ruler ? 6.0f : 8.0f, ruler ? 3.0f : 7.0f).toNearestInt(), ruler ? Justification::centredLeft : Justification::topLeft, ruler ? 1 : 2, 0.8f);
            }
        }
        if (st.haveSong && ! dirty && proc.studio.length() > 0 && proc.songIsPlaying())
        {
            const float frac = (float) jlimit (0.0, 1.0, proc.playheadPos.load() / (double) proc.studio.length());
            const double atBar = proc.playheadPos.load() / StudioPlayer::fileRate / (240.0 / std::max (40.0, st.song.bpm));
            const float x = zoomView ? view->xOf (atBar, ln.getX(), ln.getWidth(), totalBars) : ln.getX() + ln.getWidth() * frac;
            g.setColour (Colours::white);
            g.drawLine (x, ln.getY() - 4.0f, x, ln.getBottom(), 1.5f);
        }
    }
    if (busy || waiting)
    {
        auto bar = Rectangle<float> (b.getX() + 8.0f, b.getBottom() - 3.0f, b.getWidth() - 16.0f, 3.0f);
        g.setColour (Colours::white.withAlpha (0.1f));
        g.fillRoundedRectangle (bar, 1.5f);
        g.setColour (th.grass);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * st.progress), 1.5f);
    }
}

void FilePill::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    const bool ready = onClick ? (bool) isEnabled() : ! getFiles().isEmpty();
    const bool over = isMouseOver() && ready;
    g.setColour (Colour (over ? 0xff2e2e33 : 0xff232326).withAlpha (ready ? 1.0f : 0.5f));
    g.fillRoundedRectangle (r, 7.0f);
    g.setColour (Colour (0xff2c2c30));
    g.drawRoundedRectangle (r.reduced (0.5f), 7.0f, 1.0f);
    g.setColour (Colours::white.withAlpha (ready ? 0.85f : 0.25f));
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 2; ++j)
            g.fillEllipse (r.getX() + 12.0f + (float) j * 4.5f, r.getCentreY() - 5.0f + (float) i * 4.5f, 2.2f, 2.2f);
    g.setFont (lp::ui::mono (11.0f, false, 0.1f));
    g.drawText (label.toUpperCase(), r.withTrimmedLeft (26.0f), Justification::centredLeft, false);
}

void FilePill::mouseDrag (const MouseEvent& e)
{
    if (dragged || e.getDistanceFromDragStart() < 6) return;
    if (getFiles().isEmpty()) return;
    dragged = true;
    const auto files = filesToDrag ? filesToDrag() : getFiles();
    if (files.isEmpty()) return;
    DragAndDropContainer::performExternalDragDropOfFiles (files, false, this);
}

void FilePill::mouseUp (const MouseEvent&)
{
    if (! dragged && onClick) { onClick(); dragged = false; return; }
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
    : proc (p),
      liveSwitch ({ "Live", "Manual" },
                  [&p] { return p.params.getRawParameterValue ("live")->load() > 0.5f ? 0 : 1; },
                  [&p] (int i) { if (auto* q = p.params.getParameter ("live")) { q->beginChangeGesture(); q->setValueNotifyingHost (i == 0 ? 1.0f : 0.0f); q->endChangeGesture(); } },
                  lnf.th),
      keySwitch ({ "Play", "Chord" },
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
      trackEditor (p, lnf.th),
      voicePage (p, lnf.th),
      grainsPage (p, lnf.th),
      vocalsHub (p, lnf.th),
      drawSing (p, lnf.th),
      grainView (p),
      sourcesPanel (p, lnf.th),
      pillMidi ("drag midi", [&p] { auto st = p.songJob.getStatus(); return st.haveSong ? StringArray (st.files.midiFile.getFullPathName()) : StringArray(); }),
      pillChords ("chords + bass", [&p] { auto st = p.songJob.getStatus(); if (! st.haveSong) return StringArray();
                                          const auto f = st.files.folder.getChildFile (st.song.title + " - chords and bass.mid");
                                          return StringArray (f.existsAsFile() ? f.getFullPathName() : st.files.folder.getFullPathName()); }),
      pillStems ("drag stems", [&p] { auto st = p.songJob.getStatus(); StringArray a; if (st.haveSong) for (auto& t : st.tracks) a.add (t.stem.getFullPathName()); return a; }),
      pillMix ("drag mix", [&p] { auto st = p.songJob.getStatus(); return st.haveSong ? StringArray (st.files.mixFile.getFullPathName()) : StringArray(); }),
      pillHelper ("locators", [&p]
      {
          // Ableton: the helper device. Any other DAW: the tempo and the sections as markers in a MIDI file
          if (PluginHostType().isFruityLoops())
          {
              const auto f = p.songJob.flpMarkers(); // FL: a small FL project with the tempo and the markers (a click lists the sections)
              return f.existsAsFile() ? StringArray (f.getFullPathName()) : StringArray();
          }
          if (PluginHostType().isBitwigStudio())
              return StringArray(); // (it can't import markers: a click shows the sections instead)
          if (PluginHostType().isLogic() || PluginHostType().isGarageBand() || PluginHostType().isReaper())
          {
              const auto f = p.songJob.markersWav (p.getSampleRate()); // Logic reads markers from audio files
              return f.existsAsFile() ? StringArray (f.getFullPathName()) : StringArray();
          }
          if (! PluginHostType().isAbletonLive())
          {
              const auto f = p.songJob.locatorsMidi();
              return f.existsAsFile() ? StringArray (f.getFullPathName()) : StringArray();
          }
          auto st = p.songJob.getStatus();
          return st.abletonSet.existsAsFile() ? StringArray (st.abletonSet.getFullPathName()) : StringArray();
      }),
      pillFolder ("folder", [&p] { auto st = p.songJob.getStatus(); return st.haveSong ? StringArray (st.files.folder.getFullPathName()) : StringArray(); }),
      pillExport ("export", [] { return StringArray(); }),
      themeDots (p, [this] { applyTheme(); })
{
    setLookAndFeel (&lnf);
    LookAndFeel::setDefaultLookAndFeel (&lnf); // menus, boxes and tips open in their own windows: they take this look too
    setOpaque (true);
    startMs = Time::getMillisecondCounterHiRes();

    cityBox.setTextToShowWhenEmpty ("Search any city", lp::ui::theme (0).inkDim);
    cityBox.setFont (font (15.0f, lp::ui::Medium));
    cityBox.setIndents (40, 9);
    cityBox.setColour (TextEditor::backgroundColourId, Colours::transparentBlack);
    cityBox.setColour (TextEditor::outlineColourId, Colours::transparentBlack);
    cityBox.setColour (TextEditor::focusedOutlineColourId, Colours::transparentBlack);
    cityBox.onReturnKey = [this] { runSearch(); };
    cityBox.onEscapeKey = [this] { cityBox.setText ({}, false); cityBox.giveAwayKeyboardFocus(); };
    addChildComponent (cityBox);

    addChildComponent (liveSwitch);
    addChildComponent (keySwitch);
    addChildComponent (keys);
    addAndMakeVisible (playButton);
    addChildComponent (presetBrowser);
    addChildComponent (sampleView);
    addChildComponent (dragNotes);
    addChildComponent (songPanel);
    addChildComponent (songView);
    addChildComponent (studioView);
    addChildComponent (trackEditor);
    addChildComponent (voicePage);
    addChildComponent (vocalsHub);
    addChildComponent (grainsPage);
    addChildComponent (drawSing);
    drawSing.statusText = [this] { return autoAdd.status; };
    drawSing.autoAdd = &autoAdd;
    addChildComponent (vocalTabs);
    vocalsHub.onOpenInStudio = [this] (int t) { trackEditor.select (t); setPage (Studio); };
    vocalsHub.onGoTab = [this] (int t) { vocalTab = t; setPage (Voice); };
    vocalsHub.voiceAvailable = [this] { return voiceOn(); };
    songView.onLayer = [this] (int first, int count)
    {
        Component::SafePointer<LivePlanetEditor> sp (this);
        layerPopup = std::make_unique<lp::ai::LayerPopup> (proc, lnf.th, first, count, [sp]
        {
            MessageManager::callAsync ([sp] { if (sp != nullptr) sp->layerPopup.reset(); });
        });
        layerPopup->setBackdrop (createComponentSnapshot (getLocalBounds(), true, 0.25f));
        addAndMakeVisible (*layerPopup);
        layerPopup->setBounds (getLocalBounds());
        layerPopup->toFront (true);
    };
    // right-click a track, "change its sound": the layer window on "generate sound", playing that track's notes
    studioView.onResound = [this] (int track)
    {
        const int n = (int) proc.songJob.getStatus().song.sections.size();
        if (n <= 0 || ! songView.onLayer) return;
        songView.onLayer (0, n);
        if (layerPopup != nullptr) layerPopup->useNotesOf (track);
    };
    proc.onRecordMessage = [this] (const String& m) { recNote = m; recNoteAt = Time::getMillisecondCounter(); studioView.repaint(); };
    studioView.statusText = [this]
    {
        if (autoAdd.status.isNotEmpty()) return autoAdd.status;
        if (proc.trackRecording.load()) return String ("RECORDING");
        if (recNote.isNotEmpty() && Time::getMillisecondCounter() - recNoteAt < 8000) return recNote;
        // (a word after an edit, for a few seconds)
        const auto st = proc.songJob.getStatus();
        return st.notice.isNotEmpty() && Time::getMillisecondCounter() - st.noticeAt < 7000 ? st.notice : String();
    };
    studioView.ghosts = [this]
    {
        std::vector<StudioView::Ghost> g;
        for (auto& p : autoAdd.pending) g.push_back ({ p.start, p.len, p.label });
        return g;
    };
    setUpKeyBoxes (p, studioKey, studioMode, studioKeyAtt, studioModeAtt);
    addChildComponent (studioKey);
    addChildComponent (studioMode);
    addChildComponent (aiLayer);
    aiLayer.getProperties().set ("ai", true);
    aiLayer.setWantsKeyboardFocus (false);
    aiLayer.setMouseClickGrabsKeyboardFocus (false);
    aiLayer.setTooltip ("The AI plays a new instrument over the sections you picked (shift-click for more; none picked: the whole song): drums, bass, guitar, keys, strings...");
    aiLayer.onClick = [this] { songView.requestLayer(); };
    // takes
    for (int i = 0; i < 3; ++i)
    {
        takeBtn[i].onClick = [this, i] { proc.songJob.openTake (i); };
        takeBtn[i].setTooltip ("listen to take " + String (i + 1) + " (switch while it plays to compare)");
        takeBtn[i].setWantsKeyboardFocus (false);
        addChildComponent (takeBtn[i]);
    }
    mixTakes.onClick = [this] { showMixTakes(); };
    mixTakes.setTooltip ("use a track from another take in this one (the drums from take 2, the keys from take 3...)");
    mixTakes.setWantsKeyboardFocus (false);
    addChildComponent (mixTakes);
    // mastering
    {
        masterBox.addItem ("Master: off", 1);
        int count = 0;
        const auto* presets = lp::masterPresets (count);
        for (int i = 0; i < count; ++i)
            masterBox.addItem ("Master: " + String (presets[i].name) + " (" + String (roundToInt (presets[i].lufs)) + " LUFS)", i + 2);
        masterBox.setSelectedId (p.masterPreset.load() + 2, dontSendNotification);
        masterBox.setTooltip ("Master the song for where it's going: right loudness, a gentle glue and a safe peak. Made from your mix as you hear it.");
        masterBox.onChange = [this] { proc.startMaster (masterBox.getSelectedId() - 2); };
        masterBox.setWantsKeyboardFocus (false);
        masterAB.setClickingTogglesState (false);
        masterAB.setTooltip ("Switch between the master (after) and your mix before mastering");
        masterAB.onClick = [this] { proc.masterAfter = ! proc.masterAfter.load(); };
        masterAB.setWantsKeyboardFocus (false);
        masterInfo.setFont (lp::ui::mono (10.0f, false, 0.08f));
        masterInfo.setJustificationType (Justification::centredRight);
        masterInfo.setInterceptsMouseClicks (false, false);
        addChildComponent (masterBox);
        addChildComponent (masterAB);
        addChildComponent (masterInfo);
        masterMeter.setInterceptsMouseClicks (false, false);
        addChildComponent (masterMeter);
    }
    addChildComponent (grainView);
    addChildComponent (sourcesPanel);
    songPanel.onShowSources = [this] { sourcesPanel.open(); };
    // a track clicked in the arrangement opens below it (the Studio's own editor)
    studioView.onOpenTrack = [this] (int t)
    {
        if (t != trackEditor.selected()) trackEditor.select (t);
        if (dockCollapsed) { dockCollapsed = false; setPage (Studio); resized(); } // (a track clicked: the panel opens again)
        repaint();
    };
    studioSplit = jlimit (200, 4000, (int) lp::setting ("studioSplit", 358));
    // the arrangement's zoom, shared by the sections ruler and the lanes; and how tall the lanes are
    songView.view = &arrView;
    studioView.view = &arrView;
    arrView.changed = [this] { songView.repaint(); studioView.repaint(); };
    studioView.rowStep = jlimit (22, 72, (int) lp::setting ("laneHeight", 42));
    studioView.onRowStep = [] (int h) { lp::setSetting ("laneHeight", h); };
    studioView.selectedTrack = [this] { return trackEditor.selected(); };
    dockTabs.setTooltip ("Sample: the track's sample, its drum grid or its notes. Sound: its volume and effects. Effects: its devices (drag more in from the browser). Grains: re-plays it in tiny slices, in time.");
    // effects: a browser to drag from, the devices of the open track
    addChildComponent (fxBrowser);
    addChildComponent (fxRack);
    addChildComponent (fxBtn);
    fxBtn.getProperties().set ("glass", true);
    fxBtn.setWantsKeyboardFocus (false);
    fxBtn.setTooltip ("Audio effects: drag one onto a track (or double-click it for the open track)");
    // export, on the Studio page too (and Cmd + E anywhere)
    addChildComponent (exportBtn);
    exportBtn.getProperties().set ("glass", true);
    exportBtn.setWantsKeyboardFocus (false);
    exportBtn.setTooltip ("Save the mix, the stems or the MIDI as WAV, AIFF, FLAC or M4A (Cmd + E)");
    exportBtn.onClick = [this] { openExport(); };
    fxBtn.onClick = [this] { browserOpen = ! browserOpen; fxBtn.getProperties().set ("primary", browserOpen); resized(); repaint(); };
    fxBrowser.onAdd = [this] (const String& id) { addFx (trackEditor.selected(), id); };
    studioView.onDropFx = [this] (int t, String id) { addFx (t, id); };
    studioView.onOpenDrawn = [this] (const String& sec) { vocalTab = 1; setPage (Voice); drawSing.openSection (sec); };
    studioView.onEditNotes = [this] (int t)
    {
        trackEditor.select (t);
        dockCollapsed = false;
        dockPageShown = 0;
        trackEditor.setDockPage (0);
        // (room for the note editor: the arrangement gives up some height if it has to)
        if (getHeight() - 32 - 84 - studioSplit - 10 < 430) { studioSplit = jmax (200, getHeight() - 32 - 84 - 10 - 430); splitByHand = true; }
        showDock();
        resized();
        repaint();
    };
    fxRack.onOpenChain = [this] (int t)
    {
        Component::SafePointer<LivePlanetEditor> sp (this);
        chainWindow = std::make_unique<VocalPanel> (proc, t, [sp] { MessageManager::callAsync ([sp] { if (sp != nullptr) sp->chainWindow.reset(); }); });
        addAndMakeVisible (*chainWindow);
        chainWindow->setBounds (getLocalBounds());
        chainWindow->toFront (true);
    };
    addChildComponent (dockTabs);
    trackEditor.setDockPage (0);
    playButton.songMode = [this] { return page == Write || page == Studio || page == Edit; };
    // each track's own Print FX choice decides whether its stem comes with its effects recorded in
    pillStems.filesToDrag = [&p] { auto st = p.songJob.getStatus(); StringArray a;
                                   for (int t = 0; t < (int) st.tracks.size(); ++t) if (auto f = p.fileToDrag (t); f.existsAsFile()) a.add (f.getFullPathName());
                                   return a; };
    // the chords: read from the song's tracks when you drag (or click) it
    // the mix: with a master on, the mastered song as you hear it now (re-mastered if you changed anything)
    pillMix.filesToDrag = [&p]
    {
        if (p.masterPreset.load() >= 0 && p.masterAfter.load())
            if (const auto f = p.masteredFile(); f.existsAsFile()) return StringArray (f.getFullPathName());
        auto st = p.songJob.getStatus();
        return st.haveSong ? StringArray (st.files.mixFile.getFullPathName()) : StringArray();
    };
    pillChords.filesToDrag = [&p] { const auto f = p.songJob.chordsMidi(); return f.existsAsFile() ? StringArray (f.getFullPathName()) : StringArray(); };
    // one drag: every MIDI part (drums, 808, hook, bass lines, and the chords + bass your loops play)
    pillMidi.filesToDrag = [&p] { const auto f = p.songJob.allMidi(); return f.existsAsFile() ? StringArray (f.getFullPathName()) : StringArray(); };
    // export: the song's files in the type you choose (WAV, AIFF, FLAC, M4A), wherever you want them
    pillExport.setTooltip ("Save the mix, the stems or the MIDI as WAV, AIFF, FLAC or M4A, in a folder you choose");
    pillExport.onClick = [this] { openExport(); };
    pillMidi.setTooltip ("Every MIDI part in one drag: drums, 808, hook, bass lines, and the chords + bass your loops play. Each lands on its own MIDI track, sections as markers.");
    if (PluginHostType::getPluginLoadedAs() == AudioProcessor::wrapperType_Standalone)
        pillHelper.setTooltip ("Your song's tempo and sections as markers, in a MIDI file. Pro Tools: File > Import > MIDI and keep its markers and tempo. "
                               "Cubase, Reaper and others: drag it in at bar 1. (Using Ableton? Load Loopwright as a plugin there: Locators sets everything up.)");
    else if (PluginHostType().isAbletonLive())
        pillHelper.setTooltip ("Drag onto any track in Ableton once (Live Suite): it sets the tempo and places your song's sections as named locators, keeps them in step when you change sections here, and 'build song tracks' brings in every track.");
    else if (PluginHostType().isFruityLoops() || PluginHostType().isBitwigStudio())
    {
        pillHelper.setTooltip (PluginHostType().isFruityLoops()
            ? "Drag onto FL Studio (save your project first): a new project opens with your song's tempo and every section as a time marker. "
              "Then drag your stems or MIDI in. Click to see the sections and their bars instead."
            : "Your song's sections and the bar each one starts on, to add as markers (this DAW can't import them from a file). Click to see them.");
        pillHelper.onClick = [this]
        {
            auto secs = proc.songJob.sectionList();
            if (secs.empty()) return;
            SafePointer<LivePlanetEditor> self (this);
            sectionsPanel = std::make_unique<SectionsPanel> (std::move (secs), PluginHostType().isFruityLoops() ? "FL Studio" : "Bitwig",
                                                             [self] { MessageManager::callAsync ([self] { if (self != nullptr) self->sectionsPanel.reset(); }); });
            addAndMakeVisible (*sectionsPanel);
            sectionsPanel->setBounds (getLocalBounds());
            sectionsPanel->toFront (true);
        };
    }
    else if (PluginHostType().isReaper())
        pillHelper.setTooltip ("Your song's sections as markers inside an audio file: drag it onto a track at the start. If the markers don't appear, "
                               "select the item and run the action \"Item: Import item media cues as project markers\". The item is silent: delete it after.");
    else if (PluginHostType().isStudioOne())
        pillHelper.setTooltip ("Your song's tempo and sections as markers, in a MIDI file. Studio One reads the markers when the file is opened as a new song "
                               "(drag it onto the Start page); copy them from there into your song.");
    else if (PluginHostType().isLogic() || PluginHostType().isGarageBand())
        pillHelper.setTooltip ("Your song's sections as Logic markers: drag onto a new audio track at bar 1 and click Import when Logic asks about markers. "
                               "If it doesn't ask: select the region, then Navigate > Other > Import Marker from Audio File. The region itself is silent: delete it after.");
    else
        pillHelper.setTooltip ("Your song's tempo and sections as markers, in a MIDI file. Drag it into your DAW at bar 1 (say yes if it asks to import the tempo). "
                               "Logic: if the markers don't show, use File > Import > MIDI File with this file (it's in the song's folder).");
    for (auto* pill : { &pillMidi, &pillStems, &pillMix, &pillHelper, &pillFolder, &pillExport })
        addChildComponent (pill);
    addAndMakeVisible (themeDots);

    keyStepper = std::make_unique<Stepper> (*p.params.getParameter ("key"), lnf.th);
    scaleStepper = std::make_unique<Stepper> (*p.params.getParameter ("scale"), lnf.th);
    addChildComponent (*keyStepper);
    addChildComponent (*scaleStepper);

    loadButton.onClick = [this]
    {
        chooser = std::make_unique<FileChooser> ("Choose a sound to play", File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                              [this] (const FileChooser& fc) { if (fc.getResult().existsAsFile()) proc.loadSample (fc.getResult()); });
    };
    addChildComponent (loadButton);
    oneShotButton.onClick = [this] { showOneShotMenu(); };
    oneShotButton.setTooltip ("Play a one-shot from your library across the keyboard (keyboard: Play)");
    addChildComponent (oneShotButton);
    chopButton.setTooltip ("Pick a recording of your voice: it's cut into its syllables, one on each key from C3 up. Keyboard: Play.");
    chopButton.onClick = [this]
    {
        // recording: this click stops it, saves it and chops it
        if (proc.recording.load())
        {
            String msg;
            if (proc.stopRecording (msg) > 0)
                if (auto* km = proc.params.getParameter ("keymode")) km->setValueNotifyingHost (0.0f);
            chopMessage = msg;
            chopMessageUntil = Time::getMillisecondCounter() + 8000;
            chopButton.setButtonText ("Chop my vocal");
            repaint();
            return;
        }
        PopupMenu m;
        if (proc.canRecordMic()) m.addItem (1, "Record now (from your mic)");
        else m.addItem (3, "Record in your DAW, then drop the file here", false, false);
        m.addItem (2, "From a file...");
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (&chopButton), [this] (int r)
        {
            if (r == 1) { proc.startRecording(); chopMessage = {}; }
            if (r == 2) chopFromFile();
        });
    };
    addChildComponent (chopButton);
    chopButton.getProperties().set ("warn", true); // red-orange while recording
    inMeter.setInterceptsMouseClicks (false, false);
    addChildComponent (inMeter);

    addKnob ("pad", "chords", 0.86f, 52);
    addKnob ("drops", "melody", 0.78f, 52);
    addKnob ("bass", "bass", 0.68f, 52);
    addKnob ("grain", "grain", 0.08f, 84);
    addKnob ("texture", "scatter", 0.12f, 56);
    addKnob ("sparkle", "sparkle", 0.16f, 56);
    addKnob ("position", "position", 0.2f, 56);
    addKnob ("warmth", "warmth", 0.03f, 50);
    addKnob ("echo", "echo", 0.45f, 50);
    addKnob ("space", "space", 0.52f, 50);
    addKnob ("shimmer", "shimmer", 0.58f, 50);
    addKnob ("atmos", "atmos", 0.36f, 50);
    addKnob ("gain", "output", 0.0f, 50);
    addKnob ("nostalgia", "nostalgia", 0.1f, 50);

    const char* skyIds[] = { "temp", "rain", "snow", "wind", "clouds", "pressure", "humidity" };
    for (int i = 0; i < 7; ++i)
        addKnob (skyIds[i], skyIds[i], 0.5f + 0.03f * (float) i, 34);
    dayToggle.setName ("daylight");
    dayToggle.setButtonText ("Daylight");
    thunderToggle.setButtonText ("Thunder");
    thunderToggle.setName ("thunder");
    addChildComponent (dayToggle);
    addChildComponent (thunderToggle);
    dayAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (p.params, "daylight", dayToggle);
    thunderAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (p.params, "thunder", thunderToggle);

    setSize (kW, kH);
    if (auto* scale = std::getenv ("LP_UI_SCALE")) // for sharp screenshots (website, docs)
        Desktop::getInstance().setGlobalScaleFactor ((float) std::atof (scale));
    applyTheme();
    setPage (Write);
    if (std::getenv ("LP_VOICE_DEMO") != nullptr) // (for screenshots: the Voice page in the middle of singing)
        MessageManager::callAsync ([sp = Component::SafePointer<LivePlanetEditor> (this)]
    {
        if (sp == nullptr) return;
        auto& voicePage = sp->voicePage;
        lp::voice::Snapshot d;
        d.connected = true;
        d.voices = { "my voice (clean)", "demo voice" };
        d.stages = { { "sing", "Writing melodies" }, { "score", "Checking words and key" }, { "clean", "Removing background noise" },
                     { "voice", "Singing in your voice" }, { "mix", "Finishing and mixing" } };
        lp::voice::Job j;
        j.id = "a1b2c3d4e5f6"; j.state = "running"; j.stage = "voice"; j.note = "version 2 of 2"; j.title = "Hold me close";
        j.created = Time::getCurrentTime().toMilliseconds() / 1000.0 - 263.0;
        j.results.push_back ({ 1, "/audio/a1b2c3d4e5f6/vocal_1.wav", "/audio/a1b2c3d4e5f6/mix_1.wav", "hold me close", 92, 96 });
        d.jobs.push_back (j);
        voicePage.setSnapshot (d);
        voicePage.setSong ({ "Chorus 1   (bars 33 to 40, 16 s)", "Verse 1   (bars 9 to 24, 32 s)", "Chorus 2   (bars 57 to 64, 16 s)" }, 0, "A MINOR", "120 BPM");
        sp->vocalTab = 2;
        sp->setPage (Voice);
    });

    // only the text boxes ever take the keyboard, so your DAW's keys keep playing notes
    setWantsKeyboardFocus (false);
    for (auto* c : getChildren())
        if (c != &cityBox)
        {
            c->setWantsKeyboardFocus (false);
            c->setMouseClickGrabsKeyboardFocus (false);
        }
    addMouseListener (this, true);

    // help: the tour, and the "?"s
    tourBtn.onClick = [this] { startTour(); };
    tourBtn.setTooltip ("Take the tour again");
    for (auto* h : { &tourBtn, &hMidi, &hStems, &hLocators }) addAndMakeVisible (*h);
    // the first time on this computer: the tour, before anything else
    if (! (bool) lp::setting ("tourDone", false))
        MessageManager::callAsync ([safe = Component::SafePointer<LivePlanetEditor> (this)] { if (safe != nullptr) safe->startTour(); });

    timerCallback();
    startTimerHz (30);
    checkForUpdate();
}

// ---- updates: is a newer release out than this build? (asked once a session; nothing about you is sent)
#ifndef LOOPWRIGHT_BUILD
 #define LOOPWRIGHT_BUILD 0
#endif
void LivePlanetEditor::checkForUpdate()
{
    static std::atomic<bool> asked { false };
    if (LOOPWRIGHT_BUILD <= 0 || asked.exchange (true)) return; // (a test build, or already asked this session)
    Component::SafePointer<LivePlanetEditor> safe (this);
    Thread::launch ([safe]
    {
        const URL url ("https://api.github.com/repos/olmosvip-oss/live-planet/releases/latest");
        auto in = url.createInputStream (URL::InputStreamOptions (URL::ParameterHandling::inAddress)
                                             .withConnectionTimeoutMs (8000)
                                             .withExtraHeaders ("User-Agent: Loopwright\r\nAccept: application/vnd.github+json"));
        if (in == nullptr) return;
        const auto tag = JSON::parse (in->readEntireStreamAsString())["tag_name"].toString(); // e.g. "v0.9.131"
        const int build = tag.fromLastOccurrenceOf (".", false, false).getIntValue();
        if (! tag.startsWith ("v") || build <= LOOPWRIGHT_BUILD) return;
        if ((int) lp::setting ("skipUpdate", 0) == build) return; // ("skip this version")
        MessageManager::callAsync ([safe, tag]
        {
            if (safe == nullptr || safe->updateNotice != nullptr) return;
            auto* ed = safe.getComponent();
            ed->updateNotice = std::make_unique<UpdateNotice> (ed->lnf.th, tag.substring (1), [safe]
            {
                MessageManager::callAsync ([safe] { if (safe != nullptr) safe->updateNotice.reset(); });
            });
            ed->addAndMakeVisible (*ed->updateNotice);
            ed->updateNotice->setBounds (ed->getLocalBounds());
            ed->updateNotice->toFront (true);
        });
    });
}

UpdateNotice::UpdateNotice (const lp::ui::Theme*& theme, String v, std::function<void()> closed) : th (theme), version (std::move (v))
{
    download.getProperties().set ("primary", true);
    download.onClick = [closed] { URL ("https://loop-wright.com/download").launchInDefaultBrowser(); closed(); };
    later.onClick = [closed] { closed(); };
    skip.onClick = [this, closed] { lp::setSetting ("skipUpdate", version.fromLastOccurrenceOf (".", false, false).getIntValue()); closed(); };
    for (auto* b : { &download, &later, &skip }) { b->setWantsKeyboardFocus (false); addAndMakeVisible (*b); }
}

Rectangle<float> UpdateNotice::card() const
{
    return getLocalBounds().toFloat().withSizeKeepingCentre (520.0f, 250.0f);
}

void UpdateNotice::resized()
{
    const auto c = card().toNearestInt();
    download.setBounds (c.getX() + 28, c.getBottom() - 66, 210, 40);
    later.setBounds (download.getRight() + 10, c.getBottom() - 66, 90, 40);
    skip.setBounds (later.getRight() + 10, c.getBottom() - 66, c.getRight() - 28 - later.getRight() - 10, 40);
}

void UpdateNotice::paint (Graphics& g)
{
    const auto& t = *th;
    g.fillAll (Colours::black.withAlpha (0.45f));
    const auto c = card();
    g.setColour (t.body.withAlpha (0.97f));
    g.fillRoundedRectangle (c, 18.0f);
    g.setGradientFill (ColourGradient (t.grass.withAlpha (0.25f), c.getX(), c.getY(), t.grass.withAlpha (0.0f), c.getX(), c.getY() + 120.0f, false));
    g.fillRoundedRectangle (c, 18.0f);
    g.setColour (t.grass.withAlpha (0.6f));
    g.drawRoundedRectangle (c.reduced (0.5f), 18.0f, 1.2f);
    auto r = c.reduced (28.0f, 24.0f);
    g.setFont (lp::ui::mono (10.0f, true, 0.14f));
    g.setColour (t.grassLight);
    g.drawText ("UPDATE  " + version, r.removeFromTop (16.0f), Justification::centredLeft, false);
    r.removeFromTop (8.0f);
    g.setFont (lp::ui::font (24.0f, lp::ui::Medium, -0.01f));
    g.setColour (t.ink);
    g.drawText ("There's a new version of Loopwright", r.removeFromTop (32.0f), Justification::centredLeft, false);
    r.removeFromTop (6.0f);
    g.setFont (lp::ui::font (14.0f));
    g.setColour (t.inkDim);
    g.drawFittedText ("Download it and run the installer, then restart Loopwright (and your DAW, if you use it there) to get the new features. "
                      "Your songs and settings stay as they are.", r.removeFromTop (60.0f).toNearestInt(), Justification::topLeft, 3);
}

void LivePlanetEditor::startTour()
{
    auto inSong = [this] (Rectangle<int> r) { return r + songPanel.getPosition(); };
    std::vector<Tour::Step> steps {
        { Write, nullptr, "Welcome to Loopwright",
          "Loopwright writes complete, produced songs from your own samples, in your project's tempo and key, and lets you change every part of them. "
          "This takes about a minute: click next (or press the right arrow) to go through it." },
        { Write, [=] { auto r = inSong (songPanel.settingsArea()); return r.removeFromBottom (44); }, "1. Your samples",
          "First, show it where your samples live: ADD FOLDER for each folder of sounds and packs you have. "
          "It looks through them once (SCAN does it again after you add new ones), and SOURCES lets you choose which packs it may use." },
        { Write, [=] { return inSong (songPanel.heroArea()); }, "2. Start a song",
          "Drop a song you love here: it learns its tempo, key, feel and energy (never its melody). "
          "Or just describe what you want, like \"dark trap in G minor with flute\". Then press Write a song." },
        { Write, [=] { return inSong (songPanel.settingsArea()); }, "3. How it's written",
          "Pick a key (or leave it on auto), whether it follows your DAW's tempo, and where the sounds come from: only your samples, "
          "your samples with the plugin's instruments, or only built-in. Look for the ? marks: click one for a short explanation." },
        { Write, [=] { return cardTimeline; }, "4. Your song",
          "Its sections, intro to outro. Drag one to move it, drag its edge to make it longer, right-click it to make it hit harder, "
          "a breakdown, or a build. The buttons along the bottom drag the MIDI, the stems, the mix or the locators into your DAW." },
        { Studio, [=] { return studioView.getBounds(); }, "5. Studio",
          "Your arrangement: the sections along the top (shift-click to pick several, Delete takes them out) and every track's sound "
          "in each one. Lock the tracks you love and write the rest again; right-click a track to swap, reverse or remove it; "
          "+ add instrument brings in a hook, an 808 and more. The FEEL row shapes the whole song at once." },
        { Studio, [=] { return trackEditor.getBounds(); }, "6. The open track",
          "Click any track and it opens under the arrangement: its sample (drums, 808s and hooks show their notes on a grid: "
          "click to add, drag to move), its sound and effects, and its grains." },
        { Sound, [=] { return cardInstrument; }, "7. Sound",
          "Play any sound on your keyboard: pick one of your one-shots or drop a sample, or drop your own vocal and it's chopped onto the keys." },
        { Write, [=] { return cardTimeline; }, "8. Your DAW",
          "Stems, MIDI and the mix drag into any DAW. Section markers: one drag in Ableton (Live Suite), Logic, Reaper and Cubase. "
          "FL Studio and Bitwig can't import markers, so LOCATORS lists each section's bar for you. "
          "Pro Tools: use the Loopwright app (not a plugin there yet) and drag the files in." },
        { Write, [=] { return tourBtn.getBounds().expanded (10, 8); }, "That's it",
          "Changed something you don't like? Undo is in the top bar (or Cmd+Z). This ? takes the tour again any time. Have fun." },
    };
    tour = std::make_unique<Tour> (std::move (steps), [this] (int pg) { setPage (pg); },
                                   [this]
                                   {
                                       lp::setSetting ("tourDone", true);
                                       Component::SafePointer<LivePlanetEditor> safe (this);
                                       MessageManager::callAsync ([safe] { if (safe != nullptr) safe->tour.reset(); });
                                       if (tour != nullptr) tour->setVisible (false);
                                       setPage (Write);
                                   });
    addAndMakeVisible (*tour);
    tour->setBounds (getLocalBounds());
    tour->toFront (true);
    tour->start();
}

LivePlanetEditor::~LivePlanetEditor()
{
    stopTimer();
    if (&LookAndFeel::getDefaultLookAndFeel() == &lnf) LookAndFeel::setDefaultLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

LivePlanetEditor::Knob& LivePlanetEditor::addKnob (const char* id, const String& name, float hue, int size)
{
    auto k = std::make_unique<Knob>();
    k->slider.setName (name);
    k->slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    k->slider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    k->slider.setRotaryParameters (MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
    k->slider.getProperties().set ("hue", hue);
    k->slider.getProperties().set ("size", size);
    k->slider.setMouseDragSensitivity (240);
    k->attachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.params, id, k->slider);
    addChildComponent (k->slider);
    auto* raw = k.get();
    knobById[id] = raw;
    knobs.push_back (std::move (k));
    return *raw;
}

void LivePlanetEditor::applyTheme()
{
    lnf.th = &lp::ui::theme (proc.themeIndex.load());
    lnf.setColour (ComboBox::backgroundColourId, lnf.th->bodyDark);
    lnf.setColour (ComboBox::textColourId, lnf.th->ink);
    lnf.setColour (ComboBox::outlineColourId, lnf.th->line);
    lnf.setColour (ComboBox::arrowColourId, lnf.th->inkDim);
    lnf.applyThemeColours(); // menus, boxes and tips
    for (auto* ed : { &cityBox, &songPanel.promptBox })
    {
        ed->setColour (TextEditor::textColourId, lnf.th->ink);
        ed->setColour (CaretComponent::caretColourId, lnf.th->grass);
        ed->setColour (TextEditor::highlightColourId, lnf.th->grass.withAlpha (0.35f));
        ed->applyColourToAllText (lnf.th->ink);
    }
    cityBox.setTextToShowWhenEmpty ("Search any city", lnf.th->inkDim);
    songPanel.promptBox.setTextToShowWhenEmpty ("Describe it: dark trap in F minor with piano and flute", lnf.th->inkDim);
    for (auto* c : getChildren()) c->repaint();
    repaint();
}

Rectangle<int> LivePlanetEditor::tabBounds (int i) const
{
    static const int widths[] = { 64, 72, 72, 72, 64 };
    int x = 300;
    for (int k = 0; k < i; ++k) x += widths[k] + 22;
    return { x, 20, widths[i], 30 };
}

// the experimental Voice page: off unless switched on (Settings), or LP_VOICE=1 for testing
bool LivePlanetEditor::voiceOn() const
{
    return (bool) lp::setting ("voiceLab", false) || std::getenv ("LP_VOICE") != nullptr;
}

void LivePlanetEditor::setPage (int p)
{
    if (p == Edit) p = Studio; // (the track editor lives in the Studio now, under the arrangement)
    page = p;
    if (p != Studio) trackEditor.stopLive(); // the drum editor's loop stops when you leave it
    const bool write = p == Write, studio = p == Studio, edit = false, sound = p == Sound, sky = false; // (the weather page is gone)
    const bool haveSong0 = proc.songJob.getStatus().haveSong;
    trackEditor.setVisible (studio && haveSong0 && dockPageShown != 1);
    dockTabs.setVisible (studio && haveSong0);
    fxBtn.setVisible (studio && haveSong0);
    exportBtn.setVisible (studio && haveSong0);
    fxBrowser.setVisible (studio && haveSong0 && browserOpen);
    if (! studio) fxRack.setVisible (false);
    // vocals: your vocals, or (with the voice helper switched on) a new one made with AI
    if (! voiceOn()) vocalTab = 0;
    vocalTabs.setVisible (p == Voice && voiceOn());
    vocalsHub.setVisible (p == Voice && vocalTab == 0);
    drawSing.setVisible (p == Voice && vocalTab == 1);
    if (! (p == Voice && vocalTab == 1)) drawSing.leave(); // (the guide tone and your live voice: only while you're drawing)
    voicePage.setVisible (p == Voice && vocalTab == 2);
    voicePage.shown (p == Voice && vocalTab == 2);
    grainsPage.setVisible (p == GrainsTab);
    grainsPage.shown (p == GrainsTab);
    studioKey.setVisible (studio);
    studioMode.setVisible (studio);
    masterBox.setVisible (studio);
    masterAB.setVisible (studio);
    masterInfo.setVisible (studio);
    masterMeter.setVisible (studio);
    grainView.setVisible (p == Sound);
    const bool haveSong = proc.songJob.getStatus().haveSong;
    songPanel.setVisible (write);
    songView.setVisible (write || studio);
    studioView.setVisible (studio && haveSong);
    for (auto* pill : { &pillMidi, &pillStems, &pillMix, &pillHelper, &pillFolder, &pillExport })
        pill->setVisible (write);
    for (auto* h : { &hMidi, &hStems, &hLocators }) h->setVisible (write);
    songView.setRuler (studio);
    // sound: the instrument and the knobs
    keys.setVisible (sound);
    presetBrowser.setVisible (sound);
    loadButton.setVisible (sound);
    oneShotButton.setVisible (sound);
    chopButton.setVisible (sound);
    inMeter.setVisible (sound);
    keyStepper->setVisible (sound);
    scaleStepper->setVisible (sound);
    keySwitch.setVisible (sound);
    dragNotes.setVisible (sound);
    for (auto id : { "pad", "drops", "bass", "grain", "texture", "sparkle", "position", "warmth", "echo", "space", "shimmer", "atmos", "gain", "nostalgia" })
        knobById[id]->slider.setVisible (sound);
    // sky: the weather
    cityBox.setVisible (sky);
    liveSwitch.setVisible (sky);
    for (auto id : { "temp", "rain", "snow", "wind", "clouds", "pressure", "humidity" })
        knobById[id]->slider.setVisible (sky);
    dayToggle.setVisible (sky);
    thunderToggle.setVisible (sky);
    playButton.setVisible (write || studio || sound || edit);
    resized();
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

void LivePlanetEditor::chopFromFile()
{
    chooser = std::make_unique<FileChooser> ("Choose a recording of your voice to chop", File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.m4a");
    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this] (const FileChooser& fc)
    {
        if (! fc.getResult().existsAsFile()) return;
        if (proc.loadVocalChops (fc.getResult()) > 0)
            if (auto* km = proc.params.getParameter ("keymode")) km->setValueNotifyingHost (0.0f); // Play mode, ready to play
        repaint();
    });
}

void LivePlanetEditor::showMixTakes()
{
    const auto st = proc.songJob.getStatus();
    if (st.takes.size() < 2) return;
    PopupMenu menu;
    menu.addSectionHeader ("use a track from another take");
    struct Pick { int dst, take, src; };
    std::vector<Pick> picks { {} };
    for (int k = 0; k < st.takes.size(); ++k)
    {
        if (k == st.take) continue;
        PopupMenu sub;
        const auto other = proc.songJob.takeTracks (k);
        for (int j = 0; j < (int) other.size(); ++j)
        {
            // in place of the track with the same name here, or added
            int dst = -1;
            for (int t = 0; t < (int) st.tracks.size(); ++t) if (st.tracks[(size_t) t].name == other[(size_t) j].first) dst = t;
            picks.push_back ({ dst, k, j });
            sub.addItem ((int) picks.size() - 1, other[(size_t) j].first + (dst >= 0 ? "  (replaces this take's)" : "  (adds it)") + "   " + other[(size_t) j].second);
        }
        menu.addSubMenu ("from take " + String (k + 1), sub);
    }
    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (&mixTakes), [this, picks] (int id)
    {
        if (id > 0 && id < (int) picks.size()) proc.songJob.useFromTake (picks[(size_t) id].dst, picks[(size_t) id].take, picks[(size_t) id].src);
    });
}

void LivePlanetEditor::showOneShotMenu()
{
    const auto shots = proc.songJob.oneShots();
    PopupMenu menu;
    menu.addSectionHeader ("Play a one-shot across the keyboard");
    if (proc.samplerOn.load()) menu.addItem (1, "Off: back to the built-in instrument");
    // every pack (none left empty), up to 300 sounds in each
    std::vector<String> paths { String(), String() };
    for (auto& [pack, files] : shots)
    {
        PopupMenu sub;
        for (int k = 0; k < files.size() && k < 300; ++k)
        {
            paths.push_back (files[k]);
            sub.addItem ((int) paths.size() - 1, SongJob::niceName (File (files[k]).getFileNameWithoutExtension()));
        }
        if (files.size() > 300) sub.addItem (-1, "(" + String (files.size() - 300) + " more in this pack)", false);
        menu.addSubMenu (SongJob::niceName (pack) + "   (" + String (files.size()) + ")", sub);
    }
    if (shots.empty()) menu.addItem (-1, "No melodic one-shots found in your sample folders", false);
    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (&oneShotButton), [this, paths] (int id)
    {
        if (id == 1) { proc.samplerOff(); repaint(); return; }
        if (id >= 2 && id < (int) paths.size())
        {
            proc.loadSamplerInstrument (File (paths[(size_t) id]));
            // the keyboard plays notes (not chords) so you can play it straight away
            if (auto* km = proc.params.getParameter ("keymode")) km->setValueNotifyingHost (0.0f);
            repaint();
        }
    });
}

void LivePlanetEditor::timerCallback()
{
    if (proc.trackTakeReady.load()) proc.finishTrackTake(); // (a take just ended: into its track)
    if (proc.songJob.getStatus().undoWhat != undoShown) repaint (0, 0, getWidth(), 76); // the undo button in the top bar
    if (page == Sound)
    {
        const float in = proc.inPeak.exchange (0.0f);
        inMeter.l.update (in); inMeter.r.update (in);
        inMeter.repaint();
        if (proc.recording.load())
        {
            const int secs = (int) (proc.recPos.load() / std::max (1.0, proc.recRate));
            chopButton.setButtonText (String::fromUTF8 ("\xe2\x97\x8f Stop  ") + String (secs / 60) + ":" + String (secs % 60).paddedLeft ('0', 2));
            chopButton.setToggleState (true, dontSendNotification);
        }
        else
        {
            if (chopButton.getButtonText() != "Chop my vocal") chopButton.setButtonText ("Chop my vocal"); // (a minute is the most)
            chopButton.setToggleState (false, dontSendNotification);
        }
    }
    {
        // the takes bar: shown on the write page when there are takes to compare
        const auto st = proc.songJob.getStatus();
        const bool show = page == Write && st.takes.size() > 1;
        for (int i = 0; i < 3; ++i)
        {
            const bool v = show && i < st.takes.size();
            if (takeBtn[i].isVisible() != v) takeBtn[i].setVisible (v);
            takeBtn[i].setToggleState (st.take == i, dontSendNotification);
        }
        if (mixTakes.isVisible() != show) mixTakes.setVisible (show);
    }
    if (page == Studio)
    {
        String info;
        if (proc.mastering.load()) info = "MASTERING YOUR MIX...";
        else if (proc.masterPreset.load() >= 0)
        {
            const SpinLock::ScopedLockType sl (proc.masterLock);
            info = proc.masterReport.toUpperCase();
        }
        if (masterInfo.getText() != info) masterInfo.setText (info, dontSendNotification);
        pillMix.setLabel (proc.masterPreset.load() >= 0 && proc.masterAfter.load() ? "drag master" : "drag mix");
        masterMeter.l.update (proc.outPeakL.exchange (0.0f));
        masterMeter.r.update (proc.outPeakR.exchange (0.0f));
        masterMeter.repaint();
        if (proc.masterResets.load() != seenMasterResets) { seenMasterResets = proc.masterResets.load(); masterBox.setSelectedId (1, dontSendNotification); }
        masterInfo.setColour (Label::textColourId, lnf.th->inkDim);
        const bool after = proc.masterAfter.load();
        masterAB.setButtonText (after ? "after" : "before");
        masterAB.setToggleState (after && proc.masterPreset.load() >= 0, dontSendNotification);
        masterAB.setEnabled (proc.masterPreset.load() >= 0);
    }
    lp::VisualEvent e;
    while (proc.engine.popVisual (e)) {} // (the old planet drew these; nothing to show now)

    const auto status = proc.getSkyStatus();
    const lp::Weather real = proc.engine.getTargetWeather();
    const bool usingLive = status.live && status.haveLive;
    cityText = status.live ? status.city : String ("Your own sky");
    const String deg (CharPointer_UTF8 ("\xc2\xb0"));
    headerText = (status.live && status.country.isNotEmpty() ? status.country + "  \xc2\xb7  " : String())
               + real.describe() + "  " + String (roundToInt (real.temperatureC)) + deg + "C  \xc2\xb7  wind "
               + String (roundToInt (real.windKmh)) + " km/h  \xc2\xb7  "
               + (real.snowCm > 0.05f ? "snow " + String (real.snowCm, 1) + " cm" : "rain " + String (real.rainMm, 1) + " mm");
    headerText = String (CharPointer_UTF8 (headerText.toRawUTF8()));
    if (usingLive)
    {
        const auto mins = (int) (Time::getCurrentTime() - status.fetchedAt).inMinutes();
        statusText = mins < 1 ? String ("LIVE") : "LIVE \xc2\xb7 " + String (mins) + " MIN AGO";
    }
    else
        statusText = status.live ? "CONNECTING" : "MANUAL";
    if (status.busy) statusText = "CHECKING THE SKY";
    if (status.message.isNotEmpty()) statusText = status.message.toUpperCase();
    statusText = String (CharPointer_UTF8 (statusText.toRawUTF8()));

    const int q = proc.engine.uiChordMinor.load();
    const String chord = q < 0 ? String ("your chord")
                               : String (lp::noteName (proc.engine.uiChordRoot.load())) + (q == 1 ? "m" : (q == 2 ? "sus2" : ""));
    nowText = String (lp::profileFor ((lp::SoundId) proc.engine.uiSound.load()).name).toUpperCase() + "  \xc2\xb7  "
            + String (lp::noteName (proc.engine.uiKey.load())) + " " + String (lp::getScale ((lp::ScaleId) proc.engine.uiScale.load()).name).toUpperCase()
            + "  \xc2\xb7  " + chord.toUpperCase() + "  \xc2\xb7  " + String (roundToInt (proc.engine.uiBpm.load())) + " BPM";
    nowText = String (CharPointer_UTF8 (nowText.toRawUTF8()));

    const bool manual = ! status.live;
    for (auto id : { "temp", "rain", "snow", "wind", "clouds", "pressure", "humidity" })
        knobById[id]->slider.setEnabled (manual);
    dayToggle.setEnabled (manual);
    thunderToggle.setEnabled (manual);

    playButton.repaint();
    if (page == Write || page == Studio)
    {
        const bool haveSong = proc.songJob.getStatus().haveSong;
        if (page == Studio && studioView.isVisible() != haveSong) studioView.setVisible (haveSong);
        if (haveSong && page == Studio) studioView.repaint();
        songPanel.refresh();
        songView.refresh();
        for (auto* pill : { &pillMidi, &pillStems, &pillMix, &pillHelper, &pillFolder, &pillExport })
            pill->repaint();
    }
    if (page == Sound)
    {
        keys.repaint();
        presetBrowser.repaint();
        dragNotes.repaint();
        const bool sampleMode = (int) proc.params.getRawParameterValue ("sound")->load() == LivePlanetProcessor::kSampleChoice;
        if (sampleView.isVisible() != sampleMode) sampleView.setVisible (sampleMode);
        if (sampleMode) sampleView.repaint();
        repaint (cardInstrument);
        grainView.repaint();
    }
    else if (sampleView.isVisible())
        sampleView.setVisible (false);
    if (page == Studio && proc.songJob.getStatus().haveSong)
    {
        if (! trackEditor.isVisible() && ! fxRack.isVisible() && ! dockCollapsed) { showDock(); dockTabs.setVisible (true); }
        if (fxRack.isVisible()) { fxRack.setTrack (trackEditor.selected()); fxRack.repaint(); }
        trackEditor.refresh();
        if (trackEditor.wantsTall() != dockTall) { dockTall = trackEditor.wantsTall(); resized(); repaint(); }
    }
    if (page == Voice && vocalTab == 2) voicePage.refresh();
    if (page == Voice && vocalTab == 1) { proc.voiceClient.setWanted (true); drawSing.refresh(); }
    if (page == Voice && vocalTab == 0) vocalsHub.refresh();
    if (voiceOn()) autoAdd.tick (proc);
    // the Studio talks to the voice helper too (its AI layers), in the background
    if (voiceOn() && page == Studio) proc.voiceClient.setWanted (true);
    songView.layerAvailable = voiceOn();
    {
        const bool show = page == Studio && proc.songJob.getStatus().haveSong; // (flipping a sample needs no helper)
        if (aiLayer.isVisible() != show) aiLayer.setVisible (show);
        aiLayer.setEnabled (songView.layerReady());
        const int n = songView.pickedCount();
        const String t = n > 1 ? "AI layer on " + String (n) + " sections" : n == 1 ? String ("AI layer on this section") : String ("AI layer");
        if (aiLayer.getButtonText() != t) aiLayer.setButtonText (t);
    }
    repaint (0, 0, getWidth(), 70); // the top bar (library and tempo)
}

//==============================================================================

namespace
{
    // a card: a slightly lighter panel with a hairline border
    void card (Graphics& g, Rectangle<int> rect, const lp::ui::Theme& th)
    {
        auto r = rect.toFloat();
        g.setColour (th.body);
        g.fillRoundedRectangle (r, 12.0f);
        g.setColour (th.line);
        g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, 1.0f);
    }

    // a small mono label, like "NEW RELEASES" headers
    void label (Graphics& g, const String& text, Rectangle<float> where, const lp::ui::Theme& th, Justification j = Justification::centredLeft)
    {
        g.setFont (lp::ui::mono (10.5f, false, 0.14f));
        g.setColour (th.inkDim);
        g.drawText (text.toUpperCase(), where, j, false);
    }

    // a section title in the clean sans
    void heading (Graphics& g, const String& text, Rectangle<float> where, const lp::ui::Theme& th)
    {
        g.setFont (font (19.0f, lp::ui::Medium, -0.01f));
        g.setColour (th.ink);
        g.drawText (text, where, Justification::centredLeft, false);
    }
}

void LivePlanetEditor::paint (Graphics& g)
{
    const auto& th = *lnf.th;
    auto bounds = getLocalBounds().toFloat();
    ColourGradient bg (th.skyTop, 0, 0, th.skyBottom, 0, bounds.getBottom(), false);
    g.setGradientFill (bg);
    g.fillRect (bounds);

    // ---- top bar: the wordmark, the tabs, the library and tempo
    {
        g.setColour (th.line.withAlpha (0.8f));
        g.drawLine (0.0f, 68.5f, bounds.getRight(), 68.5f, 1.0f);
        // wordmark: heavy, slanted
        GlyphArrangement ga;
        ga.addLineOfText (font (26.0f, lp::ui::Bold, -0.04f), lp::ui::productName(), 0.0f, 0.0f);
        Path word;
        ga.createPath (word);
        const auto wb = word.getBounds();
        word.applyTransform (AffineTransform::translation (-wb.getX(), -wb.getY())
                                 .followedBy (AffineTransform::shear (-0.18f, 0.0f))
                                 .followedBy (AffineTransform::translation (32.0f, 34.0f - wb.getHeight() * 0.5f)));
        g.setColour (th.ink);
        g.fillPath (word);
        // a small "beta" badge
        auto badge = Rectangle<float> (word.getBounds().getRight() + 10.0f, 27.0f, 40.0f, 16.0f);
        g.setColour (th.grass.withAlpha (0.18f));
        g.fillRoundedRectangle (badge, 4.0f);
        g.setFont (lp::ui::mono (9.0f, true, 0.12f));
        g.setColour (th.grassLight);
        g.drawText ("BETA", badge, Justification::centred, false);

        static const char* tabs[] = { "WRITE", "STUDIO", "VOCALS", "GRAINS", "SOUND" };
        for (int i = 0; i < 5; ++i)
        {
            auto t = tabBounds (i).toFloat();
            const bool on = tabPage (i) == page;
            g.setFont (lp::ui::mono (12.0f, on, 0.14f));
            g.setColour (on ? th.ink : th.inkDim);
            g.drawText (tabs[i], t, Justification::centred, false);
            if (on)
            {
                g.setColour (th.ink);
                g.fillRect (t.getX(), 66.0f, t.getWidth(), 2.0f);
            }
        }

        // right: the library and the project's tempo, as quiet chips
        const auto st = proc.songJob.getStatus();
        auto right = Rectangle<float> (bounds.getRight() - 32.0f - 360.0f, 20.0f, 360.0f, 30.0f);
        auto chipAt = [&] (const String& text, bool lit)
        {
            const float w = GlyphArrangement::getStringWidth (lp::ui::mono (10.5f, false, 0.12f), text) + 30.0f;
            auto c = right.removeFromRight (w);
            right.removeFromRight (8.0f);
            g.setColour (th.body);
            g.fillRoundedRectangle (c, 15.0f);
            g.setColour (th.line);
            g.drawRoundedRectangle (c.reduced (0.5f), 15.0f, 1.0f);
            g.setColour (lit ? th.grass : th.inkDim.withAlpha (0.6f));
            g.fillEllipse (c.getX() + 11.0f, c.getCentreY() - 3.0f, 6.0f, 6.0f);
            g.setFont (lp::ui::mono (10.5f, false, 0.12f));
            g.setColour (th.ink.withAlpha (0.85f));
            g.drawText (text, c.withTrimmedLeft (22.0f), Justification::centredLeft, false);
        };
        const double bpm = proc.dawBpm.load();
        chipAt (bpm > 1.0 ? String (bpm, bpm == std::round (bpm) ? 0 : 1) + " BPM" : String ("NO DAW TEMPO"), bpm > 1.0);
        chipAt (st.scanning ? String ("SCANNING LIBRARY") : String (st.librarySounds) + " SAMPLES", ! st.scanning && st.librarySounds > 0);
        // undo: the last change to the song, taken back (Cmd + Z)
        undoChip = {};
        undoShown = st.undoWhat;
        if (st.undoWhat.isNotEmpty() && st.haveSong)
        {
            const String text = "UNDO " + st.undoWhat.toUpperCase();
            const float w = std::min (260.0f, GlyphArrangement::getStringWidth (lp::ui::mono (10.5f, false, 0.12f), text) + 26.0f);
            auto c = right.removeFromRight (w);
            const bool over = isMouseOver (true) && c.contains (getMouseXYRelative().toFloat());
            g.setColour (over ? th.grass.withAlpha (0.35f) : th.grass.withAlpha (0.18f));
            g.fillRoundedRectangle (c, 15.0f);
            g.setColour (th.grass);
            g.drawRoundedRectangle (c.reduced (0.5f), 15.0f, 1.0f);
            g.setFont (lp::ui::mono (10.5f, false, 0.12f));
            g.setColour (th.ink);
            g.drawText (text, c.reduced (12.0f, 0.0f), Justification::centred, true);
            undoChip = c;
        }
    }

    // ---- page cards
    if (page == Write || page == Studio)
    {
        card (g, cardTimeline, th);
        heading (g, page == Write ? "Your song" : "Arrangement", cardTimeline.toFloat().reduced (20.0f, 0.0f).withHeight (page == Studio ? 40.0f : 46.0f), th);
    }
    if (page == Studio && proc.songJob.getStatus().haveSong)
    {
        // the divider's grip (drag it; double-click it to hide or show the open track)
        {
            const auto sb = splitBounds();
            g.setColour (th.inkDim.withAlpha (draggingSplit ? 0.9f : 0.45f));
            g.fillRoundedRectangle (Rectangle<float> ((float) sb.getCentreX() - 22.0f, (float) sb.getCentreY() - 1.5f, 44.0f, 3.0f), 1.5f);
        }
        // the section tools' row label, over the track names
        g.setFont (lp::ui::mono (10.0f, true, 0.16f));
        g.setColour (th.grassLight);
        if (! aiLayer.isVisible())
            g.drawText ("SECTIONS", Rectangle<int> (cardTimeline.getX() + 20, songView.getY() + 4, 120, 24), Justification::centredLeft, false);
        card (g, cardDock, th);
        if (dockCollapsed)
        {
            g.setFont (lp::ui::mono (10.0f, false, 0.14f));
            g.setColour (th.inkDim);
            g.drawText (String::fromUTF8 ("THE OPEN TRACK IS HIDDEN  \xc2\xb7  CLICK A TRACK, OR DOUBLE-CLICK THE LINE ABOVE, TO SHOW IT"),
                        cardDock.reduced (20, 0), Justification::centredLeft, true);
        }
        else
        {
        // which track is open, over its editor
        const auto st = proc.songJob.getStatus();
        const int t = trackEditor.selected();
        g.setFont (lp::ui::mono (10.0f, false, 0.14f));
        g.setColour (th.inkDim);
        g.drawText ("TRACK " + String (t + 1) + " OF " + String ((int) st.tracks.size()) + "  \xc2\xb7  DRAG THE LINE ABOVE FOR MORE ROOM, DOUBLE-CLICK IT TO HIDE THIS",
                    Rectangle<int> (cardDock.getX() + 20, cardDock.getY() + 10, cardDock.getWidth() - 320, 20), Justification::centredLeft, true);
        }
    }
    if (page == Studio && ! studioView.isVisible())
    {
        auto r = Rectangle<int> (28, 84, getWidth() - 56, getHeight() - 32 - 84);
        card (g, r, th);
        g.setFont (font (16.0f, lp::ui::Medium));
        g.setColour (th.inkDim);
        g.drawText ("Write a song first: every track shows up here to mix, swap and remix.", r, Justification::centred, false);
    }
    if (page == Sound)
    {
        card (g, cardInstrument, th);
        auto r = cardInstrument.toFloat().reduced (22.0f, 18.0f);
        heading (g, "Instrument", r.removeFromTop (26.0f), th);
        label (g, nowText, Rectangle<float> (r.getX(), r.getY() + 2.0f, r.getWidth(), 16.0f), th);
        const auto smp = proc.getSampleName();
        g.setFont (font (12.5f));
        g.setColour (th.inkDim);
        String line = smp.isEmpty() ? String ("or drop a sound here (a vocal from Ableton: drop it to chop it)") : smp + String (CharPointer_UTF8 ("  \xc2\xb7  ")) + proc.getSampleInfo();
        if (proc.samplerOn.load())
        {
            const SpinLock::ScopedLockType sl (proc.samplerInfoLock);
            line = proc.samplerName + String (CharPointer_UTF8 ("  \xc2\xb7  ")) + proc.samplerInfo;
        }
        if (proc.recording.load()) line = "recording your voice: sing with short gaps between words, then click Stop";
        else if (chopMessage.isNotEmpty() && Time::getMillisecondCounter() < chopMessageUntil) line = chopMessage;
        g.drawText (line, Rectangle<int> (cardInstrument.getX() + 22, oneShotButton.getBottom() + 4, cardInstrument.getWidth() - 44, 20),
                    Justification::centredLeft, true);
        label (g, "key", keyStepper->getBounds().translated (0, -20).withHeight (16).toFloat(), th);
        label (g, "scale", scaleStepper->getBounds().translated (0, -20).withHeight (16).toFloat(), th);
        label (g, "keyboard", keySwitch.getBounds().translated (0, -20).withHeight (16).toFloat(), th);
        for (auto [rect, name] : { std::pair { plateMix, "Mix" }, { plateTex, "Texture" }, { plateFx, "Space" } })
        {
            card (g, rect, th);
            heading (g, name, rect.toFloat().reduced (22.0f, 14.0f).withHeight (24.0f), th);
        }
    }
    if (page == Edit)
        card (g, Rectangle<int> (28, 84, getWidth() - 56, 644), th);
    if (false)
    {
        card (g, cardSky, th);
        auto r = cardSky.toFloat().reduced (24.0f, 20.0f);
        label (g, "the weather plays the music", r.removeFromTop (16.0f), th);
        r.removeFromTop (10.0f);
        g.setFont (font (34.0f, lp::ui::Medium, -0.02f));
        g.setColour (th.ink);
        g.drawText (cityText, r.removeFromTop (44.0f), Justification::centredLeft, true);
        g.setFont (font (14.0f));
        g.setColour (th.inkDim);
        g.drawText (headerText, r.removeFromTop (22.0f), Justification::centredLeft, true);
        const bool live = proc.params.getRawParameterValue ("live")->load() > 0.5f;
        auto stRow = r.removeFromTop (30.0f);
        g.setColour (live ? Colour (0xff3fd98a) : Colour (0xffffa23a));
        g.fillEllipse (stRow.getX(), stRow.getCentreY() - 3.5f, 7.0f, 7.0f);
        label (g, statusText, stRow.withTrimmedLeft (16.0f), th);
        // the search box
        auto sb = cityBox.getBounds().toFloat();
        g.setColour (th.bodyDark);
        g.fillRoundedRectangle (sb, sb.getHeight() * 0.5f);
        g.setColour (cityBox.hasKeyboardFocus (true) ? th.grass.withAlpha (0.7f) : th.line);
        g.drawRoundedRectangle (sb.reduced (0.5f), sb.getHeight() * 0.5f, 1.0f);
        const float ix = sb.getX() + 20.0f, iy = sb.getCentreY();
        g.setColour (th.inkDim);
        g.drawEllipse (ix - 5.5f, iy - 5.5f, 11.0f, 11.0f, 1.4f);
        g.drawLine (ix + 4.0f, iy + 4.0f, ix + 8.0f, iy + 8.0f, 1.4f);

        card (g, cardWeather, th);
        auto w = cardWeather.toFloat().reduced (24.0f, 18.0f);
        heading (g, "Make your own weather", w.removeFromTop (26.0f), th);
        if (live)
        {
            g.setFont (font (13.0f));
            g.setColour (th.inkDim);
            g.drawText ("Switch to manual to set it yourself", w.removeFromTop (20.0f), Justification::centredLeft, false);
        }
    }
    label (g, "colour", themeDots.getBounds().translated (-58, 0).withWidth (52).toFloat(), th, Justification::centredRight);
}

// the Studio's divider: the gap between the arrangement and the open track
// an effect onto a track: switched on there, and the dock shows that track's effects
void LivePlanetEditor::addFx (int track, const String& id)
{
    const auto st = proc.songJob.getStatus();
    if (track < 0 || track >= (int) st.tracks.size()) return;
    lp::fx::add (proc, track, id);
    if (trackEditor.selected() != track) trackEditor.select (track);
    dockCollapsed = false;
    dockPageShown = 1;
    showDock();
    resized();
    repaint();
}

void LivePlanetEditor::showDock()
{
    const bool studio = page == Studio && proc.songJob.getStatus().haveSong && ! dockCollapsed;
    const bool fx = studio && dockPageShown == 1;
    trackEditor.setVisible (studio && ! fx);
    fxRack.setVisible (fx);
    if (fx) { fxRack.setTrack (trackEditor.selected()); fxRack.setBounds (trackEditor.getBounds()); }
}

Rectangle<int> LivePlanetEditor::splitBounds() const
{
    return page == Studio ? Rectangle<int> (cardTimeline.getX(), cardTimeline.getBottom(), cardTimeline.getWidth(), 10) : Rectangle<int>();
}

void LivePlanetEditor::mouseMove (const MouseEvent& e)
{
    if (e.eventComponent != this) return;
    const bool over = splitBounds().expanded (0, 3).contains (e.getPosition());
    setMouseCursor (over ? MouseCursor::UpDownResizeCursor : MouseCursor::NormalCursor);
}

void LivePlanetEditor::mouseDrag (const MouseEvent& e)
{
    if (! draggingSplit) return;
    // as far down as you like: past the bottom, the open track folds away and the arrangement has the whole page
    const int want = splitDragFrom + e.getDistanceFromDragStartY(), most = getHeight() - 32 - 84 - 120;
    dockCollapsed = want > most + 50;
    splitByHand = true;
    studioSplit = jlimit (200, most, want);
    resized();
    repaint();
}

void LivePlanetEditor::mouseUp (const MouseEvent&)
{
    if (draggingSplit) lp::setSetting ("studioSplit", studioSplit);
    draggingSplit = false;
}

void LivePlanetEditor::mouseDoubleClick (const MouseEvent& e)
{
    if (e.eventComponent != this || ! splitBounds().expanded (0, 3).contains (e.getPosition())) return;
    dockCollapsed = ! dockCollapsed; // (the open track hidden: the tracks get the whole page)
    resized();
    repaint();
}

void LivePlanetEditor::mouseDown (const MouseEvent& e)
{
    if (e.eventComponent == this && splitBounds().expanded (0, 3).contains (e.getPosition()))
    {
        draggingSplit = true;
        splitDragFrom = cardTimeline.getHeight();
        return;
    }
    if (e.eventComponent != &cityBox && ! cityBox.isParentOf (e.eventComponent) && cityBox.hasKeyboardFocus (true))
        cityBox.giveAwayKeyboardFocus();
    auto& pb = songPanel.promptBox;
    if (e.eventComponent != &pb && ! pb.isParentOf (e.eventComponent) && pb.hasKeyboardFocus (true))
        pb.giveAwayKeyboardFocus();
    if (e.eventComponent == this && ! undoChip.isEmpty() && undoChip.contains (e.position)) { proc.songJob.undo(); repaint(); return; }
    if (e.eventComponent == this)
        for (int i = 0; i < 5; ++i)
            if (tabBounds (i).expanded (8, 10).contains (e.getPosition())) { setPage (tabPage (i)); return; }
}

// the Export window (from the EXPORT pill, the Studio's Export button, or Cmd + E)
void LivePlanetEditor::openExport()
{
    auto* top = lp::ui::overlayHost (this);
    if (top == nullptr || exportPanel != nullptr || ! proc.songJob.getStatus().haveSong) return;
    SafePointer<LivePlanetEditor> self (this);
    exportPanel = std::make_unique<ExportPanel> (proc, [self] { MessageManager::callAsync ([self] { if (self != nullptr) self->exportPanel.reset(); }); });
    top->addAndMakeVisible (*exportPanel);
    exportPanel->setBounds (top->getLocalBounds());
    exportPanel->toFront (true);
}

bool LivePlanetEditor::keyPressed (const KeyPress& k)
{
    if (k.getModifiers().isCommandDown() && k.getKeyCode() == 'E') { openExport(); return true; }
    // Cmd + Z: undo the last change to the song (the note and drum editors keep their own undo while you edit there)
    if (k.getModifiers().isCommandDown() && k.getKeyCode() == 'Z' && proc.songJob.getStatus().undoWhat.isNotEmpty())
    {
        proc.songJob.undo();
        repaint();
        return true;
    }
    // the Grains page: your letters play it, wherever the keyboard focus landed on the page
    if (page == GrainsTab && grainsPage.isVisible() && grainsPage.keyPressed (k)) return true;
    return false;
}

void LivePlanetEditor::paintOverChildren (Graphics& g)
{
    if (dragging)
    {
        g.setColour (Colours::black.withAlpha (0.6f));
        g.fillAll();
        auto r = getLocalBounds().toFloat().reduced (40.0f);
        g.setColour (lnf.th->grass);
        const float dashes[] = { 8.0f, 6.0f };
        Path outline, dashed;
        outline.addRoundedRectangle (r, 18.0f);
        PathStrokeType (2.0f).createDashedStroke (dashed, outline, dashes, 2);
        g.fillPath (dashed);
        g.setColour (Colours::white);
        g.setFont (font (26.0f, lp::ui::Medium));
        g.drawText (page == Sound ? "Drop to play this sound" : "Drop a song to learn its feel", getLocalBounds(), Justification::centred);
    }
}

void LivePlanetEditor::resized()
{
    auto k = [this] (const char* id) { return &knobById[id]->slider; };
    const int W = getWidth(), M = 28;
    // (the window can be any size: the pages grow to fill it; B is where they end)
    const int B = getHeight() - 32;
    themeDots.setBounds (W - M - (lp::ui::numThemes * 22 + 4), getHeight() - 34, lp::ui::numThemes * 22 + 4, 24);
    tourBtn.setBounds (210, 26, 18, 18);  // right after the logo's beta badge
    if (tour != nullptr) tour->setBounds (getLocalBounds());
    sourcesPanel.setBounds (getLocalBounds());

    // ---- write: the hero + settings (song panel), the song, the drag buttons
    songPanel.setBounds (M, 84, W - 2 * M, 342);
    // the studio: the arrangement (sections over every track's lane), and the open track docked under it
    // (the effects browser, when open, takes a column on the left)
    const int L = page == Studio && browserOpen ? M + 206 + 12 : M;
    fxBrowser.setBounds (M, 84, 206, B - 84);
    fxBrowser.setVisible (page == Studio && browserOpen && proc.songJob.getStatus().haveSong);
    cardTimeline = page == Studio ? Rectangle<int> (L, 84, W - M - L, dockCollapsed ? B - 84 - 10 - 40 : dockTall && ! splitByHand ? jmin (232, studioSplit) : studioSplit)
                                  : Rectangle<int> (M, 442, W - 2 * M, B - 442);
    cardDock = { cardTimeline.getX(), cardTimeline.getBottom() + 10, cardTimeline.getWidth(), B - cardTimeline.getBottom() - 10 };
    if (page == Studio)
    {
        studioView.setBounds (cardTimeline.getX() + 2, cardTimeline.getY() + 82, cardTimeline.getWidth() - 4, cardTimeline.getHeight() - 86);
        // (the ruler's sections line up with the lanes below)
        const int lx = studioView.getX() + (int) studioView.laneLeft(), rx = studioView.getX() + (int) studioView.laneRight();
        songView.setBounds (lx - 8, cardTimeline.getY() + 34, rx - lx + 16, 50);
        trackEditor.setBounds (cardDock.getX() + 18, cardDock.getY() + 34, cardDock.getWidth() - 36, jmax (10, cardDock.getHeight() - 42));
        showDock();
        dockTabs.setVisible (! dockCollapsed && proc.songJob.getStatus().haveSong);
        dockTabs.setBounds (cardDock.getRight() - 18 - 340, cardDock.getY() + 8, 340, 24);
        fxBtn.setBounds (cardTimeline.getX() + 196, cardTimeline.getY() + 9, 96, 30);
        exportBtn.setBounds (fxBtn.getRight() + 8, fxBtn.getY(), 90, 30);
    }
    else
        songView.setBounds (cardTimeline.getX() + 12, cardTimeline.getY() + 46, cardTimeline.getWidth() - 24, cardTimeline.getHeight() - 104);
    {
        const int y = cardTimeline.getBottom() - 54;
        playButton.setBounds (cardTimeline.getX() + 18, y + 2, 40, 40);
        int px = cardTimeline.getX() + 74;
        const int pw = (cardTimeline.getRight() - 18 - px) / 6;
        for (auto* pill : { &pillMidi, &pillStems, &pillMix, &pillHelper, &pillFolder, &pillExport })
        {
            pill->setBounds (px, y + 4, pw - 8, 36);
            px += pw;
        }
        hMidi.setTopLeftPosition (pillMidi.getRight() - 24, pillMidi.getY() + 10);
        hStems.setTopLeftPosition (pillStems.getRight() - 24, pillStems.getY() + 10);
        hLocators.setTopLeftPosition (pillHelper.getRight() - 24, pillHelper.getY() + 10);
    }
    studioMode.setBounds (cardTimeline.getRight() - 18 - 92, cardTimeline.getY() + 12, 92, 26);
    // (over the track names, left of the sections it plays over)
    aiLayer.setBounds (cardTimeline.getX() + 16, songView.getY() + 8, (int) studioView.laneLeft() - 28, 34);
    studioKey.setBounds (cardTimeline.getRight() - 18 - 92 - 8 - 128, cardTimeline.getY() + 12, 128, 26);
    masterAB.setBounds (studioKey.getX() - 24 - 72, cardTimeline.getY() + 12, 72, 26);
    {
        // takes, top right of the song on the write page
        int x = cardTimeline.getRight() - 18 - 104;
        mixTakes.setBounds (x, cardTimeline.getY() + 12, 104, 26);
        for (int i = 2; i >= 0; --i) { x -= 8 + 78; takeBtn[i].setBounds (x, cardTimeline.getY() + 12, 78, 26); }
    }
    masterBox.setBounds (masterAB.getX() - 8 - 236, cardTimeline.getY() + 12, 236, 26);
    masterInfo.setBounds (masterBox.getX() - 8 - 240, cardTimeline.getY() + 12, 240, 26);
    masterMeter.setBounds (masterBox.getX() - 26, cardTimeline.getY() + 8, 13, 34);
    vocalTabs.setBounds (M, 84, 420, 28);
    {
        const int top = voiceOn() ? 124 : 84;
        voicePage.setBounds (M, top, W - 2 * M, B - top);
        vocalsHub.setBounds (M, top, W - 2 * M, B - top);
        grainsPage.setBounds (M, 84, W - 2 * M, B - 84);
        drawSing.setBounds (M, top, W - 2 * M, B - top);
    }
    if (page == Studio) playButton.setBounds (cardTimeline.getX() + 150, cardTimeline.getY() + 6, 30, 30);

    // ---- sound: the instrument on the left, three knob cards on the right
    cardInstrument = { M, 84, 500, 644 };
    {
        auto r = cardInstrument.reduced (22, 18);
        r.removeFromTop (52);
        presetBrowser.setBounds (r.removeFromTop (34));
        r.removeFromTop (14);
        keys.setBounds (r.removeFromTop (190));
        r.removeFromTop (12);
        {
            auto lr = r.removeFromTop (34);
            loadButton.setBounds (lr.removeFromLeft (124));
            lr.removeFromLeft (8);
            oneShotButton.setBounds (lr.removeFromLeft (156));
            lr.removeFromLeft (8);
            chopButton.setBounds (lr.removeFromLeft (136));
            inMeter.setBounds (chopButton.getRight() + 6, chopButton.getY() + 2, 9, chopButton.getHeight() - 4);
        }
        r.removeFromTop (46); // (room for what's loaded)
        auto row = r.removeFromTop (32);
        const int sw = (row.getWidth() - 20) / 3;
        keyStepper->setBounds (row.removeFromLeft (sw)); row.removeFromLeft (10);
        scaleStepper->setBounds (row.removeFromLeft (sw)); row.removeFromLeft (10);
        keySwitch.setBounds (row);
        r.removeFromTop (16);
        sampleView.setBounds (r.removeFromTop (130));
        dragNotes.setBounds (cardInstrument.getRight() - 22 - 210, cardInstrument.getBottom() - 18 - 36, 210, 36);
        if (page == Sound) playButton.setBounds (cardInstrument.getX() + 22, cardInstrument.getBottom() - 18 - 40, 40, 40);
    }
    const int rx = M + 500 + 16, rw = W - M - rx;
    plateMix = { rx, 84, rw, 158 };
    plateTex = { rx, 258, rw, 300 };
    plateFx  = { rx, 574, rw, 154 };
    grainView.setBounds (rx + 20, plateTex.getY() + 50, rw - 40, 92);
    {
        const int cw = (rw - 24) / 3;
        k ("pad")->setBounds   (rx + 12,          plateMix.getY() + 40, cw, 112);
        k ("drops")->setBounds (rx + 12 + cw,     plateMix.getY() + 40, cw, 112);
        k ("bass")->setBounds  (rx + 12 + 2 * cw, plateMix.getY() + 40, cw, 112);
        const int c4 = (rw - 24) / 4;
        const int ty = plateTex.getY() + 152;
        k ("texture")->setBounds  (rx + 12,          ty + 8, c4, 132);
        k ("sparkle")->setBounds  (rx + 12 + c4,     ty + 8, c4, 132);
        k ("grain")->setBounds    (rx + 12 + 2 * c4, ty, c4, 146);
        k ("position")->setBounds (rx + 12 + 3 * c4, ty + 8, c4, 132);
        const char* fx[] = { "warmth", "echo", "space", "shimmer", "nostalgia", "atmos", "gain" };
        const int c7 = (rw - 16) / 7;
        for (int i = 0; i < 7; ++i)
            k (fx[i])->setBounds (rx + 8 + i * c7, plateFx.getY() + 40, c7, 108);
    }

    // ---- sky: the city on the left, your own weather on the right
    cardSky = { M, 84, 500, 300 };
    cityBox.setBounds (cardSky.getX() + 24, cardSky.getBottom() - 24 - 44, cardSky.getWidth() - 48 - 150, 44);
    liveSwitch.setBounds (cardSky.getRight() - 24 - 140, cardSky.getBottom() - 24 - 40, 140, 36);
    cardWeather = { rx, 84, rw, 300 };
    {
        const char* top[] = { "temp", "rain", "snow", "wind" };
        const char* bottom[] = { "clouds", "pressure", "humidity" };
        const int c4 = (rw - 24) / 4;
        for (int i = 0; i < 4; ++i) k (top[i])->setBounds (rx + 12 + i * c4, cardWeather.getY() + 66, c4, 96);
        for (int i = 0; i < 3; ++i) k (bottom[i])->setBounds (rx + 12 + i * c4, cardWeather.getY() + 172, c4, 96);
        dayToggle.setBounds (rx + 12 + 3 * c4 + 6, cardWeather.getY() + 186, c4 - 24, 28);
        thunderToggle.setBounds (rx + 12 + 3 * c4 + 6, cardWeather.getY() + 224, c4 - 24, 28);
    }
}

//==============================================================================

bool LivePlanetEditor::isInterestedInFileDrag (const StringArray& files)
{
    for (auto& f : files)
        if (File (f).hasFileExtension ("wav;aif;aiff;flac;mp3;ogg;m4a"))
            return true;
    return false;
}

void LivePlanetEditor::filesDropped (const StringArray& files, int, int)
{
    dragging = false;
    if (page == Write || page == Studio) // song mode: the dropped song becomes the reference
    {
        if (! files.isEmpty()) proc.songJob.setReference (File (files[0]));
        repaint();
        return;
    }
    // the sound page: what to do with it (a vocal recording from Ableton: chop it)
    File file;
    for (auto& f : files) if (File (f).existsAsFile()) { file = File (f); break; }
    if (! file.existsAsFile()) return;
    PopupMenu m;
    m.addSectionHeader (file.getFileNameWithoutExtension());
    m.addItem (4, "Make a catchy chop hook for my song (a new track)", proc.songJob.hasSong());
    m.addItem (5, "Auto-tune it to my song: natural (a new track)", proc.songJob.hasSong());
    m.addItem (6, "Auto-tune it to my song: hard (a new track)", proc.songJob.hasSong());
    m.addItem (1, "Chop it: one syllable on each key (for vocals)");
    m.addItem (2, "Play it across the keyboard (a one-shot)");
    m.addItem (3, "Make it the instrument's sound (grains)");
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&chopButton), [this, file] (int r)
    {
        bool ok = false;
        if (r == 1)
        {
            const int n = proc.loadVocalChops (file);
            ok = n > 0;
            chopMessage = ok ? String (n) + " chops on the keys from C3 up: play them in Play mode"
                             : String ("couldn't find syllables in it: try a recording with short gaps between words");
            chopMessageUntil = Time::getMillisecondCounter() + 8000;
        }
        else if (r == 4)
        {
            proc.songJob.addChopHookFrom (file);
            chopMessage = "writing a vocal-chop hook from your vocal: it's added to your song (Studio) in a few seconds";
            chopMessageUntil = Time::getMillisecondCounter() + 8000;
        }
        else if (r == 5 || r == 6)
        {
            proc.songJob.addTunedVocal (file, r == 6);
            chopMessage = "tuning your vocal to the song's key: it's added as a track (Studio) in a few seconds";
            chopMessageUntil = Time::getMillisecondCounter() + 8000;
        }
        else if (r == 2) ok = proc.loadSamplerInstrument (file);
        else if (r == 3) { proc.samplerOff(); proc.loadSample (file); }
        if (ok) if (auto* km = proc.params.getParameter ("keymode")) km->setValueNotifyingHost (0.0f); // Play mode, ready to play
        repaint();
    });
    repaint();
}

Component* lp::ui::overlayHost (Component* from)
{
    if (from == nullptr) return nullptr;
    if (auto* e = dynamic_cast<LivePlanetEditor*> (from)) return e;
    if (auto* e = from->findParentComponentOfClass<LivePlanetEditor>()) return e;
    return from->getTopLevelComponent();
}

// the window: Loopwright is laid out at its own size and scaled to whatever size the window is dragged to
LivePlanetWindow::LivePlanetWindow (LivePlanetProcessor& p) : AudioProcessorEditor (p), view (p)
{
    // the size you left it at, last time (read before anything can resize the window)
    const double scale = jlimit (0.6, 2.0, (double) lp::setting ("uiScale", 1.0));
    const int w0 = (int) lp::setting ("uiWidth", roundToInt (kW * scale)), h0 = (int) lp::setting ("uiHeight", roundToInt (kH * scale));
    addAndMakeVisible (view);
    view.setBounds (0, 0, kW, kH);
    setResizable (true, true);
    // any shape: as big as your screen (the pages grow into the room), or smaller (everything scales down)
    setResizeLimits ((int) (kW * 0.6), (int) (kH * 0.6), 8000, 5000);
    setSize (jmax ((int) (kW * 0.6), w0), jmax ((int) (kH * 0.6), h0));
    ready = true;
    if (p.wrapperType == AudioProcessor::wrapperType_Standalone)
    {
        // (so the space bar reaches it: the window takes the keys unless a text box or the arrangement has them)
        setWantsKeyboardFocus (true);
        addMouseListener (this, true);
        SafePointer<LivePlanetWindow> sp (this);
        MessageManager::callAsync ([sp] { if (sp != nullptr && sp->isShowing()) sp->grabKeyboardFocus(); });
    }
}

bool LivePlanetWindow::keyPressed (const KeyPress& k)
{
    if (k == KeyPress::spaceKey && processor.wrapperType == AudioProcessor::wrapperType_Standalone)
    {
        if (dynamic_cast<TextEditor*> (Component::getCurrentlyFocusedComponent()) != nullptr) return false; // (typing)
        view.togglePlay();
        return true;
    }
    if (static_cast<juce::Component&> (view).keyPressed (k)) return true; // (the keys reached the window itself: the page still gets them)
    return false;
}

void LivePlanetWindow::mouseDown (const MouseEvent&)
{
    // a click on something that doesn't take the keys: the window keeps them (for the space bar)
    if (processor.wrapperType != AudioProcessor::wrapperType_Standalone) return;
    auto* f = Component::getCurrentlyFocusedComponent();
    if (f == nullptr || (f != this && ! isParentOf (f))) grabKeyboardFocus();
}

void LivePlanetWindow::resized()
{
    // drawn at the scale that fits the window's narrower side; the other side gets more room (not stretched)
    const float scale = jlimit (0.6f, 2.0f, jmin ((float) getWidth() / (float) kW, (float) getHeight() / (float) kH));
    view.setBounds (0, 0, roundToInt (getWidth() / scale), roundToInt (getHeight() / scale));
    view.setTransform (AffineTransform::scale (scale));
    if (ready && (std::abs ((double) scale - lastSaved) > 0.01 || getWidth() != lastW || getHeight() != lastH)) // (remembered)
    {
        lastSaved = scale; lastW = getWidth(); lastH = getHeight();
        lp::setSetting ("uiScale", (double) scale);
        lp::setSetting ("uiWidth", getWidth());
        lp::setSetting ("uiHeight", getHeight());
    }
}

AudioProcessorEditor* LivePlanetProcessor::createEditor()
{
    return new LivePlanetWindow (*this);
}
