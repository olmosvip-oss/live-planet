#include "PluginEditor.h"
#include "../song/Chops.h"
#include "PadLook.h"

using namespace juce;
using lp::ui::font;

// The chop pads: a vocal chops track's recording with its chops marked on it, and 16 pads, one per chop
// (like a sampler's slice mode). Play them with the mouse, your computer keys or a MIDI controller; press
// Record and play along with the song, and what you play replaces those bars of the track. Drag a chop's
// edges to change it, double-click to add one, Delete removes the picked one.

namespace
{
    const lp::ui::Theme& themeOf (Component& c)
    {
        const auto* lnf = dynamic_cast<LivePlanetLookAndFeel*> (&c.getLookAndFeel());
        return lnf != nullptr ? *lnf->th : lp::ui::theme (0);
    }
    // Loopwright's own four colours (purple, green, amber, blue), going round; each row a touch deeper
    Colour padColour (int i)
    {
        static const Colour base[4] = { Colour (0xff8b6cf6), Colour (0xff5fbf8f), Colour (0xffd9a05b), Colour (0xff6aaed6) };
        return base[i % 4].withMultipliedBrightness (1.0f - 0.07f * (float) ((i / 4) % 4));
    }
    // pads 1-4 on the bottom row, like a drum machine: Z X C V, then A S D F, Q W E R, 1 2 3 4
    const char* padKeys = "zxcvasdfqwer1234";
    constexpr double rate = LiveDrums::kitRate;
    constexpr int maxPads = 16;
}

ChopPanel::ChopPanel (LivePlanetProcessor& p, int trackIndex, std::function<void()> closed)
    : proc (p), track (trackIndex), onClosed (std::move (closed))
{
    setWantsKeyboardFocus (true);
    playBtn.onClick = [this] { if (proc.studio.length() > 0) proc.previewPlaying = ! proc.previewPlaying.load(); };
    recBtn.onClick = [this]
    {
        recording = ! recording;
        if (! recording) finishTake();
        else { take.clear(); takeFrom = takeTo = -1.0; message = "Recording: press Play (or play your DAW) and play the pads"; }
        repaint();
    };
    quantBtn.onClick = [this] { quantize = ! quantize; repaint(); };
    autoBtn.onClick = [this]
    {
        chops = lp::song::chops::find (audio, rate, maxPads);
        origin.assign (chops.size(), -1);
        selected = -1;
        cutPads();
        commitChops();
        message = "Found " + String ((int) chops.size()) + " chops again";
        repaint();
    };
    for (auto* b : { &playBtn, &recBtn, &quantBtn, &autoBtn }) { b->setWantsKeyboardFocus (false); addAndMakeVisible (*b); }
    recBtn.getProperties().set ("primary", true);
    load();
    proc.padsTrack = track;
    startTimerHz (30);
}

ChopPanel::~ChopPanel()
{
    if (recording) finishTake();
    if (chopsDirty) commitChops();
    proc.padsTrack = -1;
    proc.livePads.setKit ({});
    if (mutedForTake) proc.studio.mix[(size_t) jlimit (0, StudioPlayer::maxTracks - 1, track)].mute = false;
}

void ChopPanel::load()
{
    view = proc.songJob.chopView (track);
    seenVersion = view.version;
    audio.setSize (2, 0);
    if (! view.valid) return;
    AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<AudioFormatReader> rd (fm.createReaderFor (File (view.path)));
    if (rd == nullptr || rd->sampleRate <= 0) return;
    const int len = (int) std::min<int64> (rd->lengthInSamples, (int64) (rd->sampleRate * 120.0));
    AudioBuffer<float> raw (2, len);
    raw.clear();
    rd->read (&raw, 0, len, 0, true, rd->numChannels > 1);
    if (rd->numChannels == 1) raw.copyFrom (1, 0, raw, 0, 0, len);
    // at the pads' rate
    const double ratio = rd->sampleRate / rate;
    const int outLen = (int) (len / ratio);
    audio.setSize (2, outLen);
    for (int ch = 0; ch < 2; ++ch)
    {
        LagrangeInterpolator li;
        li.process (ratio, raw.getReadPointer (ch), audio.getWritePointer (ch), outLen);
    }
    chops = view.chops;
    origin.resize (chops.size());
    for (size_t i = 0; i < origin.size(); ++i) origin[i] = (int) i;
    if (chops.empty())
    {
        // an older chops track: its chops are found now, and kept with it
        chops = lp::song::chops::find (audio, rate, maxPads);
        origin.assign (chops.size(), -1);
        commitChops();
    }
    if ((int) chops.size() > maxPads) chops.resize (maxPads), origin.resize (maxPads);
    cutPads();
    peaksWidth = -1;
}

