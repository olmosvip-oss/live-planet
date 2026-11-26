#include "PluginEditor.h"

using namespace juce;
using lp::ui::font;

// Effects, like a DAW's. The chain's effects (EQ, compressor, saturator, de-esser, width, delay, reverb) are each
// track's own channel strip, played live; warmth, tape, chorus and the filter are the track's own inserts.
// Dragging one onto a track switches it on there; the open track's devices sit side by side to tweak or remove.
namespace lp::fx
{
const std::vector<Kind>& kinds()
{
    static const std::vector<Kind> k {
        { "eq",     "EQ",          "Cut the rumble, lift the presence and the air", 0 },
        { "comp",   "Compressor",  "Evens it out: quiet parts up, loud parts held", 0 },
        { "sat",    "Saturator",   "Warm drive to grit, dark to bright", 0 },
        { "deess",  "De-esser",    "Tames sharp S and T sounds", 0 },
        { "width",  "Width",       "A soft double out to the sides; the lows stay centred", 0 },
        { "delay",  "Delay",       "Echoes in time with the song, ping-pong", 0 },
        { "reverb", "Reverb",      "A room to a hall, after a short gap", 0 },
        { "warmth", "Warmth",      "A softer top and gentle tape saturation", 1 },
        { "tape",   "Tape",        "Wobble, a worn band and a narrower picture: nostalgia", 1 },
        { "chorus", "Chorus",      "Two moving copies, one each side: width and shimmer", 1 },
        { "filter", "Filter",      "Darker (left) or thinner (right)", 1 },
        { "room",   "Room",        "A send to the song's shared room (reverb)", 1 },
        { "echo",   "Echo",        "A send to the song's tempo delay", 1 },
    };
    return k;
}

static bool* chainFlag (lp::song::VocalChain& c, const String& id)
{
    if (id == "eq") return &c.eqOn;
    if (id == "comp") return &c.compOn;
    if (id == "sat") return &c.satOn;
    if (id == "deess") return &c.deessOn;
    if (id == "width") return &c.airOn;
    if (id == "delay") return &c.delayOn;
    if (id == "reverb") return &c.verbOn;
    return nullptr;
}

static std::atomic<float>* mixValue (StudioPlayer::Mix& m, const String& id)
{
    if (id == "warmth") return &m.warmth;
    if (id == "tape") return &m.nostalgia;
    if (id == "chorus") return &m.chorus;
    if (id == "filter") return &m.tone;
    if (id == "room") return &m.space;
    if (id == "echo") return &m.echo;
    return nullptr;
}

static void applyChain (LivePlanetProcessor& proc, int track, const lp::song::VocalChain& c)
{
    proc.studio.setVocalChain (track, c);
    proc.songJob.setVocalChain (track, c);
}

bool isOn (LivePlanetProcessor& proc, int track, const String& id)
{
    if (track < 0 || track >= StudioPlayer::maxTracks) return false;
    auto c = proc.songJob.vocalChainOf (track);
    if (auto* f = chainFlag (c, id)) return c.on && *f;
    if (auto* v = mixValue (proc.studio.mix[(size_t) track], id)) return std::abs (v->load()) > 0.001f;
    return false;
}

void add (LivePlanetProcessor& proc, int track, const String& id)
{
    if (track < 0 || track >= StudioPlayer::maxTracks) return;
    auto c = proc.songJob.vocalChainOf (track);
    if (auto* f = chainFlag (c, id))
    {
        if (! c.on)
        {
            // the first effect from the chain on this track: only the one you dropped (the rest stay off)
            c.on = true;
            c.eqOn = c.compOn = c.satOn = c.deessOn = c.airOn = c.delayOn = c.verbOn = false;
            f = chainFlag (c, id);
        }
        *f = true;
        applyChain (proc, track, c);
        return;
    }
    auto& m = proc.studio.mix[(size_t) track];
    if (auto* v = mixValue (m, id))
        if (std::abs (v->load()) <= 0.001f) v->store (id == "filter" ? -0.3f : id == "tape" ? 0.4f : 0.5f);
}

void remove (LivePlanetProcessor& proc, int track, const String& id)
{
    if (track < 0 || track >= StudioPlayer::maxTracks) return;
    auto c = proc.songJob.vocalChainOf (track);
    if (auto* f = chainFlag (c, id))
    {
        *f = false;
        if (! (c.eqOn || c.compOn || c.satOn || c.deessOn || c.airOn || c.delayOn || c.verbOn)) c.on = false;
        applyChain (proc, track, c);
        return;
    }
    if (auto* v = mixValue (proc.studio.mix[(size_t) track], id)) v->store (0.0f);
}

// ---- the browser
Rectangle<float> Browser::itemRect (int i) const
{
    // the chain's effects, then (after a gap and a heading) the track's own
    const float y0 = 44.0f + (float) i * 34.0f + (kinds()[(size_t) i].group == 1 ? 30.0f : 0.0f);
    return { 10.0f, y0, (float) getWidth() - 20.0f, 30.0f };
}

int Browser::itemAt (Point<float> p) const
{
    for (int i = 0; i < (int) kinds().size(); ++i) if (itemRect (i).contains (p)) return i;
    return -1;
}

void Browser::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (th->body);
    g.fillRoundedRectangle (r, 14.0f);
    g.setColour (th->line);
    g.drawRoundedRectangle (r.reduced (0.5f), 14.0f, 1.0f);
    g.setFont (lp::ui::mono (10.0f, true, 0.16f));
    g.setColour (th->grassLight);
    g.drawText ("AUDIO EFFECTS", Rectangle<float> (16.0f, 14.0f, r.getWidth() - 32.0f, 16.0f), Justification::centredLeft, false);
    g.setFont (lp::ui::mono (9.0f, false, 0.14f));
    g.setColour (th->inkDim);
    g.drawText ("CHANNEL STRIP", Rectangle<float> (16.0f, 30.0f, r.getWidth() - 32.0f, 12.0f), Justification::centredLeft, false);
    const auto firstOwn = itemRect (7);
    g.drawText ("COLOUR", Rectangle<float> (16.0f, firstOwn.getY() - 20.0f, r.getWidth() - 32.0f, 12.0f), Justification::centredLeft, false);
    for (int i = 0; i < (int) kinds().size(); ++i)
    {
        const auto ir = itemRect (i);
        if (i == hover) { g.setColour (Colours::white.withAlpha (0.07f)); g.fillRoundedRectangle (ir, 7.0f); }
        // a little device icon: a box with a wave
        auto ic = Rectangle<float> (ir.getX() + 8.0f, ir.getCentreY() - 8.0f, 20.0f, 16.0f);
        g.setColour (Colours::white.withAlpha (0.5f));
        g.drawRoundedRectangle (ic, 3.0f, 1.2f);
        Path w;
        w.startNewSubPath (ic.getX() + 4.0f, ic.getCentreY());
        for (int k = 1; k <= 8; ++k) w.lineTo (ic.getX() + 4.0f + (ic.getWidth() - 8.0f) * k / 8.0f, ic.getCentreY() + std::sin (k * 1.4f + i) * 3.5f);
        g.strokePath (w, PathStrokeType (1.0f));
        g.setColour (th->ink);
        g.setFont (font (13.5f, lp::ui::Medium));
        g.drawText (kinds()[(size_t) i].name, ir.withTrimmedLeft (38.0f), Justification::centredLeft, true);
    }
    g.setFont (font (11.0f));
    g.setColour (th->inkDim);
    g.drawFittedText ("Drag one onto a track. Double-click: onto the open track.",
                      Rectangle<float> (16.0f, r.getBottom() - 50.0f, r.getWidth() - 32.0f, 40.0f).toNearestInt(), Justification::topLeft, 3);
}

