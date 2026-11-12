#include "PluginEditor.h"

using namespace juce;
using lp::ui::font;

// EDIT > Drums: the drum pattern on a timeline, like a DAW's clip editor.
//   - the overview strip (top) is the whole song: click or drag it to jump anywhere
//   - scroll to move along the song, Cmd/Ctrl + scroll (or pinch) to zoom, +/- and "whole song" too
//   - drag along the ruler to pick bars: they're what play loops, and what copy / clear work on
//   - click a step to add a hit, click a hit to erase it, drag to paint a row,
//     Option/Alt + scroll on a hit for how hard it plays
// Each change is re-recorded in the background, and heard live while the loop plays.

namespace
{
    struct RowInfo { int note; const char* name; };
    const RowInfo knownRows[] = {
        { 49, "CRASH" }, { 46, "OPEN HAT" }, { 42, "HAT" }, { 39, "CLAP" }, { 38, "SNARE" },
        { 45, "TOM" }, { 70, "PERC" }, { 37, "RIM" }, { 36, "KICK" },
    };
    juce::String rowName (int note)
    {
        for (auto& r : knownRows) if (r.note == note) return r.name;
        return "NOTE " + juce::String (note);
    }
    juce::String baseName (const std::string& s)
    {
        return juce::String (s).trimCharactersAtEnd ("0123456789 ");
    }
    constexpr float nameW = 132.0f, toolbarH = 40.0f, overviewH = 22.0f, lanesGap = 4.0f, sectionH = 18.0f, rulerH = 18.0f, hintH = 22.0f;
}

DrumGrid::DrumGrid (LivePlanetProcessor& p, const lp::ui::Theme*& theme) : proc (p), th (theme)
{
    auto setup = [this] (TextButton& b, const char* tip)
    {
        b.setWantsKeyboardFocus (false);
        b.setMouseClickGrabsKeyboardFocus (false);
        b.setTooltip (tip);
        addAndMakeVisible (b);
    };
    setup (playBtn, "loop the bars you picked on the ruler (or the ones you see) with the rest of the song");
    playBtn.getProperties().set ("primary", true);
    playBtn.onClick = [this]
    {
        if (proc.drumLoopPlaying()) { stopLoop(); return; }
        sendLoop();
        double a, b;
        loopRange (a, b);
        proc.startDrumLoop (track, a, b - a);
        playBtn.setButtonText ("stop");
    };
    setup (zoomOut, "zoom out (or Cmd / Ctrl + scroll, or pinch)");
    setup (zoomIn, "zoom in");
    setup (fitBtn, "see the whole song");
    setup (resBtn, "grid: 1/8, 1/16 or 1/32 notes (1/32 for hi-hat rolls)");
    setup (fillSection, "copy the bars you picked to the rest of their section");
    setup (fillAll, "copy the bars you picked to every section of the same kind (every chorus, every verse...)");
    setup (clearBtn, "erase the hits in the bars you picked (or the ones you see)");
    setup (undoBtn, "undo your last change");
    setup (bigBtn, "make the drum editor bigger");
    setup (feelBtn, "the drums' feel: swing and timing taken from your reference (or the style). Click: straight, half, as heard, more");
    feelBtn.onClick = [this]
    {
        const float steps[] = { 0.0f, 0.5f, 1.0f, 1.5f };
        int i = 0;
        while (i < 4 && std::abs (steps[i] - groove.amount) > 0.01f) ++i;
        groove.amount = steps[(i + 1) % 4];
        if (! groove.valid) groove.valid = true;
        proc.songJob.setFeel (track, groove.amount);
        updateFeelText();
        sendLoop();
    };
    zoomOut.onClick = [this] { zoomAround (1.6, gridArea().getCentreX()); };
    zoomIn.onClick = [this] { zoomAround (1.0 / 1.6, gridArea().getCentreX()); };
    fitBtn.onClick = [this] { setView (0.0, totalBeats()); };
    resBtn.onClick = [this]
    {
        stepsPerBeat = stepsPerBeat == 4 ? 8 : (stepsPerBeat == 8 ? 2 : 4);
        resBtn.setButtonText (stepsPerBeat == 2 ? "1/8" : stepsPerBeat == 4 ? "1/16" : "1/32");
        repaint();
    };
    fillSection.onClick = [this] { copyAcross (false); };
    fillAll.onClick = [this] { copyAcross (true); };
    clearBtn.onClick = [this]
    {
        pushUndo();
        double a, b;
        loopRange (a, b);
        notes.erase (std::remove_if (notes.begin(), notes.end(), [&] (const lp::song::Note& n) { return n.beat >= a - 1e-6 && n.beat < b - 1e-6; }), notes.end());
        changed();
    };
    undoBtn.onClick = [this]
    {
        if (undo.empty()) return;
        notes = undo.back();
        undo.pop_back();
        changed();
    };
    bigBtn.onClick = [this] { if (onToggleBig) onToggleBig(); };
}

