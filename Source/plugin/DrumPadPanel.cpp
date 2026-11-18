#include "PluginEditor.h"
#include "PadLook.h"

using namespace juce;
using lp::ui::font;

// The drum pads: the drum track's kit on pads, like a drum machine. Play them with the mouse, your computer
// keys or a MIDI controller; press Record, play along with the song, and your hits are added to the pattern
// when you stop (snapped to 1/16, or as you played them). Undo takes the whole take back.

namespace
{
    const lp::ui::Theme& themeOf (Component& c)
    {
        const auto* lnf = dynamic_cast<LivePlanetLookAndFeel*> (&c.getLookAndFeel());
        return lnf != nullptr ? *lnf->th : lp::ui::theme (0);
    }
    struct Pad { int note; char key; const char* name; };
    // bottom row first (pad 1 bottom left), like the keys under your fingers
    const Pad pads[9] = { { 36, 'z', "KICK" }, { 38, 'x', "SNARE" }, { 39, 'c', "CLAP" },
                          { 42, 'a', "HAT" }, { 46, 's', "OPEN HAT" }, { 70, 'd', "PERC" },
                          { 49, 'q', "CRASH" }, { 45, 'w', "TOM" }, { 37, 'e', "RIM" } };
    constexpr int numPads = 9;
    Colour padColour (int i)
    {
        static const Colour base[4] = { Colour (0xff8b6cf6), Colour (0xff5fbf8f), Colour (0xffd9a05b), Colour (0xff6aaed6) };
        return base[i % 4];
    }
}

DrumPadPanel::DrumPadPanel (LivePlanetProcessor& p, int trackIndex, std::function<void()> closed)
    : proc (p), track (trackIndex), onClosed (std::move (closed))
{
    setWantsKeyboardFocus (true);
    playBtn.onClick = [this] { if (proc.studio.length() > 0) proc.previewPlaying = ! proc.previewPlaying.load(); };
    recBtn.onClick = [this]
    {
        recording = ! recording;
        if (! recording) finishTake();
        else { take.clear(); message = "Recording: press Play (or play your DAW) and play the pads"; }
        repaint();
    };
    quantBtn.onClick = [this] { quantize = ! quantize; repaint(); };
    for (auto* b : { &playBtn, &recBtn, &quantBtn }) { b->setWantsKeyboardFocus (false); addAndMakeVisible (*b); }
    recBtn.getProperties().set ("primary", true);
    proc.drumPadsOpen = true;
    const auto pv = proc.songJob.partView (track);
    title = pv.name.isNotEmpty() ? pv.name : String ("Drums");
    startTimerHz (30);
}

DrumPadPanel::~DrumPadPanel()
{
    if (recording) finishTake();
    proc.drumPadsOpen = false;
}

void DrumPadPanel::finishTake()
{
    if (take.empty()) return;
    auto notes = proc.songJob.partView (track).notes;
    int added = 0;
    for (auto& h : take)
    {
        double beat = quantize ? std::round (h.beat * 4.0) / 4.0 : h.beat;
        beat = std::max (0.0, beat);
        const bool there = std::any_of (notes.begin(), notes.end(), [&] (const lp::song::Note& n) { return n.pitch == h.pad && std::abs (n.beat - beat) < 0.12; });
        if (there) continue;
        notes.push_back ({ beat, 0.25, h.pad, jlimit (0.2f, 1.0f, h.vel) });
        ++added;
    }
    std::sort (notes.begin(), notes.end(), [] (const lp::song::Note& a, const lp::song::Note& b) { return a.beat < b.beat; });
    proc.songJob.setNotes (track, notes);
    message = "Added " + String (added) + " hits to the drums (Undo takes them back)";
    take.clear();
}

