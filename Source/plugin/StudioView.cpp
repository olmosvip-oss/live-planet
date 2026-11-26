#include "PluginEditor.h"

using namespace juce;
using lp::ui::font;

// The studio: one row per track, showing where its sound came from, with a mixer and remix tools.

namespace
{
    // each track its own colour, the way a DAW does it
    Colour trackColour (int i)
    {
        static const uint32 pal[] = { 0xffff8a5c, 0xfff2c94c, 0xff8fd16a, 0xff4fd1c5, 0xff5aa9ff, 0xffa08cff, 0xffe57ad1, 0xffff6b81, 0xffd4a373, 0xff6fd3ff };
        return Colour (pal[(size_t) ((i % 10) + 10) % 10]);
    }
    const int heights[] = { 24, 42, 64 };
}

Rectangle<float> StudioView::displayBounds (int d) const
{
    return { 10.0f, rowsTop + (float) (d - scroll) * (float) rowStep, (float) getWidth() - 20.0f, (float) rowStep - 2.0f };
}

Rectangle<float> StudioView::rowBounds (int row) const
{
    const int d = displayOf (row);
    return d < 0 ? Rectangle<float> (10.0f, -10000.0f, (float) getWidth() - 20.0f, (float) rowStep - 2.0f) : displayBounds (d);
}

int StudioView::displayOf (int track) const
{
    if (track >= (int) rowOfTrack.size()) return (int) drows.size(); // (the "add" row, after everything)
    return track >= 0 ? rowOfTrack[(size_t) track] : -1;
}

Rectangle<float> StudioView::groupBounds (const String& g) const
{
    for (size_t d = 0; d < drows.size(); ++d) if (drows[d].track < 0 && drows[d].group == g) return displayBounds ((int) d);
    return {};
}

std::vector<int> StudioView::membersOf (const String& g) const
{
    std::vector<int> out;
    const auto st = proc.songJob.getStatus();
    for (int i = 0; i < (int) st.tracks.size(); ++i) if (st.tracks[(size_t) i].folder == g) out.push_back (i);
    return out;
}

void StudioView::buildRows()
{
    const auto st = proc.songJob.getStatus();
    const int n = (int) st.tracks.size();
    drows.clear();
    rowOfTrack.assign ((size_t) n, -1);
    inGroup.assign ((size_t) n, false);
    std::set<String> done;
    for (int i = 0; i < n; ++i)
    {
        const auto& g = st.tracks[(size_t) i].folder;
        if (g.isEmpty()) { rowOfTrack[(size_t) i] = (int) drows.size(); drows.push_back ({ i, {} }); continue; }
        if (done.count (g)) continue;
        done.insert (g);
        drows.push_back ({ -1, g });
        for (int j = i; j < n; ++j)
            if (st.tracks[(size_t) j].folder == g)
            {
                inGroup[(size_t) j] = true;
                if (collapsed.count (g)) continue;
                rowOfTrack[(size_t) j] = (int) drows.size();
                drows.push_back ({ j, g });
            }
    }
}

int StudioView::visibleRows() const
{
    return std::max (1, (int) (((float) getHeight() - rowsTop - 34.0f) / (float) rowStep)); // (the feel row below)
}

Rectangle<float> StudioView::zoomBtn (int i) const
{
    const float r = laneRight();
    if (i == 2) return { r - 40.0f, 5.0f, 40.0f, 18.0f };
    return { r - 40.0f - 6.0f - (float) (2 - i) * 22.0f, 5.0f, 20.0f, 18.0f };
}

Rectangle<float> StudioView::heightBtn (int i) const
{
    return { laneLeft() - 82.0f + (float) i * 22.0f, 5.0f, 20.0f, 18.0f };
}

int StudioView::sectionAt (float x) const
{
    const auto st = proc.songJob.getStatus();
    const int total = std::max (1, st.song.totalBars());
    const double bar = view != nullptr ? view->barAt (x, laneLeft(), laneRight() - laneLeft(), total)
                                       : (double) ((x - laneLeft()) / std::max (1.0f, laneRight() - laneLeft())) * total;
    for (int i = 0; i < (int) st.song.sections.size(); ++i)
        if (bar >= st.song.sections[(size_t) i].startBar && bar < st.song.sections[(size_t) i].startBar + st.song.sections[(size_t) i].bars) return i;
    return -1;
}

double StudioView::barAtX (float x) const
{
    const int total = std::max (1, proc.songJob.getStatus().song.totalBars());
    return view != nullptr ? view->barAt (x, laneLeft(), laneRight() - laneLeft(), total)
                           : (double) ((x - laneLeft()) / std::max (1.0f, laneRight() - laneLeft())) * total;
}

const StudioView::ClipHit* StudioView::clipAt (int track, Point<float> p) const
{
    for (auto& h : clipHits) if (h.track == track && h.r.contains (p)) return &h;
    return nullptr;
}

// the grid clips snap to: finer as you zoom in (a bar, a beat, a 1/8, a 1/16... down to a 1/64)
int StudioView::gridTicks() const
{
    const int total = std::max (1, proc.songJob.getStatus().song.totalBars());
    const double shown = view != nullptr ? view->shown (total) : (double) total;
    const double pxPerTick = (laneRight() - laneLeft()) / (shown * lp::song::kTicksPerBar);
    int g = 1;
    while (g < lp::song::kTicksPerBar && g * pxPerTick < 9.0) g *= 2;
    return g;
}

int StudioView::tickAtX (float x, bool snap) const
{
    const double t = barAtX (x) * lp::song::kTicksPerBar;
    if (! snap) return std::max (0, (int) std::round (t));
    const int g = gridTicks();
    return std::max (0, (int) std::round (t / g) * g);
}

// a cut: the clip becomes two, here (cmd+E, or right-click "cut here")
void StudioView::splitClipAt (int track, int tick)
{
    for (auto& h : clipHits)
        if (h.track == track && tick > h.b0 && tick < h.b1)
        {
            proc.songJob.setSplit (track, tick, true);
            pickedClips.clear();
            pickedClips.insert ({ track, h.b0, tick });
            repaint();
            return;
        }
}

void StudioView::deletePicked()
{
    // each picked clip goes silent on its track (undo brings it back)
    const auto st = proc.songJob.getStatus();
    std::map<int, std::vector<std::pair<int, int>>> byTrack;
    for (auto [tr, b0, b1] : pickedClips)
        if (tr >= 0 && tr < (int) st.tracks.size()) byTrack[tr].push_back ({ b0, b1 });
    pickedClips.clear();
    for (auto& [tr, spans] : byTrack) proc.songJob.silenceTicks (tr, spans);
    repaint();
}

bool StudioView::keyPressed (const KeyPress& k)
{
    if (k == KeyPress::deleteKey || k == KeyPress::backspaceKey)
    {
        if (! pickedClips.empty()) { deletePicked(); return true; }
        if (trackPicked && selectedTrack) { askRemove (selectedTrack()); return true; }
    }
    if (k.getModifiers().isCommandDown() && (k.getKeyCode() == 'E' || k.getKeyCode() == 'e') && ! pickedClips.empty())
    {
        // cmd+E: cut the picked clip where the mouse is (like Ableton)
        const auto [tr, b0, b1] = *pickedClips.begin();
        splitClipAt (tr, tickAtX (lastMouse.x, ! k.getModifiers().isAltDown()));
        return true;
    }
    if (k.getModifiers().isCommandDown() && (k.getKeyCode() == 'G' || k.getKeyCode() == 'g'))
    {
        if (k.getModifiers().isShiftDown())
        {
            // cmd+shift+G: the picked tracks out of their group
            std::vector<int> t (selTracks.begin(), selTracks.end());
            if (t.empty() && selectedTrack) t.push_back (selectedTrack());
            proc.songJob.setGroup (t, {});
        }
        else groupPicked();
        repaint();
        return true;
    }
    if (k.getModifiers().isCommandDown() && (k.getKeyCode() == 'D' || k.getKeyCode() == 'd') && selectedTrack) { proc.songJob.duplicateTrack (selectedTrack()); return true; }
    if (k.getModifiers().isCommandDown() && (k.getKeyCode() == 'R' || k.getKeyCode() == 'r') && selectedTrack) { askRename (selectedTrack()); return true; }
    if (k == KeyPress::escapeKey && (! pickedClips.empty() || trackPicked)) { pickedClips.clear(); trackPicked = false; repaint(); return true; }
    if (view != nullptr && (k.getTextCharacter() == '=' || k.getTextCharacter() == '+' || k.getTextCharacter() == '-'))
    {
        const int total = std::max (1, proc.songJob.getStatus().song.totalBars());
        view->zoom (k.getTextCharacter() == '-' ? 1.5 : 1.0 / 1.5, view->first (total) + view->shown (total) * 0.5, total);
        return true;
    }
    return false;
}

void StudioView::mouseMagnify (const MouseEvent& e, float scale)
{
    if (view == nullptr || scale <= 0.0f) return;
    const int total = std::max (1, proc.songJob.getStatus().song.totalBars());
    view->zoom (1.0 / scale, view->barAt (e.position.x, laneLeft(), laneRight() - laneLeft(), total), total);
}

void StudioView::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w)
{
    buildRows();
    const auto st = proc.songJob.getStatus();
    const int totalBars = std::max (1, st.song.totalBars());
    if (view != nullptr && view->wheel (e, w, laneLeft(), laneRight() - laneLeft(), totalBars)) return;
    if (e.mods.isAltDown())
    {
        // option + scroll: taller or shorter lanes
        wheelZoomRows += (double) w.deltaY * (w.isSmooth ? 40.0 : 60.0);
        const int d = (int) wheelZoomRows;
        if (d != 0)
        {
            wheelZoomRows -= d;
            rowStep = jlimit (22, 72, rowStep + d);
            if (onRowStep) onRowStep (rowStep);
            repaint();
        }
        return;
    }
    const int total = totalRows(); // + the "add" row
    const int maxScroll = std::max (0, total - visibleRows());
    // as far as you scroll: a trackpad's many small steps add up to a row at a time
    wheelRows -= (double) w.deltaY * (w.isSmooth ? 6.0 : 10.0) * 24.0 / rowStep;
    const int rows = (int) wheelRows;
    if (rows == 0) return;
    wheelRows -= rows;
    scroll = jlimit (0, maxScroll, scroll + rows);
    hoverRow = -1;
    repaint();
}

Rectangle<float> StudioView::colBounds (int row, Col c) const
{
    // the track's head, left of its lane: mute, solo, lock, its name, its volume
    auto r = rowBounds (row);
    const float x = r.getX() + 4.0f + (row >= 0 && row < (int) inGroup.size() && inGroup[(size_t) row] ? 10.0f : 0.0f), y = r.getCentreY() - 11.0f;
    switch (c)
    {
        case Mute:   return { x + 2, y + 2, 18, 18 };
        case Solo:   return { x + 23, y + 2, 18, 18 };
        case Lock:   return { x + 44, y + 2, 18, 18 };
        case Volume: return { laneLeft() - 70.0f, y + 7, 54.0f, 8 };
        default:     return {};
    }
}

