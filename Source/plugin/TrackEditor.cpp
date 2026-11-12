#include "PluginEditor.h"

using namespace juce;
using lp::ui::font;

// EDIT: one track at a time, like a sampler. Its sample on a dark screen (drag to choose which bars play),
// a row of dials for its own mix and effects, and the remix tools.

TrackEditor::TrackEditor (LivePlanetProcessor& p, const lp::ui::Theme*& theme)
    : proc (p), th (theme),
      dragTrack ("drag track", [this] { auto st = proc.songJob.getStatus();
                                        return st.haveSong && track < (int) st.tracks.size() ? StringArray (st.tracks[(size_t) track].stem.getFullPathName()) : StringArray(); })
{
    // drags the dry track, or with Print FX on, the track with its own effects recorded into it
    dragTrack.filesToDrag = [this] { const auto f = proc.fileToDrag (track); return f.existsAsFile() ? StringArray (f.getFullPathName()) : StringArray(); };
    printFx.setClickingTogglesState (false);
    printFx.setTooltip ("On: dragging this track gives you it with its reverb, echo, warmth, nostalgia, chorus, volume and pan recorded in. Off: the dry track.");
    printFx.onClick = [this]
    {
        auto& m = proc.studio.mix[(size_t) track];
        m.printFx = ! m.printFx.load();
        // start recording it now, so it's ready by the time you drag
        if (m.printFx.load())
        {
            ++proc.printsRunning;
            juce::Thread::launch ([&pr = proc, t = track] { pr.fileToDrag (t); --pr.printsRunning; });
        }
        repaint();
    };
    addAndMakeVisible (dragTrack);
    auto mixOf = [this] (int t) -> StudioPlayer::Mix& { return proc.studio.mix[(size_t) jlimit (0, StudioPlayer::maxTracks - 1, t)]; };
    addDial ("volume", -30.0, 6.0, [mixOf] (int t) -> std::atomic<float>& { return mixOf (t).gainDb; }, 72);
    addDial ("pan", -1.0, 1.0, [mixOf] (int t) -> std::atomic<float>& { return mixOf (t).pan; }, 64);
    addDial ("tone", -1.0, 1.0, [mixOf] (int t) -> std::atomic<float>& { return mixOf (t).tone; }, 64);
    addDial ("space", 0.0, 1.0, [mixOf] (int t) -> std::atomic<float>& { return mixOf (t).space; }, 64);
    addDial ("echo", 0.0, 1.0, [mixOf] (int t) -> std::atomic<float>& { return mixOf (t).echo; }, 64);
    addDial ("warmth", 0.0, 1.0, [mixOf] (int t) -> std::atomic<float>& { return mixOf (t).warmth; }, 64);
    addDial ("nostalgia", 0.0, 1.0, [mixOf] (int t) -> std::atomic<float>& { return mixOf (t).nostalgia; }, 64);
    addDial ("chorus", 0.0, 1.0, [mixOf] (int t) -> std::atomic<float>& { return mixOf (t).chorus; }, 64);
    // the grains row: re-records the track when you let go of a dial
    addGrainDial ("grains", 0.0, 1.0, &grains.amount, 76);
    addGrainDial ("size", 10.0, 500.0, &grains.sizeMs, 64);
    addGrainDial ("rate", 0.0, 3.0, &grainRateF, 64).s.setRange (0.0, 3.0, 1.0);
    addGrainDial ("chaos", 0.0, 1.0, &grains.chaos, 64);
    addGrainDial ("position", 0.0, 1.0, &grains.position, 64);

    auto count = [this] { return (int) proc.songJob.getStatus().tracks.size(); };
    prev.onClick = [this, count] { if (count() > 0) select ((track + count() - 1) % count()); };
    next.onClick = [this, count] { if (count() > 0) select ((track + 1) % count()); };
    swap.onClick = [this] { proc.songJob.editTrack (track, SongJob::EditOp::Swap); };
    rev.onClick = [this] { proc.songJob.editTrack (track, SongJob::EditOp::Reverse); };
    pitchDown.onClick = [this] { proc.songJob.editTrack (track, SongJob::EditOp::PitchDown); };
    pitchUp.onClick = [this] { proc.songJob.editTrack (track, SongJob::EditOp::PitchUp); };
    mute.onClick = [this] { auto& m = proc.studio.mix[(size_t) track]; m.mute = ! m.mute.load(); repaint(); };
    solo.onClick = [this] { auto& m = proc.studio.mix[(size_t) track]; m.solo = ! m.solo.load(); repaint(); };
    swap.getProperties().set ("primary", true);
    love.setTooltip ("Love this sound: later songs lean toward it, its pack, and sounds like it");
    mute.getProperties().set ("warn", true);
    notThis.getProperties().set ("warn", true);
    notThis.setTooltip ("Not this: swaps it now, and it won't be picked again");
    love.onClick = [this] { proc.songJob.rateTrack (track, 1); };
    notThis.onClick = [this] { proc.songJob.rateTrack (track, -1); };
    for (auto* b : { &prev, &next, &swap, &rev, &pitchDown, &pitchUp, &mute, &solo, &printFx, &love, &notThis })
    {
        b->setWantsKeyboardFocus (false);
        b->setMouseClickGrabsKeyboardFocus (false);
        addAndMakeVisible (*b);
    }
}