void DrumPadPanel::timerCallback()
{
    // while it's open, your keys and MIDI notes play this kit
    proc.drumKeysTrack = track;
    proc.drumKeysLease = Time::getMillisecondCounter();
    if (isShowing() && ! hasKeyboardFocus (true)) grabKeyboardFocus(); // your keys play the pads
    const auto now = Time::getMillisecondCounter();
    for (auto& h : proc.takePadHits())
    {
        flashAt[h.pad] = now;
        if (recording && h.playing) take.push_back (h);
    }
    const bool playing = proc.songIsPlaying();
    if (recording && wasPlaying && ! playing) finishTake(); // you stopped: the take goes in (still armed)
    wasPlaying = playing;
    playBtn.setButtonText (proc.previewPlaying.load() ? "Stop" : "Play");
    recBtn.setButtonText (recording ? (playing ? "Recording..." : "Record: on") : "Record");
    quantBtn.setButtonText (quantize ? "Snap: 1/16" : "Snap: off");
    repaint();
}

Rectangle<int> DrumPadPanel::card() const
{
    return Rectangle<int> (jmin (900, getWidth() - 40), jmin (640, getHeight() - 40)).withCentre (getLocalBounds().getCentre());
}
Rectangle<float> DrumPadPanel::padRect (int i) const
{
    const auto c = card().toFloat();
    const float top = c.getY() + 104.0f, side = std::min (c.getHeight() - 104.0f - 32.0f, 470.0f);
    const auto a = Rectangle<float> (c.getX() + 32.0f, top, side, side);
    const float gap = 14.0f, w = (a.getWidth() - 2 * gap) / 3.0f;
    const int col = i % 3, row = 2 - i / 3; // pad 1 bottom left
    return { a.getX() + col * (w + gap), a.getY() + row * (w + gap), w, w };
}

void DrumPadPanel::resized()
{
    const auto c = card();
    closeX = Rectangle<int> (c.getRight() - 50, c.getY() + 24, 30, 30);
    recBtn.setBounds (c.getRight() - 196, c.getY() + 22, 132, 36);
    playBtn.setBounds (c.getRight() - 282, c.getY() + 22, 76, 36);
    quantBtn.setBounds (c.getRight() - 408, c.getY() + 22, 116, 36);
}