void ChopPanel::cutPads()
{
    LiveDrums::Kit kit;
    const int n = audio.getNumSamples();
    for (int i = 0; i < (int) chops.size() && i < maxPads; ++i)
    {
        const int a0 = jlimit (0, n, (int) (chops[(size_t) i].start * rate));
        const int len = jlimit (0, n - a0, (int) (chops[(size_t) i].len * rate));
        if (len < 32) continue;
        auto b = std::make_shared<AudioBuffer<float>> (2, len);
        for (int ch = 0; ch < 2; ++ch) b->copyFrom (ch, 0, audio, ch, a0, len);
        const int fi = std::min (len / 4, (int) (0.003 * rate)), fo = std::min (len / 3, (int) (0.02 * rate));
        b->applyGainRamp (0, fi, 0.0f, 1.0f);
        b->applyGainRamp (len - fo, fo, 1.0f, 0.0f);
        kit[i] = b;
    }
    proc.livePads.setKit (std::move (kit));
}

void ChopPanel::commitChops()
{
    std::vector<int> remap (view.chops.size(), -1);
    for (size_t i = 0; i < origin.size(); ++i)
        if (origin[i] >= 0 && origin[i] < (int) remap.size()) remap[(size_t) origin[i]] = (int) i;
    proc.songJob.setChops (track, chops, remap);
    view.chops = chops;
    for (size_t i = 0; i < origin.size(); ++i) origin[i] = (int) i;
    chopsDirty = false;
}

double ChopPanel::songBeat() const
{
    return proc.playheadPos.load() / StudioPlayer::fileRate * view.bpm / 60.0;
}

void ChopPanel::finishTake()
{
    auto& mix = proc.studio.mix[(size_t) jlimit (0, StudioPlayer::maxTracks - 1, track)];
    if (mutedForTake) { mix.mute = false; mutedForTake = false; }
    if (take.empty() || takeFrom < 0.0) { take.clear(); takeFrom = takeTo = -1.0; return; }
    // the bars you played over are replaced by what you played
    const double b0 = std::floor (takeFrom / 4.0) * 4.0, b1 = std::ceil ((takeTo + 0.01) / 4.0) * 4.0;
    const auto latest = proc.songJob.chopView (track);
    std::vector<lp::song::Note> notes;
    for (auto& nt : latest.notes) if (nt.beat < b0 || nt.beat >= b1) notes.push_back (nt);
    int added = 0;
    for (auto& h : take)
    {
        if (h.pad < 0 || h.pad >= (int) chops.size()) continue;
        double beat = quantize ? std::round (h.beat * 4.0) / 4.0 : h.beat;
        if (beat < b0 - 0.13 || beat >= b1) continue;
        beat = std::max (0.0, beat);
        const auto& c = chops[(size_t) h.pad];
        const double len = jlimit (0.125, 2.0, c.len * view.bpm / 60.0);
        notes.push_back ({ beat, len, c.midi > 0.0 ? roundToInt (c.midi) : 60, jlimit (0.2f, 1.0f, h.vel), false, h.pad });
        ++added;
    }
    std::sort (notes.begin(), notes.end(), [] (const lp::song::Note& a, const lp::song::Note& b) { return a.beat < b.beat; });
    proc.songJob.setNotes (track, notes);
    message = "Recorded " + String (added) + " hits into bars " + String ((int) (b0 / 4.0) + 1) + String::fromUTF8 ("\xe2\x80\x93") + String ((int) (b1 / 4.0))
            + ": they replace what was there (Undo brings it back)";
    take.clear();
    takeFrom = takeTo = -1.0;
}