void Browser::mouseMove (const MouseEvent& e) { const int h = itemAt (e.position); if (h != hover) { hover = h; repaint(); } }
void Browser::mouseDown (const MouseEvent& e) { pressed = itemAt (e.position); dragging = false; }

void Browser::mouseDrag (const MouseEvent& e)
{
    if (pressed < 0 || dragging || e.getDistanceFromDragStart() < 5) return;
    if (auto* dc = DragAndDropContainer::findParentDragContainerFor (this))
    {
        dragging = true;
        // what's dragged: a small card with its name
        const auto& k = kinds()[(size_t) pressed];
        Image img (Image::ARGB, 150, 30, true);
        {
            Graphics ig (img);
            ig.setColour (th->grass.withAlpha (0.9f));
            ig.fillRoundedRectangle (0.0f, 0.0f, 150.0f, 30.0f, 8.0f);
            ig.setColour (Colours::white);
            ig.setFont (font (13.0f, lp::ui::Medium));
            ig.drawText (k.name, Rectangle<int> (0, 0, 150, 30), Justification::centred, false);
        }
        dc->startDragging (String ("fx:") + k.id, this, ScaledImage (img), true);
    }
}

void Browser::mouseDoubleClick (const MouseEvent& e)
{
    if (const int i = itemAt (e.position); i >= 0 && onAdd) onAdd (kinds()[(size_t) i].id);
}