void DrumPadPanel::paint (Graphics& g)
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
        g.setGradientFill (ColourGradient (t.grass.withAlpha (0.16f), cf.getRight() - 80.0f, cf.getY(), t.grass.withAlpha (0.0f), cf.getRight() - 460.0f, cf.getY() + 360.0f, true));
        g.fillRect (cf);
    }
    g.setColour (Colours::white.withAlpha (0.1f));
    g.drawRoundedRectangle (cf.reduced (0.5f), 22.0f, 1.0f);
    auto r = cf.reduced (28.0f, 24.0f);
    g.setFont (lp::ui::mono (10.0f, true, 0.18f));
    g.setColour (t.grassLight);
    g.drawText ("DRUM PADS", r.removeFromTop (14.0f), Justification::centredLeft, false);
    g.setFont (font (26.0f, lp::ui::Medium, -0.02f));
    g.setColour (t.ink);
    g.drawText (title.substring (0, 1).toUpperCase() + title.substring (1), r.removeFromTop (40.0f).withTrimmedRight (440.0f), Justification::centredLeft, true);
    g.setColour (t.inkDim);
    const auto cx = closeX.toFloat().reduced (9.0f);
    g.drawLine (cx.getX(), cx.getY(), cx.getRight(), cx.getBottom(), 1.6f);
    g.drawLine (cx.getRight(), cx.getY(), cx.getX(), cx.getBottom(), 1.6f);

    const auto now = Time::getMillisecondCounter();
    for (int i = 0; i < numPads; ++i)
    {
        float lit = 0.0f;
        if (auto it = flashAt.find (pads[i].note); it != flashAt.end()) lit = jlimit (0.0f, 1.0f, 1.0f - (float) (now - it->second) / 300.0f);
        lp::ui::drawPad (g, padRect (i), padColour (i), lit, pads[i].name, String::charToString ((juce_wchar) std::toupper (pads[i].key)), {},
                         false, lp::ui::mono (10.0f, true, 0.14f), lp::ui::mono (9.0f, false, 0.08f), t.ink, t.inkDim);
    }

    auto info = Rectangle<float> (padRect (2).getRight() + 36.0f, padRect (6).getY() + 4.0f, cf.getRight() - 32.0f - padRect (2).getRight() - 36.0f, cf.getHeight() - 140.0f);
    auto head = [&] (const char* h) { g.setFont (lp::ui::mono (9.0f, true, 0.16f)); g.setColour (t.grassLight); g.drawText (h, info.removeFromTop (14.0f), Justification::centredLeft, false); info.removeFromTop (4.0f); };
    auto para = [&] (const String& txt, float h) { g.setFont (font (12.5f, lp::ui::Light)); g.setColour (t.ink.withAlpha (0.7f)); g.drawFittedText (txt, info.removeFromTop (h).toNearestInt(), Justification::topLeft, 5, 1.0f); info.removeFromTop (14.0f); };
    head ("PLAY");
    para ("Tap a pad or its key. Shift hits harder. A MIDI controller works too: drum notes land on the right sound.", 52.0f);
    head ("RECORD");
    para ("Record, then Play, and play along. When you stop, your hits join the beat, snapped to 1/16. Undo takes them back.", 52.0f);
    if (recording && proc.songIsPlaying())
    {
        g.setFont (font (20.0f, lp::ui::Medium));
        g.setColour (Colour (0xffff5a6e));
        const double beat = proc.playheadPos.load() / StudioPlayer::fileRate * proc.songBpm.load() / 60.0;
        g.drawText (String::fromUTF8 ("\xe2\x97\x8f  bar ") + String ((int) (beat / 4.0) + 1) + "   " + String ((int) take.size()) + " hits", info.removeFromTop (30.0f), Justification::centredLeft, false);
    }
    if (message.isNotEmpty())
    {
        g.setFont (font (13.0f, lp::ui::Medium));
        g.setColour (t.inkDim);
        g.drawFittedText (message, info.removeFromBottom (40.0f).toNearestInt(), Justification::bottomLeft, 2, 1.0f);
    }
}

void DrumPadPanel::mouseDown (const MouseEvent& e)
{
    grabKeyboardFocus();
    if (closeX.contains (e.getPosition()) || ! card().contains (e.getPosition())) { if (onClosed) onClosed(); return; }
    for (int i = 0; i < numPads; ++i)
        if (padRect (i).contains (e.position))
        {
            const auto pr = padRect (i);
            const float vel = jlimit (0.35f, 1.0f, 0.45f + 0.6f * (1.0f - (e.position.y - pr.getY()) / pr.getHeight()));
            proc.hitDrum (pads[i].note, vel);
            return;
        }
}

bool DrumPadPanel::keyPressed (const KeyPress& k)
{
    if (k == KeyPress::escapeKey) { if (onClosed) onClosed(); return true; }
    if (k == KeyPress::spaceKey) { if (proc.studio.length() > 0) proc.previewPlaying = ! proc.previewPlaying.load(); return true; }
    if (k.getModifiers().isCommandDown() || k.getModifiers().isCtrlDown()) return false;
    const auto ch = (char) std::tolower ((int) k.getTextCharacter());
    for (auto& p : pads)
        if (p.key == ch)
        {
            const int code = k.getKeyCode();
            if (heldKeys.count (code)) return true;
            heldKeys.insert (code);
            proc.hitDrum (p.note, k.getModifiers().isShiftDown() ? 1.0f : 0.85f);
            return true;
        }
    return false;
}

bool DrumPadPanel::keyStateChanged (bool)
{
    for (auto it = heldKeys.begin(); it != heldKeys.end();)
        it = KeyPress::isKeyCurrentlyDown (*it) ? std::next (it) : heldKeys.erase (it);
    return false;
}