void DrumGrid::load (int trackIndex)
{
    const auto pv = proc.songJob.partView (trackIndex);
    const bool sameTrack = trackIndex == track && pv.title == songTitle;
    track = trackIndex;
    songTitle = pv.title;
    totalBars = std::max (1, pv.totalBars);
    sections = pv.sections;
    if (! pv.valid) return;
    groove = proc.songJob.groove();
    updateFeelText();
    if (pv.kitKey + songTitle + String (track) != kitKey) { kitKey = pv.kitKey + songTitle + String (track); loadKit(); }
    // keep what you're drawing (and your place) while the track is being re-recorded
    if (! sameTrack || (! dirty && ! waitingForRender))
    {
        notes = pv.notes;
        if (! sameTrack)
        {
            undo.clear();
            selA = selB = -1.0;
            // start with 4 bars where the drums first play (past a quiet intro)
            const double first = notes.empty() ? 0.0 : std::floor (notes.front().beat / 4.0) * 4.0;
            setView (first, 16.0);
        }
    }
    waitingForRender = false;
    // rows: the standard kit plus anything else the pattern uses, kick at the bottom
    rows.clear();
    std::set<int> used;
    for (auto& n : notes) used.insert (n.pitch);
    for (auto& [note, path] : pv.drumSamples) used.insert (note);
    for (int must : { 36, 38, 39, 42, 46 }) used.insert (must);
    for (auto& r : knownRows) if (used.count (r.note)) rows.push_back (r.note);
    for (int n : used)
        if (std::find (rows.begin(), rows.end(), n) == rows.end()) rows.insert (rows.begin(), n);
    repaint();
}

// ---- the timeline

Rectangle<float> DrumGrid::overviewArea() const
{
    return { nameW, toolbarH, (float) getWidth() - nameW - 12.0f, overviewH };
}

// the song's sections, lined up with the grid: click one to pick all of its bars
Rectangle<float> DrumGrid::sectionLane() const
{
    return { nameW, toolbarH + overviewH + lanesGap, (float) getWidth() - nameW - 12.0f, sectionH };
}

Rectangle<float> DrumGrid::rulerArea() const
{
    return { nameW, toolbarH + overviewH + lanesGap + sectionH, (float) getWidth() - nameW - 12.0f, rulerH };
}

Rectangle<float> DrumGrid::gridArea() const
{
    const float top = toolbarH + overviewH + lanesGap + sectionH + rulerH;
    return { nameW, top, (float) getWidth() - nameW - 12.0f, (float) getHeight() - top - hintH };
}

float DrumGrid::xOf (double beat) const
{
    const auto ga = gridArea();
    return ga.getX() + (float) ((beat - viewStart) / viewLen) * ga.getWidth();
}

double DrumGrid::beatAt (float x) const
{
    const auto ga = gridArea();
    return viewStart + (double) (x - ga.getX()) / ga.getWidth() * viewLen;
}

void DrumGrid::setView (double start, double len)
{
    lastViewChange = Time::getMillisecondCounter();
    const double total = totalBeats();
    viewLen = jlimit (2.0, std::max (2.0, total), len);
    viewStart = jlimit (0.0, std::max (0.0, total - viewLen), start);
    if (selA < 0.0) sendLoop(); // with no bars picked, the loop follows what you see
    repaint();
}

void DrumGrid::zoomAround (double factor, float x)
{
    const double at = beatAt (x);
    const double len = jlimit (2.0, std::max (2.0, totalBeats()), viewLen * factor);
    setView (at - (at - viewStart) * len / viewLen, len);
}

void DrumGrid::loopRange (double& a, double& b) const
{
    if (selA >= 0.0 && selB > selA) { a = selA; b = selB; return; }
    // the whole bars you can see (at most 8)
    a = std::ceil (viewStart / 4.0 - 0.25) * 4.0;   // the first bar (mostly) on screen
    b = std::ceil ((viewStart + viewLen) / 4.0 - 0.25) * 4.0;
    b = std::min ({ b, a + 32.0, totalBeats() });
    if (b <= a) b = a + 4.0;
}

// ---- drawing hits