String Browser::getTooltip()
{
    const int i = itemAt (getMouseXYRelative().toFloat());
    return i >= 0 ? String (kinds()[(size_t) i].about) : String();
}

// ---- the rack
// the track's own strip (always there): volume and pan knobs, and its buttons in a column
Rectangle<float> Rack::stripBox() const { return { 0.0f, 0.0f, 290.0f, std::min ((float) getHeight(), 150.0f) }; }
Rectangle<float> Rack::volRect() const { return { 18.0f, 44.0f, 60.0f, 60.0f }; }
Rectangle<float> Rack::panRect() const { return { 92.0f, 44.0f, 60.0f, 60.0f }; }
Rectangle<float> Rack::bypassRect() const { return { 172.0f, 44.0f, 104.0f, 26.0f }; }
Rectangle<float> Rack::dryRect() const { return { 172.0f, 76.0f, 104.0f, 26.0f }; }
Rectangle<float> Rack::dragRect() const { return { 172.0f, 108.0f, 104.0f, 26.0f }; }

StringArray Rack::knobNames (const String& id)
{
    if (id == "eq") return { "LOW CUT", "MUD", "PRESENCE", "AIR" };
    if (id == "comp") return { "AMOUNT", "SPEED" };
    if (id == "sat") return { "DRIVE", "TONE", "MIX" };
    if (id == "deess") return { "AMOUNT", "WHERE" };
    if (id == "width") return { "WIDTH", "FOCUS" };
    if (id == "delay") return { "MIX", "FEEDBACK", "NOTE", "TONE" };
    if (id == "reverb") return { "MIX", "SIZE", "PRE-DELAY", "TONE" };
    return { "AMOUNT" };
}

float Rack::value (const String& id, int k) const
{
    if (track < 0) return 0.0f;
    const auto c = pendingChain ? editing : proc.songJob.vocalChainOf (track);
    auto n = [] (float v, float a, float b) { return jlimit (0.0f, 1.0f, (v - a) / (b - a)); };
    if (id == "eq") return k == 0 ? n (c.lowCutHz, 20.0f, 400.0f) : k == 1 ? n (c.mudDb, -12.0f, 0.0f) : k == 2 ? n (c.presenceDb, -6.0f, 8.0f) : n (c.airDb, -6.0f, 8.0f);
    if (id == "comp") return k == 0 ? c.compAmount : c.compSpeed;
    if (id == "sat") return k == 0 ? c.drive : k == 1 ? n (c.satTone, -1.0f, 1.0f) : c.satMix;
    if (id == "deess") return k == 0 ? c.deess : n (c.deessHz, 4000.0f, 10000.0f);
    if (id == "width") return k == 0 ? c.width : n (c.focusHz, 80.0f, 600.0f);
    if (id == "delay") return k == 0 ? c.delayMix : k == 1 ? n (c.delayFeedback, 0.0f, 0.9f) : k == 2 ? (float) c.delayNote / 2.0f : n (c.delayTone, -1.0f, 1.0f);
    if (id == "reverb") return k == 0 ? c.verbMix : k == 1 ? c.verbSize : k == 2 ? n (c.verbPredelayMs, 0.0f, 120.0f) : n (c.verbTone, -1.0f, 1.0f);
    auto& m = proc.studio.mix[(size_t) track];
    if (id == "filter") return m.tone.load() * 0.5f + 0.5f;
    if (auto* v = mixValue (m, id)) return v->load();
    return 0.0f;
}