Rectangle<float> StudioView::addBounds (int numTracks) const
{
    auto r = rowBounds (numTracks);
    return { r.getX() + 2.0f, r.getY() + 1.0f, 118.0f, std::min (24.0f, r.getHeight() - 2.0f) };
}

// next to "add instrument": a new take of every track you haven't locked
Rectangle<float> StudioView::rewriteBounds (int numTracks) const
{
    // (both stay in the names column, clear of the lanes where a new track pulses while it's made)
    const auto a = addBounds (numTracks);
    return { a.getRight() + 8.0f, a.getY(), laneLeft() - 8.0f - (a.getRight() + 8.0f), a.getHeight() };
}

void StudioView::showAddMenu()
{
    const auto st = proc.songJob.getStatus();
    using LK = lp::song::LoopKind;
    PopupMenu m;
    m.addSectionHeader ("add a loop from your library (in key and tempo)");
    for (LK k : { LK::Keys, LK::Guitar, LK::Synth, LK::Lead, LK::Pluck, LK::Pad, LK::Strings, LK::Brass, LK::Woodwind,
                  LK::Mallet, LK::Vocal, LK::Bass, LK::Drums, LK::Perc, LK::FX, LK::Other })
    {
        const int n = (size_t) k < st.kindCounts.size() ? st.kindCounts[(size_t) k] : 0;
        m.addItem ((int) k, String (lp::song::loopKindName (k)) + "   (" + String (n) + ")", n > 0);
    }
    {
        // a MIDI track: empty, on a sound; you write its notes below
        PopupMenu midi;
        midi.addItem (2999, "A sound from my library... (type what it should sound like)");
        midi.addSeparator();
        for (int s = 0; s < lp::kNumBuiltIns; ++s) midi.addItem (3000 + s, lp::profileFor ((lp::SoundId) s).name);
        m.addSeparator();
        m.addSubMenu ("MIDI track (write your own notes)", midi);
    }
    m.addSectionHeader ("write something new over your chords");
    m.addItem (1001, "Hook: a melody (original, written for this song)");
    m.addItem (1002, "Hook: vocal chops (vocals from your sample folders, sung to a new melody)");
    m.addItem (1003, "Hook: vocal chops from my own vocal...");
    m.addItem (1004, "808: plays your bass loop's notes (or your chords) on your kick, with slides");
    m.addItem (1005, "Your vocal, auto-tuned to the song: natural...");
    m.addItem (1006, "Your vocal, auto-tuned to the song: hard...");
    m.showMenuAsync (PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (addBounds ((int) st.tracks.size()).toNearestInt())),
                     [this] (int r)
                     {
                         if (r >= 3000 && r < 3000 + lp::kNumBuiltIns) { proc.songJob.addMidiTrack (r - 3000, {}); return; }
                         if (r == 2999)
                         {
                             auto* w = new AlertWindow ("A MIDI track", "On the one-shot in your library that fits what you type.", MessageBoxIconType::NoIcon, this);
                             w->addTextEditor ("words", "", "what it should sound like, e.g. a vibrant synth pluck");
                             w->addButton ("Add it", 1, KeyPress (KeyPress::returnKey));
                             w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
                             SafePointer<StudioView> sp (this);
                             w->enterModalState (true, ModalCallbackFunction::create ([sp, w] (int ok)
                             {
                                 const auto words = w->getTextEditorContents ("words").trim();
                                 if (sp != nullptr && ok == 1 && words.isNotEmpty()) sp->proc.songJob.addMidiTrack (-1, words);
                             }), true);
                             return;
                         }
                         if (r == 1001) proc.songJob.editTrack (0, SongJob::EditOp::AddHook);
                         else if (r == 1002) proc.songJob.editTrack (0, SongJob::EditOp::AddChops);
                         else if (r == 1004) proc.songJob.editTrack (0, SongJob::EditOp::Add808);
                         else if (r == 1005 || r == 1006)
                         {
                             const bool hard = r == 1006;
                             chooser = std::make_unique<FileChooser> ("Choose your vocal: it's tuned to the song's key", File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.m4a");
                             chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this, hard] (const FileChooser& fc)
                             {
                                 if (fc.getResult().existsAsFile()) proc.songJob.addTunedVocal (fc.getResult(), hard);
                             });
                         }
                         else if (r == 1003)
                         {
                             chooser = std::make_unique<FileChooser> ("Choose your vocal: it becomes a catchy chop hook for this song", File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.m4a");
                             chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this] (const FileChooser& fc)
                             {
                                 if (fc.getResult().existsAsFile()) proc.songJob.addChopHookFrom (fc.getResult());
                             });
                         }
                         else if (r > 0) proc.songJob.editTrack (r, SongJob::EditOp::Add);
                         repaint();
                     });
}

StudioView::Col StudioView::colAt (int row, Point<float> p) const
{
    for (Col c : { Mute, Solo, Lock, Volume })
        if (colBounds (row, c).expanded (2.0f, 4.0f).contains (p)) return c;
    return None;
}

// a track's whole stem, as a loudness line (read in the background, once per file version)
const StudioView::Env* StudioView::envelopeOf (const File& f)
{
    if (! f.existsAsFile()) return nullptr;
    const auto key = f.getFullPathName() + "|" + String (f.getLastModificationTime().toMilliseconds());
    {
        const std::lock_guard<std::mutex> l (envs->lock);
        auto it = envs->done.find (key);
        if (it != envs->done.end()) return &it->second;
        if (envs->pending.count (key) > 0) return nullptr;
        envs->pending.insert (key);
    }
    auto shared = envs;
    SafePointer<StudioView> self (this);
    Thread::launch ([shared, f, key, self]
    {
        Env e;
        AudioFormatManager fm;
        fm.registerBasicFormats();
        if (std::unique_ptr<AudioFormatReader> rd { fm.createReaderFor (f) })
        {
            // 400 buckets a second (fine enough to zoom in on a single hit), the wave's top and bottom in each
            e.rate = rd->sampleRate;
            e.length = rd->lengthInSamples;
            const int64 per = std::max<int64> (1, (int64) (rd->sampleRate / 400.0));
            const int n = (int) std::max<int64> (1, (e.length + per - 1) / per);
            e.peaks.assign ((size_t) n, 0.0f);
            e.hi.assign ((size_t) n, 0.0f);
            e.lo.assign ((size_t) n, 0.0f);
            const int chunk = (int) (per * 256);
            AudioBuffer<float> buf ((int) rd->numChannels, chunk);
            for (int64 at = 0; at < e.length; at += chunk)
            {
                const int len = (int) std::min<int64> (chunk, e.length - at);
                rd->read (&buf, 0, len, at, true, rd->numChannels > 1);
                for (int i = 0; i < len; ++i)
                {
                    float v = 0.0f;
                    for (int ch = 0; ch < buf.getNumChannels(); ++ch) v += buf.getSample (ch, i);
                    v /= (float) std::max (1, buf.getNumChannels());
                    const size_t k = (size_t) ((at + i) / per);
                    if (k >= (size_t) n) break;
                    e.hi[k] = std::max (e.hi[k], v);
                    e.lo[k] = std::min (e.lo[k], v);
                }
            }
            for (int k = 0; k < n; ++k) e.peaks[(size_t) k] = std::max (e.hi[(size_t) k], -e.lo[(size_t) k]);
        }
        {
            const std::lock_guard<std::mutex> l (shared->lock);
            shared->done[key] = std::move (e);
            shared->pending.erase (key);
        }
        MessageManager::callAsync ([self] { if (self != nullptr) self->repaint(); });
    });
    return nullptr;
}