void ChopPanel::timerCallback()
{
    bool redraw = false;
    if (isShowing() && ! hasKeyboardFocus (true)) grabKeyboardFocus(); // your keys play the pads
    const auto now = Time::getMillisecondCounter();
    for (auto& h : proc.takePadHits())
    {
        if (h.pad >= 0 && h.pad < maxPads) flashAt[(size_t) h.pad] = now;
        if (h.pad < (int) chops.size()) selected = h.pad;
        if (recording && h.playing)
        {
            take.push_back (h);
            if (takeFrom < 0.0) takeFrom = h.beat;
        }
        redraw = true;
    }
    const bool playing = proc.songIsPlaying();
    if (recording && playing)
    {
        const double b = songBeat();
        if (takeFrom < 0.0) takeFrom = b;
        takeTo = std::max (takeTo, b);
        // the track's old notes are quiet while you record over it
        auto& mix = proc.studio.mix[(size_t) jlimit (0, StudioPlayer::maxTracks - 1, track)];
        if (! mutedForTake && ! mix.mute.load()) { mix.mute = true; mutedForTake = true; }
        redraw = true;
    }
    if (recording && wasPlaying && ! playing) finishTake(); // you stopped: the take goes in (still armed for another)
    wasPlaying = playing;
    if (chopsDirty && now - changedAt > 400 && dragChop < 0) commitChops();
    // the song changed elsewhere (an undo, a new take): its notes
    if (const auto st = proc.songJob.getStatus(); st.version != seenVersion && ! chopsDirty)
    {
        seenVersion = st.version;
        const auto v = proc.songJob.chopView (track);
        if (! v.valid) { if (onClosed) onClosed(); return; }
        view.notes = v.notes;
        if (v.chops.size() != chops.size()) { view.chops = v.chops; chops = v.chops; origin.resize (chops.size()); for (size_t i = 0; i < origin.size(); ++i) origin[i] = (int) i; cutPads(); }
    }
    playBtn.setButtonText (proc.previewPlaying.load() ? "Stop" : "Play");
    recBtn.setButtonText (recording ? (playing ? "Recording..." : "Record: on") : "Record");
    quantBtn.setButtonText (quantize ? "Snap: 1/16" : "Snap: off");
    for (auto t : flashAt) if (now - t < 260) redraw = true;
    if (redraw || now - lastPaint > 250) { lastPaint = now; repaint(); }
}

Rectangle<int> ChopPanel::card() const
{
    return Rectangle<int> (jmin (1040, getWidth() - 40), jmin (700, getHeight() - 40)).withCentre (getLocalBounds().getCentre());
}
Rectangle<float> ChopPanel::waveArea() const
{
    const auto c = card().toFloat();
    return { c.getX() + 28.0f, c.getY() + 96.0f, c.getWidth() - 56.0f, 170.0f };
}
Rectangle<float> ChopPanel::padArea() const
{
    const auto c = card().toFloat();
    const auto w = waveArea();
    const float top = w.getBottom() + 26.0f, h = c.getBottom() - 60.0f - top;
    const float side = std::min (h, c.getWidth() * 0.5f);
    return { c.getX() + 28.0f, top, side * 1.25f, h };
}
Rectangle<float> ChopPanel::padRect (int i) const
{
    const auto a = padArea();
    const float gap = 10.0f, w = (a.getWidth() - 3 * gap) / 4.0f, h = (a.getHeight() - 3 * gap) / 4.0f;
    const int col = i % 4, row = 3 - i / 4; // pad 1 bottom left
    return { a.getX() + col * (w + gap), a.getY() + row * (h + gap), w, h };
}