void Rack::setValue (const String& id, int k, float v)
{
    if (track < 0) return;
    v = jlimit (0.0f, 1.0f, v);
    auto& m = proc.studio.mix[(size_t) track];
    if (id == "filter") { m.tone = v * 2.0f - 1.0f; if (onChanged) onChanged(); return; }
    if (auto* mv = mixValue (m, id)) { mv->store (std::max (0.002f, v)); if (onChanged) onChanged(); return; }
    auto c = pendingChain ? editing : proc.songJob.vocalChainOf (track);
    auto d = [v] (float a, float b) { return a + (b - a) * v; };
    if (id == "eq") { if (k == 0) c.lowCutHz = d (20.0f, 400.0f); else if (k == 1) c.mudDb = d (-12.0f, 0.0f); else if (k == 2) c.presenceDb = d (-6.0f, 8.0f); else c.airDb = d (-6.0f, 8.0f); }
    else if (id == "comp") { if (k == 0) c.compAmount = v; else c.compSpeed = v; }
    else if (id == "sat") { if (k == 0) c.drive = v; else if (k == 1) c.satTone = d (-1.0f, 1.0f); else c.satMix = v; }
    else if (id == "deess") { if (k == 0) c.deess = v; else c.deessHz = d (4000.0f, 10000.0f); }
    else if (id == "width") { if (k == 0) c.width = v; else c.focusHz = d (80.0f, 600.0f); }
    else if (id == "delay") { if (k == 0) c.delayMix = v; else if (k == 1) c.delayFeedback = d (0.0f, 0.9f); else if (k == 2) c.delayNote = roundToInt (v * 2.0f); else c.delayTone = d (-1.0f, 1.0f); }
    else if (id == "reverb") { if (k == 0) c.verbMix = v; else if (k == 1) c.verbSize = v; else if (k == 2) c.verbPredelayMs = d (0.0f, 120.0f); else c.verbTone = d (-1.0f, 1.0f); }
    proc.studio.setVocalChain (track, c); // (heard right away; kept with the song when you let go)
    editing = c;
    pendingChain = true;
}

String Rack::valueText (const String& id, int k) const
{
    const float v = value (id, k);
    if (id == "eq") { const auto c = pendingChain ? editing : proc.songJob.vocalChainOf (track); return k == 0 ? String (roundToInt (c.lowCutHz)) + " Hz" : String (k == 1 ? c.mudDb : k == 2 ? c.presenceDb : c.airDb, 1) + " dB"; }
    if (id == "deess" && k == 1) return String ((pendingChain ? editing : proc.songJob.vocalChainOf (track)).deessHz / 1000.0f, 1) + " kHz";
    if (id == "width" && k == 1) return String (roundToInt ((pendingChain ? editing : proc.songJob.vocalChainOf (track)).focusHz)) + " Hz";
    if (id == "delay" && k == 2) { const int nt = (pendingChain ? editing : proc.songJob.vocalChainOf (track)).delayNote; return nt == 0 ? "1/4" : nt == 1 ? "1/8 dot" : "1/8"; }
    if (id == "reverb" && k == 2) return String (roundToInt ((pendingChain ? editing : proc.songJob.vocalChainOf (track)).verbPredelayMs)) + " ms";
    if ((id == "sat" && k == 1) || ((id == "delay" || id == "reverb") && k == 3) || id == "filter")
        return v < 0.47f ? "dark " + String (roundToInt ((0.5f - v) * 200.0f)) : v > 0.53f ? "bright " + String (roundToInt ((v - 0.5f) * 200.0f)) : String ("neutral");
    return String (roundToInt (v * 100.0f)) + "%";
}

std::vector<Rack::Dev> Rack::layout() const
{
    std::vector<Dev> out;
    if (track < 0) return out;
    float x = stripBox().getRight() + 10.0f;
    const float h = std::min ((float) getHeight(), 150.0f); // (a compact strip, like a DAW's device view)
    for (auto& k : kinds())
    {
        if (! isOn (proc, track, k.id)) continue;
        const auto names = knobNames (k.id);
        const float w = std::max (150.0f, 30.0f + (float) names.size() * 74.0f); // (room for its name and buttons)
        Dev d;
        d.id = k.id;
        d.box = { x, 0.0f, w, h };
        d.power = { x + 10.0f, 10.0f, 16.0f, 16.0f };
        d.close = { x + w - 24.0f, 10.0f, 14.0f, 14.0f };
        d.more = { x + w - 70.0f, 10.0f, 40.0f, 14.0f };
        for (int i = 0; i < names.size(); ++i)
            d.knobs.push_back ({ k.id, i, Rectangle<float> (x + (w - (float) names.size() * 74.0f) * 0.5f + 7.0f + (float) i * 74.0f, 44.0f, 60.0f, 60.0f) });
        out.push_back (d);
        x += w + 10.0f;
    }
    return out;
}