void StudioView::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    const auto& th = lp::ui::theme (proc.themeIndex.load());
    const auto st = proc.songJob.getStatus();
    const Colour txt = th.ink, dim = th.inkDim, accent = th.grass;

    g.setFont (lp::ui::mono (10.0f, false, 0.14f));
    g.setColour (dim);
    auto head = Rectangle<float> (16.0f, 6.0f, laneLeft() - 16.0f, 16.0f);
    g.drawText (String (st.tracks.size()) + " TRACKS", head, Justification::centredLeft, false);
    {
        buildRows();
        const int total = totalRows();
        scroll = jlimit (0, std::max (0, total - visibleRows()), scroll);
        const int hiddenBelow = total - (scroll + visibleRows()), hiddenAbove = scroll;
        if (hiddenBelow > 0 || hiddenAbove > 0)
        {
            g.setColour (accent);
            g.setFont (font (10.5f, lp::ui::Medium));
            String more;
            if (hiddenAbove > 0) more << hiddenAbove << " above";
            if (hiddenBelow > 0) more << (more.isEmpty() ? "" : ", ") << hiddenBelow << " below";
            g.drawText (more + " (scroll)", Rectangle<float> (112.0f, 6.0f, laneLeft() - 200.0f, 16.0f), Justification::centredLeft, false);
        }
    }

    // ---- the lanes: a bar ruler, then each track's clips, zoomed to the bars in view
    const float lx = laneLeft(), lw = laneRight() - lx;
    const int totalBars = std::max (1, st.song.totalBars());
    auto xOfBar = [&] (double bar) { return view != nullptr ? view->xOf (bar, lx, lw, totalBars) : lx + lw * (float) (bar / totalBars); };
    const double firstBar = view != nullptr ? view->first (totalBars) : 0.0, shownBars = view != nullptr ? view->shown (totalBars) : (double) totalBars;
    const float pxPerBar = lw / (float) shownBars;
    const float lanesBottom = rowsTop + (float) visibleRows() * (float) rowStep;
    clipHits.clear();
    {
        // the bar numbers (as many as fit), and a faint grid down through the lanes: every bar, stronger every 4
        Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (Rectangle<float> (lx, 0.0f, lw, lanesBottom).toNearestInt());
        int every = 1;
        while (every * pxPerBar < 30.0f && every < 64) every *= 2;
        const int b0 = std::max (0, (int) std::floor (firstBar)), b1 = std::min (totalBars, (int) std::ceil (firstBar + shownBars));
        g.setFont (lp::ui::mono (9.5f, false, 0.06f));
        for (int bar = b0; bar <= b1; ++bar)
        {
            const float x = xOfBar (bar);
            const bool four = bar % 4 == 0;
            if (pxPerBar >= 9.0f || four)
            {
                g.setColour (Colours::white.withAlpha (four ? 0.07f : 0.03f));
                g.drawVerticalLine ((int) x, rowsTop - 2.0f, lanesBottom);
            }
            if (bar % every == 0 && bar < totalBars && x < zoomBtn (0).getX() - 22.0f)
            {
                g.setColour (Colours::white.withAlpha (0.18f));
                g.drawVerticalLine ((int) x, 17.0f, 26.0f);
                g.setColour (dim);
                g.drawText (String (bar + 1), Rectangle<float> (x + 3.0f, 9.0f, 40.0f, 14.0f), Justification::centredLeft, false);
            }
        }
        g.setColour (Colours::white.withAlpha (0.08f));
        g.drawHorizontalLine (26, lx, lx + lw);
        // where each section starts
        g.setColour (Colours::white.withAlpha (0.13f));
        for (auto& sc : st.song.sections)
            g.drawVerticalLine ((int) xOfBar (sc.startBar), rowsTop - 2.0f, lanesBottom);
    }
    // zoom: out, in, the whole song (or cmd + scroll, pinch, - and + keys); lane height: small, medium, large
    {
        const char* zl[] = { "-", "+", "FIT" };
        for (int i = 0; i < 3; ++i)
        {
            const auto zb = zoomBtn (i);
            const bool on = i == 2 && view != nullptr && view->zoomed (totalBars);
            g.setColour (Colours::white.withAlpha (zb.contains (lastMouse) ? 0.16f : 0.07f));
            g.fillRoundedRectangle (zb, 4.0f);
            g.setColour (on ? th.grassLight : txt.withAlpha (0.85f));
            g.setFont (i == 2 ? lp::ui::mono (9.0f, true, 0.1f) : font (14.0f, lp::ui::Medium));
            g.drawText (zl[i], zb.translated (0.0f, i == 2 ? 0.0f : -1.0f), Justification::centred, false);
        }
        for (int i = 0; i < 3; ++i)
        {
            const auto hb = heightBtn (i);
            const bool on = rowStep == heights[i];
            g.setColour (on ? accent.withAlpha (0.55f) : Colours::white.withAlpha (hb.contains (lastMouse) ? 0.14f : 0.06f));
            g.fillRoundedRectangle (hb, 4.0f);
            // little lanes, as tall as they'd be
            g.setColour (Colours::white.withAlpha (on ? 0.95f : 0.6f));
            const float hh = 2.0f + (float) i * 2.5f;
            g.fillRoundedRectangle (hb.withSizeKeepingCentre (12.0f, hh), 1.0f);
        }
    }

    // each group's own row: fold, its name, M / S and a volume for all of it; its lane a band in its colour
    for (size_t d = 0; d < drows.size(); ++d)
    {
        if (drows[d].track >= 0 || (int) d < scroll || (int) d >= scroll + visibleRows()) continue;
        const auto& gname = drows[d].group;
        const auto row = displayBounds ((int) d);
        std::vector<int> mem;
        for (int j = 0; j < (int) st.tracks.size(); ++j) if (st.tracks[(size_t) j].folder == gname) mem.push_back (j);
        if (mem.empty()) continue;
        const Colour gc = st.tracks[(size_t) mem.front()].colour != 0 ? Colour (st.tracks[(size_t) mem.front()].colour) : accent.brighter (0.15f);
        bool allMuted = true, allSolo = true;
        float gainSum = 0.0f;
        for (int j : mem) { auto& mm = proc.studio.mix[(size_t) j]; allMuted = allMuted && mm.mute.load(); allSolo = allSolo && mm.solo.load(); gainSum += mm.gainDb.load(); }
        auto hr = row.withRight (lx - 6.0f);
        g.setColour (gc.withAlpha (0.16f));
        g.fillRoundedRectangle (hr, 5.0f);
        g.setColour (gc);
        g.fillRoundedRectangle (Rectangle<float> (hr.getX(), hr.getY(), 4.0f, hr.getHeight()), 2.0f);
        // the fold (a triangle), M, S, its name and how many tracks
        const bool folded = collapsed.count (gname) > 0;
        const float cy = row.getCentreY();
        Path tri;
        if (folded) tri.addTriangle (hr.getX() + 12.0f, cy - 5.0f, hr.getX() + 12.0f, cy + 5.0f, hr.getX() + 19.0f, cy);
        else tri.addTriangle (hr.getX() + 10.0f, cy - 3.5f, hr.getX() + 20.0f, cy - 3.5f, hr.getX() + 15.0f, cy + 3.5f);
        g.setColour (txt);
        g.fillPath (tri);
        auto gpill = [&g] (Rectangle<float> r, const String& label, bool on, Colour onCol)
        {
            g.setColour (on ? onCol : Colours::white.withAlpha (0.08f));
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (on ? Colour (0xff0b1015) : Colours::white.withAlpha (0.75f));
            g.setFont (font (10.5f, lp::ui::Medium));
            g.drawText (label, r, Justification::centred, false);
        };
        gpill ({ hr.getX() + 26.0f, cy - 9.0f, 18.0f, 18.0f }, "M", allMuted, Colour (0xfff2a33a));
        gpill ({ hr.getX() + 47.0f, cy - 9.0f, 18.0f, 18.0f }, "S", allSolo, th.grassLight);
        g.setColour (txt);
        g.setFont (font (12.5f, lp::ui::Medium));
        g.drawText (gname, Rectangle<float> (hr.getX() + 74.0f, cy - 12.0f, laneLeft() - 160.0f - hr.getX(), 14.0f), Justification::centredLeft, true);
        g.setColour (dim);
        g.setFont (lp::ui::mono (9.0f, false, 0.1f));
        g.drawText (String ((int) mem.size()) + " TRACKS" + (folded ? "  (FOLDED)" : ""), Rectangle<float> (hr.getX() + 74.0f, cy + 1.0f, 200.0f, 12.0f), Justification::centredLeft, false);
        {
            // the group's volume: where its tracks sit on average (dragging moves them all together)
            const auto vb = Rectangle<float> (laneLeft() - 70.0f, cy - 4.0f, 54.0f, 8.0f);
            g.setColour (Colours::white.withAlpha (0.1f));
            g.fillRoundedRectangle (vb, 4.0f);
            g.setColour (gc);
            g.fillRoundedRectangle (vb.withWidth (std::max (vb.getHeight(), vb.getWidth() * jmap (gainSum / (float) mem.size(), -30.0f, 6.0f, 0.0f, 1.0f))), 4.0f);
        }
        g.setColour (gc.withAlpha (0.08f));
        g.fillRect (Rectangle<float> (lx, row.getY(), lw, row.getHeight()));
    }

    for (int i = 0; i < (int) st.tracks.size() && i < StudioPlayer::maxTracks; ++i)
    {
        if (! rowShown (i)) continue;
        const auto& t = st.tracks[(size_t) i];
        auto& m = proc.studio.mix[(size_t) i];
        auto row = rowBounds (i);
        // the theme colour (picked at the bottom of the window), unless you gave this track its own
        const Colour tc = t.colour != 0 ? Colour (t.colour) : accent.brighter (0.15f);
        const bool picked = (selectedTrack && selectedTrack() == i) || selTracks.count (i) > 0;
        {
            // its head: lit when it's open below or picked (outlined when Delete would remove it); in a group, a little in
            auto hr = row.withRight (lx - 6.0f).withTrimmedLeft (i < (int) inGroup.size() && inGroup[(size_t) i] ? 10.0f : 0.0f);
            g.setColour (Colours::white.withAlpha (picked ? 0.08f : i == hoverRow ? 0.05f : 0.025f));
            g.fillRoundedRectangle (hr, 5.0f);
            if (picked && trackPicked)
            {
                g.setColour (Colours::white.withAlpha (0.6f));
                g.drawRoundedRectangle (hr.reduced (0.5f), 5.0f, 1.0f);
            }
            g.setColour (tc.withAlpha (m.mute.load() ? 0.35f : 1.0f));
            g.fillRoundedRectangle (Rectangle<float> (hr.getX(), hr.getY(), 4.0f, hr.getHeight()), 2.0f);
        }
        // the lane itself: a faint stripe
        g.setColour (Colours::white.withAlpha (i % 2 == 0 ? 0.018f : 0.03f));
        g.fillRect (Rectangle<float> (lx, row.getY(), lw, row.getHeight()));

        auto pill = [&g] (Rectangle<float> r, const String& label, bool on, Colour onCol)
        {
            g.setColour (on ? onCol : Colours::white.withAlpha (0.08f));
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (on ? Colour (0xff0b1015) : Colours::white.withAlpha (0.75f));
            g.setFont (font (10.5f, lp::ui::Medium));
            g.drawText (label, r, Justification::centred, false);
        };
        pill (colBounds (i, Mute), "M", m.mute.load(), Colour (0xfff2a33a));
        pill (colBounds (i, Solo), "S", m.solo.load(), th.grassLight);
        {
            // the lock: a track you love stays when the rest is written again
            const auto lb = colBounds (i, Lock);
            g.setColour (t.locked ? th.grass : Colours::white.withAlpha (0.08f));
            g.fillRoundedRectangle (lb, 4.0f);
            g.setColour (t.locked ? Colours::white : Colours::white.withAlpha (0.55f));
            const auto c = lb.getCentre();
            g.fillRoundedRectangle (Rectangle<float> (c.x - 4.0f, c.y - 1.0f, 8.0f, 6.0f), 1.2f);
            Path shackle;
            shackle.addCentredArc (c.x + (t.locked ? 0.0f : 2.0f), c.y - 1.5f, 2.6f, 3.0f, 0.0f, -MathConstants<float>::halfPi, MathConstants<float>::halfPi, true);
            g.strokePath (shackle, PathStrokeType (1.3f));
        }

        // its name and where it came from
        const float hy = row.getCentreY() - 11.0f;
        const float ind = i < (int) inGroup.size() && inGroup[(size_t) i] ? 10.0f : 0.0f;
        auto nameArea = Rectangle<float> (row.getX() + 72.0f + ind, hy, colBounds (i, Volume).getX() - 22.0f - row.getX() - 76.0f, 22.0f);
        String name = t.name;
        if (t.reversed) name << " (rev)";
        if (t.pitch != 0) name << " " << (t.pitch > 0 ? "+" : "") << t.pitch;
        g.setColour (txt);
        g.setFont (font (11.5f, lp::ui::Medium));
        g.drawText (name, nameArea.removeFromTop (12.0f), Justification::bottomLeft, true);
        g.setColour (! t.source.startsWith ("loopwright") ? accent.brighter (0.3f) : dim);
        g.setFont (font (9.0f));
        g.drawText (SongJob::niceSource (t.source), nameArea, Justification::topLeft, true);

        // its volume, and its level just left of it
        {
            const auto r = colBounds (i, Volume);
            g.setColour (Colours::white.withAlpha (0.1f));
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (accent);
            g.fillRoundedRectangle (r.withWidth (std::max (r.getHeight(), r.getWidth() * jmap (m.gainDb.load(), -30.0f, 6.0f, 0.0f, 1.0f))), 4.0f);
            meterL[(size_t) i].update (proc.studio.peakL[(size_t) i].exchange (0.0f));
            meterR[(size_t) i].update (proc.studio.peakR[(size_t) i].exchange (0.0f));
            lp::ui::drawStereoMeter (g, Rectangle<float> (r.getX() - 16.0f, hy + 2.0f, 9.0f, 18.0f), meterL[(size_t) i], meterR[(size_t) i]);
        }

        // a MIDI track: its clips are where its notes are, each showing its notes (like a DAW's MIDI clip)
        if (t.midi)
        {
            const auto pv = proc.songJob.partView (i);
            Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (Rectangle<float> (lx, row.getY(), lw, row.getHeight()).toNearestInt());
            if (pv.notes.empty())
            {
                g.setColour (Colours::white.withAlpha (0.35f));
                g.setFont (font (11.0f));
                g.drawText ("an empty MIDI track: double-click its lane to write notes", Rectangle<float> (lx + 10.0f, row.getY(), lw - 20.0f, row.getHeight()), Justification::centredLeft, true);
                continue;
            }
            int lo = 127, hi = 0;
            std::set<int> bars;
            for (auto& nt : pv.notes) { lo = std::min (lo, nt.pitch); hi = std::max (hi, nt.pitch); for (int b = (int) (nt.beat / 4.0); b <= (int) ((nt.beat + nt.length - 1e-6) / 4.0); ++b) bars.insert (b); }
            hi = std::max (hi, lo + 6);
            for (auto it = bars.begin(); it != bars.end();)
            {
                const int b0 = *it;
                int b1 = b0 + 1;
                ++it;
                while (it != bars.end() && *it == b1) { ++b1; ++it; }
                auto clip = Rectangle<float> (xOfBar (b0) + 1.0f, row.getY(), xOfBar (b1) - xOfBar (b0) - 2.0f, row.getHeight());
                g.setColour (tc.brighter (0.25f).withAlpha (m.mute.load() ? 0.14f : 0.9f));
                g.fillRoundedRectangle (clip, 3.0f);
                auto body = clip;
                if (row.getHeight() >= 32.0f && clip.getWidth() > 30.0f)
                {
                    auto strip = body.removeFromTop (12.0f);
                    g.setColour (tc.withAlpha (0.75f));
                    g.fillRoundedRectangle (strip, 3.0f);
                    g.fillRect (strip.withTrimmedTop (6.0f));
                    g.setColour (Colour (0xff101014).withAlpha (0.9f));
                    g.setFont (font (9.5f, lp::ui::Medium));
                    g.drawText (t.name, strip.reduced (5.0f, 0.0f), Justification::centredLeft, true);
                }
                // its notes: small bars, higher notes higher up
                g.setColour (Colour (0xff121216).withAlpha (0.78f));
                const float nh = std::max (1.5f, (body.getHeight() - 4.0f) / (float) (hi - lo + 1));
                for (auto& nt : pv.notes)
                {
                    if (nt.beat / 4.0 >= b1 || (nt.beat + nt.length) / 4.0 <= b0) continue;
                    const float x0 = xOfBar (nt.beat / 4.0), x1 = xOfBar ((nt.beat + nt.length) / 4.0);
                    const float y = body.getBottom() - 2.0f - (float) (nt.pitch - lo + 1) * nh;
                    g.fillRect (Rectangle<float> (x0, y, std::max (1.5f, x1 - x0 - 0.5f), std::max (1.2f, nh - 0.6f)));
                }
            }
            continue;
        }
        // its lane: a clip in each section it plays in (in the track's colour), with its real waveform as a thin line
        const auto* env = envelopeOf (t.stem);
        const bool muted = m.mute.load();
        if (env == nullptr || env->peaks.empty() || env->rate <= 0.0) continue;
        const double barSamples = 4.0 * 60.0 / std::max (40.0, st.song.bpm) * env->rate;
        const int n = (int) env->peaks.size();
        auto bucketOf = [&] (double bar) { return jlimit (0, n, (int) (bar * barSamples / (double) std::max<int64> (1, env->length) * n)); };
        float top = 0.0f;
        for (float v : env->peaks) top = std::max (top, v);
        if (top <= 0.0f) continue;
        const auto& silent = t.silent;
        // how loud it is on average in each section: a section where it's (nearly) silent gets no clip
        std::vector<float> avg;
        float loudest = 0.0f;
        for (auto& sc : st.song.sections)
        {
            const int b0 = bucketOf (sc.startBar), b1 = bucketOf (sc.startBar + sc.bars);
            double sum = 0.0;
            for (int k = b0; k < b1; ++k) sum += env->peaks[(size_t) k];
            avg.push_back (b1 > b0 ? (float) (sum / (b1 - b0)) : 0.0f);
            loudest = std::max (loudest, avg.back());
        }
        Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (Rectangle<float> (lx, row.getY(), lw, row.getHeight()).toNearestInt());
        for (size_t si = 0; si < st.song.sections.size(); ++si)
        {
            const auto& sc = st.song.sections[si];
            if (avg[si] < loudest * 0.12f) continue; // (only a tail or silence here)
            // the section's bars, less any you deleted, cut where you cut them: one clip per stretch that plays
            const int T = lp::song::kTicksPerBar, S = sc.startBar * T, E = (sc.startBar + sc.bars) * T;
            for (int tk = S; tk < E;)
            {
                if (silent.covers (tk)) { tk = silent.nextEdge (tk, E); continue; }
                int end = silent.nextEdge (tk, E);
                if (auto sp = t.splitTicks.upper_bound (tk); sp != t.splitTicks.end() && *sp < end) end = *sp;
                const float x0 = xOfBar ((double) tk / T) + 1.0f, x1 = xOfBar ((double) end / T) - 1.0f;
                const int c0 = tk;
                tk = end;
                if (x1 < lx || x0 > lx + lw || x1 - x0 < 1.0f) continue;
                const bool isPicked = pickedClips.count ({ i, c0, end }) > 0;
                auto clip = Rectangle<float> (x0, row.getY(), x1 - x0, row.getHeight());
                clipHits.push_back ({ i, (int) si, c0, end, clip });
                // the clip in its track's colour (light, like a DAW), the wave dark over it
                g.setColour (muted ? tc.withAlpha (0.14f) : tc.brighter (isPicked ? 0.45f : 0.25f).withAlpha (0.9f));
                g.fillRoundedRectangle (clip, 3.0f);
                g.setColour (tc.withAlpha (muted ? 0.2f : 0.6f));
                g.drawRoundedRectangle (clip.reduced (0.5f), 3.0f, 1.0f);
                auto body = clip;
                if (row.getHeight() >= 32.0f && clip.getWidth() > 30.0f)
                {
                    // a name strip along its top, the way a DAW labels a clip
                    auto strip = body.removeFromTop (12.0f);
                    g.setColour (tc.withAlpha (muted ? 0.25f : isPicked ? 0.95f : 0.7f));
                    g.fillRoundedRectangle (strip, 3.0f);
                    g.fillRect (strip.withTrimmedTop (6.0f));
                    g.setColour (Colour (0xff101014).withAlpha (0.9f));
                    g.setFont (font (9.5f, lp::ui::Medium));
                    g.drawText (t.name, strip.reduced (5.0f, 0.0f), Justification::centredLeft, true);
                }
                // the wave as one filled shape: its top and bottom traced along the clip (half-pixel steps, smooth when zoomed in)
                {
                    const float mid = body.getCentreY(), half = body.getHeight() * 0.5f - 1.5f;
                    const float xa = std::max (body.getX() + 1.0f, lx), xb = std::min (body.getRight() - 1.0f, lx + lw);
                    auto at = [&] (float x) { return view != nullptr ? view->barAt (x, lx, lw, totalBars) : (x - lx) / lw * totalBars; };
                    const auto& hi = env->hi;
                    const auto& lo = env->lo;
                    std::vector<Point<float>> upper, lower;
                    for (float x = xa; x <= xb; x += 0.5f)
                    {
                        const int ka = bucketOf (at (x)), kb = bucketOf (at (x + 0.5f));
                        float h = 0.0f, l = 0.0f;
                        if (kb > ka)
                            for (int k = ka; k < kb && k < n; ++k) { h = std::max (h, hi[(size_t) k]); l = std::min (l, lo[(size_t) k]); }
                        else if (ka < n)
                        {
                            // zoomed in past one bucket a step: in between two buckets, smoothly
                            const double exact = at (x) * barSamples / (double) std::max<int64> (1, env->length) * n;
                            const int k0 = jlimit (0, n - 1, (int) exact), k1 = std::min (n - 1, k0 + 1);
                            const float f = (float) (exact - k0);
                            h = hi[(size_t) k0] + (hi[(size_t) k1] - hi[(size_t) k0]) * f;
                            l = lo[(size_t) k0] + (lo[(size_t) k1] - lo[(size_t) k0]) * f;
                        }
                        // (slim: the loudest moment fills about 60% of the clip)
                        upper.push_back ({ x, mid - std::max (0.3f, std::min (1.0f, h / top) * half * 0.6f) });
                        lower.push_back ({ x, mid - std::min (-0.3f, std::max (-1.0f, l / top) * half * 0.6f) });
                    }
                    if (upper.size() > 1)
                    {
                        Path wave;
                        wave.startNewSubPath (upper.front());
                        for (size_t k = 1; k < upper.size(); ++k) wave.lineTo (upper[k]);
                        for (size_t k = lower.size(); k-- > 0;) wave.lineTo (lower[k]);
                        wave.closeSubPath();
                        g.setColour (Colour (0xff121216).withAlpha (muted ? 0.3f : 0.7f));
                        g.fillPath (wave);
                    }
                }
                if (isPicked)
                {
                    g.setColour (Colours::white);
                    g.drawRoundedRectangle (clip.reduced (0.75f), 3.0f, 1.5f);
                    // its edges: drag them to make it shorter or longer
                    if (clip.getWidth() > 24.0f)
                        for (float ex : { clip.getX() + 2.0f, clip.getRight() - 5.0f })
                            g.fillRoundedRectangle (Rectangle<float> (ex, clip.getCentreY() - 7.0f, 3.0f, 14.0f), 1.5f);
                }
            }
        }
    }

    // a track still being made (an AI layer, a melody being sung): where it goes, pulsing, in the row below the tracks
    if (ghosts && ! st.tracks.empty())
    {
        const auto list = ghosts();
        if (list.empty()) hadGhost = false;
        else if (! hadGhost)
        {
            // (one is starting: the list scrolls down so it has its own row, under the tracks)
            hadGhost = true;
            scroll = std::max (0, totalRows() - visibleRows());
        }
        if (! list.empty() && rowShown ((int) st.tracks.size()))
        {
            const int row = (int) st.tracks.size();
            const auto rb = rowBounds (row);
            const float pulse = 0.5f + 0.5f * std::sin ((float) Time::getMillisecondCounter() * 0.004f);
            const double secsPerBar = 240.0 / std::max (40.0, st.song.bpm);
            for (auto& gh : list)
            {
                const float x0 = xOfBar (gh.start / secsPerBar), x1 = xOfBar ((gh.start + gh.len) / secsPerBar);
                auto clip = Rectangle<float> (x0 + 1.0f, rb.getY() + 1.0f, std::max (8.0f, x1 - x0 - 2.0f), rb.getHeight() - 2.0f)
                                .getIntersection (Rectangle<float> (lx, rb.getY(), lw, rb.getHeight()));
                if (clip.isEmpty()) continue;
                g.setColour (accent.withAlpha (0.18f + 0.32f * pulse));
                g.fillRoundedRectangle (clip, 3.0f);
                g.setColour (th.grassLight.withAlpha (0.5f + 0.5f * pulse));
                g.drawRoundedRectangle (clip.reduced (0.5f), 3.0f, 1.0f);
                // a light sweeping across it, left to right
                const float sweep = clip.getX() + clip.getWidth() * (float) std::fmod (Time::getMillisecondCounter() * 0.0004, 1.0);
                g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.0f), sweep - 40.0f, 0.0f, Colours::white.withAlpha (0.0f), sweep + 40.0f, 0.0f, false));
                {
                    ColourGradient cg (Colours::white.withAlpha (0.0f), sweep - 40.0f, 0.0f, Colours::white.withAlpha (0.0f), sweep + 40.0f, 0.0f, false);
                    cg.addColour (0.5, Colours::white.withAlpha (0.22f));
                    g.setGradientFill (cg);
                    g.fillRoundedRectangle (clip.getIntersection (Rectangle<float> (sweep - 40.0f, clip.getY(), 80.0f, clip.getHeight())), 3.0f);
                }
                g.setColour (Colours::white.withAlpha (0.92f));
                g.setFont (font (10.5f, lp::ui::Medium));
                g.drawText (gh.label, clip.reduced (8.0f, 0.0f), Justification::centredLeft, true);
            }
        }
    }

    // a clip being dragged, or its edge: where it lands
    if (clipDragging && dragClipTrack >= 0 && dropStart >= 0 && dropEnd > dropStart && rowShown (dragClipTrack))
    {
        const auto rb = rowBounds (dragClipTrack);
        const double T = lp::song::kTicksPerBar;
        auto r = Rectangle<float> (xOfBar (dropStart / T) + 1.0f, rb.getY(), xOfBar (dropEnd / T) - xOfBar (dropStart / T) - 2.0f, rb.getHeight())
                     .getIntersection (Rectangle<float> (lx, rb.getY(), lw, rb.getHeight()));
        if (! r.isEmpty())
        {
            const bool same = dropStart == dragB0 && dropEnd == dragB1;
            g.setColour (Colours::white.withAlpha (same ? 0.05f : 0.16f));
            g.fillRoundedRectangle (r, 3.0f);
            Path outline;
            outline.addRoundedRectangle (r.reduced (1.0f), 3.0f);
            Path dashed;
            const float dashes[] = { 5.0f, 4.0f };
            PathStrokeType (1.5f).createDashedStroke (dashed, outline, dashes, 2);
            g.setColour (Colours::white.withAlpha (same ? 0.3f : 0.9f));
            g.fillPath (dashed);
            if (! same)
            {
                g.setFont (font (11.0f, lp::ui::Medium));
                // (lengths and places as bars and beats: "2 bars 1.5 beats", "bar 9 beat 3")
                auto len = [] (int ticks)
                {
                    const int bars = ticks / lp::song::kTicksPerBar;
                    const double beats = (ticks % lp::song::kTicksPerBar) / 16.0;
                    String o;
                    if (bars > 0) o << bars << (bars == 1 ? " bar" : " bars");
                    if (beats > 0.0) o << (o.isEmpty() ? "" : " ") << String (beats, beats == std::floor (beats) ? 0 : 2) << (beats == 1.0 ? " beat" : " beats");
                    return o.isEmpty() ? String ("0") : o;
                };
                const int bar = dropStart / lp::song::kTicksPerBar + 1;
                const double beat = (dropStart % lp::song::kTicksPerBar) / 16.0 + 1.0;
                String what = dragMode != 0 ? len (dropEnd - dropStart)
                                            : String (dragCopy ? "copy to bar " : "move to bar ") + String (bar) + (beat > 1.0 ? " beat " + String (beat, beat == std::floor (beat) ? 0 : 2) : String());
                g.drawText (what, r.reduced (8.0f, 0.0f), Justification::centredLeft, true);
            }
        }
    }

    // an effect being dragged over a track: that track, outlined
    if (fxDropRow >= 0 && rowShown (fxDropRow))
    {
        const auto rb = rowBounds (fxDropRow);
        g.setColour (th.grassLight.withAlpha (0.18f));
        g.fillRoundedRectangle (rb, 6.0f);
        g.setColour (th.grassLight);
        g.drawRoundedRectangle (rb.reduced (0.5f), 6.0f, 1.5f);
    }

    // a scrollbar when there are more tracks than fit (scroll, or click it)
    {
        const int total = totalRows(), shown = visibleRows();
        if (total > shown)
        {
            const auto sb = scrollBar();
            g.setColour (Colours::white.withAlpha (0.06f));
            g.fillRoundedRectangle (sb, 2.5f);
            const float h = sb.getHeight() * (float) shown / (float) total;
            const float y = sb.getY() + (sb.getHeight() - h) * (float) scroll / (float) std::max (1, total - shown);
            g.setColour (accent.withAlpha (0.8f));
            g.fillRoundedRectangle (Rectangle<float> (sb.getX(), y, sb.getWidth(), h), 2.5f);
        }
    }

    // where the song is playing, down through the lanes (stopped: where it will start, fainter)
    if (proc.studio.length() > 0 && (proc.songIsPlaying() || cuedBar >= 0.0) && st.song.bpm > 0.0)
    {
        const bool playing = proc.songIsPlaying();
        const double atBar = proc.playheadPos.load() / StudioPlayer::fileRate / (240.0 / st.song.bpm);
        g.setColour (Colours::white.withAlpha (playing ? 0.9f : 0.45f));
        const float px = xOfBar (atBar);
        if (px >= lx && px <= lx + lw)
        {
            g.drawVerticalLine ((int) px, 8.0f, lanesBottom);
            Path tri;
            tri.addTriangle (px - 5.0f, 6.0f, px + 5.0f, 6.0f, px, 12.0f);
            g.fillPath (tri);
        }
    }

    // add another instrument from your library, and write the unlocked tracks again
    if (! st.tracks.empty() && (int) st.tracks.size() < StudioPlayer::maxTracks && rowShown ((int) st.tracks.size()))
    {
        auto r = addBounds ((int) st.tracks.size());
        g.setColour (Colours::white.withAlpha (hoverAdd ? 0.16f : 0.08f));
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (Colours::white.withAlpha (0.85f));
        g.setFont (font (11.0f, lp::ui::Medium));
        g.drawText ("+  instrument", r, Justification::centred, false);
        int locked = 0;
        for (auto& tr : st.tracks) locked += tr.locked ? 1 : 0;
        auto w = rewriteBounds ((int) st.tracks.size());
        g.setColour (hoverRewrite ? th.grass.withAlpha (0.45f) : th.grass.withAlpha (0.22f));
        g.fillRoundedRectangle (w, 5.0f);
        g.setColour (Colours::white.withAlpha (0.9f));
        g.drawFittedText (locked > 0 ? "rewrite the rest (" + String (locked) + " locked)" : String ("write the rest again"), w.reduced (6.0f, 0.0f).toNearestInt(), Justification::centred, 1, 0.8f);
    }

    // ---- the song's feel, for the whole song at once (one line along the bottom)
    if (! st.tracks.empty())
    {
        const float y = (float) getHeight() - 24.0f;
        g.setColour (th.line);
        g.drawHorizontalLine ((int) y - 6, 12.0f, b.getRight() - 12.0f);
        g.setFont (lp::ui::mono (10.0f, true, 0.14f));
        g.setColour (txt);
        g.drawText ("FEEL", Rectangle<float> (18.0f, y, 90.0f, 16.0f), Justification::centredLeft, false);
        const char* names[] = { "PUMP", "BRIGHT", "SPARSE", "PUNCH" };
        for (int i = 0; i < 4; ++i)
        {
            const auto fb = feelBar (i);
            const float v = feelValue (i);
            const bool on = i == 1 ? std::abs (v - 0.5f) > 0.01f : v > 0.001f;
            g.setFont (lp::ui::mono (9.5f, false, 0.12f));
            g.setColour (on ? th.grassLight : dim);
            String label = names[i];
            if (i == 1 && on) label = v < 0.5f ? "DARK" : "BRIGHT";
            g.drawText (label, Rectangle<float> (fb.getX() - 62.0f, y, 58.0f, 16.0f), Justification::centredRight, false);
            g.setColour (Colours::white.withAlpha (0.08f));
            g.fillRoundedRectangle (fb, 4.5f);
            g.setColour (accent);
            if (i == 1)
            {
                const float mid = fb.getCentreX(), x = fb.getX() + fb.getWidth() * v;
                g.fillRoundedRectangle (Rectangle<float> (std::min (mid, x), fb.getY(), std::max (2.0f, std::abs (x - mid)), fb.getHeight()), 4.0f);
            }
            else if (on) g.fillRoundedRectangle (fb.withWidth (std::max (fb.getHeight(), fb.getWidth() * v)), 4.5f);
        }
        const auto ab = autoMixBounds();
        const bool am = proc.studio.autoMix.load();
        g.setColour (am ? th.grass.withAlpha (0.35f) : Colours::white.withAlpha (0.06f));
        g.fillRoundedRectangle (ab, 6.0f);
        g.setFont (lp::ui::mono (9.5f, true, 0.12f));
        g.setColour (am ? Colours::white : dim);
        g.drawText (am ? "AUTO MIX ON" : "AUTO MIX OFF", ab, Justification::centred, false);
    }

    // what's happening (an edit being made, what the AI is making), in a pill over the bar numbers, left of the zoom
    {
        String msg = statusText ? statusText() : String();
        if (msg.isEmpty() && st.editing) msg = "updating...";
        if (msg.isNotEmpty())
        {
            const auto f = font (11.0f, lp::ui::Medium);
            const float w = std::min (laneRight() - laneLeft() - 140.0f, GlyphArrangement::getStringWidth (f, msg) + 22.0f);
            auto pr = Rectangle<float> (zoomBtn (0).getX() - 10.0f - w, 4.0f, w, 20.0f);
            g.setColour (th.body.withAlpha (0.92f));
            g.fillRoundedRectangle (pr, 10.0f);
            g.setColour ((msg.contains ("didn't work") ? Colour (0xffff8f8f) : th.grassLight).withAlpha (0.5f));
            g.drawRoundedRectangle (pr.reduced (0.5f), 10.0f, 1.0f);
            g.setFont (f);
            g.setColour (msg.contains ("didn't work") ? Colour (0xffff8f8f) : th.grassLight);
            g.drawText (msg, pr.reduced (11.0f, 0.0f), Justification::centred, true);
        }
    }
}

