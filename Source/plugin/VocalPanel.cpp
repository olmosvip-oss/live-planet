#include "PluginEditor.h"
#include "../song/Vocal.h"

using namespace juce;
using lp::ui::font;

// The Vocal chain window: a vocal track's chain, module by module (left), the picked module's controls
// and the EQ curve (right), and Analyze, which listens to the vocal and sets it all up. Every change is
// recorded into the track a moment after you stop turning, so what you drag out has it.

namespace
{
    struct Module { const char* name; const char* what; uint32 colour; };
    const Module modules[7] = {
        { "EQ",          "Cuts rumble and mud, lifts the words and the air.", 0xff7aa7ff },
        { "Compress",    "Evens out loud and quiet lines so every word is heard.", 0xff5ad19a },
        { "Saturate",    "Warmth and thickness, up to grit when you push it.", 0xfff2c14e },
        { "De-ess",      "Holds back sharp S, T and SH sounds only when they jump out.", 0xfff29a5a },
        { "Air & Width", "A soft double out to the sides; the low end stays in the middle.", 0xffb48cff },
        { "Delay",       "Echoes in time with your song, bouncing left and right.", 0xff4fd1c5 },
        { "Reverb",      "A room around the voice, kept out of the low end.", 0xffff7ab6 },
    };
    bool& moduleOn (lp::song::VocalChain& c, int i)
    {
        switch (i)
        {
            case 0: return c.eqOn; case 1: return c.compOn; case 2: return c.satOn; case 3: return c.deessOn;
            case 4: return c.airOn; case 5: return c.delayOn; default: return c.verbOn;
        }
    }
    const lp::ui::Theme& themeOf (Component& c)
    {
        const auto* lnf = dynamic_cast<LivePlanetLookAndFeel*> (&c.getLookAndFeel());
        return lnf != nullptr ? *lnf->th : lp::ui::theme (0);
    }
}

VocalPanel::VocalPanel (LivePlanetProcessor& p, int trackIndex, std::function<void()> closed)
    : proc (p), track (trackIndex), onClosed (std::move (closed))
{
    setWantsKeyboardFocus (true);
    chain = proc.songJob.vocalChainOf (track);
    seenVersion = proc.songJob.getStatus().version;
    analyzeBtn.getProperties().set ("primary", true);
    analyzeBtn.onClick = [this]
    {
        flush();
        analyzing = true;
        proc.songJob.analyzeVocal (track);
        repaint();
    };
    powerBtn.setClickingTogglesState (false);
    powerBtn.onClick = [this] { chain.on = ! chain.on; changed(); };
    for (auto* b : { &analyzeBtn, &powerBtn }) { b->setWantsKeyboardFocus (false); addAndMakeVisible (*b); }
    for (int i = 0; i < 3; ++i)
    {
        noteBtns[i].setButtonText (i == 0 ? "1/4" : i == 1 ? "Dotted 1/8" : "1/8");
        noteBtns[i].setClickingTogglesState (false);
        noteBtns[i].setWantsKeyboardFocus (false);
        noteBtns[i].onClick = [this, i] { chain.delayNote = i; changed(); };
        addChildComponent (noteBtns[i]);
    }
    buildKnobs();
    startTimerHz (10);
}

VocalPanel::~VocalPanel() { flush(); }

