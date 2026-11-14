#include "PluginEditor.h"

using namespace juce;
using lp::ui::font;

// Help: the "?" bubbles next to confusing parts, and the first-run tour.

namespace
{
    const lp::ui::Theme& currentTheme() { return lp::ui::theme (0); }

    // what a "?" shows: a title and a few lines, in the plugin's look
    class HelpBubble : public Component
    {
    public:
        HelpBubble (const String& title, const String& text, const lp::ui::Theme& t) : th (t)
        {
            AttributedString s;
            s.setLineSpacing (3.0f);
            if (title.isNotEmpty()) s.append (title + "\n", font (15.0f, lp::ui::Medium), t.ink);
            s.append (text, font (13.0f), t.ink.withAlpha (0.82f));
            layout.createLayoutWithBalancedLineLengths (s, 300.0f);
            setSize ((int) layout.getWidth() + 8, (int) layout.getHeight() + 6);
        }
        void paint (Graphics& g) override { layout.draw (g, getLocalBounds().toFloat().reduced (4.0f, 3.0f)); }
    private:
        const lp::ui::Theme& th;
        TextLayout layout;
    };
}

void HelpDot::paint (Graphics& g)
{
    const auto* lnf = dynamic_cast<LivePlanetLookAndFeel*> (&getLookAndFeel());
    const auto& t = lnf != nullptr ? *lnf->th : currentTheme();
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    const bool over = isMouseOver();
    g.setColour (over ? t.grass : Colours::white.withAlpha (0.09f));
    g.fillEllipse (r);
    g.setColour (over ? Colours::white : t.grassLight.withAlpha (0.9f));
    g.drawEllipse (r.reduced (0.5f), 1.0f);
    g.setFont (font (r.getHeight() * 0.72f, lp::ui::Medium));
    g.drawText ("?", r.translated (0.0f, -0.5f), Justification::centred, false);
}

void HelpDot::mouseUp (const MouseEvent& e)
{
    if (! getLocalBounds().contains (e.getPosition())) return;
    if (onClick) { onClick(); return; }
    const auto* lnf = dynamic_cast<LivePlanetLookAndFeel*> (&getLookAndFeel());
    const auto& t = lnf != nullptr ? *lnf->th : currentTheme();
    CallOutBox::launchAsynchronously (std::make_unique<HelpBubble> (heading, body, t), getScreenBounds(), nullptr);
}

// ---- the tour

Tour::Tour (std::vector<Step> s, std::function<void (int)> showPage, std::function<void()> done)
    : steps (std::move (s)), setPage (std::move (showPage)), onDone (std::move (done))
{
    setWantsKeyboardFocus (true);
    setInterceptsMouseClicks (true, true);
    for (auto* b : { &back, &next }) { b->setWantsKeyboardFocus (false); addAndMakeVisible (*b); }
    next.getProperties().set ("primary", true);
    back.onClick = [this] { go (at - 1); };
    next.onClick = [this]
    {
        if (at + 1 < (int) steps.size()) { go (at + 1); return; }
        if (onDone) onDone();   // (this deletes the tour)
    };
}

bool Tour::keyPressed (const KeyPress& k)
{
    if (k == KeyPress::rightKey || k == KeyPress::returnKey) { next.triggerClick(); return true; }
    if (k == KeyPress::leftKey && at > 0) { go (at - 1); return true; }
    return true; // nothing else gets through while the tour is on
}

void Tour::go (int i)
{
    at = jlimit (0, (int) steps.size() - 1, i);
    if (steps[(size_t) at].page >= 0 && setPage) setPage (steps[(size_t) at].page);
    spotNow = steps[(size_t) at].spot ? steps[(size_t) at].spot() : Rectangle<int>();
    back.setVisible (at > 0);
    next.setButtonText (at + 1 < (int) steps.size() ? "next" : "start making music");
    resized();
    repaint();
    grabKeyboardFocus();
}

// the card sits beside what's lit up (below it if there's room, else above; the middle when nothing is)
Rectangle<int> Tour::card() const
{
    const int w = 440, h = 210;
    if (spotNow.isEmpty()) return Rectangle<int> (w, h).withCentre (getLocalBounds().getCentre());
    int y = spotNow.getBottom() + 18;
    if (y + h > getHeight() - 12) y = spotNow.getY() - h - 18;
    if (y < 12) y = jlimit (12, getHeight() - h - 12, spotNow.getCentreY() - h / 2);
    const int x = jlimit (16, getWidth() - w - 16, spotNow.getCentreX() - w / 2);
    return { x, y, w, h };
}

void Tour::resized()
{
    if (steps.empty()) return;
    auto c = card().reduced (22, 18);
    auto row = c.removeFromBottom (34);
    next.setBounds (row.removeFromRight (at + 1 < (int) steps.size() ? 96 : 170));
    row.removeFromRight (8);
    back.setBounds (row.removeFromRight (80));
}

void Tour::paint (Graphics& g)
{
    const auto* lnf = dynamic_cast<LivePlanetLookAndFeel*> (&getLookAndFeel());
    const auto& t = lnf != nullptr ? *lnf->th : currentTheme();
    // the dimmed screen, with the part we're talking about cut out and outlined
    Path dim;
    dim.addRectangle (getLocalBounds().toFloat());
    if (! spotNow.isEmpty())
    {
        dim.addRoundedRectangle (spotNow.toFloat().expanded (6.0f), 14.0f);
        dim.setUsingNonZeroWinding (false);
    }
    g.setColour (Colours::black.withAlpha (0.72f));
    g.fillPath (dim);
    if (! spotNow.isEmpty())
    {
        g.setColour (t.grass);
        g.drawRoundedRectangle (spotNow.toFloat().expanded (6.0f), 14.0f, 2.0f);
    }
    // the card
    const auto c = card().toFloat();
    g.setColour (t.bodyDark.interpolatedWith (Colour (0xff101014), 0.55f));
    g.fillRoundedRectangle (c, 16.0f);
    g.setColour (Colours::white.withAlpha (0.1f));
    g.drawRoundedRectangle (c.reduced (0.5f), 16.0f, 1.0f);
    g.setColour (t.grass);
    g.fillRoundedRectangle (Rectangle<float> (c.getX() + 22.0f, c.getY() + 1.0f, 36.0f, 3.0f), 1.5f);
    auto in = c.reduced (22.0f, 18.0f);
    const auto& st = steps[(size_t) at];
    g.setFont (lp::ui::mono (9.5f, true, 0.16f));
    g.setColour (t.grassLight);
    g.drawText (String (at + 1) + " / " + String ((int) steps.size()), in.removeFromTop (14.0f), Justification::centredLeft, false);
    in.removeFromTop (6.0f);
    g.setFont (font (20.0f, lp::ui::Medium, -0.01f));
    g.setColour (t.ink);
    g.drawText (st.title, in.removeFromTop (26.0f), Justification::centredLeft, true);
    in.removeFromTop (6.0f);
    in.removeFromBottom (40.0f);
    AttributedString s;
    s.setLineSpacing (3.0f);
    s.append (st.text, font (13.5f), t.ink.withAlpha (0.85f));
    TextLayout tl;
    tl.createLayout (s, in.getWidth());
    tl.draw (g, in);
    // progress dots
    const float dy = c.getBottom() - 36.0f;
    for (int i = 0; i < (int) steps.size(); ++i)
    {
        g.setColour (i == at ? t.grass : Colours::white.withAlpha (0.18f));
        g.fillEllipse (c.getX() + 22.0f + (float) i * 12.0f, dy, 6.0f, 6.0f);
    }
}