void StudioView::setFromX (int row, Col c, float x)
{
    auto r = colBounds (row, c);
    const float v = jlimit (0.0f, 1.0f, (x - r.getX()) / r.getWidth());
    auto& m = proc.studio.mix[(size_t) row];
    if (c == Volume) m.gainDb = jmap (v, 0.0f, 1.0f, -30.0f, 6.0f);
    if (c == Pan)    m.pan = v * 2.0f - 1.0f;
    if (c == Tone)   m.tone = v * 2.0f - 1.0f;
    mixDirty = true;
    if (onMixChanged) onMixChanged();
}

Rectangle<float> StudioView::pumpBounds() const { return feelBar (0); }

// the feel row: four bars and the auto mix switch, along the bottom
Rectangle<float> StudioView::feelBar (int i) const
{
    const float y = (float) getHeight() - 24.0f, w = ((float) getWidth() - 120.0f - 150.0f) / 4.0f;
    return { 120.0f + (float) i * w + 62.0f, y + 4.0f, w - 80.0f, 9.0f };
}
Rectangle<float> StudioView::autoMixBounds() const
{
    return { (float) getWidth() - 140.0f, (float) getHeight() - 26.0f, 126.0f, 20.0f };
}
float StudioView::feelValue (int i) const
{
    auto& st = proc.studio;
    switch (i)
    {
        case 0: return st.pumpDepth.load();
        case 1: return st.bright.load() * 0.5f + 0.5f;
        case 2: return st.sparse.load();
        default: return st.punch.load();
    }
}
void StudioView::setFeel (int i, float x)
{
    const auto r = feelBar (i);
    const float v = jlimit (0.0f, 1.0f, (x - r.getX()) / r.getWidth());
    auto& st = proc.studio;
    if (i == 0) st.pumpDepth = v;
    else if (i == 1) st.bright = std::abs (v - 0.5f) < 0.04f ? 0.0f : v * 2.0f - 1.0f; // (snaps to the middle)
    else if (i == 2) st.sparse = v;
    else st.punch = v;
    mixDirty = true;
    if (onMixChanged) onMixChanged();
    repaint();
}