void Rack::paint (Graphics& g)
{
    const auto devs = layout();
    if (track < 0) return;
    {
        // the track's strip
        auto& m = proc.studio.mix[(size_t) jlimit (0, StudioPlayer::maxTracks - 1, track)];
        const auto sb = stripBox();
        g.setColour (Colours::white.withAlpha (0.045f));
        g.fillRoundedRectangle (sb, 10.0f);
        g.setColour (Colours::white.withAlpha (0.1f));
        g.drawRoundedRectangle (sb.reduced (0.5f), 10.0f, 1.0f);
        g.setColour (Colours::white.withAlpha (0.06f));
        g.fillRoundedRectangle (sb.withHeight (36.0f), 10.0f);
        g.fillRect (sb.withHeight (36.0f).withTrimmedTop (18.0f));
        g.setColour (th->ink);
        g.setFont (font (13.5f, lp::ui::Medium));
        g.drawText ("Track", Rectangle<float> (14.0f, 6.0f, 120.0f, 24.0f), Justification::centredLeft, false);
        auto knob = [&] (Rectangle<float> r, float v, const String& name, const String& txt, bool bipolar)
        {
            const auto c = r.getCentre();
            const float rad = r.getWidth() * 0.4f, a0 = MathConstants<float>::pi * 1.25f, a1 = MathConstants<float>::pi * 2.75f;
            const float am = bipolar ? (a0 + a1) * 0.5f : a0, av = a0 + (a1 - a0) * v;
            Path t0, arc;
            t0.addCentredArc (c.x, c.y, rad, rad, 0.0f, a0, a1, true);
            arc.addCentredArc (c.x, c.y, rad, rad, 0.0f, std::min (am, av), std::max (am, av), true);
            g.setColour (Colours::white.withAlpha (0.1f));
            g.strokePath (t0, PathStrokeType (4.0f, PathStrokeType::curved, PathStrokeType::rounded));
            g.setColour (th->grassLight);
            g.strokePath (arc, PathStrokeType (4.0f, PathStrokeType::curved, PathStrokeType::rounded));
            const float ang = av - MathConstants<float>::halfPi;
            g.setColour (Colours::white);
            g.fillEllipse (c.x + std::cos (ang) * rad - 3.0f, c.y + std::sin (ang) * rad - 3.0f, 6.0f, 6.0f);
            g.setFont (lp::ui::mono (8.5f, true, 0.1f));
            g.setColour (th->inkDim);
            g.drawText (name, Rectangle<float> (r.getX() - 10.0f, r.getBottom() + 2.0f, r.getWidth() + 20.0f, 12.0f), Justification::centred, false);
            g.setFont (font (11.0f, lp::ui::Medium));
            g.setColour (th->ink);
            g.drawText (txt, Rectangle<float> (r.getX() - 14.0f, r.getBottom() + 15.0f, r.getWidth() + 28.0f, 14.0f), Justification::centred, false);
        };
        const float db = m.gainDb.load(), pan = m.pan.load();
        knob (volRect(), jmap (db, -30.0f, 6.0f, 0.0f, 1.0f), "VOLUME", String (db, 1) + " dB", false);
        knob (panRect(), pan * 0.5f + 0.5f, "PAN", std::abs (pan) < 0.01f ? String ("centre") : (pan < 0 ? "L " : "R ") + String (roundToInt (std::abs (pan) * 100.0f)), true);
        auto btn = [&] (Rectangle<float> r, const String& label, bool on)
        {
            g.setColour (on ? th->grass.withAlpha (0.7f) : Colours::white.withAlpha (0.06f));
            g.fillRoundedRectangle (r, 13.0f);
            g.setColour (Colours::white.withAlpha (on ? 0.3f : 0.13f));
            g.drawRoundedRectangle (r.reduced (0.5f), 13.0f, 1.0f);
            g.setColour (th->ink);
            g.setFont (font (12.0f, lp::ui::Medium));
            g.drawText (label, r, Justification::centred, false);
        };
        btn (bypassRect(), m.fxBypass.load() ? "Bypassed" : "Bypass", m.fxBypass.load());
        btn (dryRect(), m.printsFx() ? "Drag: with FX" : "Drag: dry", false);
        btn (dragRect(), String::fromUTF8 ("\xe2\xa0\xbf  Drag track"), false);
    }
    for (auto& d : devs)
    {
        const auto* kd = [&]() -> const Kind* { for (auto& k : kinds()) if (d.id == k.id) return &k; return nullptr; }();
        g.setColour (Colours::white.withAlpha (0.045f));
        g.fillRoundedRectangle (d.box, 10.0f);
        g.setColour (Colours::white.withAlpha (0.1f));
        g.drawRoundedRectangle (d.box.reduced (0.5f), 10.0f, 1.0f);
        // the title bar: power, name, more..., remove
        g.setColour (th->grass.withAlpha (0.25f));
        g.fillRoundedRectangle (d.box.withHeight (36.0f), 10.0f);
        g.fillRect (d.box.withHeight (36.0f).withTrimmedTop (18.0f));
        g.setColour (th->grassLight);
        g.fillEllipse (d.power);
        g.setColour (Colours::black.withAlpha (0.55f));
        g.fillEllipse (d.power.reduced (5.0f));
        g.setColour (th->ink);
        g.setFont (font (13.5f, lp::ui::Medium));
        g.drawText (kd != nullptr ? kd->name : "", Rectangle<float> (d.power.getRight() + 8.0f, 6.0f, d.box.getWidth() - 110.0f, 24.0f), Justification::centredLeft, true);
        if (kd != nullptr && kd->group == 0)
        {
            g.setFont (lp::ui::mono (8.5f, true, 0.1f));
            g.setColour (th->inkDim);
            g.drawText ("MORE", d.more, Justification::centred, false);
        }
        g.setColour (Colours::white.withAlpha (0.6f));
        g.drawLine (d.close.getX() + 3.0f, d.close.getY() + 3.0f, d.close.getRight() - 3.0f, d.close.getBottom() - 3.0f, 1.5f);
        g.drawLine (d.close.getRight() - 3.0f, d.close.getY() + 3.0f, d.close.getX() + 3.0f, d.close.getBottom() - 3.0f, 1.5f);
        // its knobs
        const auto names = knobNames (d.id);
        for (auto& k : d.knobs)
        {
            const float v = value (d.id, k.index);
            const auto c = k.r.getCentre();
            const float rad = k.r.getWidth() * 0.4f, a0 = MathConstants<float>::pi * 1.25f, a1 = MathConstants<float>::pi * 2.75f;
            Path track0, arc;
            track0.addCentredArc (c.x, c.y, rad, rad, 0.0f, a0, a1, true);
            arc.addCentredArc (c.x, c.y, rad, rad, 0.0f, a0, a0 + (a1 - a0) * v, true);
            g.setColour (Colours::white.withAlpha (0.1f));
            g.strokePath (track0, PathStrokeType (4.0f, PathStrokeType::curved, PathStrokeType::rounded));
            g.setColour (th->grassLight);
            g.strokePath (arc, PathStrokeType (4.0f, PathStrokeType::curved, PathStrokeType::rounded));
            const float ang = a0 + (a1 - a0) * v - MathConstants<float>::halfPi;
            g.setColour (Colours::white);
            g.fillEllipse (c.x + std::cos (ang) * rad - 3.0f, c.y + std::sin (ang) * rad - 3.0f, 6.0f, 6.0f);
            g.setFont (lp::ui::mono (8.5f, true, 0.1f));
            g.setColour (th->inkDim);
            g.drawText (names[k.index], Rectangle<float> (k.r.getX() - 10.0f, k.r.getBottom() + 2.0f, k.r.getWidth() + 20.0f, 12.0f), Justification::centred, false);
            g.setFont (font (11.0f, lp::ui::Medium));
            g.setColour (th->ink);
            g.drawText (valueText (d.id, k.index), Rectangle<float> (k.r.getX() - 14.0f, k.r.getBottom() + 15.0f, k.r.getWidth() + 28.0f, 14.0f), Justification::centred, false);
        }
    }
    if (devs.empty() || dropping)
    {
        auto area = devs.empty() ? getLocalBounds().toFloat().withTrimmedLeft (stripBox().getRight() + 10.0f).withHeight (stripBox().getHeight()) : Rectangle<float> (devs.back().box.getRight() + 10.0f, 0.0f, 200.0f, (float) getHeight());
        Path outline;
        outline.addRoundedRectangle (area.reduced (2.0f), 10.0f);
        Path dashed;
        const float dashes[] = { 6.0f, 5.0f };
        PathStrokeType (1.2f).createDashedStroke (dashed, outline, dashes, 2);
        g.setColour ((dropping ? th->grassLight : Colours::white.withAlpha (0.2f)));
        g.fillPath (dashed);
        g.setFont (font (13.0f));
        g.setColour (th->inkDim);
        g.drawText (dropping ? "Drop it here" : "No effects on this track yet. Drag one in from the browser (Effects, top left), or double-click one there.",
                    area.reduced (20.0f), Justification::centred, true);
    }
}

