#include "PluginEditor.h"
#include "../song/Vocal.h"
#include <juce_dsp/juce_dsp.h>

using namespace juce;
using lp::ui::font;
namespace V = lp::song::vocal;

// The Vocal chain window. The chain plays live in the studio (every knob is heard at once, nothing is
// recorded again); it's recorded into the track only when you drag it out. Left: the modules in order,
// each with its switch. Right: the picked module's live picture and its knobs, and what Analyze found.

namespace
{
    struct Module { const char* name; const char* what; uint32 colour; };
    const Module modules[7] = {
        { "EQ",          "Drag the points anywhere: left and right for where, up and down for how much. Behind them, your vocal live.", 0xff7aa7ff },
        { "Compress",    "Evens out loud and quiet lines so every word sits in front.", 0xff5ad19a },
        { "Saturate",    "Warmth and thickness; push it for grit. The curve shows how it bends the sound.", 0xfff2c14e },
        { "De-ess",      "Catches sharp S, T and SH sounds, only when they jump out.", 0xfff29a5a },
        { "Air & Width", "A soft double out to the sides; below Focus it all stays in the middle.", 0xffb48cff },
        { "Delay",       "Echoes in time with your song. Ducking keeps them out of the way while you sing.", 0xff4fd1c5 },
        { "Reverb",      "A smooth room around the voice, kept out of the low end.", 0xffff7ab6 },
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
    constexpr int fftOrder = 12, fftSize = 1 << fftOrder;
    // frequency <-> x on a log scale (20 Hz .. 20 kHz)
    float xOfHz (Rectangle<float> g, double f) { return g.getX() + g.getWidth() * (float) (std::log (f / 20.0) / std::log (1000.0)); }
    double hzOfX (Rectangle<float> g, float x) { return 20.0 * std::pow (1000.0, (double) jlimit (0.0f, 1.0f, (x - g.getX()) / g.getWidth())); }
    void panelBox (Graphics& g, Rectangle<float> r)
    {
        g.setColour (Colours::black.withAlpha (0.38f));
        g.fillRoundedRectangle (r, 12.0f);
        g.setColour (Colours::white.withAlpha (0.05f));
        g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, 1.0f);
    }
    // a gain-reduction meter: a bar going down from the top, with its number
    void grMeter (Graphics& g, Rectangle<float> r, float grDb, Colour col, const lp::ui::Theme& t, const String& label)
    {
        panelBox (g, r);
        auto bar = r.reduced (16.0f, 30.0f).withTrimmedBottom (6.0f);
        g.setColour (Colours::white.withAlpha (0.05f));
        g.fillRoundedRectangle (bar, 5.0f);
        const float frac = jlimit (0.0f, 1.0f, -grDb / 18.0f);
        g.setColour (col);
        g.fillRoundedRectangle (bar.withHeight (bar.getHeight() * frac), 5.0f);
        for (int db : { 3, 6, 12 })
        {
            const float y = bar.getY() + bar.getHeight() * (float) db / 18.0f;
            g.setColour (Colours::white.withAlpha (0.12f));
            g.drawHorizontalLine ((int) y, bar.getX() - 4.0f, bar.getX());
        }
        g.setFont (lp::ui::mono (9.0f, false, 0.12f));
        g.setColour (t.inkDim);
        g.drawText (label, r.withHeight (24.0f).translated (0.0f, 6.0f), Justification::centred, false);
        g.setFont (lp::ui::mono (11.0f, true, 0.06f));
        g.setColour (t.ink);
        g.drawText (String (grDb, 1) + " dB", r.withTop (r.getBottom() - 26.0f), Justification::centred, false);
    }
}

VocalPanel::VocalPanel (LivePlanetProcessor& p, int trackIndex, std::function<void()> closed)
    : proc (p), track (trackIndex), onClosed (std::move (closed))
{
    setWantsKeyboardFocus (true);
    chain = proc.songJob.vocalChainOf (track);
    seenSerial = proc.songJob.getStatus().vocalSerial;
    spectrum.assign (fftSize / 2, -100.0f);
    proc.studio.scopeTrack = track;
    lastScope = proc.studio.scopeWrite.load();

    analyzeBtn.getProperties().set ("primary", true);
    analyzeBtn.onClick = [this] { save(); analyzing = true; proc.songJob.analyzeVocal (track); repaint(); };
    powerBtn.onClick = [this] { chain.on = ! chain.on; changed(); };
    playBtn.onClick = [this] { if (proc.studio.length() > 0) proc.previewPlaying = ! proc.previewPlaying.load(); };
    soloBtn.onClick = [this] { auto& m = proc.studio.mix[(size_t) jlimit (0, StudioPlayer::maxTracks - 1, track)]; m.solo = ! m.solo.load(); };
    pingBtn.onClick = [this] { chain.delayPingPong = ! chain.delayPingPong; changed(); };
    for (auto* b : { &analyzeBtn, &powerBtn, &playBtn, &soloBtn }) { b->setWantsKeyboardFocus (false); addAndMakeVisible (*b); }
    pingBtn.setWantsKeyboardFocus (false);
    addChildComponent (pingBtn);
    for (int i = 0; i < 3; ++i)
    {
        noteBtns[i].setButtonText (i == 0 ? "1/4" : i == 1 ? "Dotted 1/8" : "1/8");
        noteBtns[i].setWantsKeyboardFocus (false);
        noteBtns[i].onClick = [this, i] { chain.delayNote = i; changed(); };
        addChildComponent (noteBtns[i]);
    }
    buildKnobs();
    proc.studio.setVocalChain (track, chain);
    startTimerHz (30);
}

VocalPanel::~VocalPanel()
{
    save();
    if (proc.studio.scopeTrack.load() == track) proc.studio.scopeTrack = -1;
}

