#include "PluginEditor.h"

using namespace juce;
using lp::ui::font;

// EDIT > a played part (an 808, a hook, a bass line): its notes on a piano roll, like a DAW's MIDI editor.
//   - click an empty spot to add a note (drag right to make it longer), drag a note to move it,
//     drag its right end to make it longer or shorter, double-click a note (or Delete) to erase it
//   - "in key" (on): notes you move up or down stay on the song's key
//   - Option / Alt + click a note: it slides into its pitch from the note before (808 glide)
//   - Shift + drag on an empty spot: pick several notes, then move or erase them together
//   - scroll: up and down the notes, Shift + scroll (or sideways): along the song, Cmd + scroll or pinch: zoom
// Each change is re-recorded in the background, and heard live while the loop plays.

namespace
{
    constexpr float keysW = 64.0f, toolbarH = 40.0f, overviewH = 22.0f, lanesGap = 4.0f, sectionH = 18.0f, rulerH = 18.0f, hintH = 22.0f;
    juce::String baseName (const std::string& s) { return juce::String (s).trimCharactersAtEnd ("0123456789 "); }
    juce::String noteText (int midi) { return juce::String (lp::noteName (((midi % 12) + 12) % 12)) + juce::String (midi / 12 - 2); } // Ableton's octaves: C3 = 60
    bool blackKey (int midi) { const int pc = ((midi % 12) + 12) % 12; return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10; }
}

NoteGrid::NoteGrid (LivePlanetProcessor& p, const lp::ui::Theme*& theme) : proc (p), th (theme)
{
    setWantsKeyboardFocus (true);
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
    setup (resBtn, "grid: notes snap to 1/4, 1/8, 1/16 or 1/32");
    setup (keyBtn, "on: notes you move up or down stay in the song's key. off: any note");
    setup (fillSection, "copy the bars you picked to the rest of their section");
    setup (fillAll, "copy the bars you picked to every section of the same kind (every chorus, every verse...)");
    setup (clearBtn, "erase the notes in the bars you picked (or the ones you see)");
    setup (undoBtn, "undo your last change (Cmd + Z)");
    setup (bigBtn, "make the note editor bigger");
    zoomOut.onClick = [this] { zoomAround (1.6, gridArea().getCentreX()); };
    zoomIn.onClick = [this] { zoomAround (1.0 / 1.6, gridArea().getCentreX()); };
    fitBtn.onClick = [this] { setView (0.0, totalBeats()); };
    resBtn.onClick = [this]
    {
        stepsPerBeat = stepsPerBeat == 4 ? 8 : stepsPerBeat == 8 ? 1 : stepsPerBeat == 1 ? 2 : 4;
        resBtn.setButtonText (stepsPerBeat == 1 ? "1/4" : stepsPerBeat == 2 ? "1/8" : stepsPerBeat == 4 ? "1/16" : "1/32");
        repaint();
    };
    keyBtn.setClickingTogglesState (true);
    keyBtn.setToggleState (true, dontSendNotification);
    keyBtn.onClick = [this] { keySnap = keyBtn.getToggleState(); keyBtn.setButtonText (keySnap ? "in key" : "any note"); repaint(); };
    fillSection.onClick = [this] { copyAcross (false); };
    fillAll.onClick = [this] { copyAcross (true); };
    clearBtn.onClick = [this]
    {
        pushUndo();
        double a, b;
        loopRange (a, b);
        notes.erase (std::remove_if (notes.begin(), notes.end(), [&] (const lp::song::Note& n) { return n.beat >= a - 1e-6 && n.beat < b - 1e-6; }), notes.end());
        picked.clear();
        changed();
    };
    undoBtn.onClick = [this]
    {
        if (undo.empty()) return;
        notes = undo.back();
        undo.pop_back();
        picked.clear();
        changed();
    };
    bigBtn.onClick = [this] { if (onToggleBig) onToggleBig(); };
}