bool DrumGrid::cellAt (Point<float> p, int& row, long& step) const
{
    const auto g = gridArea();
    if (! g.contains (p) || rows.empty()) return false;
    row = jlimit (0, (int) rows.size() - 1, (int) ((p.y - g.getY()) / (g.getHeight() / (float) rows.size())));
    const double beat = beatAt (p.x);
    if (beat < 0.0 || beat >= totalBeats()) return false;
    step = (long) std::floor (beat * stepsPerBeat + 1e-9);
    return true;
}

// the hits inside one step of one row
std::vector<size_t> DrumGrid::hitsIn (int row, long step) const
{
    std::vector<size_t> out;
    const double len = 1.0 / stepsPerBeat, a = step * len;
    for (size_t i = 0; i < notes.size(); ++i)
        if (notes[i].pitch == rows[(size_t) row] && notes[i].beat >= a - 1e-6 && notes[i].beat < a + len - 1e-6)
            out.push_back (i);
    return out;
}

void DrumGrid::setCell (int row, long step, bool on)
{
    auto here = hitsIn (row, step);
    if (on && here.empty())
    {
        const double len = 1.0 / stepsPerBeat;
        notes.push_back ({ step * len, std::min (0.25, len), rows[(size_t) row], 0.8f });
        changed();
    }
    else if (! on && ! here.empty())
    {
        std::sort (here.rbegin(), here.rend());
        for (auto i : here) notes.erase (notes.begin() + (long) i);
        changed();
    }
}

Rectangle<float> DrumGrid::swapArrow (size_t row, int which) const
{
    const auto ga = gridArea();
    const float rowH = ga.getHeight() / (float) std::max<size_t> (1, rows.size());
    const float y = ga.getY() + (float) row * rowH + (rowH - 16.0f) * 0.5f;
    return { 84.0f + (float) which * 22.0f, y, 19.0f, 16.0f };
}

void DrumGrid::mouseMove (const MouseEvent&) { repaint(); } // hover highlights (swap arrows, sections)

void DrumGrid::refresh()
{
    canSwapRows = proc.songJob.kitIsSamples();
    // a drum sound was swapped: show its name, and play it once its new sound is loaded
    const auto [swapName, swapCount] = proc.songJob.lastDrumSwap();
    if (swapCount != seenSwaps)
    {
        seenSwaps = swapCount;
        swapMessage = swapName;
        swapMessageUntil = Time::getMillisecondCounter() + 5000;
    }
    if (auditionAfterSwap >= 0 && kitReloads != kitReloadsAtSwap && ! proc.drumLoopPlaying())
    {
        proc.liveDrums.audition (auditionAfterSwap, 0.9f);
        auditionAfterSwap = -1;
    }
    const bool playing = proc.drumLoopPlaying();
    if (playBtn.getButtonText() != (playing ? "stop" : "play")) playBtn.setButtonText (playing ? "stop" : "play");
    if (proc.songIsPlaying())
    {
        // follow the song like a DAW: when the playhead runs off the edge, turn the page
        // (not while you're drawing, and not for the drum loop, which stays where you put it)
        const double beat = proc.playheadPos.load() / StudioPlayer::fileRate * proc.songBpm.load() / 60.0;
        if (! playing && drag == Drag::None && (beat > viewStart + viewLen || beat < viewStart - 0.01) && Time::getMillisecondCounter() - lastViewChange > 1500)
            setView (beat - viewLen * 0.05, viewLen);
        repaint();
    }
    // send your drawing a moment after you stop, so a quick paint isn't recorded twenty times
    if (dirty && Time::getMillisecondCounter() - lastChange > 350)
    {
        dirty = false;
        waitingForRender = true;
        proc.songJob.setNotes (track, notes);
    }
}

void DrumGrid::pushUndo()
{
    undo.push_back (notes);
    if (undo.size() > 60) undo.erase (undo.begin());
}

void DrumGrid::changed()
{
    dirty = true;
    lastChange = Time::getMillisecondCounter();
    sendLoop();
    repaint();
}

// the kit's sounds, one hit each (made in the background: a second or so for sample kits)
void DrumGrid::loadKit()
{
    ++proc.printsRunning; // (the plugin waits for this before closing)
    Thread::launch ([&pr = proc, t = track, &reloads = kitReloads]
    {
        pr.liveDrums.setKit (pr.songJob.drumHits (t));
        ++reloads;
        --pr.printsRunning;
    });
}