void VocalPanel::buildKnobs()
{
    auto add = [this] (int module, const char* label, double lo, double hi, float* value, std::function<String (double)> text, double reset)
    {
        auto k = std::make_unique<Knob>();
        k->module = module;
        k->value = value;
        auto& s = k->s;
        s.setName (String (label).toLowerCase());
        s.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
        s.setRotaryParameters (MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
        s.setRange (lo, hi, 0.0);
        s.getProperties().set ("size", 60);
        s.setMouseDragSensitivity (220);
        s.setDoubleClickReturnValue (true, reset);
        s.setValue (*value, dontSendNotification);
        s.textFromValueFunction = std::move (text);
        auto* raw = k.get();
        s.onValueChange = [this, raw] { *raw->value = (float) raw->s.getValue(); chain.on = true; moduleOn (chain, raw->module) = true; changed(); };
        addChildComponent (s);
        knobs.push_back (std::move (k));
    };
    auto pct = [] (double v) { return String (roundToInt (v * 100.0)) + " %"; };
    auto db = [] (double v) { return (v > 0.05 ? "+" : "") + String (v, 1) + " dB"; };
    auto hz = [] (double v) { return v >= 1000.0 ? String (v / 1000.0, 1) + " kHz" : String (roundToInt (v)) + " Hz"; };
    auto tilt = [] (double v) { return std::abs (v) < 0.03 ? String ("neutral") : v < 0 ? "darker " + String (roundToInt (-v * 100.0)) : "brighter " + String (roundToInt (v * 100.0)); };
    add (0, "LOW CUT", 30.0, 300.0, &chain.lowCutHz, hz, 90.0);
    add (0, "MUD", -12.0, 3.0, &chain.mudDb, db, 0.0);
    add (0, "PRESENCE", -6.0, 9.0, &chain.presenceDb, db, 0.0);
    add (0, "AIR", -6.0, 9.0, &chain.airDb, db, 0.0);
    add (1, "AMOUNT", 0.0, 1.0, &chain.compAmount, pct, 0.4);
    add (1, "SPEED", 0.0, 1.0, &chain.compSpeed, [] (double v) { return v < 0.33 ? String ("smooth") : v < 0.66 ? String ("medium") : String ("fast"); }, 0.5);
    add (2, "DRIVE", 0.0, 1.0, &chain.drive, pct, 0.2);
    add (2, "TONE", -1.0, 1.0, &chain.satTone, tilt, 0.0);
    add (2, "MIX", 0.0, 1.0, &chain.satMix, pct, 1.0);
    add (3, "AMOUNT", 0.0, 1.0, &chain.deess, pct, 0.35);
    add (3, "FREQUENCY", 3500.0, 11000.0, &chain.deessHz, hz, 6500.0);
    add (4, "WIDTH", 0.0, 1.0, &chain.width, pct, 0.25);
    add (4, "FOCUS", 80.0, 600.0, &chain.focusHz, hz, 220.0);
    add (5, "MIX", 0.0, 1.0, &chain.delayMix, pct, 0.18);
    add (5, "FEEDBACK", 0.0, 1.0, &chain.delayFeedback, pct, 0.35);
    add (5, "TONE", -1.0, 1.0, &chain.delayTone, tilt, 0.0);
    add (5, "DUCKING", 0.0, 1.0, &chain.delayDuck, pct, 0.5);
    add (6, "SIZE", 0.0, 1.0, &chain.verbSize, pct, 0.5);
    add (6, "MIX", 0.0, 1.0, &chain.verbMix, pct, 0.16);
    add (6, "PRE-DELAY", 0.0, 150.0, &chain.verbPredelayMs, [] (double v) { return String (roundToInt (v)) + " ms"; }, 20.0);
    add (6, "TONE", -1.0, 1.0, &chain.verbTone, tilt, 0.0);
    add (6, "DUCKING", 0.0, 1.0, &chain.verbDuck, pct, 0.3);
}

void VocalPanel::changed()
{
    proc.studio.setVocalChain (track, chain); // heard right away
    dirty = true;
    changedAt = Time::getMillisecondCounter();
    repaint();
}

void VocalPanel::save()
{
    if (! dirty) return;
    dirty = false;
    proc.songJob.setVocalChain (track, chain); // kept with the song
    seenSerial = proc.songJob.getStatus().vocalSerial;
}

void VocalPanel::readScope()
{
    // what the chain sent out since last time: the stereo picture and a fresh spectrum
    const int w = proc.studio.scopeWrite.load();
    constexpr int size = StudioPlayer::scopeSize;
    int fresh = (w - lastScope + size) & (size - 1);
    lastScope = w;
    const bool playing = fresh > 0;
    if (! playing)
    {
        // nothing new: the picture just settles (no spectrum maths)
        for (auto& v : spectrum) v = v + (-100.0f - v) * 0.08f;
        compGr *= 0.85f; deessGr *= 0.85f;
        return;
    }
    const int take = 1024;
    scopeL.resize ((size_t) take); scopeR.resize ((size_t) take);
    for (int i = 0; i < take; ++i)
    {
        const int at = (w - take + i + size) & (size - 1);
        scopeL[(size_t) i] = playing ? proc.studio.scopeL[(size_t) at] : 0.0f;
        scopeR[(size_t) i] = playing ? proc.studio.scopeR[(size_t) at] : 0.0f;
    }
    // spectrum of the newest samples (Hann window), smoothed: rises fast, falls slowly
    static dsp::FFT fft (fftOrder);
    std::vector<float> data ((size_t) fftSize * 2, 0.0f);
    float peak = 0.0f;
    for (int i = 0; i < fftSize; ++i)
    {
        const int at = (w - fftSize + i + size) & (size - 1);
        const float x = playing ? 0.5f * (proc.studio.scopeL[(size_t) at] + proc.studio.scopeR[(size_t) at]) : 0.0f;
        peak = std::max (peak, std::abs (x));
        data[(size_t) i] = x * (0.5f - 0.5f * std::cos (MathConstants<float>::twoPi * (float) i / (float) (fftSize - 1)));
    }
    fft.performFrequencyOnlyForwardTransform (data.data());
    for (int b = 0; b < fftSize / 2; ++b)
    {
        const float db = Decibels::gainToDecibels (data[(size_t) b] / (float) fftSize * 4.0f, -100.0f);
        auto& s = spectrum[(size_t) b];
        s = db > s ? db : s + (db - s) * 0.12f;
    }
    outDb = Decibels::gainToDecibels (peak, -90.0f);
    const float c = proc.studio.vocalCompGr (track), d = proc.studio.vocalDeessGr (track);
    compGr = c < compGr ? c : compGr + (c - compGr) * 0.15f;
    deessGr = d < deessGr ? d : deessGr + (d - deessGr) * 0.15f;
    if (! proc.previewPlaying.load()) { compGr *= 0.85f; deessGr *= 0.85f; }
}

void VocalPanel::timerCallback()
{
    const bool turning = std::any_of (knobs.begin(), knobs.end(), [] (auto& k) { return k->s.isMouseButtonDown(); }) || dragPoint >= 0;
    if (dirty && ! turning && Time::getMillisecondCounter() - changedAt > 600) save();
    const auto st = proc.songJob.getStatus();
    if (st.vocalSerial != seenSerial)
    {
        seenSerial = st.vocalSerial;
        if (analyzing) // Analyze finished: show (and play) what it set up
        {
            analyzing = false;
            chain = proc.songJob.vocalChainOf (track);
            for (auto& k : knobs) k->s.setValue (*k->value, dontSendNotification);
            proc.studio.setVocalChain (track, chain);
            repaint();
        }
    }
    const bool wasPlaying = livePicture;
    readScope();
    livePicture = proc.previewPlaying.load();
    playBtn.setButtonText (proc.previewPlaying.load() ? "Stop" : "Play");
    const bool solo = proc.studio.mix[(size_t) jlimit (0, StudioPlayer::maxTracks - 1, track)].solo.load();
    soloBtn.setToggleState (solo, dontSendNotification);
    soloBtn.setButtonText (solo ? "Solo: on" : "Solo vocal");
    powerBtn.setButtonText (chain.on ? "Chain: on" : "Chain: off");
    powerBtn.setToggleState (chain.on, dontSendNotification);
    pingBtn.setToggleState (chain.delayPingPong, dontSendNotification);
    for (int i = 0; i < 3; ++i) noteBtns[i].setToggleState (chain.delayNote == i, dontSendNotification);
    // only the live picture moves (the rest is redrawn when something changes)
    if (livePicture || wasPlaying) repaint (visualArea().expanded (4.0f).toNearestInt());
}

Rectangle<int> VocalPanel::card() const
{
    return Rectangle<int> (jmin (1040, getWidth() - 40), jmin (700, getHeight() - 40)).withCentre (getLocalBounds().getCentre());
}
Rectangle<int> VocalPanel::rowArea (int i) const
{
    return Rectangle<int> (card().getX() + 28, card().getY() + 118 + i * 52, 236, 46);
}
Rectangle<int> VocalPanel::detailArea() const
{
    auto c = card();
    return Rectangle<int> (c.getX() + 28 + 256, c.getY() + 118, c.getWidth() - 56 - 256, c.getHeight() - 118 - 26);
}
Rectangle<float> VocalPanel::visualArea() const
{
    const auto d = detailArea().toFloat();
    return Rectangle<float> (d.getX() + 20.0f, d.getY() + 70.0f, d.getWidth() - 40.0f, 196.0f);
}

void VocalPanel::resized()
{
    auto c = card();
    closeX = Rectangle<int> (c.getRight() - 50, c.getY() + 24, 30, 30);
    analyzeBtn.setBounds (c.getRight() - 186, c.getY() + 22, 122, 36);
    powerBtn.setBounds (c.getRight() - 312, c.getY() + 22, 116, 36);
    soloBtn.setBounds (c.getRight() - 438, c.getY() + 22, 116, 36);
    playBtn.setBounds (c.getRight() - 524, c.getY() + 22, 76, 36);
    layoutDetail();
}

void VocalPanel::layoutDetail()
{
    const auto d = detailArea();
    const auto v = visualArea();
    std::vector<Knob*> shown;
    for (auto& k : knobs) { const bool on = k->module == picked; k->s.setVisible (on); if (on) shown.push_back (k.get()); }
    const int top = (int) v.getBottom() + 14;
    const int w = jmin (124, (d.getWidth() - 40) / std::max (1, (int) shown.size()));
    for (size_t i = 0; i < shown.size(); ++i) shown[i]->s.setBounds (d.getX() + 20 + (int) i * w, top, w - 6, 112);
    // delay: its time and ping-pong under the knobs' row, at the right
    for (int i = 0; i < 3; ++i)
    {
        noteBtns[i].setVisible (picked == 5);
        noteBtns[i].setBounds (d.getX() + 20 + i * 104, top + 122, 98, 30);
    }
    pingBtn.setVisible (picked == 5);
    pingBtn.setBounds (d.getX() + 20 + 3 * 104 + 12, top + 122, 110, 30);
}

void VocalPanel::paintVisual (Graphics& g, Rectangle<float> v)
{
    const auto& t = themeOf (*this);
    const Colour col (modules[picked].colour);
    const V::Params p = V::Params::of (chain);
    const bool on = chain.on && moduleOn (chain, picked);
    const float alpha = on ? 1.0f : 0.35f;
    g.setFont (lp::ui::mono (9.0f, false, 0.08f));

    auto spectrumPath = [&] (Rectangle<float> r, double loHz, double hiHz)
    {
        Path sp;
        const double binHz = StudioPlayer::fileRate / fftSize; // (close enough to the device rate for a picture)
        bool started = false;
        for (int i = 0; i <= 160; ++i)
        {
            const double f = loHz * std::pow (hiHz / loHz, i / 160.0);
            const int b = jlimit (1, fftSize / 2 - 1, (int) (f / binHz));
            float db = -100.0f;
            for (int k = std::max (1, b - 1); k <= std::min (fftSize / 2 - 1, b + 1); ++k) db = std::max (db, spectrum[(size_t) k]);
            const float x = r.getX() + r.getWidth() * (float) (std::log (f / loHz) / std::log (hiHz / loHz));
            const float y = r.getBottom() - r.getHeight() * jlimit (0.0f, 1.0f, (db + 84.0f) / 78.0f);
            if (! started) { sp.startNewSubPath (x, r.getBottom()); started = true; }
            sp.lineTo (x, y);
        }
        sp.lineTo (r.getRight(), r.getBottom());
        sp.closeSubPath();
        return sp;
    };

    switch (picked)
    {
        case 0: // EQ: live spectrum, the curve, its draggable points
        {
            panelBox (g, v);
            auto r = v.reduced (12.0f, 10.0f);
            for (double f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
            {
                const float x = xOfHz (r, f);
                g.setColour (Colours::white.withAlpha (0.045f));
                g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
                g.setColour (Colours::white.withAlpha (0.28f));
                g.drawText (f >= 1000.0 ? String ((int) (f / 1000.0)) + "k" : String ((int) f), Rectangle<float> (x + 3.0f, r.getBottom() - 12.0f, 30.0f, 11.0f), Justification::centredLeft, false);
            }
            for (int dB : { -12, -6, 6, 12 })
            {
                const float y = r.getCentreY() - r.getHeight() * 0.5f * (float) dB / 15.0f;
                g.setColour (Colours::white.withAlpha (0.03f));
                g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
            }
            g.setColour (Colours::white.withAlpha (0.09f));
            g.fillPath (spectrumPath (r, 20.0, 20000.0));
            Path curve, fill;
            for (int i = 0; i <= 220; ++i)
            {
                const double f = 20.0 * std::pow (1000.0, i / 220.0);
                const float x = xOfHz (r, f), y = r.getCentreY() - r.getHeight() * 0.5f * (float) jlimit (-1.0, 1.0, V::eqDbAt (p, f) / 15.0);
                if (i == 0) { curve.startNewSubPath (x, y); fill.startNewSubPath (x, r.getCentreY()); }
                else curve.lineTo (x, y);
                fill.lineTo (x, y);
            }
            fill.lineTo (r.getRight(), r.getCentreY());
            fill.closeSubPath();
            g.setGradientFill (ColourGradient (col.withAlpha (0.28f * alpha), r.getX(), r.getY(), col.withAlpha (0.02f), r.getX(), r.getBottom(), false));
            g.fillPath (fill);
            g.setColour (col.withAlpha (alpha));
            g.strokePath (curve, PathStrokeType (2.2f, PathStrokeType::curved));
            const std::pair<double, double> pts[] = { { chain.lowCutHz, -3.0 }, { chain.mudHz, chain.mudDb }, { chain.presenceHz, chain.presenceDb }, { chain.airHz, chain.airDb } };
            const char* names[] = { "CUT", "MUD", "PRESENCE", "AIR" };
            for (int i = 0; i < 4; ++i)
            {
                const auto pt = Point<float> (xOfHz (r, pts[i].first), r.getCentreY() - r.getHeight() * 0.5f * (float) jlimit (-1.0, 1.0, pts[i].second / 15.0));
                const bool hot = i == dragPoint || pt.getDistanceFrom (getMouseXYRelative().toFloat()) < 12.0f;
                if (hot) { g.setColour (col.withAlpha (0.25f)); g.fillEllipse (Rectangle<float> (26.0f, 26.0f).withCentre (pt)); }
                g.setColour (Colour (0xff0b0b0e));
                g.fillEllipse (Rectangle<float> (13.0f, 13.0f).withCentre (pt));
                g.setColour (col.withAlpha (alpha));
                g.drawEllipse (Rectangle<float> (13.0f, 13.0f).withCentre (pt), 2.0f);
                g.setColour (Colours::white.withAlpha (hot ? 0.85f : 0.35f));
                const double f = pts[i].first;
                const String fs = f >= 1000.0 ? String (f / 1000.0, 1) + "k" : String (roundToInt (f));
                g.drawText (hot ? String (names[i]) + "  " + fs + (i > 0 ? "  " + String (pts[i].second, 1) + " dB" : String()) : String (names[i]),
                            Rectangle<float> (pt.x - 80.0f, pt.y - 24.0f, 160.0f, 11.0f), Justification::centred, false);
            }
            break;
        }
        case 1: // Compress: its curve with the knee, where the voice is on it, and the meter
        {
            auto graph = v.withWidth (v.getHeight() + 40.0f);
            panelBox (g, graph);
            auto r = graph.reduced (18.0f, 16.0f);
            auto px = [&] (float db) { return r.getX() + r.getWidth() * (db + 60.0f) / 60.0f; };
            auto py = [&] (float db) { return r.getBottom() - r.getHeight() * (db + 60.0f) / 60.0f; };
            for (int db = -48; db < 0; db += 12)
            {
                g.setColour (Colours::white.withAlpha (0.04f));
                g.drawVerticalLine ((int) px ((float) db), r.getY(), r.getBottom());
                g.drawHorizontalLine ((int) py ((float) db), r.getX(), r.getRight());
            }
            g.setColour (Colours::white.withAlpha (0.12f));
            g.drawLine (px (-60), py (-60), px (0), py (0), 1.0f);
            Path c;
            for (int i = 0; i <= 120; ++i)
            {
                const float in = -60.0f + 60.0f * (float) i / 120.0f;
                const float out = on ? V::compCurve (p, in) : in;
                if (i == 0) c.startNewSubPath (px (in), py (out)); else c.lineTo (px (in), py (out));
            }
            g.setColour (col.withAlpha (alpha));
            g.strokePath (c, PathStrokeType (2.2f));
            // the threshold
            const float thr = V::compThresholdDb (p);
            g.setColour (col.withAlpha (0.35f * alpha));
            g.drawVerticalLine ((int) px (thr), r.getY(), r.getBottom());
            g.setColour (t.inkDim);
            g.drawText ("IN", Rectangle<float> (r.getRight() - 20.0f, r.getBottom() - 12.0f, 20.0f, 11.0f), Justification::centredRight, false);
            g.drawText ("OUT", Rectangle<float> (r.getX(), r.getY(), 30.0f, 11.0f), Justification::centredLeft, false);
            // the voice right now: its level going in, on the curve
            const float inNow = jlimit (-60.0f, 0.0f, outDb - compGr);
            if (proc.previewPlaying.load() && outDb > -60.0f)
            {
                const auto pt = Point<float> (px (inNow), py (on ? V::compCurve (p, inNow) : inNow));
                g.setColour (col.withAlpha (0.3f)); g.fillEllipse (Rectangle<float> (22.0f, 22.0f).withCentre (pt));
                g.setColour (Colours::white); g.fillEllipse (Rectangle<float> (8.0f, 8.0f).withCentre (pt));
            }
            grMeter (g, Rectangle<float> (graph.getRight() + 16.0f, v.getY(), 92.0f, v.getHeight()), on ? compGr : 0.0f, col, t, "REDUCTION");
            // in words
            auto words = Rectangle<float> (graph.getRight() + 124.0f, v.getY() + 8.0f, v.getRight() - graph.getRight() - 124.0f, v.getHeight() - 16.0f);
            g.setFont (font (13.0f));
            g.setColour (t.ink.withAlpha (0.7f));
            g.drawFittedText ("Above " + String (roundToInt (thr)) + " dB the voice is turned down " + String (V::compRatio (p), 1)
                              + " : 1, gently around the bend. The quiet words come up to meet the loud ones.", words.toNearestInt(), Justification::topLeft, 6);
            break;
        }
        case 2: // Saturate: the curve it bends the sound with, and a sine through it
        {
            auto graph = v.withWidth (v.getHeight() + 40.0f);
            panelBox (g, graph);
            auto r = graph.reduced (16.0f);
            g.setColour (Colours::white.withAlpha (0.06f));
            g.drawLine (r.getX(), r.getCentreY(), r.getRight(), r.getCentreY(), 1.0f);
            g.drawLine (r.getCentreX(), r.getY(), r.getCentreX(), r.getBottom(), 1.0f);
            g.setColour (Colours::white.withAlpha (0.12f));
            g.drawLine (r.getX(), r.getBottom(), r.getRight(), r.getY(), 1.0f);
            Path c;
            for (int i = 0; i <= 120; ++i)
            {
                const float x = -1.4f + 2.8f * (float) i / 120.0f;
                const float y = jlimit (-1.4f, 1.4f, on ? V::satCurve (p, x) : x);
                const float sx = r.getX() + r.getWidth() * (x + 1.4f) / 2.8f, sy = r.getBottom() - r.getHeight() * (y + 1.4f) / 2.8f;
                if (i == 0) c.startNewSubPath (sx, sy); else c.lineTo (sx, sy);
            }
            g.setColour (col.withAlpha (alpha));
            g.strokePath (c, PathStrokeType (2.2f));
            // a loud sine wave, before (faint) and after
            auto wave = Rectangle<float> (graph.getRight() + 16.0f, v.getY(), v.getRight() - graph.getRight() - 16.0f, v.getHeight());
            panelBox (g, wave);
            auto wr = wave.reduced (16.0f, 26.0f);
            Path before, after;
            for (int i = 0; i <= 200; ++i)
            {
                const float ph = MathConstants<float>::twoPi * 2.0f * (float) i / 200.0f, x = 1.2f * std::sin (ph);
                const float y = on ? V::satCurve (p, x) : x;
                const float sx = wr.getX() + wr.getWidth() * (float) i / 200.0f;
                auto yOf = [&] (float s) { return wr.getCentreY() - wr.getHeight() * 0.5f * s / 1.3f; };
                if (i == 0) { before.startNewSubPath (sx, yOf (x)); after.startNewSubPath (sx, yOf (y)); }
                else { before.lineTo (sx, yOf (x)); after.lineTo (sx, yOf (y)); }
            }
            g.setColour (Colours::white.withAlpha (0.14f));
            g.strokePath (before, PathStrokeType (1.2f));
            g.setColour (col.withAlpha (alpha));
            g.strokePath (after, PathStrokeType (2.0f));
            g.setColour (t.inkDim);
            g.drawText ("A LOUD NOTE, BEFORE AND AFTER", wave.withHeight (22.0f).translated (0.0f, 6.0f), Justification::centred, false);
            break;
        }
        case 3: // De-ess: the top of the spectrum with the S band, and the meter
        {
            auto spec = v.withWidth (v.getWidth() - 108.0f);
            panelBox (g, spec);
            auto r = spec.reduced (12.0f, 10.0f);
            const double lo = 1500.0, hi = 18000.0;
            auto xOf = [&] (double f) { return r.getX() + r.getWidth() * (float) (std::log (f / lo) / std::log (hi / lo)); };
            const float bx = xOf (chain.deessHz);
            g.setGradientFill (ColourGradient (col.withAlpha (0.16f * alpha), bx, r.getY(), col.withAlpha (0.0f), r.getRight(), r.getY(), false));
            g.fillRect (Rectangle<float> (bx, r.getY(), r.getRight() - bx, r.getHeight()));
            g.setColour (col.withAlpha (0.8f * alpha));
            g.drawVerticalLine ((int) bx, r.getY(), r.getBottom());
            g.setColour (Colours::white.withAlpha (0.12f));
            g.fillPath (spectrumPath (r, lo, hi));
            for (double f : { 2000.0, 4000.0, 6000.0, 8000.0, 12000.0, 16000.0 })
            {
                g.setColour (Colours::white.withAlpha (0.28f));
                g.drawText (String ((int) (f / 1000.0)) + "k", Rectangle<float> (xOf (f) + 3.0f, r.getBottom() - 12.0f, 30.0f, 11.0f), Justification::centredLeft, false);
            }
            g.setColour (col.withAlpha (alpha));
            g.drawText ("S, T, SH", Rectangle<float> (bx + 8.0f, r.getY() + 4.0f, 120.0f, 11.0f), Justification::centredLeft, false);
            grMeter (g, Rectangle<float> (spec.getRight() + 16.0f, v.getY(), 92.0f, v.getHeight()), on ? deessGr : 0.0f, col, t, "CAUGHT");
            break;
        }
        case 4: // Air & Width: the stereo picture (a vectorscope) and how wide it is
        {
            auto scope = v.withWidth (v.getHeight() + 40.0f);
            panelBox (g, scope);
            auto r = scope.reduced (14.0f);
            const auto c = r.getCentre();
            g.setColour (Colours::white.withAlpha (0.06f));
            g.drawLine (c.x, r.getY(), c.x, r.getBottom(), 1.0f);
            g.drawLine (r.getX(), c.y, r.getRight(), c.y, 1.0f);
            g.setColour (t.inkDim);
            g.drawText ("L", Rectangle<float> (r.getX(), r.getY(), 12.0f, 11.0f), Justification::centredLeft, false);
            g.drawText ("R", Rectangle<float> (r.getRight() - 12.0f, r.getY(), 12.0f, 11.0f), Justification::centredRight, false);
            float peak = 1e-4f;
            for (size_t i = 0; i < scopeL.size(); ++i) peak = std::max (peak, std::max (std::abs (scopeL[i]), std::abs (scopeR[i])));
            const float sc = 0.45f * r.getHeight() / peak;
            double corr = 0, eL = 0, eR = 0;
            for (size_t i = 0; i < scopeL.size(); i += 2)
            {
                const float l = scopeL[i], rr = scopeR[i];
                corr += (double) l * rr; eL += (double) l * l; eR += (double) rr * rr;
                const float side = (rr - l) * 0.7071f, mid = (l + rr) * 0.7071f;
                g.setColour (col.withAlpha (0.35f * alpha));
                g.fillRect (c.x + side * sc, c.y - mid * sc, 1.6f, 1.6f);
            }
            const double cor = eL > 1e-9 && eR > 1e-9 ? corr / std::sqrt (eL * eR) : 1.0;
            auto info = Rectangle<float> (scope.getRight() + 16.0f, v.getY(), v.getRight() - scope.getRight() - 16.0f, v.getHeight());
            panelBox (g, info);
            auto ir = info.reduced (18.0f, 16.0f);
            g.setColour (t.inkDim);
            g.drawText ("HOW WIDE (1 = MIDDLE ONLY)", ir.removeFromTop (12.0f), Justification::centredLeft, false);
            ir.removeFromTop (10.0f);
            auto bar = ir.removeFromTop (10.0f);
            g.setColour (Colours::white.withAlpha (0.06f));
            g.fillRoundedRectangle (bar, 5.0f);
            const float pos = jlimit (0.0f, 1.0f, (float) (1.0 - cor) * 0.5f);
            g.setColour (col.withAlpha (alpha));
            g.fillRoundedRectangle (bar.withWidth (std::max (6.0f, bar.getWidth() * pos * 2.0f)), 5.0f);
            ir.removeFromTop (10.0f);
            g.setFont (font (22.0f, lp::ui::Medium));
            g.setColour (t.ink);
            g.drawText (String (cor, 2), ir.removeFromTop (28.0f), Justification::centredLeft, false);
            g.setFont (font (13.0f));
            g.setColour (t.ink.withAlpha (0.65f));
            g.drawFittedText ("Play the song to see it. Wider looks like a cloud; centred, a straight line up. Below "
                              + String (roundToInt (chain.focusHz)) + " Hz stays in the middle so it hits on phones and in clubs.", ir.toNearestInt(), Justification::topLeft, 5);
            break;
        }
        case 5: // Delay: the echoes, drawn in time over two bars
        {
            panelBox (g, v);
            auto r = v.reduced (18.0f, 22.0f);
            const double beat = 60.0 / std::max (40.0, (double) proc.songBpm.load());
            const double secs = V::delaySeconds (p, proc.songBpm.load()), span = beat * 8.0;
            auto xOf = [&] (double s) { return r.getX() + r.getWidth() * (float) (s / span); };
            for (int b = 0; b <= 8; ++b)
            {
                g.setColour (Colours::white.withAlpha (b % 4 == 0 ? 0.12f : 0.05f));
                g.drawVerticalLine ((int) xOf (b * beat), r.getY(), r.getBottom());
            }
            // the voice, then its echoes
            g.setColour (Colours::white.withAlpha (0.85f));
            g.fillRoundedRectangle (Rectangle<float> (xOf (0) - 3.0f, r.getY(), 6.0f, r.getHeight()), 3.0f);
            double level = chain.delayMix * 1.25;
            for (int k = 1; k < 24; ++k)
            {
                const double at = secs * k;
                if (at > span || level < 0.02) break;
                const bool left = ! chain.delayPingPong || k % 2 == 1;
                const float h = r.getHeight() * (float) jlimit (0.0, 1.0, level);
                const auto bar = Rectangle<float> (xOf (at) - 3.0f, left ? r.getCentreY() - h * 0.5f - (chain.delayPingPong ? h * 0.25f : 0.0f)
                                                                           : r.getCentreY() - h * 0.5f + h * 0.25f, 6.0f, h);
                g.setColour ((left ? col : col.withRotatedHue (0.08f)).withAlpha ((float) jlimit (0.15, 1.0, level * 2.2) * alpha));
                g.fillRoundedRectangle (bar, 3.0f);
                level *= 0.85 * chain.delayFeedback;
            }
            g.setColour (t.inkDim);
            g.drawText (chain.delayPingPong ? "LEFT  /  RIGHT" : "BOTH SIDES", Rectangle<float> (r.getRight() - 140.0f, v.getY() + 6.0f, 140.0f, 11.0f), Justification::centredRight, false);
            g.drawText ("2 BARS", Rectangle<float> (r.getX(), v.getY() + 6.0f, 80.0f, 11.0f), Justification::centredLeft, false);
            break;
        }
        default: // Reverb: its tail (the gap, then the fade), tinted by its tone
        {
            panelBox (g, v);
            auto r = v.reduced (18.0f, 22.0f);
            const double t60 = V::reverbSeconds (p), span = std::max (3.0, t60 * 1.2), pre = chain.verbPredelayMs / 1000.0;
            auto xOf = [&] (double s) { return r.getX() + r.getWidth() * (float) (s / span); };
            for (int s = 0; s <= (int) span; ++s)
            {
                g.setColour (Colours::white.withAlpha (0.05f));
                g.drawVerticalLine ((int) xOf (s), r.getY(), r.getBottom());
                g.setColour (Colours::white.withAlpha (0.28f));
                g.drawText (String (s) + "s", Rectangle<float> (xOf (s) + 3.0f, r.getBottom() + 4.0f, 30.0f, 11.0f), Justification::centredLeft, false);
            }
            Path tail;
            tail.startNewSubPath (xOf (pre), r.getBottom());
            const float top = r.getY() + r.getHeight() * (1.0f - jlimit (0.15f, 1.0f, chain.verbMix * 2.4f));
            for (int i = 0; i <= 160; ++i)
            {
                const double s = pre + (span - pre) * i / 160.0;
                const double env = std::pow (10.0, -3.0 * (s - pre) / t60);
                const float y = r.getBottom() - (r.getBottom() - top) * (float) env * (i == 0 ? 0.0f : 1.0f);
                tail.lineTo (xOf (s), i == 0 ? top : y);
            }
            tail.lineTo (xOf (span), r.getBottom());
            tail.closeSubPath();
            const Colour tint = col.interpolatedWith (chain.verbTone < 0 ? Colour (0xff7a5cff) : Colour (0xffffe0f0), std::abs (chain.verbTone) * 0.5f);
            g.setGradientFill (ColourGradient (tint.withAlpha (0.55f * alpha), xOf (pre), top, tint.withAlpha (0.02f), xOf (span), r.getBottom(), false));
            g.fillPath (tail);
            g.setColour (Colours::white.withAlpha (0.85f));
            g.fillRoundedRectangle (Rectangle<float> (xOf (0) - 3.0f, r.getY(), 6.0f, r.getHeight()), 3.0f);
            g.setColour (t.inkDim);
            g.drawText ("FADES OVER " + String (t60, 1) + " S", Rectangle<float> (r.getRight() - 200.0f, v.getY() + 6.0f, 200.0f, 11.0f), Justification::centredRight, false);
            break;
        }
    }
}

void VocalPanel::paint (Graphics& g)
{
    const auto& t = themeOf (*this);
    g.fillAll (Colours::black.withAlpha (0.72f));
    const auto cf = card().toFloat();
    {
        Graphics::ScopedSaveState s (g);
        Path clip; clip.addRoundedRectangle (cf, 22.0f);
        g.reduceClipRegion (clip);
        g.setColour (t.bodyDark.interpolatedWith (Colour (0xff09090c), 0.7f));
        g.fillRect (cf);
        const Colour glowCol (modules[picked].colour);
        g.setGradientFill (ColourGradient (glowCol.withAlpha (0.16f), cf.getRight() - 80.0f, cf.getY(), glowCol.withAlpha (0.0f), cf.getRight() - 460.0f, cf.getY() + 360.0f, true));
        g.fillRect (cf);
        g.setGradientFill (ColourGradient (t.grass.withAlpha (0.12f), cf.getX(), cf.getBottom(), t.grass.withAlpha (0.0f), cf.getX() + 380.0f, cf.getBottom() - 300.0f, true));
        g.fillRect (cf);
    }
    g.setColour (Colours::white.withAlpha (0.1f));
    g.drawRoundedRectangle (cf.reduced (0.5f), 22.0f, 1.0f);

    // header
    const auto st = proc.songJob.getStatus();
    const String name = track < (int) st.tracks.size() ? String (st.tracks[(size_t) track].name) : String ("vocal");
    auto r = cf.reduced (28.0f, 24.0f);
    g.setFont (lp::ui::mono (10.0f, true, 0.18f));
    g.setColour (t.grassLight);
    g.drawText ("VOCAL CHAIN", r.removeFromTop (14.0f), Justification::centredLeft, false);
    g.setFont (font (28.0f, lp::ui::Medium, -0.02f));
    g.setColour (t.ink);
    g.drawText (name.substring (0, 1).toUpperCase() + name.substring (1), r.removeFromTop (40.0f).withTrimmedRight (540.0f), Justification::centredLeft, true);
    const String sep = String::fromUTF8 ("  \xc2\xb7  ");
    g.setFont (lp::ui::mono (10.0f, false, 0.12f));
    g.setColour (analyzing ? t.grassLight : t.inkDim);
    g.drawText (analyzing ? String ("LISTENING TO YOUR VOCAL...")
                          : chain.on ? "LIVE" + sep + "RECORDED IN WHEN YOU DRAG THE TRACK" : "OFF" + sep + "THE VOCAL AS IT WAS",
                r.removeFromTop (16.0f), Justification::centredLeft, false);
    {
        auto x = closeX.toFloat().reduced (9.0f);
        g.setColour (Colours::white.withAlpha (closeX.contains (getMouseXYRelative()) ? 0.9f : 0.45f));
        g.drawLine (x.getX(), x.getY(), x.getRight(), x.getBottom(), 1.6f);
        g.drawLine (x.getRight(), x.getY(), x.getX(), x.getBottom(), 1.6f);
    }

    // the chain, in order, joined by a line through the switches
    {
        const float x = (float) rowArea (0).getRight() - 24.0f;
        g.setColour (Colours::white.withAlpha (0.07f));
        g.drawLine (x, (float) rowArea (0).getCentreY(), x, (float) rowArea (6).getCentreY(), 2.0f);
    }
    for (int i = 0; i < 7; ++i)
    {
        const auto row = rowArea (i).toFloat();
        const bool on = chain.on && moduleOn (chain, i), sel = i == picked;
        const Colour col (modules[i].colour);
        if (sel)
        {
            g.setGradientFill (ColourGradient (col.withAlpha (0.16f), row.getX(), row.getY(), col.withAlpha (0.03f), row.getRight(), row.getY(), false));
            g.fillRoundedRectangle (row, 12.0f);
            g.setColour (col.withAlpha (0.5f));
            g.drawRoundedRectangle (row.reduced (0.5f), 12.0f, 1.0f);
        }
        else if (row.contains (getMouseXYRelative().toFloat()))
        {
            g.setColour (Colours::white.withAlpha (0.04f));
            g.fillRoundedRectangle (row, 12.0f);
        }
        g.setFont (lp::ui::mono (10.0f, false, 0.1f));
        g.setColour (sel ? col : t.inkDim);
        g.drawText (String (i + 1).paddedLeft ('0', 2), row.withWidth (42.0f), Justification::centred, false);
        g.setFont (font (16.5f, lp::ui::Medium));
        g.setColour (on ? t.ink : t.ink.withAlpha (0.38f));
        g.drawText (modules[i].name, row.withTrimmedLeft (42.0f).withTrimmedRight (48.0f), Justification::centredLeft, false);
        // its switch
        const auto sw = Rectangle<float> (row.getRight() - 42.0f, row.getCentreY() - 9.0f, 34.0f, 18.0f);
        g.setColour (on ? col.withAlpha (0.9f) : Colours::white.withAlpha (0.1f));
        g.fillRoundedRectangle (sw, 9.0f);
        g.setColour (on ? Colour (0xff0b0b0e) : Colours::white.withAlpha (0.45f));
        g.fillEllipse ((on ? sw.getRight() - 16.0f : sw.getX() + 2.0f), sw.getY() + 2.0f, 14.0f, 14.0f);
    }

    // the picked module
    const auto d = detailArea().toFloat();
    g.setColour (Colours::black.withAlpha (0.22f));
    g.fillRoundedRectangle (d, 18.0f);
    g.setColour (Colours::white.withAlpha (0.06f));
    g.drawRoundedRectangle (d.reduced (0.5f), 18.0f, 1.0f);
    const Colour col (modules[picked].colour);
    auto head = d.reduced (20.0f, 16.0f);
    g.setColour (col);
    g.fillEllipse (head.getX(), head.getY() + 9.0f, 10.0f, 10.0f);
    g.setFont (font (22.0f, lp::ui::Medium, -0.01f));
    g.setColour (t.ink);
    g.drawText (modules[picked].name, head.withTrimmedLeft (20.0f).removeFromTop (28.0f), Justification::centredLeft, false);
    head.removeFromTop (28.0f);
    g.setFont (font (13.0f));
    g.setColour (moduleOn (chain, picked) && chain.on ? t.ink.withAlpha (0.6f) : Colour (0xfff2a33a));
    g.drawText (moduleOn (chain, picked) && chain.on ? String (modules[picked].what) : String ("Off: flip its switch on the left (or turn any knob) to hear it."),
                head.removeFromTop (18.0f), Justification::centredLeft, true);
    paintVisual (g, visualArea());

    // what Analyze found
    const float top = (float) (visualArea().getBottom() + 14.0f + 112.0f + (picked == 5 ? 44.0f : 8.0f));
    auto findings = Rectangle<float> (d.getX() + 20.0f, top, d.getWidth() - 40.0f, d.getBottom() - 16.0f - top);
    if (findings.getHeight() > 40.0f)
    {
        g.setColour (Colours::white.withAlpha (0.03f));
        g.fillRoundedRectangle (findings, 12.0f);
        auto fr = findings.reduced (16.0f, 10.0f);
        g.setFont (lp::ui::mono (9.5f, true, 0.16f));
        g.setColour (t.grassLight);
        g.drawText ("ANALYSIS", fr.removeFromTop (14.0f), Justification::centredLeft, false);
        fr.removeFromTop (4.0f);
        g.setFont (font (12.5f));
        if (chain.analysis.isEmpty())
        {
            g.setColour (t.ink.withAlpha (0.55f));
            g.drawFittedText ("Press Analyze: it listens to this vocal, sets up every module for it and tells you what it found.", fr.toNearestInt(), Justification::topLeft, 2);
        }
        else
        {
            auto lines = StringArray::fromLines (chain.analysis);
            const int cols = fr.getHeight() < 18.0f * (float) lines.size() ? 2 : 1;
            const float cw = fr.getWidth() / (float) cols;
            for (int i = 0; i < lines.size(); ++i)
            {
                const int c = cols == 2 ? i % 2 : 0, rI = cols == 2 ? i / 2 : i;
                auto lr = Rectangle<float> (fr.getX() + (float) c * cw, fr.getY() + (float) rI * 18.0f, cw - 10.0f, 18.0f);
                if (lr.getBottom() > fr.getBottom() + 2.0f) break;
                g.setColour (t.grass);
                g.fillEllipse (lr.getX(), lr.getCentreY() - 2.5f, 5.0f, 5.0f);
                g.setColour (t.ink.withAlpha (0.85f));
                g.drawText (lines[i], lr.withTrimmedLeft (13.0f), Justification::centredLeft, true);
            }
        }
    }
}

void VocalPanel::mouseMove (const MouseEvent&) { repaint(); }

void VocalPanel::mouseDown (const MouseEvent& e)
{
    dragPoint = -1;
    if (picked != 0) return;
    const auto r = visualArea().reduced (12.0f, 10.0f);
    const std::pair<double, double> pts[] = { { chain.lowCutHz, -3.0 }, { chain.mudHz, chain.mudDb }, { chain.presenceHz, chain.presenceDb }, { chain.airHz, chain.airDb } };
    for (int i = 0; i < 4; ++i)
    {
        const auto pt = Point<float> (xOfHz (r, pts[i].first), r.getCentreY() - r.getHeight() * 0.5f * (float) jlimit (-1.0, 1.0, pts[i].second / 15.0));
        if (pt.getDistanceFrom (e.position) < 14.0f) { dragPoint = i; return; }
    }
}

void VocalPanel::mouseDrag (const MouseEvent& e)
{
    if (dragPoint < 0) return;
    const auto r = visualArea().reduced (12.0f, 10.0f);
    const float db = jlimit (-12.0f, 9.0f, (r.getCentreY() - e.position.y) / (r.getHeight() * 0.5f) * 15.0f);
    // left / right: where the band sits; up / down: how much (the low cut only moves left / right)
    const double hz = hzOfX (r, e.position.x);
    if (dragPoint == 0) chain.lowCutHz = (float) jlimit (30.0, 300.0, hz);
    else if (dragPoint == 1) { chain.mudHz = (float) jlimit (120.0, 900.0, hz); chain.mudDb = jlimit (-12.0f, 3.0f, db); }
    else if (dragPoint == 2) { chain.presenceHz = (float) jlimit (1000.0, 7000.0, hz); chain.presenceDb = jlimit (-6.0f, 9.0f, db); }
    else { chain.airHz = (float) jlimit (5000.0, 16000.0, hz); chain.airDb = jlimit (-6.0f, 9.0f, db); }
    chain.on = true; chain.eqOn = true;
    for (auto& k : knobs) k->s.setValue (*k->value, dontSendNotification);
    changed();
}

void VocalPanel::mouseUp (const MouseEvent& e)
{
    if (dragPoint >= 0) { dragPoint = -1; return; }
    const auto p = e.getPosition();
    if (closeX.contains (p) || ! card().contains (p)) { if (onClosed) onClosed(); return; }
    for (int i = 0; i < 7; ++i)
    {
        const auto row = rowArea (i);
        if (! row.contains (p)) continue;
        if (Rectangle<int> (row.getRight() - 50, row.getY(), 50, row.getHeight()).contains (p))
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
    if (k == KeyPress::spaceKey) { if (proc.studio.length() > 0) proc.previewPlaying = ! proc.previewPlaying.load(); return true; }
    return false;
}