void NoteGrid::load (int trackIndex)
{
    const auto pv = proc.songJob.partView (trackIndex);
    const bool sameTrack = trackIndex == track && pv.title == songTitle;
    track = trackIndex;
    songTitle = pv.title;
    partName = pv.name;
    totalBars = std::max (1, pv.totalBars);
    sections = pv.sections;
    key = pv.key; minor = pv.minor;
    bass = pv.kind == lp::song::PartKind::Bass;
    if (! pv.valid) return;
    if (pv.kitKey + pv.samplePath + songTitle + String (track) != soundKey) { soundKey = pv.kitKey + pv.samplePath + songTitle + String (track); loadSound(); }
    if (! sameTrack || (! dirty && ! waitingForRender))
    {
        notes = pv.notes;
        if (! sameTrack)
        {
            undo.clear();
            picked.clear();
            selA = selB = -1.0;
            // start where the part first plays, with its notes in view
            const double first = notes.empty() ? 0.0 : std::floor (notes.front().beat / 4.0) * 4.0;
            setView (first, 16.0);
            int lo = 127, hi = 0;
            for (auto& n : notes) { lo = std::min (lo, n.pitch); hi = std::max (hi, n.pitch); }
            if (notes.empty()) { lo = hi = bass ? 36 : 60; }
            setPitchView ((lo + hi) / 2 - rowsShown() / 2);
        }
    }
    waitingForRender = false;
    repaint();
}

// its sound on every note (made in the background)
void NoteGrid::loadSound()
{
    ++proc.printsRunning;
    Thread::launch ([&pr = proc, t = track]
    {
        pr.liveDrums.setKit (pr.songJob.noteSounds (t, 12, 96));
        --pr.printsRunning;
    });
}

// ---- the timeline and the notes' rows

Rectangle<float> NoteGrid::overviewArea() const { return { keysW, toolbarH, (float) getWidth() - keysW - 12.0f, overviewH }; }
Rectangle<float> NoteGrid::sectionLane() const { return { keysW, toolbarH + overviewH + lanesGap, (float) getWidth() - keysW - 12.0f, sectionH }; }
Rectangle<float> NoteGrid::rulerArea() const { return { keysW, toolbarH + overviewH + lanesGap + sectionH, (float) getWidth() - keysW - 12.0f, rulerH }; }
Rectangle<float> NoteGrid::gridArea() const
{
    const float top = toolbarH + overviewH + lanesGap + sectionH + rulerH;
    return { keysW, top, (float) getWidth() - keysW - 12.0f, (float) getHeight() - top - hintH };
}
float NoteGrid::xOf (double beat) const { const auto ga = gridArea(); return ga.getX() + (float) ((beat - viewStart) / viewLen) * ga.getWidth(); }
double NoteGrid::beatAt (float x) const { const auto ga = gridArea(); return viewStart + (double) (x - ga.getX()) / ga.getWidth() * viewLen; }
int NoteGrid::rowsShown() const { return std::max (4, (int) (gridArea().getHeight() / rowH())); }
float NoteGrid::yOf (int pitch) const { const auto ga = gridArea(); return ga.getBottom() - (float) (pitch - lowPitch + 1) * rowH(); }
int NoteGrid::pitchAt (float y) const { const auto ga = gridArea(); return lowPitch + (int) std::floor ((ga.getBottom() - y) / rowH()); }
void NoteGrid::setPitchView (int low) { lowPitch = jlimit (0, 127 - rowsShown(), low); repaint(); }

void NoteGrid::setView (double start, double len)
{
    lastViewChange = Time::getMillisecondCounter();
    const double total = totalBeats();
    viewLen = jlimit (2.0, std::max (2.0, total), len);
    viewStart = jlimit (0.0, std::max (0.0, total - viewLen), start);
    if (selA < 0.0) sendLoop();
    repaint();
}

void NoteGrid::zoomAround (double factor, float x)
{
    const double at = beatAt (x);
    const double len = jlimit (2.0, std::max (2.0, totalBeats()), viewLen * factor);
    setView (at - (at - viewStart) * len / viewLen, len);
}

void NoteGrid::loopRange (double& a, double& b) const
{
    if (selA >= 0.0 && selB > selA) { a = selA; b = selB; return; }
    a = std::ceil (viewStart / 4.0 - 0.25) * 4.0;
    b = std::ceil ((viewStart + viewLen) / 4.0 - 0.25) * 4.0;
    b = std::min ({ b, a + 32.0, totalBeats() });
    if (b <= a) b = a + 4.0;
}

double NoteGrid::snap (double beat) const { return std::round (beat / gridLen()) * gridLen(); }