void DrumGrid::updateFeelText()
{
    const String t = groove.amount <= 0.01f ? String ("feel: straight") : "feel " + String (roundToInt (groove.amount * 100.0f)) + "%";
    if (feelBtn.getButtonText() != t) feelBtn.setButtonText (t);
    String tip = "the drums' feel, from your reference (or the style): ";
    tip << "swing " << roundToInt (groove.swing16 * 100.0f) << "% on 16ths, " << roundToInt (groove.swing8 * 100.0f) << "% on 8ths";
    if (groove.snareLate > 0.0f) tip << ", the snare a little late";
    tip << ". click: straight, half, as heard, more";
    feelBtn.setTooltip (tip);
}

void DrumGrid::sendLoop()
{
    double a, b;
    loopRange (a, b);
    std::vector<LiveDrums::Hit> h;
    const double bpm = std::max (40.0, proc.songBpm.load());
    for (auto& n : notes)
        if (n.beat >= a - 1e-6 && n.beat < b - 1e-6)
        {
            // the same feel as the recorded drums
            double beat = n.beat;
            float vel = n.velocity;
            const double ms = lp::song::grooveJitterMs (groove, beat, n.pitch);
            lp::song::applyGroove (groove, beat, vel, n.pitch);
            beat = std::max (a, std::min (b - 1e-4, beat + ms / 1000.0 * bpm / 60.0));
            h.push_back ({ beat, n.pitch, vel });
        }
    proc.liveDrums.setPattern (std::move (h), a, b - a);
    proc.setDrumLoop (a, b - a);
}

void DrumGrid::stopLoop()
{
    proc.stopDrumLoop();
    playBtn.setButtonText ("play");
}

// ---- mouse

void DrumGrid::mouseDown (const MouseEvent& e)
{
    const auto ga = gridArea();
    drag = Drag::None;
    // the overview: jump there (and drag to slide along the song)
    if (overviewArea().contains (e.position))
    {
        const auto ov = overviewArea();
        const double at = (e.position.x - ov.getX()) / ov.getWidth() * totalBeats();
        const bool onWindow = at >= viewStart && at <= viewStart + viewLen;
        overviewGrab = onWindow ? at - viewStart : viewLen * 0.5;
        setView (at - overviewGrab, viewLen);
        drag = Drag::Overview;
        return;
    }
    // a section: pick all of its bars (click it again to let go)
    if (sectionLane().contains (e.position))
    {
        const double beat = beatAt (e.position.x);
        for (auto& s : sections)
            if (beat >= s.startBar * 4.0 && beat < (s.startBar + s.bars) * 4.0)
            {
                const bool same = selA == s.startBar * 4.0 && selB == (s.startBar + s.bars) * 4.0;
                selA = same ? -1.0 : s.startBar * 4.0;
                selB = same ? -1.0 : (s.startBar + s.bars) * 4.0;
                sendLoop();
                repaint();
                return;
            }
        return;
    }
    // the ruler: drag to pick bars (a click picks one bar; click your pick again to let go of it)
    if (rulerArea().contains (e.position))
    {
        const double bar = std::floor (jlimit (0.0, totalBeats() - 0.001, beatAt (e.position.x)) / 4.0) * 4.0;
        if (selA >= 0.0 && bar >= selA && bar < selB && e.getNumberOfClicks() > 1) { selA = selB = -1.0; sendLoop(); repaint(); return; }
        rulerFrom = bar;
        selA = bar; selB = bar + 4.0;
        drag = Drag::Ruler;
        sendLoop();
        repaint();
        return;
    }
    // a row's < >: swap just that drum's sound (then hear it)
    if (canSwapRows)
        for (size_t i = 0; i < rows.size(); ++i)
            for (int k = 0; k < 2; ++k)
                if (swapArrow (i, k).contains (e.position))
                {
                    proc.songJob.swapDrum (track, rows[i], k == 0 ? -1 : 1);
                    auditionAfterSwap = rows[i];
                    kitReloadsAtSwap = kitReloads.load();
                    return;
                }
    // a row's name: hear that drum
    if (e.position.x < ga.getX() && e.position.y >= ga.getY() && e.position.y < ga.getBottom() && ! rows.empty())
    {
        const int row = jlimit (0, (int) rows.size() - 1, (int) ((e.position.y - ga.getY()) / (ga.getHeight() / (float) rows.size())));
        proc.liveDrums.audition (rows[(size_t) row], 0.9f);
        return;
    }
    int row;
    long step;
    if (! cellAt (e.position, row, step)) return;
    pushUndo();
    paintRow = row;
    drag = Drag::Paint;
    painting = hitsIn (row, step).empty(); // started on an empty step: paint hits; on a hit: erase
    setCell (row, step, painting);
    if (painting && ! proc.drumLoopPlaying()) proc.liveDrums.audition (rows[(size_t) row], 0.8f); // hear what you added
}

