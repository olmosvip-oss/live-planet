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
    const int heights[] = { 24, 34, 52 };
}

Rectangle<float> StudioView::rowBounds (int row) const
{
    return { 10.0f, rowsTop + (float) (row - scroll) * (float) rowStep, (float) getWidth() - 20.0f, (float) rowStep - 2.0f };
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

void StudioView::deletePicked()
{
    // each picked clip's bars go silent on its track (undo brings them back)
    const auto st = proc.songJob.getStatus();
    std::map<int, std::set<int>> byTrack;
    for (auto [tr, sc] : pickedClips)
        if (tr >= 0 && tr < (int) st.tracks.size() && sc >= 0 && sc < (int) st.song.sections.size())
            for (int b = 0; b < st.song.sections[(size_t) sc].bars; ++b) byTrack[tr].insert (st.song.sections[(size_t) sc].startBar + b);
    pickedClips.clear();
    for (auto& [tr, bars] : byTrack) proc.songJob.silenceBars (tr, bars);
    repaint();
}

bool StudioView::keyPressed (const KeyPress& k)
{
    if (k == KeyPress::deleteKey || k == KeyPress::backspaceKey)
    {
        if (! pickedClips.empty()) { deletePicked(); return true; }
        if (trackPicked && selectedTrack) { askRemove (selectedTrack()); return true; }
    }
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
    const int total = (int) st.tracks.size() + 1; // + the "add" row
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
    const float x = r.getX() + 4.0f, y = r.getCentreY() - 11.0f;
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
    return { r.getX() + 2.0f, r.getY() + 1.0f, 150.0f, std::min (24.0f, r.getHeight() - 2.0f) };
}

// next to "add instrument": a new take of every track you haven't locked
Rectangle<float> StudioView::rewriteBounds (int numTracks) const
{
    const auto a = addBounds (numTracks);
    return { a.getRight() + 10.0f, a.getY(), 200.0f, a.getHeight() };
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
            e.rate = rd->sampleRate;
            e.length = rd->lengthInSamples;
            const int n = 4000;
            e.peaks.assign ((size_t) n, 0.0f);
            const int64 per = std::max<int64> (1, e.length / n);
            AudioBuffer<float> buf ((int) rd->numChannels, (int) std::min<int64> (per, 1 << 16));
            for (int i = 0; i < n; ++i)
            {
                const int64 at = (int64) i * per;
                if (at >= e.length) break;
                const int len = (int) std::min<int64> ({ per, e.length - at, (int64) buf.getNumSamples() });
                rd->read (&buf, 0, len, at, true, rd->numChannels > 1);
                float m = 0.0f;
                for (int ch = 0; ch < buf.getNumChannels(); ++ch) m = std::max (m, buf.getMagnitude (ch, 0, len));
                e.peaks[(size_t) i] = m;
            }
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
        const int total = (int) st.tracks.size() + 1;
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

    for (int i = 0; i < (int) st.tracks.size() && i < StudioPlayer::maxTracks; ++i)
    {
        if (! rowShown (i)) continue;
        const auto& t = st.tracks[(size_t) i];
        auto& m = proc.studio.mix[(size_t) i];
        auto row = rowBounds (i);
        // the theme colour (picked at the bottom of the window), unless you gave this track its own
        const Colour tc = t.colour != 0 ? Colour (t.colour) : accent.brighter (0.15f);
        const bool picked = selectedTrack && selectedTrack() == i;
        {
            // its head: lit when it's open below (outlined when Delete would remove it)
            auto hr = row.withRight (lx - 6.0f);
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
        auto nameArea = Rectangle<float> (row.getX() + 72.0f, hy, colBounds (i, Volume).getX() - 22.0f - row.getX() - 76.0f, 22.0f);
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
        const std::set<int>& silent = t.silentBars;
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
            const bool isPicked = pickedClips.count ({ i, (int) si }) > 0;
            // the section's bars, less any you deleted: one clip per stretch that still plays
            for (int bar = sc.startBar; bar < sc.startBar + sc.bars;)
            {
                if (silent.count (bar)) { ++bar; continue; }
                int end = bar;
                while (end < sc.startBar + sc.bars && ! silent.count (end)) ++end;
                const float x0 = xOfBar (bar) + 1.0f, x1 = xOfBar (end) - 1.0f;
                bar = end;
                if (x1 < lx || x0 > lx + lw || x1 - x0 < 1.0f) continue;
                auto clip = Rectangle<float> (x0, row.getY(), x1 - x0, row.getHeight());
                clipHits.push_back ({ i, (int) si, clip });
                g.setColour (tc.withAlpha (muted ? 0.10f : isPicked ? 0.55f : 0.26f));
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
                // one thin line per pixel: the loudest moment under it
                g.setColour ((isPicked ? Colours::white : tc.brighter (0.45f)).withAlpha (muted ? 0.25f : 0.85f));
                const float mid = body.getCentreY(), half = body.getHeight() * 0.5f - 2.0f;
                const float xa = std::max (body.getX() + 1.0f, lx), xb = std::min (body.getRight() - 1.0f, lx + lw);
                for (float x = xa; x < xb; x += 1.0f)
                {
                    const double barA = view != nullptr ? view->barAt (x, lx, lw, totalBars) : (x - lx) / lw * totalBars;
                    const double barB = view != nullptr ? view->barAt (x + 1.0f, lx, lw, totalBars) : (x + 1.0f - lx) / lw * totalBars;
                    const int ka = bucketOf (barA), kb = std::max (ka + 1, bucketOf (barB));
                    float v = 0.0f;
                    for (int k = ka; k < kb && k < n; ++k) v = std::max (v, env->peaks[(size_t) k]);
                    const float h = std::max (0.5f, v / top * half);
                    g.drawVerticalLine ((int) x, mid - h, mid + h);
                }
                if (isPicked)
                {
                    g.setColour (Colours::white);
                    g.drawRoundedRectangle (clip.reduced (0.75f), 3.0f, 1.5f);
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
            scroll = std::max (0, (int) st.tracks.size() + 1 - visibleRows());
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

    // a clip being dragged: where it lands
    if (clipDragging && dragClipTrack >= 0 && dropSection >= 0 && dropSection < (int) st.song.sections.size() && rowShown (dragClipTrack))
    {
        const auto& sc = st.song.sections[(size_t) dropSection];
        const auto rb = rowBounds (dragClipTrack);
        auto r = Rectangle<float> (xOfBar (sc.startBar) + 1.0f, rb.getY(), xOfBar (sc.startBar + sc.bars) - xOfBar (sc.startBar) - 2.0f, rb.getHeight())
                     .getIntersection (Rectangle<float> (lx, rb.getY(), lw, rb.getHeight()));
        if (! r.isEmpty())
        {
            const bool same = dropSection == dragClipSection;
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
                g.drawText (String (dragCopy ? "copy to " : "move to ") + String (sc.name), r.reduced (8.0f, 0.0f), Justification::centredLeft, true);
            }
        }
    }

    // a scrollbar when there are more tracks than fit (scroll, or click it)
    {
        const int total = (int) st.tracks.size() + 1, shown = visibleRows();
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
        g.drawText ("+  add instrument", r, Justification::centred, false);
        int locked = 0;
        for (auto& tr : st.tracks) locked += tr.locked ? 1 : 0;
        auto w = rewriteBounds ((int) st.tracks.size());
        g.setColour (hoverRewrite ? th.grass.withAlpha (0.45f) : th.grass.withAlpha (0.22f));
        g.fillRoundedRectangle (w, 5.0f);
        g.setColour (Colours::white.withAlpha (0.9f));
        g.drawText (locked > 0 ? "write the rest again (" + String (locked) + " locked)" : String ("write the rest again"), w, Justification::centred, false);
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
        const int total = (int) st.tracks.size() + 1;
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
    for (int i = 0; i < (int) st.tracks.size() && i < StudioPlayer::maxTracks; ++i)
    {
        if (! rowShown (i) || ! rowBounds (i).expanded (0.0f, 1.0f).contains (e.position)) continue;
        if (e.position.x >= laneLeft())
        {
            // the lane: pick a clip (shift / cmd for more); Delete takes it out
            const ClipHit* hit = nullptr;
            for (auto& h : clipHits) if (h.track == i && h.r.contains (e.position)) hit = &h;
            const int sc = hit != nullptr ? hit->section : sectionAt (e.position.x);
            trackPicked = false;
            if (e.mods.isPopupMenu())
            {
                if (hit != nullptr && pickedClips.count ({ i, sc }) == 0) { pickedClips.clear(); pickedClips.insert ({ i, sc }); }
                bool anyGone = false;
                if (sc >= 0)
                    for (int b = 0; b < st.song.sections[(size_t) sc].bars; ++b)
                        anyGone = anyGone || st.tracks[(size_t) i].silentBars.count (st.song.sections[(size_t) sc].startBar + b) > 0;
                PopupMenu m;
                m.addSectionHeader (String (st.tracks[(size_t) i].name).toUpperCase() + (sc >= 0 ? String::fromUTF8 ("  \xc2\xb7  ") + String (st.song.sections[(size_t) sc].name).toUpperCase() : String()));
                m.addItem (1, pickedClips.size() > 1 ? "Delete " + String ((int) pickedClips.size()) + " clips" : String ("Delete this clip"), ! pickedClips.empty());
                m.addItem (2, "Bring it back (the bars deleted here)", anyGone);
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
                m.showMenuAsync (PopupMenu::Options(), [sp, i, sc] (int r)
                {
                    if (sp == nullptr) return;
                    if (r == 1) sp->deletePicked();
                    else if (r == 2 && sc >= 0)
                    {
                        const auto now = sp->proc.songJob.getStatus();
                        std::set<int> bars;
                        for (int b = 0; b < now.song.sections[(size_t) sc].bars; ++b) bars.insert (now.song.sections[(size_t) sc].startBar + b);
                        sp->proc.songJob.silenceBars (i, bars, false);
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
                dragClipTrack = i; dragClipSection = sc; clipDragging = false; dropSection = -1; // (it may be dragged)
                const std::pair<int, int> key { i, sc };
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
            const auto& tr = st.tracks[(size_t) i];
            PopupMenu m;
            m.addSectionHeader (String (tr.name).toUpperCase());
            m.addItem (2, "Swap: another sound for this part", tr.canSwap);
            m.addItem (3, tr.reversed ? "Play it forwards again" : "Reverse it", tr.canReverse);
            m.addItem (4, "Pitch up a semitone");
            m.addItem (5, "Pitch down a semitone");
            {
                // its colour: pick one (or back to its own)
                PopupMenu colours;
                const char* names[] = { "Orange", "Yellow", "Green", "Teal", "Blue", "Violet", "Pink", "Red", "Tan", "Sky" };
                for (int c = 0; c < 10; ++c)
                {
                    PopupMenu::Item it (names[c]);
                    it.itemID = 100 + c;
                    it.colour = trackColour (c);
                    it.isTicked = tr.colour == trackColour (c).getARGB();
                    colours.addItem (it);
                }
                colours.addSeparator();
                colours.addItem (99, "Theme colour (automatic)", tr.colour != 0);
                m.addSubMenu ("Colour", colours);
            }
            m.addSeparator();
            m.addItem (1, "Remove \"" + tr.name + "\" from the song", st.tracks.size() > 1);
            m.showMenuAsync (PopupMenu::Options(), [this, i] (int r)
            {
                if (r == 99) proc.songJob.setColour (i, 0);
                else if (r >= 100 && r < 110) proc.songJob.setColour (i, trackColour (r - 100).getARGB());
                if (r == 1) askRemove (i);
                else if (r == 2) proc.songJob.editTrack (i, SongJob::EditOp::Swap);
                else if (r == 3) proc.songJob.editTrack (i, SongJob::EditOp::Reverse);
                else if (r == 4) proc.songJob.editTrack (i, SongJob::EditOp::PitchUp);
                else if (r == 5) proc.songJob.editTrack (i, SongJob::EditOp::PitchDown);
            });
            if (onOpenTrack) onOpenTrack (i);
            return;
        }
        const auto c = colAt (i, e.position);
        if (c == None)
        {
            trackPicked = true; // (Delete now removes the track)
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
    if (dragClipTrack >= 0)
    {
        // a clip dragged along its lane: where it would land (a whole section), a copy with option held
        if (! clipDragging && e.getDistanceFromDragStart() < 6) return;
        clipDragging = true;
        dragCopy = e.mods.isAltDown();
        dropSection = sectionAt (e.position.x);
        setMouseCursor (dragCopy ? MouseCursor::CopyingCursor : MouseCursor::DraggingHandCursor);
        repaint();
        return;
    }
    if (draggingFeel >= 0) { setFeel (draggingFeel, e.position.x); return; }
    if (dragRow >= 0) { setFromX (dragRow, dragCol, e.position.x); repaint(); }
}

void StudioView::mouseUp (const MouseEvent& e)
{
    if (dragClipTrack >= 0 && clipDragging)
    {
        const auto st = proc.songJob.getStatus();
        const int n = (int) st.song.sections.size();
        if (dropSection >= 0 && dropSection < n && dragClipSection >= 0 && dragClipSection < n && dropSection != dragClipSection)
        {
            const auto& a = st.song.sections[(size_t) dragClipSection];
            const auto& b = st.song.sections[(size_t) dropSection];
            proc.songJob.copyBars (dragClipTrack, a.startBar, a.bars, b.startBar, b.bars, ! e.mods.isAltDown());
            pickedClips.clear();
            pickedClips.insert ({ dragClipTrack, dropSection });
        }
        setMouseCursor (MouseCursor::NormalCursor);
    }
    dragClipTrack = dragClipSection = dropSection = -1;
    clipDragging = false;
    repaint();
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
    lastMouse = e.position;
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