bool NoteGrid::inKey (int pitch) const
{
    static const int maj[7] = { 0, 2, 4, 5, 7, 9, 11 }, mn[7] = { 0, 2, 3, 5, 7, 8, 10 };
    const int d = ((pitch - key) % 12 + 12) % 12;
    for (int k = 0; k < 7; ++k) if ((minor ? mn : maj)[k] == d) return true;
    return false;
}

int NoteGrid::toKey (int pitch, int dir) const
{
    if (inKey (pitch)) return pitch;
    return inKey (pitch + (dir >= 0 ? 1 : -1)) ? pitch + (dir >= 0 ? 1 : -1) : pitch - (dir >= 0 ? 1 : -1);
}

// the note under the mouse (the last drawn wins), and whether it's on the note's right end (to stretch it)
int NoteGrid::noteAt (Point<float> p, bool& onEdge) const
{
    onEdge = false;
    for (int i = (int) notes.size() - 1; i >= 0; --i)
    {
        const auto& n = notes[(size_t) i];
        const float y = yOf (n.pitch), x0 = xOf (n.beat), x1 = std::max (x0 + 4.0f, xOf (n.beat + n.length));
        if (p.y >= y && p.y < y + rowH() && p.x >= x0 && p.x <= x1 + 2.0f)
        {
            onEdge = p.x >= x1 - std::min (8.0f, (x1 - x0) * 0.35f);
            return i;
        }
    }
    return -1;
}

void NoteGrid::hear (int pitch, double beats)
{
    if (proc.drumLoopPlaying() || pitch == lastHeard) return;
    lastHeard = pitch;
    const double bpm = std::max (40.0, proc.songBpm.load());
    proc.liveDrums.audition (pitch, 0.85f, jlimit (0.12, 1.2, beats * 60.0 / bpm));
}

void NoteGrid::sendLoop()
{
    double a, b;
    loopRange (a, b);
    std::vector<LiveDrums::Hit> h;
    for (auto& n : notes)
        if (n.beat >= a - 1e-6 && n.beat < b - 1e-6) h.push_back ({ n.beat, n.pitch, n.velocity, std::min (n.length, b - n.beat) });
    proc.liveDrums.setPattern (std::move (h), a, b - a);
    proc.setDrumLoop (a, b - a);
}

void NoteGrid::stopLoop()
{
    proc.stopDrumLoop();
    playBtn.setButtonText ("play");
}

void NoteGrid::pushUndo()
{
    undo.push_back (notes);
    if (undo.size() > 60) undo.erase (undo.begin());
}

void NoteGrid::changed()
{
    dirty = true;
    lastChange = Time::getMillisecondCounter();
    sendLoop();
    repaint();
}

void NoteGrid::refresh()
{
    const bool playing = proc.drumLoopPlaying();
    if (playBtn.getButtonText() != (playing ? "stop" : "play")) playBtn.setButtonText (playing ? "stop" : "play");
    if (proc.songIsPlaying())
    {
        const double beat = proc.playheadPos.load() / StudioPlayer::fileRate * proc.songBpm.load() / 60.0;
        if (! playing && drag == Drag::None && (beat > viewStart + viewLen || beat < viewStart - 0.01) && Time::getMillisecondCounter() - lastViewChange > 1500)
            setView (beat - viewLen * 0.05, viewLen);
        repaint();
    }
    // send your changes a moment after you stop
    if (dirty && drag == Drag::None && Time::getMillisecondCounter() - lastChange > 350)
    {
        dirty = false;
        waitingForRender = true;
        auto out = notes;
        for (auto& n : out) { n.beat = std::max (0.0, n.beat); n.length = std::max (0.05, n.length); }
        proc.songJob.setNotes (track, out);
    }
}

// ---- mouse