void DrumGrid::mouseDrag (const MouseEvent& e)
{
    if (drag == Drag::Overview)
    {
        const auto ov = overviewArea();
        const double at = (e.position.x - ov.getX()) / ov.getWidth() * totalBeats();
        setView (at - overviewGrab, viewLen);
        return;
    }
    if (drag == Drag::Ruler)
    {
        const double bar = std::floor (jlimit (0.0, totalBeats() - 0.001, beatAt (e.position.x)) / 4.0) * 4.0;
        selA = std::min (rulerFrom, bar);
        selB = std::max (rulerFrom, bar) + 4.0;
        sendLoop();
        repaint();
        return;
    }
    if (drag != Drag::Paint || paintRow < 0) return;
    const auto g = gridArea();
    // stays on the row you started on, like drawing in a drum clip
    const auto p = Point<float> (jlimit (g.getX(), g.getRight() - 1.0f, e.position.x),
                                 g.getY() + (paintRow + 0.5f) * g.getHeight() / (float) rows.size());
    int row;
    long step;
    if (cellAt (p, row, step))
    {
        const bool was = ! hitsIn (row, step).empty();
        setCell (row, step, painting);
        if (painting && ! was && ! proc.drumLoopPlaying()) proc.liveDrums.audition (rows[(size_t) row], 0.8f);
    }
}

void DrumGrid::mouseUp (const MouseEvent&) { paintRow = -1; drag = Drag::None; }

void DrumGrid::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w)
{
    // Option / Alt + scroll on a hit: how hard it plays
    if (e.mods.isAltDown())
    {
        int row;
        long step;
        if (! cellAt (e.position, row, step)) return;
        auto here = hitsIn (row, step);
        if (here.empty()) return;
        if (undo.empty() || Time::getMillisecondCounter() - lastChange > 1500) pushUndo();
        const float d = w.deltaY != 0.0f ? w.deltaY : w.deltaX;
        for (auto i : here) notes[i].velocity = jlimit (0.1f, 1.0f, notes[i].velocity + (d > 0 ? 0.05f : -0.05f));
        changed();
        return;
    }
    // Cmd / Ctrl + scroll: zoom where the mouse is
    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        zoomAround (std::exp (-(double) w.deltaY * 1.5), jlimit (gridArea().getX(), gridArea().getRight(), e.position.x));
        return;
    }
    // scroll (either way, or sideways on a trackpad): move along the song
    const double d = std::abs (w.deltaX) > std::abs (w.deltaY) ? w.deltaX : w.deltaY;
    setView (viewStart - d * viewLen * 0.5, viewLen);
}

void DrumGrid::mouseMagnify (const MouseEvent& e, float scale)
{
    if (scale > 0.0f) zoomAround (1.0 / scale, jlimit (gridArea().getX(), gridArea().getRight(), e.position.x));
}

// copy the bars you picked to the rest of their section (or of every section of that kind)
void DrumGrid::copyAcross (bool everySectionOfKind)
{
    double a, b;
    loopRange (a, b);
    const int fromBar = (int) (a / 4.0), bars = std::max (1, (int) std::lround ((b - a) / 4.0));
    const lp::song::Section* sec = nullptr;
    for (auto& s : sections) if (fromBar >= s.startBar && fromBar < s.startBar + s.bars) sec = &s;
    if (sec == nullptr) return;
    pushUndo();
    const double len = bars * 4.0;
    std::vector<lp::song::Note> pattern;
    for (auto& n : notes) if (n.beat >= a - 1e-6 && n.beat < a + len - 1e-6) pattern.push_back (n);
    const auto kind = baseName (sec->name);
    for (auto& s : sections)
    {
        if (! (&s == sec || (everySectionOfKind && baseName (s.name) == kind))) continue;
        // line up with where the picked bars sit in their section
        // (a shorter section gets as much of the pattern as fits; it never spills into the next section)
        const int offset = (fromBar - sec->startBar) % bars;
        const double secEnd = (s.startBar + s.bars) * 4.0;
        for (int bar = s.startBar + offset; bar < s.startBar + s.bars; bar += bars)
        {
            if (bar == fromBar) continue;
            const double t0 = bar * 4.0, t1 = std::min (t0 + len, secEnd);
            notes.erase (std::remove_if (notes.begin(), notes.end(), [&] (const lp::song::Note& n) { return n.beat >= t0 - 1e-6 && n.beat < t1 - 1e-6; }), notes.end());
            for (auto n : pattern) { n.beat = n.beat - a + t0; if (n.beat < t1 - 1e-6) notes.push_back (n); }
        }
    }
    changed();
}