Rectangle<float> StudioView::scrollBar() const
{
    return { (float) getWidth() - 7.0f, rowsTop, 5.0f, (float) visibleRows() * (float) rowStep - 2.0f };
}

void StudioView::mouseDown (const MouseEvent& e)
{
    buildRows();
    dragGroup = {};
    const auto st = proc.songJob.getStatus();
    setWantsKeyboardFocus (true); // (the window turns it off for everything but text boxes; Delete needs it here)
    grabKeyboardFocus();
    {
        const int totalBars = std::max (1, st.song.totalBars());
        for (int i = 0; i < 3; ++i)
            if (view != nullptr && zoomBtn (i).expanded (2.0f).contains (e.position))
            {
                if (i == 2) view->fit();
                else view->zoom (i == 0 ? 1.6 : 1.0 / 1.6, view->first (totalBars) + view->shown (totalBars) * 0.5, totalBars);
                return;
            }
        for (int i = 0; i < 3; ++i)
            if (heightBtn (i).expanded (1.0f).contains (e.position))
            {
                rowStep = heights[i];
                if (onRowStep) onRowStep (rowStep);
                repaint();
                return;
            }
        // a click on the bar numbers: the song plays from that bar (double-click: zoom to its section)
        if (e.position.y < rowsTop - 2.0f && e.position.x >= laneLeft() && e.position.x <= laneRight())
        {
            const double bar = std::floor (view != nullptr ? view->barAt (e.position.x, laneLeft(), laneRight() - laneLeft(), totalBars)
                                                           : (e.position.x - laneLeft()) / (laneRight() - laneLeft()) * totalBars);
            if (e.getNumberOfClicks() == 1 && st.song.bpm > 0.0)
            {
                const double at = jlimit (0.0, (double) totalBars, bar) * 240.0 / st.song.bpm * StudioPlayer::fileRate;
                proc.previewPos = at;
                proc.playheadPos = at;
                cuedBar = bar;
                repaint();
            }
            if (view != nullptr && e.getNumberOfClicks() > 1)
                if (const int sc = sectionAt (e.position.x); sc >= 0)
                {
                    // double-click: just that section
                    const auto& sec = st.song.sections[(size_t) sc];
                    view->bars = std::max (2, sec.bars); view->start = sec.startBar;
                    if (view->changed) view->changed();
                }
            return;
        }
    }
    if (scrollBar().expanded (4.0f, 0.0f).contains (e.position))
    {
        // a click on the scrollbar: that part of the track list
        const int total = totalRows();
        const float f = (e.position.y - scrollBar().getY()) / scrollBar().getHeight();
        scroll = jlimit (0, std::max (0, total - visibleRows()), (int) std::round (f * total - visibleRows() * 0.5f));
        repaint();
        return;
    }
    dragRow = -1;
    draggingPump = false;
    draggingFeel = -1;
    if (! st.tracks.empty())
    {
        for (int i = 0; i < 4; ++i)
            if (feelBar (i).expanded (4.0f, 7.0f).contains (e.position))
            {
                if (e.getNumberOfClicks() > 1) setFeel (i, i == 1 ? feelBar (i).getCentreX() : feelBar (i).getX()); // double-click: back to as written
                else { draggingFeel = i; setFeel (i, e.position.x); }
                return;
            }
        if (autoMixBounds().contains (e.position))
        {
            proc.studio.autoMix = ! proc.studio.autoMix.load();
            mixDirty = true;
            if (onMixChanged) onMixChanged();
            repaint();
            return;
        }
    }
    if (! st.tracks.empty() && (int) st.tracks.size() < StudioPlayer::maxTracks && rowShown ((int) st.tracks.size())
        && addBounds ((int) st.tracks.size()).contains (e.position))
    {
        showAddMenu();
        return;
    }
    if (! st.tracks.empty() && rowShown ((int) st.tracks.size()) && rewriteBounds ((int) st.tracks.size()).contains (e.position))
    {
        proc.songJob.rewriteUnlocked();
        return;
    }
    // a group's row
    for (size_t d = 0; d < drows.size(); ++d)
    {
        if (drows[d].track >= 0 || (int) d < scroll || (int) d >= scroll + visibleRows()) continue;
        const auto row = displayBounds ((int) d);
        if (! row.contains (e.position) || e.position.x >= laneLeft()) continue;
        const auto gname = drows[d].group;
        const auto mem = membersOf (gname);
        const float x = e.position.x - row.getX();
        if (e.mods.isPopupMenu())
        {
            PopupMenu m;
            m.addSectionHeader (gname.toUpperCase());
            m.addItem (1, "Rename group...");
            m.addItem (2, "Ungroup   (cmd+shift+G)");
            m.addItem (3, collapsed.count (gname) ? "Unfold" : "Fold");
            SafePointer<StudioView> sp (this);
            m.showMenuAsync (PopupMenu::Options(), [sp, gname, mem] (int r)
            {
                if (sp == nullptr) return;
                if (r == 2) { sp->proc.songJob.setGroup (mem, {}); sp->collapsed.erase (gname); }
                else if (r == 3) { if (sp->collapsed.count (gname)) sp->collapsed.erase (gname); else sp->collapsed.insert (gname); }
                else if (r == 1)
                {
                    auto* w = new AlertWindow ("Rename group", {}, MessageBoxIconType::NoIcon, sp.getComponent());
                    w->addTextEditor ("name", gname);
                    w->addButton ("Rename", 1, KeyPress (KeyPress::returnKey));
                    w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
                    w->enterModalState (true, ModalCallbackFunction::create ([sp, w, gname] (int ok)
                    {
                        const auto to = w->getTextEditorContents ("name").trim();
                        if (sp != nullptr && ok == 1 && to.isNotEmpty() && to != gname) sp->proc.songJob.renameGroup (gname, to);
                    }), true);
                }
                if (sp != nullptr) sp->repaint();
            });
            return;
        }
        if (x < 24.0f || e.getNumberOfClicks() > 1) { if (collapsed.count (gname)) collapsed.erase (gname); else collapsed.insert (gname); }
        else if (x < 66.0f)
        {
            const bool mute = x < 45.0f;
            bool all = true;
            for (int j : mem) all = all && (mute ? proc.studio.mix[(size_t) j].mute.load() : proc.studio.mix[(size_t) j].solo.load());
            for (int j : mem) (mute ? proc.studio.mix[(size_t) j].mute : proc.studio.mix[(size_t) j].solo) = ! all;
            mixDirty = true;
            if (onMixChanged) onMixChanged();
        }
        else if (e.position.x >= laneLeft() - 74.0f)
        {
            dragGroup = gname;
            dragGroupFrom.clear();
            for (int j : mem) dragGroupFrom.push_back (proc.studio.mix[(size_t) j].gainDb.load());
        }
        repaint();
        return;
    }
    for (int i = 0; i < (int) st.tracks.size() && i < StudioPlayer::maxTracks; ++i)
    {
        if (! rowShown (i) || ! rowBounds (i).expanded (0.0f, 1.0f).contains (e.position)) continue;
        if (e.position.x >= laneLeft() && st.tracks[(size_t) i].midi && e.getNumberOfClicks() > 1 && ! e.mods.isPopupMenu())
        {
            // a MIDI track's lane, double-clicked: its notes open below, to write
            if (onEditNotes) onEditNotes (i);
            return;
        }
        if (e.position.x >= laneLeft())
        {
            // the lane: pick a clip (shift / cmd for more); Delete takes it out
            const ClipHit* hit = clipAt (i, e.position);
            const int sc = hit != nullptr ? hit->section : sectionAt (e.position.x);
            trackPicked = false;
            if (e.mods.isPopupMenu())
            {
                if (hit != nullptr && pickedClips.count ({ i, hit->b0, hit->b1 }) == 0) { pickedClips.clear(); pickedClips.insert ({ i, hit->b0, hit->b1 }); }
                const int cutBar = tickAtX (e.position.x); // (a tick: where the cut would go)
                const bool canCut = hit != nullptr && cutBar > hit->b0 && cutBar < hit->b1;
                const bool canJoin = hit != nullptr && st.tracks[(size_t) i].splitTicks.count (hit->b1) > 0;
                bool anyGone = false;
                if (sc >= 0)
                    for (int b = 0; b < st.song.sections[(size_t) sc].bars; ++b)
                        anyGone = anyGone || st.tracks[(size_t) i].silent.any ((st.song.sections[(size_t) sc].startBar + b) * lp::song::kTicksPerBar,
                                                                              (st.song.sections[(size_t) sc].startBar + b + 1) * lp::song::kTicksPerBar);
                PopupMenu m;
                m.addSectionHeader (String (st.tracks[(size_t) i].name).toUpperCase() + (sc >= 0 ? String::fromUTF8 ("  \xc2\xb7  ") + String (st.song.sections[(size_t) sc].name).toUpperCase() : String()));
                m.addItem (1, pickedClips.size() > 1 ? "Delete " + String ((int) pickedClips.size()) + " clips" : String ("Delete this clip"), ! pickedClips.empty());
                m.addItem (2, "Bring it back (the bars deleted here)", anyGone);
                m.addItem (6, "Change the track's sound...   (same notes)");
                m.addItem (4, "Cut here, at bar " + String (cutBar / lp::song::kTicksPerBar + 1) + (cutBar % lp::song::kTicksPerBar != 0 ? " beat " + String ((cutBar % lp::song::kTicksPerBar) / 16.0 + 1.0, 2) : String()) + "   (cmd+E)", canCut);
                m.addItem (5, "Join with the next clip", canJoin);
                {
                    PopupMenu colours;
                    const char* names[] = { "Orange", "Yellow", "Green", "Teal", "Blue", "Violet", "Pink", "Red", "Tan", "Sky" };
                    for (int c = 0; c < 10; ++c)
                    {
                        PopupMenu::Item it (names[c]);
                        it.itemID = 100 + c;
                        it.colour = trackColour (c);
                        it.isTicked = st.tracks[(size_t) i].colour == trackColour (c).getARGB();
                        colours.addItem (it);
                    }
                    colours.addSeparator();
                    colours.addItem (99, "Theme colour (automatic)", st.tracks[(size_t) i].colour != 0);
                    m.addSubMenu ("Track colour", colours);
                }
                m.addSeparator();
                m.addItem (3, "Remove the whole track \"" + st.tracks[(size_t) i].name + "\"", st.tracks.size() > 1);
                SafePointer<StudioView> sp (this);
                const int joinAt = hit != nullptr ? hit->b1 : -1;
                m.showMenuAsync (PopupMenu::Options(), [sp, i, sc, cutBar, joinAt] (int r)
                {
                    if (sp == nullptr) return;
                    if (r == 6) sp->askResound (i);
                    if (r == 4) sp->splitClipAt (i, cutBar);
                    else if (r == 5 && joinAt >= 0) { sp->proc.songJob.setSplit (i, joinAt, false); sp->repaint(); }
                    if (r == 1) sp->deletePicked();
                    else if (r == 2 && sc >= 0)
                    {
                        const auto now = sp->proc.songJob.getStatus();
                        const auto& sec = now.song.sections[(size_t) sc];
                        sp->proc.songJob.silenceTicks (i, { { sec.startBar * lp::song::kTicksPerBar, (sec.startBar + sec.bars) * lp::song::kTicksPerBar } }, false);
                    }
                    else if (r == 3) sp->askRemove (i);
                    else if (r == 99) sp->proc.songJob.setColour (i, 0);
                    else if (r >= 100 && r < 110) sp->proc.songJob.setColour (i, trackColour (r - 100).getARGB());
                });
                if (onOpenTrack) onOpenTrack (i);
                repaint();
                return;
            }
            if (hit != nullptr)
            {
                // it may be dragged: by its middle (moves it; option: a copy), or by an edge (shorter, longer)
                dragClipTrack = i; dragB0 = hit->b0; dragB1 = hit->b1; clipDragging = false; dropStart = dropEnd = -1;
                dragMode = hit->r.getWidth() > 18.0f && e.position.x - hit->r.getX() < 6.0f ? 1
                         : hit->r.getWidth() > 18.0f && hit->r.getRight() - e.position.x < 6.0f ? 2 : 0;
                grabBar = jlimit (0, hit->b1 - hit->b0, tickAtX (e.position.x, false) - hit->b0); // (ticks into the clip)
                const std::tuple<int, int, int> key { i, hit->b0, hit->b1 };
                if (e.mods.isShiftDown() || e.mods.isCommandDown())
                {
                    if (pickedClips.count (key)) pickedClips.erase (key); else pickedClips.insert (key);
                }
                else { pickedClips.clear(); pickedClips.insert (key); }
            }
            else if (! (e.mods.isShiftDown() || e.mods.isCommandDown())) pickedClips.clear();
            if (onOpenTrack) onOpenTrack (i);
            repaint();
            return;
        }
        pickedClips.clear();
        if (e.mods.isPopupMenu())
        {
            if (selTracks.count (i) == 0) selTracks = { i };
            showTrackMenu (i);
            if (onOpenTrack) onOpenTrack (i);
            return;
        }
        const auto c = colAt (i, e.position);
        if (c == None)
        {
            trackPicked = true; // (Delete now removes the track)
            // shift / cmd-click: more tracks picked (to group them); a plain click: just this one
            if (e.mods.isShiftDown() || e.mods.isCommandDown()) { if (selTracks.count (i)) selTracks.erase (i); else selTracks.insert (i); }
            else selTracks = { i };
            if (onOpenTrack) onOpenTrack (i); // (its name or its lane: it opens below)
            repaint();
            return;
        }
        auto& m = proc.studio.mix[(size_t) i];
        switch (c)
        {
            case Mute: m.mute = ! m.mute.load(); mixDirty = true; break;
            case Solo: m.solo = ! m.solo.load(); mixDirty = true; break;
            case Lock: proc.songJob.setLocked (i, ! st.tracks[(size_t) i].locked); break;
            case Volume: case Pan: case Tone:
                if (e.getNumberOfClicks() > 1) // double-click resets
                {
                    if (c == Volume) m.gainDb = 0.0f; else if (c == Pan) m.pan = 0.0f; else m.tone = 0.0f;
                    mixDirty = true;
                }
                else { dragRow = i; dragCol = c; setFromX (i, c, e.position.x); }
                break;
            case Swap:      if (st.tracks[(size_t) i].canSwap) proc.songJob.editTrack (i, SongJob::EditOp::Swap); break;
            case Reverse:   if (st.tracks[(size_t) i].canReverse) proc.songJob.editTrack (i, SongJob::EditOp::Reverse); break;
            case PitchDown: proc.songJob.editTrack (i, SongJob::EditOp::PitchDown); break;
            case PitchUp:   proc.songJob.editTrack (i, SongJob::EditOp::PitchUp); break;
            case Remove:    askRemove (i); return;
            default: break;
        }
        if (onMixChanged) onMixChanged();
        break;
    }
    repaint();
}