void NoteGrid::mouseDown (const MouseEvent& e)
{
    grabKeyboardFocus();
    const auto ga = gridArea();
    drag = Drag::None;
    moved = false;
    lastHeard = -1;
    downAt = e.position;
    if (overviewArea().contains (e.position))
    {
        const auto ov = overviewArea();
        const double at = (e.position.x - ov.getX()) / ov.getWidth() * totalBeats();
        overviewGrab = at >= viewStart && at <= viewStart + viewLen ? at - viewStart : viewLen * 0.5;
        setView (at - overviewGrab, viewLen);
        drag = Drag::Overview;
        return;
    }
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
    // the keyboard: hear that note
    if (e.position.x < ga.getX() && e.position.y >= ga.getY() && e.position.y < ga.getBottom())
    {
        proc.liveDrums.audition (pitchAt (e.position.y), 0.85f, 0.6);
        return;
    }
    if (! ga.contains (e.position)) return;
    bool edge = false;
    const int hit = noteAt (e.position, edge);
    if (hit >= 0 && e.mods.isPopupMenu())
    {
        PopupMenu m;
        const bool glide = notes[(size_t) hit].glide;
        m.addItem (1, "Erase this note");
        m.addItem (2, glide ? "No slide into this note" : "Slide into this note (808 glide)");
        m.showMenuAsync (PopupMenu::Options(), [this, hit] (int r)
        {
            if (hit >= (int) notes.size() || r == 0) return;
            pushUndo();
            if (r == 1) notes.erase (notes.begin() + hit);
            if (r == 2) notes[(size_t) hit].glide = ! notes[(size_t) hit].glide;
            picked.clear();
            changed();
        });
        return;
    }
    if (hit >= 0 && e.mods.isAltDown())
    {
        // a slide into this note
        pushUndo();
        notes[(size_t) hit].glide = ! notes[(size_t) hit].glide;
        changed();
        return;
    }
    if (hit >= 0)
    {
        // pick it (Shift: add to what's picked), then move or stretch everything picked
        if (e.mods.isShiftDown()) { if (picked.count ((size_t) hit)) picked.erase ((size_t) hit); else picked.insert ((size_t) hit); }
        else if (! picked.count ((size_t) hit)) { picked.clear(); picked.insert ((size_t) hit); }
        pushUndo();
        before = notes;
        grabBeat = beatAt (e.position.x);
        grabPitch = pitchAt (e.position.y);
        drag = edge ? Drag::Resize : Drag::Move;
        drawn = hit;
        hear (notes[(size_t) hit].pitch, notes[(size_t) hit].length);
        repaint();
        return;
    }
    if (e.mods.isShiftDown())
    {
        drag = Drag::Box;
        boxTo = e.position;
        return;
    }
    // an empty spot: a new note (drag right to make it longer)
    const double beat = std::floor (beatAt (e.position.x) / gridLen() + 1e-9) * gridLen();
    if (beat < 0.0 || beat >= totalBeats()) return;
    int pitch = pitchAt (e.position.y);
    if (keySnap) pitch = toKey (pitch, 1);
    pushUndo();
    notes.push_back ({ beat, std::max (gridLen(), std::min (lastLen, totalBeats() - beat)), pitch, 0.85f });
    drawn = (int) notes.size() - 1;
    picked.clear();
    picked.insert ((size_t) drawn);
    before = notes;
    grabBeat = beat;
    drag = Drag::Draw;
    hear (pitch, notes.back().length);
    changed();
}