void ChopPanel::resized()
{
    auto c = card();
    closeX = Rectangle<int> (c.getRight() - 50, c.getY() + 24, 30, 30);
    recBtn.setBounds (c.getRight() - 196, c.getY() + 22, 132, 36);
    playBtn.setBounds (c.getRight() - 282, c.getY() + 22, 76, 36);
    quantBtn.setBounds (c.getRight() - 408, c.getY() + 22, 116, 36);
    autoBtn.setBounds (c.getRight() - 562, c.getY() + 22, 144, 36);
    peaksWidth = -1;
}

float ChopPanel::xOf (double sec) const
{
    const auto w = waveArea();
    const double total = std::max (0.01, audio.getNumSamples() / rate);
    return w.getX() + (float) (sec / total) * w.getWidth();
}
double ChopPanel::secOf (float x) const
{
    const auto w = waveArea();
    const double total = audio.getNumSamples() / rate;
    return jlimit (0.0, total, (double) ((x - w.getX()) / w.getWidth()) * total);
}

void ChopPanel::paint (Graphics& g)
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
    g.drawText ("CHOP PADS", r.removeFromTop (14.0f), Justification::centredLeft, false);
    g.setFont (font (26.0f, lp::ui::Medium, -0.02f));
    g.setColour (t.ink);
    g.drawText (File (view.path).getFileNameWithoutExtension(), r.removeFromTop (40.0f).withTrimmedRight (580.0f), Justification::centredLeft, true);
    // close
    g.setColour (t.inkDim);
    const auto cx = closeX.toFloat().reduced (9.0f);
    g.drawLine (cx.getX(), cx.getY(), cx.getRight(), cx.getBottom(), 1.6f);
    g.drawLine (cx.getRight(), cx.getY(), cx.getX(), cx.getBottom(), 1.6f);

    // ---- the recording, with its chops
    const auto w = waveArea();
    g.setColour (Colours::black.withAlpha (0.38f));
    g.fillRoundedRectangle (w.expanded (8.0f), 12.0f);
    const int n = audio.getNumSamples();
    if (n > 0)
    {
        if (peaksWidth != (int) w.getWidth())
        {
            peaksWidth = (int) w.getWidth();
            peaks.assign ((size_t) peaksWidth, 0.0f);
            for (int x = 0; x < peaksWidth; ++x)
            {
                const int a = (int) ((int64) x * n / peaksWidth), b = std::max (a + 1, (int) ((int64) (x + 1) * n / peaksWidth));
                peaks[(size_t) x] = std::max (audio.getMagnitude (0, a, std::min (b, n) - a), audio.getMagnitude (1, a, std::min (b, n) - a));
            }
            float pk = 0.0001f; for (float v : peaks) pk = std::max (pk, v);
            for (auto& v : peaks) v /= pk;
        }
        const auto now = Time::getMillisecondCounter();
        for (int i = 0; i < (int) chops.size(); ++i)
        {
            const auto& c = chops[(size_t) i];
            const float x0 = xOf (c.start), x1 = std::max (x0 + 2.0f, xOf (c.start + c.len));
            const float lit = jlimit (0.0f, 1.0f, 1.0f - (float) (now - flashAt[(size_t) i]) / 260.0f);
            g.setColour (padColour (i).withAlpha ((i == selected ? 0.18f : 0.07f) + 0.25f * lit));
            g.fillRect (Rectangle<float> (x0, w.getY(), x1 - x0, w.getHeight()));
            g.setColour (padColour (i).withAlpha (0.9f));
            g.fillRect (Rectangle<float> (x0, w.getY(), 2.0f, w.getHeight()));
            g.setFont (lp::ui::mono (9.0f, true, 0.06f));
            g.drawText (String (i + 1), Rectangle<float> (x0 + 4.0f, w.getY() + 3.0f, 24.0f, 12.0f), Justification::centredLeft, false);
            if (i == selected)
            {
                g.setColour (padColour (i));
                g.drawRect (Rectangle<float> (x0, w.getY(), x1 - x0, w.getHeight()), 1.5f);
                g.fillRect (Rectangle<float> (x1 - 2.0f, w.getY(), 2.0f, w.getHeight()));
            }
        }
        const float mid = w.getCentreY(), half = w.getHeight() * 0.40f;
        for (int x = 0; x < peaksWidth; ++x)
        {
            const double sec = (double) x / peaksWidth * n / rate;
            int in = -1;
            for (int i = 0; i < (int) chops.size(); ++i) if (sec >= chops[(size_t) i].start && sec < chops[(size_t) i].start + chops[(size_t) i].len) in = i;
            g.setColour (in >= 0 ? padColour (in).withAlpha (0.85f) : Colours::white.withAlpha (0.22f));
            const float h = std::max (0.5f, peaks[(size_t) x] * half);
            g.fillRect (w.getX() + (float) x, mid - h, 1.0f, h * 2.0f);
        }
    }
    else
    {
        g.setFont (font (15.0f, lp::ui::Medium));
        g.setColour (t.inkDim);
        g.drawText ("Couldn't open this track's vocal.", w, Justification::centred, false);
    }
    g.setFont (lp::ui::mono (9.0f, false, 0.12f));
    g.setColour (t.inkDim);
    g.drawText ("CLICK A CHOP: HEAR IT  |  DRAG ITS EDGES: SHORTER OR LONGER  |  DRAG ITS MIDDLE: MOVE IT  |  DOUBLE-CLICK: ADD ONE  |  DELETE: REMOVE THE PICKED ONE",
                Rectangle<float> (w.getX(), w.getBottom() + 8.0f, w.getWidth(), 14.0f), Justification::centredLeft, false);

    // ---- the pads
    const auto now = Time::getMillisecondCounter();
    for (int i = 0; i < maxPads; ++i)
    {
        const auto pr = padRect (i);
        if (i >= (int) chops.size()) { lp::ui::drawEmptyPad (g, pr); continue; }
        const float lit = jlimit (0.0f, 1.0f, 1.0f - (float) (now - flashAt[(size_t) i]) / 300.0f);
        const int m = chops[(size_t) i].midi > 0.0 ? roundToInt (chops[(size_t) i].midi) : -1;
        lp::ui::drawPad (g, pr, padColour (i), lit, "PAD " + String (i + 1),
                         String::charToString ((juce_wchar) toupper (padKeys[i])), m >= 0 ? String (lp::noteName (m)) + String (m / 12 - 1) : String(),
                         i == selected, lp::ui::mono (9.5f, true, 0.12f), lp::ui::mono (9.0f, false, 0.08f), t.ink, t.inkDim);
    }

    // ---- what's what, on the right
    const auto pa = padArea();
    auto info = Rectangle<float> (pa.getRight() + 30.0f, pa.getY(), cf.getRight() - 28.0f - pa.getRight() - 30.0f, pa.getHeight());
    auto head = [&] (const char* h) { g.setFont (lp::ui::mono (9.0f, true, 0.16f)); g.setColour (t.grassLight); g.drawText (h, info.removeFromTop (14.0f), Justification::centredLeft, false); info.removeFromTop (4.0f); };
    auto para = [&] (const String& txt, float h) { g.setFont (font (12.5f, lp::ui::Light)); g.setColour (t.ink.withAlpha (0.7f)); g.drawFittedText (txt, info.removeFromTop (h).toNearestInt(), Justification::topLeft, 5, 1.0f); info.removeFromTop (14.0f); };
    head ("PLAY");
    para ("Click a pad, tap its key, or use a MIDI controller (C1 is pad 1).", 34.0f);
    head ("RECORD");
    para ("Record, then Play, and jam with the song. When you stop, your take replaces those bars. Undo brings them back.", 52.0f);
    head ("EDIT");
    para ("Drag a chop's edges on the waveform. Double-click adds one, Delete removes it.", 36.0f);
    if (recording && proc.songIsPlaying())
    {
        g.setFont (font (20.0f, lp::ui::Medium));
        g.setColour (Colour (0xffff5a6e));
        g.drawText (String::fromUTF8 ("\xe2\x97\x8f  bar ") + String ((int) (songBeat() / 4.0) + 1) + "   " + String ((int) take.size()) + " hits",
                    info.removeFromTop (30.0f), Justification::centredLeft, false);
    }
    if (message.isNotEmpty())
    {
        g.setFont (font (13.0f, lp::ui::Medium));
        g.setColour (t.inkDim);
        g.drawFittedText (message, info.removeFromBottom (54.0f).toNearestInt(), Justification::bottomLeft, 3, 1.0f);
    }
}