void StudioView::mouseDrag (const MouseEvent& e)
{
    if (dragGroup.isNotEmpty())
    {
        // the whole group up or down together (each keeps its place against the others)
        const float delta = (float) e.getDistanceFromDragStartX() / 54.0f * 36.0f;
        const auto mem = membersOf (dragGroup);
        for (size_t k = 0; k < mem.size() && k < dragGroupFrom.size(); ++k)
            proc.studio.mix[(size_t) mem[k]].gainDb = jlimit (-30.0f, 6.0f, dragGroupFrom[k] + delta);
        mixDirty = true;
        if (onMixChanged) onMixChanged();
        repaint();
        return;
    }
    if (dragClipTrack >= 0)
    {
        // a clip dragged along its lane: where it would land (a whole section), a copy with option held
        if (! clipDragging && e.getDistanceFromDragStart() < 4) return;
        clipDragging = true;
        dragCopy = e.mods.isAltDown();
        const int T = lp::song::kTicksPerBar;
        const int total = std::max (1, proc.songJob.getStatus().song.totalBars()) * T, len = dragB1 - dragB0;
        const bool snap = ! e.mods.isAltDown(); // (option: no snapping, for an edge)
        if (dragMode == 1) { dropStart = jlimit (0, dragB1 - 1, tickAtX (e.position.x, snap)); dropEnd = dragB1; }
        else if (dragMode == 2) { dropStart = dragB0; dropEnd = jlimit (dragB0 + 1, total, tickAtX (e.position.x, snap)); }
        else
        {
            // moved by whole bars: it lands at the same place in its bar, as long as it is
            const int bar = (int) std::round ((tickAtX (e.position.x, false) - grabBar - dragB0) / (double) T);
            dropStart = jlimit (0, std::max (0, total - len), dragB0 + bar * T);
            dropEnd = dropStart + len;
        }
        setMouseCursor (dragMode != 0 ? MouseCursor::LeftRightResizeCursor : dragCopy ? MouseCursor::CopyingCursor : MouseCursor::DraggingHandCursor);
        repaint();
        return;
    }
    if (draggingFeel >= 0) { setFeel (draggingFeel, e.position.x); return; }
    if (dragRow >= 0) { setFromX (dragRow, dragCol, e.position.x); repaint(); }
}