void DrumGrid::resized()
{
    auto top = getLocalBounds().removeFromTop ((int) toolbarH - 6).reduced (10, 4);
    playBtn.setBounds (top.removeFromLeft (64)); top.removeFromLeft (10);
    zoomOut.setBounds (top.removeFromLeft (30)); top.removeFromLeft (4);
    zoomIn.setBounds (top.removeFromLeft (30)); top.removeFromLeft (4);
    fitBtn.setBounds (top.removeFromLeft (96)); top.removeFromLeft (4);
    resBtn.setBounds (top.removeFromLeft (52)); top.removeFromLeft (4);
    feelBtn.setBounds (top.removeFromLeft (104));
    bigBtn.setBounds (top.removeFromRight (70)); top.removeFromRight (6);
    undoBtn.setBounds (top.removeFromRight (56)); top.removeFromRight (6);
    clearBtn.setBounds (top.removeFromRight (56)); top.removeFromRight (6);
    fillAll.setBounds (top.removeFromRight (130)); top.removeFromRight (6);
    fillSection.setBounds (top.removeFromRight (124));
}

void DrumGrid::paint (Graphics& g)
{
    const auto& t = *th;
    auto r = getLocalBounds().toFloat();
    g.setColour (Colour (0xff0d0d0f));
    g.fillRoundedRectangle (r, 14.0f);
    g.setColour (t.line);
    g.drawRoundedRectangle (r.reduced (0.5f), 14.0f, 1.0f);

    double la, lb;
    loopRange (la, lb);
    {
        // "every chorus" follows the bars you picked
        const int bar = (int) (la / 4.0);
        String kind = "section";
        for (auto& s : sections) if (bar >= s.startBar && bar < s.startBar + s.bars) kind = baseName (s.name);
        if (fillAll.getButtonText() != "every " + kind) fillAll.setButtonText ("every " + kind);
    }

    // ---- the overview: the whole song, its sections, where the hits are, and what you see
    const auto ov = overviewArea();
    const double total = totalBeats();
    auto ovX = [&] (double beat) { return ov.getX() + (float) (beat / total) * ov.getWidth(); };
    g.setFont (lp::ui::mono (8.5f, false, 0.1f));
    for (auto& s : sections)
    {
        auto box = Rectangle<float> (ovX (s.startBar * 4.0), ov.getY(), ovX ((s.startBar + s.bars) * 4.0) - ovX (s.startBar * 4.0), ov.getHeight()).reduced (0.5f, 0.0f);
        g.setColour (t.grass.withAlpha (0.08f + 0.22f * s.energy));
        g.fillRoundedRectangle (box, 3.0f);
        g.setColour (t.inkDim);
        if (box.getWidth() > 34.0f) g.drawText (String (s.name).toUpperCase(), box.reduced (4.0f, 0.0f), Justification::centredLeft, true);
    }
    {
        // a little tick for every bar with hits in it
        std::vector<int> busy ((size_t) totalBars, 0);
        for (auto& n : notes) if (n.beat >= 0 && n.beat < total) ++busy[(size_t) (n.beat / 4.0)];
        g.setColour (t.grassLight.withAlpha (0.55f));
        for (int b = 0; b < totalBars; ++b)
            if (busy[(size_t) b] > 0) g.fillRect (ovX (b * 4.0) + 0.5f, ov.getBottom() - 3.0f, std::max (1.0f, ovX (4.0) - ovX (0.0) - 1.0f), 2.0f);
    }
    if (proc.songIsPlaying())
    {
        const double beat = proc.playheadPos.load() / StudioPlayer::fileRate * proc.songBpm.load() / 60.0;
        g.setColour (Colours::white.withAlpha (0.9f));
        g.fillRect (ovX (beat) - 0.75f, ov.getY() - 1.0f, 1.5f, ov.getHeight() + 2.0f);
    }
    {
        auto win = Rectangle<float> (ovX (viewStart), ov.getY() - 1.0f, ovX (viewStart + viewLen) - ovX (viewStart), ov.getHeight() + 2.0f);
        g.setColour (Colours::white.withAlpha (0.10f));
        g.fillRoundedRectangle (win, 3.0f);
        g.setColour (Colours::white.withAlpha (0.7f));
        g.drawRoundedRectangle (win, 3.0f, 1.2f);
    }

    // ---- the section lane: the song's parts where they are on the grid (click one to pick it)
    const auto sl = sectionLane();
    g.saveState();
    g.reduceClipRegion (sl.toNearestInt());
    g.setFont (lp::ui::mono (8.5f, true, 0.1f));
    for (auto& s : sections)
    {
        const float x0 = xOf (s.startBar * 4.0), x1 = xOf ((s.startBar + s.bars) * 4.0);
        if (x1 < sl.getX() || x0 > sl.getRight()) continue;
        auto box = Rectangle<float> (x0, sl.getY() + 1.0f, x1 - x0, sl.getHeight() - 2.0f).reduced (1.0f, 0.0f);
        const bool picked = selA == s.startBar * 4.0 && selB == (s.startBar + s.bars) * 4.0;
        const bool over = isMouseOver() && box.contains (getMouseXYRelative().toFloat());
        g.setColour (picked ? t.grass.withAlpha (0.75f) : t.grass.withAlpha (over ? 0.32f : 0.16f));
        g.fillRoundedRectangle (box, 3.0f);
        g.setColour (picked ? Colours::white : t.ink.withAlpha (0.8f));
        // keep the name in view while the section scrolls past
        auto label = box.withLeft (std::max (box.getX(), sl.getX())).reduced (5.0f, 0.0f);
        g.drawText (String (s.name).toUpperCase(), label, Justification::centredLeft, true);
    }
    g.restoreState();

    // ---- the ruler: bar numbers, and the bars you picked
    const auto ru = rulerArea();
    const auto ga = gridArea();
    g.saveState();
    g.reduceClipRegion (ru.getUnion (ga).toNearestInt());
    if (selA >= 0.0)
    {
        g.setColour (t.grass.withAlpha (0.55f));
        g.fillRoundedRectangle (Rectangle<float> (xOf (selA), ru.getY() + 2.0f, xOf (selB) - xOf (selA), ru.getHeight() - 4.0f), 3.0f);
        g.setColour (t.grass.withAlpha (0.07f));
        g.fillRect (Rectangle<float> (xOf (selA), ga.getY(), xOf (selB) - xOf (selA), ga.getHeight()));
    }
    const float barW = xOf (4.0) - xOf (0.0);
    const int labelEvery = barW >= 28.0f ? 1 : barW >= 14.0f ? 2 : barW >= 7.0f ? 4 : 8;
    g.setFont (lp::ui::mono (9.0f, false, 0.08f));
    const int b0 = std::max (0, (int) (viewStart / 4.0)), b1 = std::min (totalBars, (int) std::ceil ((viewStart + viewLen) / 4.0));
    for (int b = b0; b <= b1; ++b)
    {
        if (b % labelEvery != 0) continue;
        const float x = xOf (b * 4.0);
        g.setColour (Colours::white.withAlpha (0.35f));
        g.drawVerticalLine ((int) x, ru.getY() + 3.0f, ru.getBottom());
        if (b < totalBars)
        {
            g.setColour (t.inkDim);
            g.drawText (String (b + 1), Rectangle<float> (x + 3.0f, ru.getY(), 40.0f, ru.getHeight()), Justification::centredLeft, false);
        }
    }

    // ---- the grid
    if (! rows.empty())
    {
        const float rowH = ga.getHeight() / (float) rows.size();
        for (size_t i = 0; i < rows.size(); ++i)
            if (i % 2 == 0) { g.setColour (Colours::white.withAlpha (0.025f)); g.fillRect (ga.getX(), ga.getY() + (float) i * rowH, ga.getWidth(), rowH); }
        // section edges
        for (auto& s : sections)
        {
            g.setColour (t.grassLight.withAlpha (0.35f));
            g.fillRect (xOf (s.startBar * 4.0) - 1.0f, ga.getY(), 2.0f, ga.getHeight());
        }
        // grid lines: bars brightest, then beats, then steps (the finer ones only when there's room)
        const double stepLen = 1.0 / stepsPerBeat;
        const float stepW = xOf (stepLen) - xOf (0.0);
        const double lineLen = stepW >= 5.0f ? stepLen : (xOf (1.0) - xOf (0.0) >= 5.0f ? 1.0 : 4.0);
        for (long k = (long) std::floor (viewStart / lineLen); k * lineLen <= viewStart + viewLen; ++k)
        {
            const double beat = k * lineLen;
            if (beat < 0 || beat > total) continue;
            const bool bar = std::fmod (beat, 4.0) < 1e-6, onBeat = std::fmod (beat, 1.0) < 1e-6;
            g.setColour (Colours::white.withAlpha (bar ? 0.26f : (onBeat ? 0.11f : 0.045f)));
            g.drawVerticalLine ((int) xOf (beat), ga.getY(), ga.getBottom());
        }
        // the hits (brighter = harder)
        const float w = std::max (3.0f, std::min (stepW, xOf (0.25) - xOf (0.0)) - 2.0f);
        for (auto& n : notes)
        {
            if (n.beat < viewStart - 1.0 || n.beat > viewStart + viewLen) continue;
            const auto it = std::find (rows.begin(), rows.end(), n.pitch);
            if (it == rows.end()) continue;
            const float y = ga.getY() + (float) (it - rows.begin()) * rowH;
            auto cell = Rectangle<float> (xOf (n.beat) + 1.0f, y + 3.0f, w, rowH - 6.0f);
            g.setColour (t.grass.withAlpha (0.35f + 0.65f * n.velocity));
            g.fillRoundedRectangle (cell, std::min (3.0f, w * 0.4f));
        }
        // where the song is playing (your DAW, the play button, or the drum loop)
        if (proc.songIsPlaying())
        {
            const double beat = proc.playheadPos.load() / StudioPlayer::fileRate * proc.songBpm.load() / 60.0;
            const float x = xOf (beat);
            g.setColour (Colours::white.withAlpha (0.9f));
            g.fillRect (x - 1.0f, ru.getY(), 2.0f, ga.getBottom() - ru.getY());
            Path head;
            head.addTriangle (x - 5.0f, ru.getY(), x + 5.0f, ru.getY(), x, ru.getY() + 7.0f);
            g.fillPath (head);
        }
    }
    g.restoreState();

    // row names and their swap arrows
    if (! rows.empty())
    {
        const float rowH = ga.getHeight() / (float) rows.size();
        g.setFont (lp::ui::mono (9.5f, false, 0.12f));
        for (size_t i = 0; i < rows.size(); ++i)
        {
            const float y = ga.getY() + (float) i * rowH;
            g.setColour (t.inkDim);
            g.drawText (rowName (rows[i]), Rectangle<float> (12.0f, y, 70.0f, rowH), Justification::centredLeft, false);
            if (! canSwapRows) continue;
            for (int k = 0; k < 2; ++k)
            {
                auto btn = swapArrow (i, k);
                const bool over = btn.contains (getMouseXYRelative().toFloat()) && isMouseOver();
                g.setColour (Colours::white.withAlpha (over ? 0.16f : 0.06f));
                g.fillRoundedRectangle (btn, 4.0f);
                g.setColour (over ? t.ink : t.inkDim);
                g.drawText (k == 0 ? "<" : ">", btn, Justification::centred, false);
            }
        }
    }
    // what's picked, to the left of the ruler
    g.setFont (lp::ui::mono (9.0f, true, 0.1f));
    g.setColour (selA >= 0.0 ? t.grassLight : t.inkDim);
    const int pa = (int) (la / 4.0) + 1, pb = (int) (lb / 4.0);
    g.drawText (selA >= 0.0 ? "BARS " + String (pa) + (pb > pa ? "-" + String (pb) : String()) : String ("SONG"),
                Rectangle<float> (12.0f, ru.getY(), nameW - 16.0f, ru.getHeight()), Justification::centredLeft, false);
    g.setColour (t.inkDim);
    g.drawText ("OVERVIEW", Rectangle<float> (12.0f, ov.getY(), nameW - 16.0f, ov.getHeight()), Justification::centredLeft, false);

    g.setFont (lp::ui::mono (9.0f, false, 0.1f));
    g.setColour (t.inkDim);
    const bool showSwap = swapMessage.isNotEmpty() && Time::getMillisecondCounter() < swapMessageUntil;
    g.drawText (showSwap ? "NOW: " + swapMessage.toUpperCase()
                : waitingForRender || dirty ? String ("RECORDING YOUR CHANGES...")
                : String ("SCROLL: MOVE   |   CMD + SCROLL OR PINCH: ZOOM   |   CLICK A SECTION OR DRAG THE RULER: PICK BARS   |   CLICK: ADD OR ERASE   |   DRAG: PAINT   |   OPTION + SCROLL ON A HIT: HOW HARD"),
                Rectangle<float> (12.0f, ga.getBottom() + 4.0f, ga.getRight() - 12.0f, 16.0f), Justification::centredLeft, true);
}