int ChopPanel::chopAt (float x, int& edge) const
{
    edge = 0;
    // the picked chop's edges first, then any chop's edges, then the chop under the mouse
    auto edgeOf = [&] (int i)
    {
        const auto& c = chops[(size_t) i];
        if (std::abs (x - xOf (c.start)) < 6.0f) return 1;
        if (std::abs (x - xOf (c.start + c.len)) < 6.0f) return 2;
        return 0;
    };
    if (selected >= 0 && selected < (int) chops.size()) if (const int e = edgeOf (selected)) { edge = e; return selected; }
    for (int i = 0; i < (int) chops.size(); ++i) if (const int e = edgeOf (i)) { edge = e; return i; }
    const double s = secOf (x);
    for (int i = (int) chops.size() - 1; i >= 0; --i)
        if (s >= chops[(size_t) i].start && s < chops[(size_t) i].start + chops[(size_t) i].len) { edge = 3; return i; }
    return -1;
}

void ChopPanel::mouseMove (const MouseEvent& e)
{
    int edge = 0;
    const bool inWave = waveArea().expanded (0.0f, 6.0f).contains (e.position);
    const int c = inWave ? chopAt (e.position.x, edge) : -1;
    setMouseCursor (c >= 0 && (edge == 1 || edge == 2) ? MouseCursor::LeftRightResizeCursor
                    : c >= 0 ? MouseCursor::DraggingHandCursor
                    : closeX.contains (e.getPosition()) ? MouseCursor::PointingHandCursor : MouseCursor::NormalCursor);
}