void VocalPanel::buildKnobs()
{
    // every module's knobs, made once; only the picked module's show
    auto add = [this] (int module, const char* label, double lo, double hi, float* value, std::function<String (double)> text)
    {
        auto k = std::make_unique<Knob>();
        k->module = module;
        k->label = label;
        k->value = value;
        auto& s = k->s;
        s.setName (String (label).toLowerCase());
        s.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
        s.setRotaryParameters (MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
        s.setRange (lo, hi, 0.0);
        s.getProperties().set ("size", 64);
        s.setMouseDragSensitivity (220);
        s.setValue (*value, dontSendNotification);
        s.textFromValueFunction = std::move (text);
        auto* raw = k.get();
        s.onValueChange = [this, raw] { *raw->value = (float) raw->s.getValue(); chain.on = true; moduleOn (chain, raw->module) = true; changed(); };
        addChildComponent (s);
        knobs.push_back (std::move (k));
    };
    auto pct = [] (double v) { return String (roundToInt (v * 100.0)) + " %"; };
    auto db = [] (double v) { return (v > 0.05 ? "+" : "") + String (v, 1) + " dB"; };
    add (0, "LOW CUT", 40.0, 250.0, &chain.lowCutHz, [] (double v) { return String (roundToInt (v)) + " Hz"; });
    add (0, "MUD", -12.0, 0.0, &chain.mudDb, db);
    add (0, "PRESENCE", -6.0, 8.0, &chain.presenceDb, db);
    add (0, "AIR", -6.0, 8.0, &chain.airDb, db);
    add (1, "AMOUNT", 0.0, 1.0, &chain.compAmount, pct);
    add (2, "DRIVE", 0.0, 1.0, &chain.drive, pct);
    add (3, "AMOUNT", 0.0, 1.0, &chain.deess, pct);
    add (4, "WIDTH", 0.0, 1.0, &chain.width, pct);
    add (5, "MIX", 0.0, 1.0, &chain.delayMix, pct);
    add (6, "SIZE", 0.0, 1.0, &chain.verbSize, pct);
    add (6, "MIX", 0.0, 1.0, &chain.verbMix, pct);
}

void VocalPanel::changed()
{
    dirty = true;
    changedAt = Time::getMillisecondCounter();
    repaint();
}

void VocalPanel::flush()
{
    if (! dirty) return;
    dirty = false;
    proc.songJob.setVocalChain (track, chain);
}

void VocalPanel::timerCallback()
{
    // recorded into the track a moment after you stop turning
    const bool turning = std::any_of (knobs.begin(), knobs.end(), [] (auto& k) { return k->s.isMouseButtonDown(); });
    if (dirty && ! turning && Time::getMillisecondCounter() - changedAt > 450) flush();
    const auto st = proc.songJob.getStatus();
    if (st.version != seenVersion)
    {
        seenVersion = st.version;
        // Analyze finished (or the track was recorded): show what the chain is now
        if (analyzing && ! st.editing)
        {
            analyzing = false;
            chain = proc.songJob.vocalChainOf (track);
            for (auto& k : knobs) k->s.setValue (*k->value, dontSendNotification);
        }
    }
    if (editing != st.editing) { editing = st.editing; }
    repaint();
}

Rectangle<int> VocalPanel::card() const
{
    return Rectangle<int> (jmin (980, getWidth() - 40), jmin (660, getHeight() - 40)).withCentre (getLocalBounds().getCentre());
}

Rectangle<int> VocalPanel::rowArea (int i) const
{
    auto c = card().reduced (28, 0);
    return Rectangle<int> (c.getX(), card().getY() + 112 + i * 54, 250, 48);
}

Rectangle<int> VocalPanel::detailArea() const
{
    auto c = card().reduced (28, 0);
    return Rectangle<int> (c.getX() + 274, card().getY() + 112, c.getWidth() - 274, card().getHeight() - 112 - 28);
}

void VocalPanel::resized()
{
    auto c = card();
    closeX = Rectangle<int> (c.getRight() - 50, c.getY() + 22, 30, 30);
    analyzeBtn.setBounds (c.getRight() - 196, c.getY() + 22, 132, 36);
    powerBtn.setBounds (c.getRight() - 330, c.getY() + 22, 124, 36);
    layoutDetail();
}

void VocalPanel::layoutDetail()
{
    const auto d = detailArea();
    std::vector<Knob*> shown;
    for (auto& k : knobs) { const bool on = k->module == picked; k->s.setVisible (on); if (on) shown.push_back (k.get()); }
    // EQ: the curve on top, its knobs under it; the others: knobs under the title
    const int knobTop = picked == 0 ? d.getY() + 222 : d.getY() + 96;
    const int w = 128;
    for (size_t i = 0; i < shown.size(); ++i) shown[i]->s.setBounds (d.getX() + 14 + (int) i * w, knobTop, w - 8, 116);
    for (int i = 0; i < 3; ++i)
    {
        noteBtns[i].setVisible (picked == 5);
        noteBtns[i].setBounds (d.getX() + 160 + i * 118, knobTop + 36, 110, 34);
    }
}

void VocalPanel::paint (Graphics& g)
{
    const auto& t = themeOf (*this);
    g.fillAll (Colours::black.withAlpha (0.7f));
    const auto cf = card().toFloat();
    {
        Graphics::ScopedSaveState s (g);
        Path clip; clip.addRoundedRectangle (cf, 20.0f);
        g.reduceClipRegion (clip);
        g.setColour (t.bodyDark.interpolatedWith (Colour (0xff0b0b0e), 0.65f));
        g.fillRect (cf);
        ColourGradient glow (t.grass.withAlpha (0.26f), cf.getRight() - 60.0f, cf.getY(), t.grass.withAlpha (0.0f), cf.getRight() - 380.0f, cf.getY() + 300.0f, true);
        g.setGradientFill (glow);
        g.fillRect (cf);
    }
    g.setColour (Colours::white.withAlpha (0.1f));
    g.drawRoundedRectangle (cf.reduced (0.5f), 20.0f, 1.0f);

    // header
    const auto st = proc.songJob.getStatus();
    const String name = track < (int) st.tracks.size() ? String (st.tracks[(size_t) track].name) : String ("vocal");
    auto r = cf.reduced (28.0f, 24.0f);
    g.setFont (lp::ui::mono (10.0f, true, 0.18f));
    g.setColour (t.grassLight);
    g.drawText ("VOCAL CHAIN", r.removeFromTop (14.0f), Justification::centredLeft, false);
    g.setFont (font (26.0f, lp::ui::Medium, -0.02f));
    g.setColour (t.ink);
    g.drawText (name.substring (0, 1).toUpperCase() + name.substring (1), r.removeFromTop (38.0f).withTrimmedRight (360.0f), Justification::centredLeft, true);
    g.setFont (lp::ui::mono (10.0f, false, 0.12f));
    g.setColour (analyzing ? t.grassLight : t.inkDim);
    const String dotSep = String::fromUTF8 ("  \xc2\xb7  ");
    g.drawText (analyzing ? String ("LISTENING TO YOUR VOCAL...") : editing ? String ("RECORDING IT INTO THE TRACK...")
                : chain.on ? "ON" + dotSep + "RECORDED INTO THE TRACK" : "OFF" + dotSep + "THE VOCAL AS IT WAS",
                r.removeFromTop (16.0f), Justification::centredLeft, false);
    // close
    {
        auto x = closeX.toFloat().reduced (9.0f);
        g.setColour (Colours::white.withAlpha (closeX.contains (getMouseXYRelative()) ? 0.9f : 0.45f));
        g.drawLine (x.getX(), x.getY(), x.getRight(), x.getBottom(), 1.6f);
        g.drawLine (x.getRight(), x.getY(), x.getX(), x.getBottom(), 1.6f);
    }
    powerBtn.setButtonText (chain.on ? "Chain: on" : "Chain: off");
    powerBtn.setToggleState (chain.on, dontSendNotification);

    // the chain, in order
    for (int i = 0; i < 7; ++i)
    {
        const auto row = rowArea (i).toFloat();
        const bool on = chain.on && moduleOn (chain, i), sel = i == picked;
        const Colour col (modules[i].colour);
        g.setColour (sel ? Colours::white.withAlpha (0.07f) : Colours::white.withAlpha (row.contains (getMouseXYRelative().toFloat()) ? 0.04f : 0.0f));
        g.fillRoundedRectangle (row, 12.0f);
        if (sel) { g.setColour (col.withAlpha (0.55f)); g.drawRoundedRectangle (row.reduced (0.5f), 12.0f, 1.0f); }
        g.setFont (lp::ui::mono (10.5f, false, 0.1f));
        g.setColour (t.inkDim);
        g.drawText (String (i + 1).paddedLeft ('0', 2), row.withWidth (44.0f), Justification::centred, false);
        g.setFont (font (17.0f, lp::ui::Medium));
        g.setColour (on ? t.ink : t.ink.withAlpha (0.4f));
        g.drawText (modules[i].name, row.withTrimmedLeft (44.0f).withTrimmedRight (44.0f), Justification::centredLeft, false);
        // its switch: a dot, lit when on
        const auto dot = Rectangle<float> (row.getRight() - 30.0f, row.getCentreY() - 7.0f, 14.0f, 14.0f);
        if (on) { g.setColour (col.withAlpha (0.25f)); g.fillEllipse (dot.expanded (5.0f)); }
        g.setColour (on ? col : Colours::white.withAlpha (0.16f));
        g.fillEllipse (dot);
    }
    // a line down the chain, joining the dots
    {
        const float x = (float) rowArea (0).getRight() - 23.0f;
        g.setColour (Colours::white.withAlpha (0.06f));
        g.drawLine (x, (float) rowArea (0).getCentreY() + 12.0f, x, (float) rowArea (6).getCentreY() - 12.0f, 1.0f);
    }

    // the picked module
    const auto d = detailArea().toFloat();
    {
        auto panel = d;
        g.setColour (Colours::black.withAlpha (0.28f));
        g.fillRoundedRectangle (panel, 16.0f);
        g.setColour (Colours::white.withAlpha (0.06f));
        g.drawRoundedRectangle (panel.reduced (0.5f), 16.0f, 1.0f);
    }
    const Colour col (modules[picked].colour);
    auto head = d.reduced (22.0f, 18.0f);
    g.setColour (col);
    g.fillEllipse (head.getX(), head.getY() + 8.0f, 9.0f, 9.0f);
    g.setFont (font (22.0f, lp::ui::Medium, -0.01f));
    g.setColour (t.ink);
    g.drawText (modules[picked].name, head.withTrimmedLeft (18.0f).removeFromTop (26.0f), Justification::centredLeft, false);
    head.removeFromTop (28.0f);
    g.setFont (font (13.5f));
    g.setColour (t.ink.withAlpha (0.65f));
    g.drawText (modules[picked].what, head.removeFromTop (20.0f), Justification::centredLeft, true);
    if (! moduleOn (chain, picked))
    {
        g.setFont (lp::ui::mono (10.0f, false, 0.12f));
        g.setColour (Colour (0xfff2a33a));
        g.drawText ("OFF: CLICK ITS DOT TO TURN IT ON", head.removeFromTop (18.0f), Justification::centredLeft, false);
    }

    // EQ: the curve the four filters make, drawn from the filters themselves
    if (picked == 0)
    {
        auto graph = Rectangle<float> (d.getX() + 22.0f, d.getY() + 88.0f, d.getWidth() - 44.0f, 124.0f);
        g.setColour (Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (graph, 10.0f);
        auto xOf = [&] (double f) { return graph.getX() + graph.getWidth() * (float) (std::log (f / 20.0) / std::log (20000.0 / 20.0)); };
        auto yOf = [&] (double dB) { return graph.getCentreY() - graph.getHeight() * 0.5f * (float) jlimit (-1.0, 1.0, dB / 15.0); };
        g.setFont (lp::ui::mono (9.0f, false, 0.08f));
        for (double f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
        {
            g.setColour (Colours::white.withAlpha (0.05f));
            g.drawVerticalLine ((int) xOf (f), graph.getY(), graph.getBottom());
            g.setColour (Colours::white.withAlpha (0.3f));
            g.drawText (f >= 1000.0 ? String ((int) (f / 1000.0)) + "k" : String ((int) f), Rectangle<float> (xOf (f) + 3.0f, graph.getBottom() - 14.0f, 30.0f, 12.0f), Justification::centredLeft, false);
        }
        g.setColour (Colours::white.withAlpha (0.1f));
        g.drawHorizontalLine ((int) graph.getCentreY(), graph.getX(), graph.getRight());
        Path curve, fill;
        for (int i = 0; i <= 200; ++i)
        {
            const double f = 20.0 * std::pow (1000.0, i / 200.0);
            const float x = xOf (f), y = yOf (lp::song::vocal::eqDbAt (chain, f));
            if (i == 0) { curve.startNewSubPath (x, y); fill.startNewSubPath (x, graph.getCentreY()); fill.lineTo (x, y); }
            else { curve.lineTo (x, y); fill.lineTo (x, y); }
        }
        fill.lineTo (graph.getRight(), graph.getCentreY());
        fill.closeSubPath();
        const bool on = chain.on && chain.eqOn;
        g.setColour (col.withAlpha (on ? 0.14f : 0.05f));
        g.fillPath (fill);
        g.setColour (col.withAlpha (on ? 0.95f : 0.35f));
        g.strokePath (curve, PathStrokeType (2.0f));
        // the bands' points
        const std::pair<double, double> pts[] = { { chain.lowCutHz, -3.0 }, { 300.0, chain.mudDb }, { 3000.0, chain.presenceDb }, { 10000.0, chain.airDb } };
        for (auto& [f, dB] : pts)
        {
            const auto pt = Point<float> (xOf (f), yOf (dB));
            g.setColour (Colours::black);
            g.fillEllipse (Rectangle<float> (9.0f, 9.0f).withCentre (pt));
            g.setColour (col);
            g.drawEllipse (Rectangle<float> (9.0f, 9.0f).withCentre (pt), 1.6f);
        }
    }
    if (picked == 5)
    {
        g.setFont (lp::ui::mono (9.5f, false, 0.14f));
        g.setColour (t.inkDim);
        g.drawText ("TIME", Rectangle<float> ((float) noteBtns[0].getX(), (float) noteBtns[0].getY() - 18.0f, 200.0f, 12.0f), Justification::centredLeft, false);
        for (int i = 0; i < 3; ++i) noteBtns[i].setToggleState (chain.delayNote == i, dontSendNotification);
    }

    // what Analyze found, at the bottom of the panel
    auto findings = Rectangle<float> (d.getX() + 22.0f, d.getBottom() - 132.0f, d.getWidth() - 44.0f, 116.0f);
    g.setColour (Colours::white.withAlpha (0.035f));
    g.fillRoundedRectangle (findings, 12.0f);
    auto fr = findings.reduced (16.0f, 12.0f);
    g.setFont (lp::ui::mono (9.5f, true, 0.16f));
    g.setColour (t.grassLight);
    g.drawText ("ANALYSIS", fr.removeFromTop (14.0f), Justification::centredLeft, false);
    fr.removeFromTop (6.0f);
    g.setFont (font (13.0f));
    if (chain.analysis.isEmpty())
    {
        g.setColour (t.ink.withAlpha (0.55f));
        g.drawFittedText ("Press Analyze: it listens to this vocal, sets up every module for it and tells you what it found.", fr.toNearestInt(), Justification::topLeft, 3);
    }
    else
    {
        auto lines = StringArray::fromLines (chain.analysis);
        for (auto& line : lines)
        {
            if (fr.getHeight() < 16.0f) break;
            auto lr = fr.removeFromTop (18.0f);
            g.setColour (t.grass);
            g.fillEllipse (lr.getX(), lr.getCentreY() - 2.5f, 5.0f, 5.0f);
            g.setColour (t.ink.withAlpha (0.85f));
            g.drawText (line, lr.withTrimmedLeft (14.0f), Justification::centredLeft, true);
        }
    }
}

void VocalPanel::mouseMove (const MouseEvent&) { repaint(); }

void VocalPanel::mouseUp (const MouseEvent& e)
{
    const auto p = e.getPosition();
    if (closeX.contains (p) || ! card().contains (p)) { if (onClosed) onClosed(); return; }
    for (int i = 0; i < 7; ++i)
    {
        const auto row = rowArea (i);
        if (! row.contains (p)) continue;
        // the dot switches the module; the rest of the row picks it
        if (Rectangle<int> (row.getRight() - 44, row.getY(), 44, row.getHeight()).contains (p))
        {
            auto& on = moduleOn (chain, i);
            on = ! (on && chain.on);
            if (on) chain.on = true;
            changed();
        }
        picked = i;
        layoutDetail();
        repaint();
        return;
    }
}

bool VocalPanel::keyPressed (const KeyPress& k)
{
    if (k == KeyPress::escapeKey) { if (onClosed) onClosed(); return true; }
    return false;
}