void Rack::mouseDown (const MouseEvent& e)
{
    dragKnob = -1;
    draggingFile = false;
    if (track >= 0)
    {
        auto& m = proc.studio.mix[(size_t) jlimit (0, StudioPlayer::maxTracks - 1, track)];
        if (bypassRect().contains (e.position)) { m.fxBypass = ! m.fxBypass.load(); if (onChanged) onChanged(); repaint(); return; }
        if (dryRect().contains (e.position))
        {
            const bool on = m.printsFx();
            m.dragDry = on; m.printFx = ! on; // (with effects -> dry; dry -> recorded with them)
            repaint();
            return;
        }
        if (volRect().expanded (8.0f).contains (e.position))
        {
            if (e.getNumberOfClicks() > 1) { m.gainDb = 0.0f; if (onChanged) onChanged(); repaint(); return; }
            dragId = "~vol"; dragKnob = 0; dragStart = jmap (m.gainDb.load(), -30.0f, 6.0f, 0.0f, 1.0f); return;
        }
        if (panRect().expanded (8.0f).contains (e.position))
        {
            if (e.getNumberOfClicks() > 1) { m.pan = 0.0f; if (onChanged) onChanged(); repaint(); return; }
            dragId = "~pan"; dragKnob = 0; dragStart = m.pan.load() * 0.5f + 0.5f; return;
        }
    }
    for (auto& d : layout())
    {
        if (d.close.expanded (4.0f).contains (e.position)) { remove (proc, track, d.id); if (onChanged) onChanged(); repaint(); return; }
        if (d.power.expanded (4.0f).contains (e.position)) { remove (proc, track, d.id); if (onChanged) onChanged(); repaint(); return; } // (off: out of the chain)
        if (d.more.contains (e.position) && onOpenChain) { onOpenChain (track); return; }
        for (auto& k : d.knobs)
            if (k.r.expanded (8.0f).contains (e.position)) { dragId = d.id; dragKnob = k.index; dragStart = value (d.id, k.index); return; }
    }
}