void ChopPanel::mouseDown (const MouseEvent& e)
{
    grabKeyboardFocus();
    if (closeX.contains (e.getPosition())) { if (onClosed) onClosed(); return; }
    if (! card().contains (e.getPosition())) { if (onClosed) onClosed(); return; }
    for (int i = 0; i < maxPads; ++i)
        if (padRect (i).contains (e.position))
        {
            if (i < (int) chops.size())
            {
                // softer toward the bottom of the pad
                const auto pr = padRect (i);
                const float vel = jlimit (0.35f, 1.0f, 0.45f + 0.6f * (1.0f - (e.position.y - pr.getY()) / pr.getHeight()));
                proc.hitPad (i, vel);
            }
            return;
        }
    if (waveArea().expanded (0.0f, 6.0f).contains (e.position))
    {
        int edge = 0;
        dragChop = chopAt (e.position.x, edge);
        dragEdge = edge;
        if (dragChop >= 0)
        {
            selected = dragChop;
            dragStartSec = secOf (e.position.x);
            dragOrig = chops[(size_t) dragChop];
            if (edge == 3) proc.hitPad (dragChop, 0.85f);
        }
        repaint();
    }
}

void ChopPanel::mouseDrag (const MouseEvent& e)
{
    if (dragChop < 0 || dragChop >= (int) chops.size()) return;
    auto& c = chops[(size_t) dragChop];
    const double total = audio.getNumSamples() / rate, s = secOf (e.position.x), minLen = 0.04;
    if (dragEdge == 1) { const double end = dragOrig.start + dragOrig.len; c.start = jlimit (0.0, end - minLen, s); c.len = end - c.start; }
    else if (dragEdge == 2) c.len = jlimit (minLen, std::min (4.0, total - c.start), s - c.start);
    else if (dragEdge == 3) c.start = jlimit (0.0, std::max (0.0, total - c.len), dragOrig.start + (s - dragStartSec));
    chopsDirty = true;
    changedAt = Time::getMillisecondCounter();
    repaint();
}