void NoteGrid::mouseDrag (const MouseEvent& e)
{
    if (drag == Drag::Overview)
    {
        const auto ov = overviewArea();
        setView ((e.position.x - ov.getX()) / ov.getWidth() * totalBeats() - overviewGrab, viewLen);
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
    if (drag == Drag::Box) { boxTo = e.position; repaint(); return; }
    if (drag == Drag::None || drawn < 0 || drawn >= (int) notes.size()) return;
    if (e.getDistanceFromDragStart() < 3 && ! moved) return;
    moved = true;
    if (drag == Drag::Draw || drag == Drag::Resize)
    {
        // longer or shorter (to the grid), every picked note by the same amount
        const double d = snap (beatAt (e.position.x) - grabBeat);
        for (size_t i : picked)
        {
            if (i >= notes.size()) continue;
            const auto& b = before[i];
            const double base = drag == Drag::Draw ? gridLen() : b.length;
            notes[i].length = jlimit (gridLen() * 0.5, totalBeats() - b.beat, base + d);
        }
        if (drawn < (int) notes.size()) lastLen = notes[(size_t) drawn].length;
    }
    else
    {
        // moved in time (to the grid) and up or down (staying in key, if that's on)
        const double d = snap (beatAt (e.position.x) - grabBeat);
        const int dp = pitchAt (e.position.y) - grabPitch;
        for (size_t i : picked)
        {
            if (i >= notes.size()) continue;
            const auto& b = before[i];
            notes[i].beat = jlimit (0.0, totalBeats() - gridLen(), b.beat + d);
            int p = jlimit (0, 127, b.pitch + dp);
            if (keySnap && dp != 0) p = toKey (p, dp);
            notes[i].pitch = p;
        }
        hear (notes[(size_t) drawn].pitch, notes[(size_t) drawn].length);
    }
    changed();
}

void NoteGrid::mouseUp (const MouseEvent& e)
{
    if (drag == Drag::Box)
    {
        // every note in the box is picked
        const auto box = Rectangle<float> (downAt, e.position);
        picked.clear();
        for (size_t i = 0; i < notes.size(); ++i)
        {
            const auto& n = notes[i];
            if (Rectangle<float> (xOf (n.beat), yOf (n.pitch), std::max (4.0f, xOf (n.beat + n.length) - xOf (n.beat)), rowH()).intersects (box)) picked.insert (i);
        }
    }
    else if (drag == Drag::Move && ! moved && ! e.mods.isShiftDown() && drawn >= 0)
    {
        // a click (no drag) on a note: just that one is picked; nothing changed
        if (! undo.empty()) undo.pop_back();
        picked.clear();
        picked.insert ((size_t) drawn);
    }
    drag = Drag::None;
    drawn = -1;
    repaint();
}

void NoteGrid::mouseDoubleClick (const MouseEvent& e)
{
    bool edge = false;
    const int hit = noteAt (e.position, edge);
    if (hit < 0 || ! gridArea().contains (e.position)) return;
    pushUndo();
    notes.erase (notes.begin() + hit);
    picked.clear();
    changed();
}

bool NoteGrid::keyPressed (const KeyPress& k)
{
    if (k.getModifiers().isCommandDown() && k.getKeyCode() == 'Z') { undoBtn.onClick(); return true; }
    if ((k == KeyPress::deleteKey || k == KeyPress::backspaceKey) && ! picked.empty())
    {
        pushUndo();
        std::vector<size_t> gone (picked.begin(), picked.end());
        std::sort (gone.rbegin(), gone.rend());
        for (auto i : gone) if (i < notes.size()) notes.erase (notes.begin() + (long) i);
        picked.clear();
        changed();
        return true;
    }
    // arrows: move what's picked (up / down a note of the key, left / right a grid step)
    const int kc = k.getKeyCode();
    if (! picked.empty() && (kc == KeyPress::upKey || kc == KeyPress::downKey || kc == KeyPress::leftKey || kc == KeyPress::rightKey))
    {
        pushUndo();
        for (size_t i : picked)
        {
            if (i >= notes.size()) continue;
            auto& n = notes[i];
            if (kc == KeyPress::upKey || kc == KeyPress::downKey)
            {
                const int dir = kc == KeyPress::upKey ? 1 : -1;
                const int step = k.getModifiers().isShiftDown() ? 12 : 1;
                n.pitch = jlimit (0, 127, n.pitch + dir * step);
                if (keySnap && step == 1) n.pitch = toKey (n.pitch, dir);
            }
            else
                n.beat = jlimit (0.0, totalBeats() - gridLen(), n.beat + (kc == KeyPress::rightKey ? gridLen() : -gridLen()));
        }
        if (picked.size() == 1) { lastHeard = -1; hear (notes[*picked.begin()].pitch, notes[*picked.begin()].length); }
        changed();
        return true;
    }
    return false;
}

void NoteGrid::mouseMove (const MouseEvent& e)
{
    bool edge = false;
    const int hit = gridArea().contains (e.position) ? noteAt (e.position, edge) : -1;
    setMouseCursor (hit >= 0 ? (edge ? MouseCursor::LeftRightResizeCursor : MouseCursor::DraggingHandCursor) : MouseCursor::NormalCursor);
    repaint();
}

void NoteGrid::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w)
{
    if (e.mods.isAltDown())
    {
        // Option / Alt + scroll on a note: how hard it plays
        bool edge = false;
        const int hit = noteAt (e.position, edge);
        if (hit < 0) return;
        if (undo.empty() || Time::getMillisecondCounter() - lastChange > 1500) pushUndo();
        const float d = w.deltaY != 0.0f ? w.deltaY : w.deltaX;
        notes[(size_t) hit].velocity = jlimit (0.1f, 1.0f, notes[(size_t) hit].velocity + (d > 0 ? 0.05f : -0.05f));
        changed();
        return;
    }
    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        zoomAround (std::exp (-(double) w.deltaY * 1.5), jlimit (gridArea().getX(), gridArea().getRight(), e.position.x));
        return;
    }
    if (e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY))
    {
        const double d = std::abs (w.deltaX) > std::abs (w.deltaY) ? w.deltaX : w.deltaY;
        setView (viewStart - d * viewLen * 0.5, viewLen);
        return;
    }
    // up and down the notes
    setPitchView (lowPitch + (w.deltaY > 0 ? 2 : -2));
}