void Rack::mouseDrag (const MouseEvent& e)
{
    if (track >= 0 && ! draggingFile && dragRect().contains (e.mouseDownPosition) && e.getDistanceFromDragStart() > 5)
    {
        // the track (as you hear it) dragged out, into your DAW or a folder
        draggingFile = true;
        const auto f = proc.fileToDrag (track);
        if (f.existsAsFile()) DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this);
        return;
    }
    if (dragKnob >= 0 && dragId.startsWith ("~"))
    {
        auto& m = proc.studio.mix[(size_t) jlimit (0, StudioPlayer::maxTracks - 1, track)];
        const float v = jlimit (0.0f, 1.0f, dragStart - (float) e.getDistanceFromDragStartY() / 160.0f);
        if (dragId == "~vol") m.gainDb = jmap (v, -30.0f, 6.0f);
        else m.pan = std::abs (v - 0.5f) < 0.02f ? 0.0f : v * 2.0f - 1.0f;
        if (onChanged) onChanged();
        repaint();
        return;
    }
    if (dragKnob < 0) return;
    setValue (dragId, dragKnob, dragStart - (float) e.getDistanceFromDragStartY() / 160.0f);
    repaint();
}

void Rack::mouseUp (const MouseEvent&)
{
    if (pendingChain && track >= 0) proc.songJob.setVocalChain (track, editing);
    pendingChain = false;
    dragKnob = -1;
}

void Rack::mouseDoubleClick (const MouseEvent& e)
{
    // double-click a knob: back to where it started on this track
    for (auto& d : layout())
        for (auto& k : d.knobs)
            if (k.r.expanded (8.0f).contains (e.position))
            {
                setValue (d.id, k.index, dragStart);
                if (pendingChain) { proc.songJob.setVocalChain (track, editing); pendingChain = false; }
                repaint();
                return;
            }
}

void Rack::itemDropped (const SourceDetails& d)
{
    dropping = false;
    add (proc, track, d.description.toString().fromFirstOccurrenceOf ("fx:", false, false));
    if (onChanged) onChanged();
    repaint();
}
} // namespace lp::fx