void StudioView::mouseUp (const MouseEvent& e)
{
    if (dragClipTrack >= 0 && clipDragging && dropStart >= 0 && dropEnd > dropStart)
    {
        const int tr = dragClipTrack, T = lp::song::kTicksPerBar;
        auto& job = proc.songJob;
        if (dragMode == 0 && dropStart != dragB0)
        {
            // moved (or copied, with option) by whole bars: those bars play there; what's outside the clip in them goes
            // silent, and (a move) the clip leaves its old place
            const int sb0 = dragB0 / T, sb1 = (dragB1 + T - 1) / T, shift = (dropStart - dragB0) / T;
            job.copyBars (tr, sb0, sb1 - sb0, sb0 + shift, sb1 - sb0, false);
            std::vector<std::pair<int, int>> outside { { (sb0 + shift) * T, dropStart }, { dropEnd, (sb1 + shift) * T } };
            if (! e.mods.isAltDown()) outside.push_back ({ dragB0, dragB1 });
            outside.erase (std::remove_if (outside.begin(), outside.end(), [&] (auto& p)
            {
                // (a move: its old place is silenced only where the new one doesn't land on it)
                return p.second <= p.first;
            }), outside.end());
            std::vector<std::pair<int, int>> gone;
            for (auto [a2, b2] : outside)
            {
                // never silence the clip's own new place
                if (b2 <= dropStart || a2 >= dropEnd) gone.push_back ({ a2, b2 });
                else { if (a2 < dropStart) gone.push_back ({ a2, dropStart }); if (b2 > dropEnd) gone.push_back ({ dropEnd, b2 }); }
            }
            job.silenceTicks (tr, gone);
            job.silenceTicks (tr, { { dropStart, dropEnd } }, false);
            job.setSplit (tr, dropStart, true);
            job.setSplit (tr, dropEnd, true);
            pickedClips.clear();
            pickedClips.insert ({ tr, dropStart, dropEnd });
        }
        else if (dragMode == 1 && dropStart != dragB0)
        {
            // its start: later (trimmed away) or earlier (what was there comes back)
            if (dropStart > dragB0) job.silenceTicks (tr, { { dragB0, dropStart } });
            else job.silenceTicks (tr, { { dropStart, dragB0 } }, false);
            job.setSplit (tr, dragB0, false);
            job.setSplit (tr, dropStart, true);
            pickedClips.clear();
            pickedClips.insert ({ tr, dropStart, dragB1 });
        }
        else if (dragMode == 2 && dropEnd != dragB1)
        {
            if (dropEnd < dragB1) job.silenceTicks (tr, { { dropEnd, dragB1 } });
            else
            {
                // longer: what was there comes back; past the track's own sound, the clip loops on (whole bars)
                job.silenceTicks (tr, { { dragB1, dropEnd } }, false);
                const auto st = job.getStatus();
                const auto* env = tr < (int) st.tracks.size() ? envelopeOf (st.tracks[(size_t) tr].stem) : nullptr;
                bool emptyThere = env != nullptr && ! env->peaks.empty();
                if (emptyThere && ! st.tracks[(size_t) tr].silent.any (dragB1, dropEnd))
                {
                    const double secPerTick = 240.0 / std::max (40.0, st.song.bpm) / T;
                    const int k0 = (int) (dragB1 * secPerTick * env->rate / (double) std::max<int64> (1, env->length) * env->peaks.size());
                    const int k1 = (int) (dropEnd * secPerTick * env->rate / (double) std::max<int64> (1, env->length) * env->peaks.size());
                    float top = 0.0f, there = 0.0f;
                    for (float v : env->peaks) top = std::max (top, v);
                    for (int k = std::max (0, k0); k < std::min ((int) env->peaks.size(), k1); ++k) there = std::max (there, env->peaks[(size_t) k]);
                    emptyThere = there < top * 0.05f;
                }
                else emptyThere = false;
                if (emptyThere)
                {
                    const int sb0 = dragB0 / T, sb1 = std::max (sb0 + 1, dragB1 / T), d0 = (dragB1 + T - 1) / T, d1 = (dropEnd + T - 1) / T;
                    if (d1 > d0) job.copyBars (tr, sb0, sb1 - sb0, d0, d1 - d0, false);
                    if (d1 * T > dropEnd) job.silenceTicks (tr, { { dropEnd, d1 * T } });
                }
            }
            job.setSplit (tr, dragB1, false);
            job.setSplit (tr, dropEnd, true);
            pickedClips.clear();
            pickedClips.insert ({ tr, dragB0, dropEnd });
        }
    }
    if (clipDragging) setMouseCursor (MouseCursor::NormalCursor);
    dragClipTrack = dropStart = dropEnd = -1;
    clipDragging = false;
    repaint();
}