void NoteGrid::mouseMagnify (const MouseEvent& e, float scale)
{
    if (scale > 0.0f) zoomAround (1.0 / scale, jlimit (gridArea().getX(), gridArea().getRight(), e.position.x));
}

void NoteGrid::copyAcross (bool everySectionOfKind)
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
        const int offset = (fromBar - sec->startBar) % bars;
        const double secEnd = (s.startBar + s.bars) * 4.0;
        for (int bar = s.startBar + offset; bar < s.startBar + s.bars; bar += bars)
        {
            if (bar == fromBar) continue;
            const double t0 = bar * 4.0, t1 = std::min (t0 + len, secEnd);
            notes.erase (std::remove_if (notes.begin(), notes.end(), [&] (const lp::song::Note& n) { return n.beat >= t0 - 1e-6 && n.beat < t1 - 1e-6; }), notes.end());
            for (auto n : pattern)
            {
                n.beat = n.beat - a + t0;
                if (n.beat < t1 - 1e-6) { n.length = std::min (n.length, t1 - n.beat); notes.push_back (n); }
            }
        }
    }
    picked.clear();
    changed();
}

void NoteGrid::resized()
{
    auto top = getLocalBounds().removeFromTop ((int) toolbarH - 6).reduced (10, 4);
    playBtn.setBounds (top.removeFromLeft (64)); top.removeFromLeft (10);
    zoomOut.setBounds (top.removeFromLeft (30)); top.removeFromLeft (4);
    zoomIn.setBounds (top.removeFromLeft (30)); top.removeFromLeft (4);
    fitBtn.setBounds (top.removeFromLeft (96)); top.removeFromLeft (4);
    resBtn.setBounds (top.removeFromLeft (52)); top.removeFromLeft (4);
    keyBtn.setBounds (top.removeFromLeft (84));
    bigBtn.setBounds (top.removeFromRight (70)); top.removeFromRight (6);
    undoBtn.setBounds (top.removeFromRight (56)); top.removeFromRight (6);
    clearBtn.setBounds (top.removeFromRight (56)); top.removeFromRight (6);
    fillAll.setBounds (top.removeFromRight (130)); top.removeFromRight (6);
    fillSection.setBounds (top.removeFromRight (124));
    setPitchView (lowPitch);
}