void ChopPanel::mouseUp (const MouseEvent&)
{
    if (dragChop >= 0 && chopsDirty)
    {
        // a chop that moved is heard again from its new place (its note: listened to again later, so it keeps the one it had)
        cutPads();
        if (dragEdge != 0) proc.hitPad (dragChop, 0.85f);
    }
    dragChop = -1;
    changedAt = Time::getMillisecondCounter();
}

void ChopPanel::mouseDoubleClick (const MouseEvent& e)
{
    if (! waveArea().contains (e.position) || audio.getNumSamples() == 0) return;
    int edge = 0;
    if (chopAt (e.position.x, edge) >= 0 && edge != 3) return;
    if ((int) chops.size() >= maxPads) { message = "All 16 pads have a chop: remove one first (pick it, then Delete)"; repaint(); return; }
    const double s = secOf (e.position.x), total = audio.getNumSamples() / rate;
    double end = std::min (total, s + 0.35);
    for (auto& c : chops) if (c.start > s + 0.04) end = std::min (end, c.start);
    lp::song::ChopSlice c { s, std::max (0.05, end - s), 0.0 };
    // the note it's sung on
    {
        const int a0 = (int) (s * rate), look = std::min ((int) ((end - s) * rate), (int) (0.38 * rate));
        if (look > (int) (0.06 * rate))
        {
            AudioBuffer<float> seg (2, look);
            for (int ch = 0; ch < 2; ++ch) seg.copyFrom (ch, 0, audio, ch, a0, look);
            const auto r = lp::song::onenote::read (seg, rate);
            if (r.midi > 0.0 && r.voiced > 0.3f && r.steady > 0.6f) c.midi = r.midi;
        }
    }
    chops.push_back (c);
    origin.push_back (-1);
    selected = (int) chops.size() - 1;
    cutPads();
    proc.hitPad (selected, 0.85f);
    chopsDirty = true;
    changedAt = Time::getMillisecondCounter();
    message = "Added pad " + String (selected + 1);
    repaint();
}

bool ChopPanel::keyPressed (const KeyPress& k)
{
    if (k == KeyPress::escapeKey) { if (onClosed) onClosed(); return true; }
    if (k == KeyPress::spaceKey) { if (proc.studio.length() > 0) proc.previewPlaying = ! proc.previewPlaying.load(); return true; }
    if ((k == KeyPress::deleteKey || k == KeyPress::backspaceKey) && selected >= 0 && selected < (int) chops.size())
    {
        chops.erase (chops.begin() + selected);
        origin.erase (origin.begin() + selected);
        message = "Removed pad " + String (selected + 1) + ": the pads after it moved down one";
        selected = -1;
        cutPads();
        chopsDirty = true;
        changedAt = Time::getMillisecondCounter();
        repaint();
        return true;
    }
    const auto ch = (char) tolower ((int) k.getTextCharacter());
    if (ch != 0 && k.getModifiers().isCommandDown() == false)
        if (const char* at = std::strchr (padKeys, ch); at != nullptr)
        {
            const int pad = (int) (at - padKeys);
            const int code = k.getKeyCode();
            if (heldKeys.count (code)) return true; // held down: once, not over and over
            heldKeys.insert (code);
            if (pad < (int) chops.size()) proc.hitPad (pad, 0.85f);
            return true;
        }
    return false;
}

bool ChopPanel::keyStateChanged (bool)
{
    for (auto it = heldKeys.begin(); it != heldKeys.end();)
        it = KeyPress::isKeyCurrentlyDown (*it) ? std::next (it) : heldKeys.erase (it);
    return false;
}