// an effect dragged in from the browser: the track under it lights up; dropped, it goes on that track
void StudioView::itemDragMove (const SourceDetails& d)
{
    buildRows();
    int row = -1;
    const int n = (int) proc.songJob.getStatus().tracks.size();
    for (int i = 0; i < n; ++i) if (rowShown (i) && rowBounds (i).contains (d.localPosition.toFloat())) row = i;
    if (row != fxDropRow) { fxDropRow = row; repaint(); }
}

void StudioView::itemDropped (const SourceDetails& d)
{
    itemDragMove (d);
    const int row = fxDropRow;
    fxDropRow = -1;
    repaint();
    if (row >= 0 && onDropFx) onDropFx (row, d.description.toString().fromFirstOccurrenceOf ("fx:", false, false));
}

namespace
{
    // a grid of colours in the menu, like a DAW's: click one
    struct ColourGrid : public PopupMenu::CustomComponent
    {
        ColourGrid (std::shared_ptr<uint32> chosenOut, uint32 current) : chosen (std::move (chosenOut)), now (current) {}
        static constexpr int cols = 14, rows = 4, cell = 18, gap = 4;
        static Colour colourAt (int c, int r)
        {
            if (c == cols - 1) return Colour::greyLevel (0.85f - 0.2f * (float) r); // (greys, last column)
            const float hue = (float) c / (float) (cols - 1);
            const float sat[rows] = { 0.45f, 0.75f, 0.3f, 0.6f }, bri[rows] = { 0.98f, 0.92f, 0.8f, 0.62f };
            return Colour::fromHSV (hue, sat[r], bri[r], 1.0f);
        }
        void getIdealSize (int& w, int& h) override { w = cols * (cell + gap) + 16; h = rows * (cell + gap) + 12; }
        Rectangle<float> cellRect (int c, int r) const { return { 8.0f + (float) c * (cell + gap), 6.0f + (float) r * (cell + gap), (float) cell, (float) cell }; }
        void paint (Graphics& g) override
        {
            for (int r = 0; r < rows; ++r)
                for (int c = 0; c < cols; ++c)
                {
                    const auto cr = cellRect (c, r);
                    g.setColour (colourAt (c, r));
                    g.fillRoundedRectangle (cr, 3.0f);
                    if (colourAt (c, r).getARGB() == now || (hc == c && hr == r))
                    {
                        g.setColour (Colours::white);
                        g.drawRoundedRectangle (cr.expanded (1.5f), 4.0f, 1.5f);
                    }
                }
        }
        void mouseMove (const MouseEvent& e) override
        {
            hc = hr = -1;
            for (int r = 0; r < rows; ++r) for (int c = 0; c < cols; ++c) if (cellRect (c, r).expanded (2.0f).contains (e.position)) { hc = c; hr = r; }
            repaint();
        }
        void mouseUp (const MouseEvent&) override { if (hc >= 0) { *chosen = colourAt (hc, hr).getARGB(); triggerMenuItem(); } }
        std::shared_ptr<uint32> chosen;
        uint32 now;
        int hc = -1, hr = -1;
    };
}

// the track's menu (right-click its name), like a DAW's
void StudioView::showTrackMenu (int i)
{
    const auto st = proc.songJob.getStatus();
    if (i < 0 || i >= (int) st.tracks.size()) return;
    const auto& tr = st.tracks[(size_t) i];
    std::vector<int> picked (selTracks.begin(), selTracks.end());
    if (picked.empty()) picked.push_back (i);
    PopupMenu m;
    m.addSectionHeader (picked.size() > 1 ? String ((int) picked.size()) + " TRACKS" : String (tr.name).toUpperCase());
    m.addItem (10, "Rename   (cmd+R)", picked.size() == 1);
    m.addItem (11, "Duplicate   (cmd+D)", picked.size() == 1 && (int) st.tracks.size() < StudioPlayer::maxTracks);
    m.addItem (1, picked.size() > 1 ? "Delete " + String ((int) picked.size()) + " tracks" : String ("Delete   (Del)"), st.tracks.size() > picked.size());
    m.addSeparator();
    m.addItem (12, picked.size() > 1 ? "Group these tracks   (cmd+G)" : String ("Group track   (cmd+G)"));
    m.addItem (13, "Ungroup   (cmd+shift+G)", tr.folder.isNotEmpty());
    m.addSeparator();
    m.addItem (2, "Swap: another sound for this part", tr.canSwap && picked.size() == 1);
    m.addItem (6, "Change its sound...   (same notes)", picked.size() == 1);
    m.addItem (3, tr.reversed ? "Play it forwards again" : "Reverse it", tr.canReverse && picked.size() == 1);
    m.addItem (4, "Pitch up a semitone", picked.size() == 1);
    m.addItem (5, "Pitch down a semitone", picked.size() == 1);
    m.addSeparator();
    auto chosen = std::make_shared<uint32> (0);
    m.addCustomItem (20, std::make_unique<ColourGrid> (chosen, tr.colour), nullptr, "Colour");
    m.addItem (99, "Theme colour (automatic)", tr.colour != 0);
    SafePointer<StudioView> sp (this);
    m.showMenuAsync (PopupMenu::Options(), [sp, i, picked, chosen] (int r)
    {
        if (sp == nullptr) return;
        auto& job = sp->proc.songJob;
        if (r == 20 && *chosen != 0) for (int t : picked) job.setColour (t, *chosen);
        else if (r == 99) for (int t : picked) job.setColour (t, 0);
        else if (r == 10) sp->askRename (i);
        else if (r == 11) job.duplicateTrack (i);
        else if (r == 12) sp->groupPicked();
        else if (r == 13) job.setGroup (picked, {});
        else if (r == 6) sp->askResound (i);
        else if (r == 2) job.editTrack (i, SongJob::EditOp::Swap);
        else if (r == 3) job.editTrack (i, SongJob::EditOp::Reverse);
        else if (r == 4) job.editTrack (i, SongJob::EditOp::PitchUp);
        else if (r == 5) job.editTrack (i, SongJob::EditOp::PitchDown);
        else if (r == 1)
        {
            if (picked.size() == 1) sp->askRemove (i);
            else
            {
                // several: from the last up (so the others keep their places while each goes)
                auto order = picked;
                std::sort (order.rbegin(), order.rend());
                for (int t : order) { sp->proc.studio.removeTrackMix (t); job.editTrack (t, SongJob::EditOp::Remove); }
                sp->selTracks.clear();
            }
        }
        sp->repaint();
    });
}

// cmd+G: the picked tracks (or the open one) into a new group, named for the next number
void StudioView::groupPicked()
{
    std::vector<int> t (selTracks.begin(), selTracks.end());
    if (t.empty() && selectedTrack && selectedTrack() >= 0) t.push_back (selectedTrack());
    if (t.empty()) return;
    const auto st = proc.songJob.getStatus();
    int n = 1;
    for (;; ++n)
    {
        bool used = false;
        for (auto& tr : st.tracks) used = used || tr.folder == "Group " + String (n);
        if (! used) break;
    }
    proc.songJob.setGroup (t, "Group " + String (n));
    selTracks.clear();
    repaint();
}

void StudioView::askRename (int track)
{
    const auto st = proc.songJob.getStatus();
    if (track < 0 || track >= (int) st.tracks.size()) return;
    auto* w = new AlertWindow ("Rename track", {}, MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", st.tracks[(size_t) track].name);
    w->addButton ("Rename", 1, KeyPress (KeyPress::returnKey));
    w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
    SafePointer<StudioView> sp (this);
    w->enterModalState (true, ModalCallbackFunction::create ([sp, w, track] (int ok)
    {
        const auto to = w->getTextEditorContents ("name").trim();
        if (sp != nullptr && ok == 1 && to.isNotEmpty()) sp->proc.songJob.renameTrack (track, to);
    }), true);
}

void StudioView::askRemove (int track)
{
    const auto st = proc.songJob.getStatus();
    if (track < 0 || track >= (int) st.tracks.size() || st.tracks.size() <= 1) return;
    const auto name = st.tracks[(size_t) track].name;
    AlertWindow::showOkCancelBox (MessageBoxIconType::NoIcon, "Remove " + name + "?",
        "\"" + name + "\" comes out of the song, and the tracks below it move up. (Its file stays in the song folder.)",
        "Remove", "Keep it", this,
        ModalCallbackFunction::create ([this, track, name] (int ok)
        {
            const auto now = proc.songJob.getStatus();
            if (ok == 0 || track >= (int) now.tracks.size() || now.tracks[(size_t) track].name != name) return;
            proc.studio.removeTrackMix (track);
            proc.songJob.editTrack (track, SongJob::EditOp::Remove);
            hoverRow = -1;
            mixDirty = true;
            if (onMixChanged) onMixChanged();
            repaint();
        }));
}

void StudioView::mouseMove (const MouseEvent& e)
{
    buildRows();
    lastMouse = e.position;
    {
        // over a clip's edge: it can be dragged shorter or longer
        bool edge = false;
        for (auto& h : clipHits)
            if (h.r.getWidth() > 18.0f && h.r.expanded (0.0f, 0.0f).contains (e.position)
                && (e.position.x - h.r.getX() < 6.0f || h.r.getRight() - e.position.x < 6.0f)) edge = true;
        setMouseCursor (edge ? MouseCursor::LeftRightResizeCursor : MouseCursor::NormalCursor);
    }
    int h = -1;
    for (int i = 0; i < StudioPlayer::maxTracks; ++i)
        if (rowShown (i) && rowBounds (i).contains (e.position)) h = i;
    const auto n = (int) proc.songJob.getStatus().tracks.size();
    const bool ha = n > 0 && rowShown (n) && addBounds (n).contains (e.position);
    const bool hx = false;
    const bool hr = n > 0 && rowShown (n) && rewriteBounds (n).contains (e.position);
    if (h != hoverRow || ha != hoverAdd || hx != hoverRemove || hr != hoverRewrite) { hoverRow = h; hoverAdd = ha; hoverRemove = hx; hoverRewrite = hr; repaint(); }
}

String StudioView::getTooltip()
{
    const auto p = getMouseXYRelative().toFloat();
    const char* tips[] = {
        "Pump: everything but the drums ducks under the kick and swells back (sidechain). Double-click: off",
        "Bright: the whole song brighter and airier (right) or darker and warmer (left). Double-click: as written",
        "Sparse: in the quieter sections (intro, verses, bridge) the extra layers step back, so the choruses hit harder. Double-click: as written",
        "Punch: drums and bass forward, the rest a touch back. Double-click: as written" };
    for (int i = 0; i < 4; ++i)
        if (feelBar (i).expanded (60.0f, 8.0f).withTrimmedRight (56.0f).contains (p)) return tips[i];
    if (autoMixBounds().contains (p))
        return "Auto mix: the bass dips a little under each kick, chords and melodies stay out of the low end, and they make room for a voice when one plays";
    return {};
}

void StudioView::resized()
{
    hFeel.setTopLeftPosition (18 + (int) GlyphArrangement::getStringWidth (lp::ui::mono (10.0f, true, 0.14f), "FEEL") + 8, getHeight() - 24);
    hTracks.setTopLeftPosition (88, 4);
}