void NoteGrid::paint (Graphics& g)
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
        const int bar = (int) (la / 4.0);
        String kind = "section";
        for (auto& s : sections) if (bar >= s.startBar && bar < s.startBar + s.bars) kind = baseName (s.name);
        if (fillAll.getButtonText() != "every " + kind) fillAll.setButtonText ("every " + kind);
    }

    // ---- the overview: the whole song, where the notes are, and what you see
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
        std::vector<int> busy ((size_t) totalBars, 0);
        for (auto& n : notes) if (n.beat >= 0 && n.beat < total) ++busy[(size_t) (n.beat / 4.0)];
        g.setColour (t.grassLight.withAlpha (0.55f));
        for (int b = 0; b < totalBars; ++b)
            if (busy[(size_t) b] > 0) g.fillRect (ovX (b * 4.0) + 0.5f, ov.getBottom() - 3.0f, std::max (1.0f, ovX (4.0) - ovX (0.0) - 1.0f), 2.0f);
    }
    {
        auto win = Rectangle<float> (ovX (viewStart), ov.getY() - 1.0f, ovX (viewStart + viewLen) - ovX (viewStart), ov.getHeight() + 2.0f);
        g.setColour (Colours::white.withAlpha (0.10f));
        g.fillRoundedRectangle (win, 3.0f);
        g.setColour (Colours::white.withAlpha (0.7f));
        g.drawRoundedRectangle (win, 3.0f, 1.2f);
    }

    // ---- sections, lined up with the notes
    const auto sl = sectionLane();
    g.saveState();
    g.reduceClipRegion (sl.toNearestInt());
    g.setFont (lp::ui::mono (8.5f, true, 0.1f));
    for (auto& s : sections)
    {
        const float x0 = xOf (s.startBar * 4.0), x1 = xOf ((s.startBar + s.bars) * 4.0);
        if (x1 < sl.getX() || x0 > sl.getRight()) continue;
        auto box = Rectangle<float> (x0, sl.getY() + 1.0f, x1 - x0, sl.getHeight() - 2.0f).reduced (1.0f, 0.0f);
        const bool isPicked = selA == s.startBar * 4.0 && selB == (s.startBar + s.bars) * 4.0;
        const bool over = isMouseOver() && box.contains (getMouseXYRelative().toFloat());
        g.setColour (isPicked ? t.grass.withAlpha (0.75f) : t.grass.withAlpha (over ? 0.32f : 0.16f));
        g.fillRoundedRectangle (box, 3.0f);
        g.setColour (isPicked ? Colours::white : t.ink.withAlpha (0.8f));
        g.drawText (String (s.name).toUpperCase(), box.withLeft (std::max (box.getX(), sl.getX())).reduced (5.0f, 0.0f), Justification::centredLeft, true);
    }
    g.restoreState();

    // ---- the ruler and the grid
    const auto ru = rulerArea();
    const auto ga = gridArea();
    g.saveState();
    g.reduceClipRegion (ru.getUnion (ga).toNearestInt());
    // rows: the key's notes lighter, the home note marked, the black keys darker
    for (int p = lowPitch; p < lowPitch + rowsShown() + 1; ++p)
    {
        const float y = yOf (p);
        if (y + rowH() < ga.getY() || y > ga.getBottom()) continue;
        const bool home = ((p - key) % 12 + 12) % 12 == 0;
        g.setColour (home ? t.grass.withAlpha (0.10f) : inKey (p) ? Colours::white.withAlpha (0.045f) : Colours::black.withAlpha (0.25f));
        g.fillRect (ga.getX(), y, ga.getWidth(), rowH());
        g.setColour (Colours::white.withAlpha (((p % 12) + 12) % 12 == 0 ? 0.12f : 0.035f));
        g.drawHorizontalLine ((int) (y + rowH()), ga.getX(), ga.getRight());
    }
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
        if (b < totalBars) { g.setColour (t.inkDim); g.drawText (String (b + 1), Rectangle<float> (x + 3.0f, ru.getY(), 40.0f, ru.getHeight()), Justification::centredLeft, false); }
    }
    for (auto& s : sections) { g.setColour (t.grassLight.withAlpha (0.35f)); g.fillRect (xOf (s.startBar * 4.0) - 1.0f, ga.getY(), 2.0f, ga.getHeight()); }
    {
        const double stepLen = gridLen();
        const float stepW = xOf (stepLen) - xOf (0.0);
        const double lineLen = stepW >= 6.0f ? stepLen : (xOf (1.0) - xOf (0.0) >= 6.0f ? 1.0 : 4.0);
        for (long k = (long) std::floor (viewStart / lineLen); k * lineLen <= viewStart + viewLen; ++k)
        {
            const double beat = k * lineLen;
            if (beat < 0 || beat > total) continue;
            const bool bar = std::fmod (beat, 4.0) < 1e-6, onBeat = std::fmod (beat, 1.0) < 1e-6;
            g.setColour (Colours::white.withAlpha (bar ? 0.26f : (onBeat ? 0.11f : 0.045f)));
            g.drawVerticalLine ((int) xOf (beat), ga.getY(), ga.getBottom());
        }
    }
    // the notes (brighter = harder), picked ones outlined, slides drawn as a line from the note before
    {
        std::vector<size_t> order (notes.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::sort (order.begin(), order.end(), [this] (size_t a, size_t b) { return notes[a].beat < notes[b].beat; });
        const lp::song::Note* prev = nullptr;
        for (size_t oi : order)
        {
            const auto& n = notes[oi];
            const bool visible = n.beat + n.length >= viewStart - 1.0 && n.beat <= viewStart + viewLen;
            if (visible)
            {
                auto box = Rectangle<float> (xOf (n.beat) + 0.5f, yOf (n.pitch) + 1.0f, std::max (4.0f, xOf (n.beat + n.length) - xOf (n.beat) - 1.0f), rowH() - 2.0f);
                const bool sel = picked.count (oi) > 0;
                const bool outOfKey = ! inKey (n.pitch);
                g.setColour ((outOfKey ? Colour (0xfff2a33a) : t.grass).withAlpha (0.45f + 0.55f * n.velocity));
                g.fillRoundedRectangle (box, 3.0f);
                if (sel) { g.setColour (Colours::white); g.drawRoundedRectangle (box, 3.0f, 1.5f); }
                if (n.glide && prev != nullptr)
                {
                    g.setColour (Colours::white.withAlpha (0.8f));
                    g.drawLine (xOf (prev->beat + prev->length) - 1.0f, yOf (prev->pitch) + rowH() * 0.5f, box.getX() + 2.0f, box.getCentreY(), 1.6f);
                }
                if (box.getWidth() > 26.0f)
                {
                    g.setColour (Colour (0xff0b1015));
                    g.setFont (lp::ui::mono (8.5f, true, 0.04f));
                    g.drawText (noteText (n.pitch), box.reduced (4.0f, 0.0f), Justification::centredLeft, false);
                }
            }
            prev = &n;
        }
    }
    if (drag == Drag::Box)
    {
        g.setColour (Colours::white.withAlpha (0.12f));
        g.fillRect (Rectangle<float> (downAt, boxTo));
        g.setColour (Colours::white.withAlpha (0.6f));
        g.drawRect (Rectangle<float> (downAt, boxTo), 1.0f);
    }
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
    g.restoreState();

    // ---- the keyboard (click a key to hear it)
    g.saveState();
    g.reduceClipRegion (Rectangle<float> (8.0f, ga.getY(), keysW - 10.0f, ga.getHeight()).toNearestInt());
    for (int p = lowPitch; p < lowPitch + rowsShown() + 1; ++p)
    {
        const float y = yOf (p);
        auto keyBox = Rectangle<float> (10.0f, y + 0.5f, keysW - 14.0f, rowH() - 1.0f);
        g.setColour (blackKey (p) ? Colour (0xff1c1c22) : Colour (0xffd9d9de));
        g.fillRect (keyBox);
        if (((p % 12) + 12) % 12 == 0 || ((p - key) % 12 + 12) % 12 == 0)
        {
            g.setFont (lp::ui::mono (8.5f, true, 0.04f));
            g.setColour (blackKey (p) ? Colours::white : Colour (0xff111114));
            g.drawText (noteText (p), keyBox.reduced (4.0f, 0.0f), Justification::centredRight, false);
        }
        if (inKey (p)) { g.setColour (t.grass); g.fillRect (keyBox.withWidth (3.0f)); }
    }
    g.restoreState();

    g.setFont (lp::ui::mono (9.0f, true, 0.1f));
    g.setColour (selA >= 0.0 ? t.grassLight : t.inkDim);
    const int pa = (int) (la / 4.0) + 1, pb = (int) (lb / 4.0);
    g.drawText (selA >= 0.0 ? "BARS " + String (pa) + (pb > pa ? "-" + String (pb) : String()) : String ("SONG"),
                Rectangle<float> (12.0f, ru.getY(), keysW - 14.0f, ru.getHeight()), Justification::centredLeft, false);
    g.setColour (t.inkDim);
    g.drawText ("NOTES", Rectangle<float> (12.0f, ov.getY(), keysW - 14.0f, ov.getHeight()), Justification::centredLeft, false);

    int outside = 0;
    for (auto& n : notes) outside += inKey (n.pitch) ? 0 : 1;
    g.setFont (lp::ui::mono (9.0f, false, 0.1f));
    g.setColour (outside > 0 && ! (waitingForRender || dirty) ? Colour (0xfff2a33a) : t.inkDim);
    g.drawText (waitingForRender || dirty ? String ("RECORDING YOUR CHANGES...")
                : outside > 0 ? String (outside) + String (outside == 1 ? " NOTE IS" : " NOTES ARE") + " OUTSIDE THE KEY (ORANGE): DRAG THEM UP OR DOWN TO FIX"
                : String ("CLICK: ADD   |   DRAG: MOVE   |   DRAG ITS END: LONGER   |   DOUBLE-CLICK OR DELETE: ERASE   |   OPTION-CLICK: SLIDE   |   SHIFT-DRAG: PICK SEVERAL   |   SCROLL: UP / DOWN, SHIFT: ALONG"),
                Rectangle<float> (12.0f, ga.getBottom() + 4.0f, ga.getRight() - 12.0f, 16.0f), Justification::centredLeft, true);
}
