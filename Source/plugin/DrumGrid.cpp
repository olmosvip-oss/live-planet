#include "PluginEditor.h"

using namespace juce;
using lp::ui::font;

// EDIT > Drums: the drum pattern as a grid, like Ableton's drum rack clip.
// Click an empty step to add a hit, click a hit to erase it, drag to paint (or erase) along a row,
// scroll on a hit for how hard it plays. One or two bars at a time; copy a bar across its section,
// or to every section of the same kind. Each change is re-recorded in the background.

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
    setup (prevBar, "earlier bars");
    setup (nextBar, "later bars");
    setup (lenBtn, "show one or two bars");
    setup (resBtn, "grid: 16th or 32nd notes (for hi-hat rolls)");
    setup (fillSection, "copy what you see to every bar of this section");
    setup (fillAll, "copy what you see to every section of the same kind (every chorus, every verse...)");
    setup (clearBtn, "erase every hit you see");
    setup (undoBtn, "undo your last change");
    prevBar.onClick = [this] { startBar = std::max (0, startBar - bars); repaint(); };
    nextBar.onClick = [this] { startBar = std::min (std::max (0, totalBars - bars), startBar + bars); repaint(); };
    lenBtn.onClick = [this] { bars = bars == 1 ? 2 : 1; lenBtn.setButtonText (bars == 1 ? "1 bar" : "2 bars"); repaint(); };
    resBtn.onClick = [this] { stepsPerBeat = stepsPerBeat == 4 ? 8 : 4; resBtn.setButtonText (stepsPerBeat == 4 ? "1/16" : "1/32"); repaint(); };
    fillSection.onClick = [this] { copyAcross (false); };
    fillAll.onClick = [this] { copyAcross (true); };
    clearBtn.onClick = [this]
    {
        pushUndo();
        const double a = startBar * 4.0, b = (startBar + bars) * 4.0;
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
    // keep what you're drawing (and your place) while the track is being re-recorded
    if (! sameTrack || (! dirty && ! waitingForRender))
    {
        notes = pv.notes;
        if (! sameTrack)
        {
            undo.clear();
            // start where the drums first play (past a quiet intro)
            startBar = 0;
            if (! notes.empty()) startBar = std::min (totalBars - 1, (int) (notes.front().beat / 4.0));
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

void DrumGrid::refresh()
{
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
    repaint();
}

Rectangle<float> DrumGrid::gridArea() const
{
    return getLocalBounds().toFloat().withTrimmedTop (40.0f).withTrimmedLeft (78.0f).withTrimmedRight (12.0f).withTrimmedBottom (22.0f);
}

int DrumGrid::numSteps() const { return bars * 4 * stepsPerBeat; }

bool DrumGrid::cellAt (Point<float> p, int& row, int& step) const
{
    const auto g = gridArea();
    if (! g.contains (p) || rows.empty()) return false;
    row = jlimit (0, (int) rows.size() - 1, (int) ((p.y - g.getY()) / (g.getHeight() / (float) rows.size())));
    step = jlimit (0, numSteps() - 1, (int) ((p.x - g.getX()) / (g.getWidth() / (float) numSteps())));
    return true;
}

// the hits inside one step of one row
std::vector<size_t> DrumGrid::hitsIn (int row, int step) const
{
    std::vector<size_t> out;
    const double len = 1.0 / stepsPerBeat, a = startBar * 4.0 + step * len;
    for (size_t i = 0; i < notes.size(); ++i)
        if (notes[i].pitch == rows[(size_t) row] && notes[i].beat >= a - 1e-6 && notes[i].beat < a + len - 1e-6)
            out.push_back (i);
    return out;
}

void DrumGrid::setCell (int row, int step, bool on)
{
    auto here = hitsIn (row, step);
    if (on && here.empty())
    {
        const double len = 1.0 / stepsPerBeat;
        notes.push_back ({ startBar * 4.0 + step * len, std::min (0.25, len), rows[(size_t) row], 0.8f });
        changed();
    }
    else if (! on && ! here.empty())
    {
        std::sort (here.rbegin(), here.rend());
        for (auto i : here) notes.erase (notes.begin() + (long) i);
        changed();
    }
}

void DrumGrid::mouseDown (const MouseEvent& e)
{
    int row, step;
    if (! cellAt (e.position, row, step)) return;
    pushUndo();
    paintRow = row;
    painting = hitsIn (row, step).empty(); // started on an empty step: paint hits; on a hit: erase
    setCell (row, step, painting);
}

void DrumGrid::mouseDrag (const MouseEvent& e)
{
    if (paintRow < 0) return;
    int row, step;
    const auto g = gridArea();
    // stays on the row you started on, like drawing in a drum clip
    const auto p = Point<float> (jlimit (g.getX(), g.getRight() - 1.0f, e.position.x),
                                 g.getY() + (paintRow + 0.5f) * g.getHeight() / (float) rows.size());
    if (cellAt (p, row, step)) setCell (row, step, painting);
}

void DrumGrid::mouseUp (const MouseEvent&) { paintRow = -1; }

void DrumGrid::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w)
{
    int row, step;
    if (! cellAt (e.position, row, step)) return;
    auto here = hitsIn (row, step);
    if (here.empty()) return;
    if (undo.empty() || Time::getMillisecondCounter() - lastChange > 1500) pushUndo();
    for (auto i : here) notes[i].velocity = jlimit (0.1f, 1.0f, notes[i].velocity + (w.deltaY > 0 ? 0.05f : -0.05f));
    changed();
}

// copy the bars on screen to every slot of their section (or of every section of that kind)
void DrumGrid::copyAcross (bool everySectionOfKind)
{
    const int here = startBar;
    const lp::song::Section* sec = nullptr;
    for (auto& s : sections) if (here >= s.startBar && here < s.startBar + s.bars) sec = &s;
    if (sec == nullptr) return;
    pushUndo();
    const double a = startBar * 4.0, len = bars * 4.0;
    std::vector<lp::song::Note> pattern;
    for (auto& n : notes) if (n.beat >= a - 1e-6 && n.beat < a + len - 1e-6) pattern.push_back (n);
    const auto kind = baseName (sec->name);
    for (auto& s : sections)
    {
        if (! (&s == sec || (everySectionOfKind && baseName (s.name) == kind))) continue;
        for (int b = s.startBar; b + bars <= s.startBar + s.bars; b += bars)
        {
            if (b == startBar) continue;
            const double t0 = b * 4.0;
            notes.erase (std::remove_if (notes.begin(), notes.end(), [&] (const lp::song::Note& n) { return n.beat >= t0 - 1e-6 && n.beat < t0 + len - 1e-6; }), notes.end());
            for (auto n : pattern) { n.beat = n.beat - a + t0; notes.push_back (n); }
        }
    }
    changed();
}

void DrumGrid::resized()
{
    auto top = getLocalBounds().removeFromTop (34).reduced (10, 4);
    prevBar.setBounds (top.removeFromLeft (28)); top.removeFromLeft (4);
    nextBar.setBounds (top.removeFromLeft (28)); top.removeFromLeft (180); // the bar / section name sits here
    undoBtn.setBounds (top.removeFromRight (56)); top.removeFromRight (6);
    clearBtn.setBounds (top.removeFromRight (56)); top.removeFromRight (6);
    fillAll.setBounds (top.removeFromRight (150)); top.removeFromRight (6);
    fillSection.setBounds (top.removeFromRight (120)); top.removeFromRight (6);
    resBtn.setBounds (top.removeFromRight (56)); top.removeFromRight (6);
    lenBtn.setBounds (top.removeFromRight (64));
}

void DrumGrid::paint (Graphics& g)
{
    const auto& t = *th;
    auto r = getLocalBounds().toFloat();
    g.setColour (Colour (0xff0d0d0f));
    g.fillRoundedRectangle (r, 14.0f);
    g.setColour (t.line);
    g.drawRoundedRectangle (r.reduced (0.5f), 14.0f, 1.0f);

    // where you are: bars and section
    String where = "BAR " + String (startBar + 1) + (bars > 1 ? "-" + String (startBar + bars) : String());
    for (auto& s : sections) if (startBar >= s.startBar && startBar < s.startBar + s.bars) where << "  /  " << String (s.name).toUpperCase();
    g.setFont (lp::ui::mono (10.5f, true, 0.12f));
    g.setColour (t.ink);
    g.drawText (where, Rectangle<float> ((float) nextBar.getRight() + 12.0f, 4.0f, 180.0f, 26.0f), Justification::centredLeft, false);
    fillAll.setButtonText ("every " + [this] { for (auto& s : sections) if (startBar >= s.startBar && startBar < s.startBar + s.bars) return baseName (s.name); return String ("section"); }());

    const auto ga = gridArea();
    if (rows.empty()) return;
    const float rowH = ga.getHeight() / (float) rows.size(), stepW = ga.getWidth() / (float) numSteps();
    // rows and their names
    g.setFont (lp::ui::mono (9.5f, false, 0.12f));
    for (size_t i = 0; i < rows.size(); ++i)
    {
        const float y = ga.getY() + (float) i * rowH;
        if (i % 2 == 0) { g.setColour (Colours::white.withAlpha (0.025f)); g.fillRect (ga.getX(), y, ga.getWidth(), rowH); }
        g.setColour (t.inkDim);
        g.drawText (rowName (rows[i]), Rectangle<float> (12.0f, y, 64.0f, rowH), Justification::centredLeft, false);
    }
    // steps: bars brightest, then beats, then steps
    for (int s = 0; s <= numSteps(); ++s)
    {
        const float x = ga.getX() + (float) s * stepW;
        const bool bar = s % (4 * stepsPerBeat) == 0, beat = s % stepsPerBeat == 0;
        g.setColour (Colours::white.withAlpha (bar ? 0.28f : (beat ? 0.12f : 0.045f)));
        g.drawVerticalLine ((int) x, ga.getY(), ga.getBottom());
    }
    // the hits (brighter = harder); off-grid hits show where they really are
    const double a = startBar * 4.0, b = (startBar + bars) * 4.0;
    for (auto& n : notes)
    {
        if (n.beat < a - 1e-6 || n.beat >= b - 1e-6) continue;
        const auto it = std::find (rows.begin(), rows.end(), n.pitch);
        if (it == rows.end()) continue;
        const float y = ga.getY() + (float) (it - rows.begin()) * rowH;
        const float x = ga.getX() + (float) ((n.beat - a) * stepsPerBeat) * stepW;
        auto cell = Rectangle<float> (x + 1.5f, y + 3.0f, std::max (4.0f, stepW - 3.0f), rowH - 6.0f);
        g.setColour (t.grass.withAlpha (0.35f + 0.65f * n.velocity));
        g.fillRoundedRectangle (cell, 3.0f);
    }
    g.setFont (lp::ui::mono (9.0f, false, 0.1f));
    g.setColour (t.inkDim);
    g.drawText (waitingForRender || dirty ? "RECORDING YOUR CHANGES..." : "CLICK: ADD OR ERASE   |   DRAG: PAINT A ROW   |   SCROLL ON A HIT: HOW HARD",
                Rectangle<float> (12.0f, ga.getBottom() + 4.0f, ga.getRight() - 12.0f, 16.0f), Justification::centredLeft, false);
}