TrackEditor::Dial& TrackEditor::addGrainDial (const char* name, double lo, double hi, float* local, int size)
{
    auto& d = addDial (name, lo, hi, {}, size, 1);
    d.local = local;
    const String n (name);
    d.s.textFromValueFunction = [n] (double v)
    {
        if (n == "size") return String (roundToInt (v)) + " ms";
        if (n == "rate") { static const char* r[] = { "1/4", "1/8", "1/16", "1/32" }; return String (r[jlimit (0, 3, roundToInt (v))]); }
        return String (roundToInt (v * 100.0)) + " %";
    };
    auto* raw = &d;
    d.s.onValueChange = [raw] { *raw->local = (float) raw->s.getValue(); };
    d.s.onDragEnd = [this]
    {
        grains.rate = jlimit (0, 3, roundToInt (grainRateF));
        proc.songJob.setGrains (track, grains);
    };
    return d;
}

TrackEditor::Dial& TrackEditor::addDial (const char* name, double lo, double hi, std::function<std::atomic<float>& (int)> value, int size, int row)
{
    auto d = std::make_unique<Dial>();
    d->value = std::move (value);
    d->row = row;
    auto& s = d->s;
    s.setName (name);
    s.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    s.setRotaryParameters (MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
    s.setRange (lo, hi, 0.0);
    s.getProperties().set ("size", size);
    s.setMouseDragSensitivity (220);
    s.setDoubleClickReturnValue (true, lo < 0.0 && hi > 0.0 ? 0.0 : lo);
    const bool db = String (name) == "volume", bipolar = lo < 0.0 && hi > 0.0 && ! db;
    s.textFromValueFunction = [db, bipolar] (double v)
    {
        if (db) return (v > 0.05 ? "+" : "") + String (v, 1) + " dB";
        if (bipolar) return (v > 0.005 ? "+" : "") + String (roundToInt (v * 100.0)) + " %";
        return String (roundToInt (v * 100.0)) + " %";
    };
    auto* raw = d.get();
    s.onValueChange = [this, raw] { if (raw->value) raw->value (track).store ((float) raw->s.getValue()); };
    s.setWantsKeyboardFocus (false);
    addAndMakeVisible (s);
    dials.push_back (std::move (d));
    return *dials.back();
}

void TrackEditor::select (int t)
{
    const int n = (int) proc.songJob.getStatus().tracks.size();
    track = n > 0 ? jlimit (0, n - 1, t) : 0;
    const auto st = proc.songJob.getStatus();
    if (track < (int) st.tracks.size()) { grains = st.tracks[(size_t) track].grains; grainRateF = (float) grains.rate; }
    for (auto& d : dials)
        d->s.setValue (d->local != nullptr ? (double) *d->local : (double) d->value (track).load(), dontSendNotification);
    dragFrom = dragTo = -1;
    repaint();
}

void TrackEditor::refresh()
{
    const auto st = proc.songJob.getStatus();
    if (st.version != version) { version = st.version; select (track); }
    {
        const auto& m = proc.studio.mix[(size_t) jlimit (0, StudioPlayer::maxTracks - 1, track)];
        const int rated = proc.songJob.ratingOf (track);
        mute.setToggleState (m.mute.load(), dontSendNotification);
        solo.setToggleState (m.solo.load(), dontSendNotification);
        printFx.setToggleState (m.printFx.load(), dontSendNotification);
        love.setToggleState (rated > 0, dontSendNotification);
        notThis.setToggleState (rated < 0, dontSendNotification);
    }
    for (auto& d : dials)
        if (d->value && ! d->s.isMouseButtonDown() && std::abs (d->s.getValue() - d->value (track).load()) > 1e-4)
            d->s.setValue (d->value (track).load(), dontSendNotification);
    repaint();
}

Rectangle<float> TrackEditor::screen() const
{
    return { 0.0f, 70.0f, (float) getWidth(), 236.0f };
}

const std::vector<float>& TrackEditor::peaksFor (const File& f, bool wholeSong)
{
    auto key = f.getFullPathName();
    auto it = peakCache.find (key);
    if (it != peakCache.end()) return it->second;
    std::vector<float> peaks;
    AudioFormatManager fm;
    fm.registerBasicFormats();
    if (std::unique_ptr<AudioFormatReader> rd { fm.createReaderFor (f) })
    {
        constexpr int cols = 900;
        const auto len = std::min<int64> (rd->lengthInSamples, (int64) (rd->sampleRate * (wholeSong ? 600.0 : 120.0)));
        peaks.resize (cols, 0.0f);
        for (int c = 0; c < cols; ++c)
        {
            Range<float> lvl[2];
            const auto from = len * c / cols, to = len * (c + 1) / cols;
            rd->readMaxLevels (from, (int) std::max<int64> (1, to - from), lvl, (int) std::min (2u, rd->numChannels));
            peaks[(size_t) c] = std::max (std::max (std::abs (lvl[0].getStart()), std::abs (lvl[0].getEnd())),
                                          rd->numChannels > 1 ? std::max (std::abs (lvl[1].getStart()), std::abs (lvl[1].getEnd())) : 0.0f);
        }
    }
    if (peakCache.size() > 24) peakCache.clear();
    return peakCache[key] = peaks;
}

int TrackEditor::barAt (float x) const
{
    const auto st = proc.songJob.getStatus();
    if (track >= (int) st.tracks.size()) return 0;
    const int L = std::max (1, st.tracks[(size_t) track].loopBars);
    auto s = screen().reduced (24.0f, 40.0f);
    return jlimit (0, L - 1, (int) ((x - s.getX()) / s.getWidth() * (float) L));
}

void TrackEditor::mouseDown (const MouseEvent& e)
{
    const auto st = proc.songJob.getStatus();
    if (track < (int) st.tracks.size() && st.tracks[(size_t) track].isLoop && screen().contains (e.position))
        dragFrom = dragTo = barAt (e.position.x);
}

void TrackEditor::mouseDrag (const MouseEvent& e)
{
    if (dragFrom >= 0) { dragTo = barAt (e.position.x); repaint(); }
}

void TrackEditor::mouseUp (const MouseEvent&)
{
    if (dragFrom < 0) return;
    const auto st = proc.songJob.getStatus();
    const int L = st.tracks[(size_t) track].loopBars;
    int a = std::min (dragFrom, dragTo), b = std::max (dragFrom, dragTo) + 1;
    // musical lengths only (1, 2, 4, 8 bars), so it stays on the grid of the song
    int len = b - a;
    for (int ok : { 8, 4, 2, 1 }) if (len >= ok) { len = ok; break; }
    if (a + len > L) a = L - len;
    proc.songJob.setRegion (track, a, len >= L ? 0 : len);
    dragFrom = dragTo = -1;
    repaint();
}

void TrackEditor::mouseDoubleClick (const MouseEvent& e)
{
    if (screen().contains (e.position)) { proc.songJob.setRegion (track, 0, 0); dragFrom = dragTo = -1; }
}

void TrackEditor::resized()
{
    auto top = getLocalBounds().removeFromTop (54);
    prev.setBounds (top.removeFromLeft (40).withSizeKeepingCentre (40, 40));
    next.setBounds (top.removeFromRight (40).withSizeKeepingCentre (40, 40));
    top.removeFromRight (12);
    solo.setBounds (top.removeFromRight (40).withSizeKeepingCentre (40, 40)); top.removeFromRight (6);
    mute.setBounds (top.removeFromRight (40).withSizeKeepingCentre (40, 40)); top.removeFromRight (16);
    swap.setBounds (top.removeFromRight (140).withSizeKeepingCentre (140, 40)); top.removeFromRight (8);
    rev.setBounds (top.removeFromRight (100).withSizeKeepingCentre (100, 40)); top.removeFromRight (8);
    pitchUp.setBounds (top.removeFromRight (40).withSizeKeepingCentre (40, 40));
    top.removeFromRight (70);
    pitchDown.setBounds (top.removeFromRight (40).withSizeKeepingCentre (40, 40));
    top.removeFromRight (16);
    notThis.setBounds (top.removeFromRight (84).withSizeKeepingCentre (84, 40)); top.removeFromRight (6);
    love.setBounds (top.removeFromRight (84).withSizeKeepingCentre (84, 40));

    // two rows of dials: the track's sound and space, then its grains
    auto rows = getLocalBounds().withTrimmedTop ((int) screen().getBottom() + 30);
    const int h = rows.getHeight() / 2;
    // left of the effects: print them into the dragged track, and the track to drag
    printFx.setBounds (0, rows.getY() + 6, 100, 30);
    dragTrack.setBounds (0, rows.getY() + 42, 100, 30);
    for (int r = 0; r < 2; ++r)
    {
        std::vector<Dial*> inRow;
        for (auto& d : dials) if (d->row == r) inRow.push_back (d.get());
        auto area = Rectangle<int> (rows.getX() + 110, rows.getY() + r * h, rows.getWidth() - 110, h);
        const int w = area.getWidth() / std::max (1, (int) inRow.size());
        for (size_t i = 0; i < inRow.size(); ++i) inRow[i]->s.setBounds (area.getX() + (int) i * w, area.getY(), w, h);
    }
}

void TrackEditor::paint (Graphics& g)
{
    const auto& t = *th;
    const auto st = proc.songJob.getStatus();
    if (st.tracks.empty())
    {
        g.setFont (font (16.0f, lp::ui::Medium));
        g.setColour (t.inkDim);
        g.drawText ("Write a song first: then pick any track here to shape its sample, its sound and its space.", getLocalBounds(), Justification::centred, false);
        for (auto* c : getChildren()) c->setVisible (false);
        return;
    }
    for (auto* c : getChildren()) if (! c->isVisible()) c->setVisible (true);
    if (track >= (int) st.tracks.size()) track = 0;
    const auto& tr = st.tracks[(size_t) track];

    // ---- header: which track, where its sound came from
    auto head = getLocalBounds().removeFromTop (54).toFloat().withTrimmedLeft (54.0f).withTrimmedRight (520.0f);
    g.setFont (font (24.0f, lp::ui::Medium, -0.01f));
    g.setColour (t.ink);
    g.drawText (String (tr.name).substring (0, 1).toUpperCase() + String (tr.name).substring (1), head.removeFromTop (30.0f), Justification::centredLeft, true);
    g.setFont (lp::ui::mono (10.0f, false, 0.1f));
    g.setColour (t.inkDim);
    g.drawText (String (track + 1) + " / " + String ((int) st.tracks.size()) + "   " + tr.source.toUpperCase(), head, Justification::centredLeft, true);
    // pitch, between its buttons
    g.setFont (lp::ui::mono (12.0f));
    g.setColour (t.ink);
    g.drawText ((tr.pitch > 0 ? "+" : "") + String (tr.pitch) + " st", Rectangle<float> ((float) pitchDown.getRight(), 7.0f, (float) (pitchUp.getX() - pitchDown.getRight()), 40.0f), Justification::centred, false);
    g.setFont (lp::ui::mono (9.5f, false, 0.14f));
    g.setColour (t.inkDim);
    g.drawText ("PITCH", Rectangle<float> ((float) pitchDown.getX(), 50.0f, (float) (pitchUp.getRight() - pitchDown.getX()), 12.0f), Justification::centred, false);

    // ---- the screen: the sample, its bars, the part that plays
    auto sc = screen();
    g.setColour (Colour (0xff0a0a0b));
    g.fillRoundedRectangle (sc, 16.0f);
    g.setColour (Colours::black);
    g.drawRoundedRectangle (sc.reduced (1.0f), 16.0f, 2.0f);
    auto wave = sc.reduced (24.0f, 40.0f);
    const bool loop = tr.isLoop && tr.samplePath.isNotEmpty();
    const auto& peaks = peaksFor (loop ? File (tr.samplePath) : tr.stem, ! loop);
    const int L = std::max (1, tr.loopBars);
    int rs = tr.regionStart, rb = tr.regionBars > 0 ? tr.regionBars : L;
    if (dragFrom >= 0) { rs = std::min (dragFrom, dragTo); rb = std::abs (dragTo - dragFrom) + 1; }
    const float x0 = wave.getX() + wave.getWidth() * (float) rs / (float) L, x1 = wave.getX() + wave.getWidth() * (float) (rs + rb) / (float) L;
    if (loop)
    {
        g.setColour (t.grass.withAlpha (0.14f));
        g.fillRoundedRectangle (Rectangle<float> (x0, wave.getY() - 14.0f, x1 - x0, wave.getHeight() + 28.0f), 8.0f);
        for (int b = 0; b <= L; ++b)
        {
            const float x = wave.getX() + wave.getWidth() * (float) b / (float) L;
            g.setColour (Colours::white.withAlpha (b == 0 || b == L ? 0.0f : 0.12f));
            g.drawVerticalLine ((int) x, wave.getY() - 14.0f, wave.getBottom() + 14.0f);
            if (b < L)
            {
                g.setFont (lp::ui::mono (9.5f, false, 0.1f));
                g.setColour (Colours::white.withAlpha (0.4f));
                g.drawText ("BAR " + String (b + 1), Rectangle<float> (x + 6.0f, wave.getY() - 30.0f, 80.0f, 12.0f), Justification::centredLeft, false);
            }
        }
    }
    if (! peaks.empty())
    {
        const float mid = wave.getCentreY(), half = wave.getHeight() * 0.5f;
        float top = 0.0f;
        for (float v : peaks) top = std::max (top, v);
        const float norm = top > 0.0f ? 1.0f / top : 1.0f;
        for (size_t c = 0; c < peaks.size(); ++c)
        {
            const float x = wave.getX() + wave.getWidth() * (float) c / (float) peaks.size();
            const bool inRegion = ! loop || (x >= x0 && x <= x1);
            g.setColour (inRegion ? Colours::white.withAlpha (0.88f) : Colours::white.withAlpha (0.22f));
            const float h = std::max (1.0f, peaks[c] * norm * half);
            g.fillRect (x, mid - h, std::max (1.0f, wave.getWidth() / (float) peaks.size() - 0.5f), h * 2.0f);
        }
    }
    g.setFont (lp::ui::mono (10.0f, false, 0.12f));
    g.setColour (Colours::white.withAlpha (0.55f));
    auto foot = sc.reduced (24.0f, 12.0f).removeFromBottom (14.0f);
    if (loop)
        g.drawText (rb >= L ? "PLAYING ALL " + String (L) + " BARS  \xc2\xb7  DRAG ACROSS THE BARS TO PLAY JUST A PART"
                            : "PLAYING BARS " + String (rs + 1) + "-" + String (rs + rb) + " OF " + String (L) + "  \xc2\xb7  DOUBLE-CLICK FOR ALL OF IT",
                    foot, Justification::centredLeft, false);
    else
        g.drawText ("THE WHOLE TRACK  \xc2\xb7  PICKING A PART WORKS ON SAMPLE LOOPS", foot, Justification::centredLeft, false);
    if (st.editing)
    {
        g.setColour (t.grassLight);
        g.drawText ("UPDATING...", foot, Justification::centredRight, false);
    }
    // the row titles
    {
        const float y = sc.getBottom() + 30.0f + (float) ((getHeight() - (int) sc.getBottom() - 30) / 2);
        g.setFont (font (17.0f, lp::ui::Medium));
        g.setColour (t.ink);
        g.drawText ("Grains", Rectangle<float> (0.0f, y + 6.0f, 110.0f, 24.0f), Justification::centredLeft, false);
        g.setFont (lp::ui::mono (9.5f, false, 0.1f));
        g.setColour (t.inkDim);
        g.drawText ("IN TIME WITH", Rectangle<float> (0.0f, y + 34.0f, 110.0f, 12.0f), Justification::centredLeft, false);
        g.drawText ("YOUR SONG", Rectangle<float> (0.0f, y + 48.0f, 110.0f, 12.0f), Justification::centredLeft, false);
        g.setColour (t.line);
        g.drawHorizontalLine ((int) y, 0.0f, (float) getWidth());
    }
    // the remix note
    g.setFont (font (12.5f));
    g.setColour (t.inkDim);
    g.drawText (tr.remix, Rectangle<float> (0.0f, sc.getBottom() + 2.0f, (float) getWidth(), 18.0f), Justification::centredLeft, true);
}
