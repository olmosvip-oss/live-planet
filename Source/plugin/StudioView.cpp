#include "PluginEditor.h"

using namespace juce;
using lp::ui::font;

// The studio: one row per track, showing where its sound came from, with a mixer and remix tools.

Rectangle<float> StudioView::rowBounds (int row) const
{
    return { 10.0f, 26.0f + (float) (row - scroll) * 24.0f, (float) getWidth() - 20.0f, 22.0f };
}

int StudioView::visibleRows() const
{
    return std::max (1, (int) ((getHeight() - 26 - 34) / 24)); // (the feel row below)
}

void StudioView::mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w)
{
    const int total = (int) proc.songJob.getStatus().tracks.size() + 1; // + the "add" row
    const int maxScroll = std::max (0, total - visibleRows());
    // as far as you scroll: a trackpad's many small steps add up to a row at a time
    wheelRows -= (double) w.deltaY * (w.isSmooth ? 6.0 : 10.0);
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
    const float x = r.getX(), y = r.getY();
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
    return { r.getX() + 2.0f, r.getY() + 1.0f, 150.0f, r.getHeight() - 2.0f };
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
    auto head = Rectangle<float> (12.0f, 4.0f, laneLeft() - 12.0f, 16.0f);
    g.drawText (String (st.tracks.size()) + " TRACKS", head, Justification::centredLeft, false);
    if (st.editing)
    {
        g.setColour (accent);
        g.drawText ("updating...", head.withTrimmedRight (20.0f), Justification::centredRight, false);
    }
    if (statusText)
        if (const auto s = statusText(); s.isNotEmpty())
        {
            // what the AI is making for the song, over the lanes
            g.setFont (font (11.0f, lp::ui::Medium));
            g.setColour (s.contains ("didn't work") ? Colour (0xffff8f8f) : th.grassLight);
            g.drawText (s, Rectangle<float> (laneLeft(), 4.0f, laneRight() - laneLeft(), 16.0f), Justification::centredRight, true);
        }
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
            g.drawText (more + " (scroll)", Rectangle<float> (112.0f, 4.0f, laneLeft() - 120.0f, 16.0f), Justification::centredLeft, false);
        }
    }

    // ---- the lanes: where the song's sections are, then each track's sound in them
    const float lx = laneLeft(), lw = laneRight() - lx;
    const int totalBars = std::max (1, st.song.totalBars());
    auto xOfBar = [&] (double bar) { return lx + lw * (float) (bar / totalBars); };
    if (! st.song.sections.empty())
    {
        // a faint line where each section starts, down through every lane
        g.setColour (Colours::white.withAlpha (0.05f));
        for (auto& sc : st.song.sections)
            g.drawVerticalLine ((int) xOfBar (sc.startBar), 22.0f, (float) getHeight() - 32.0f);
    }

    for (int i = 0; i < (int) st.tracks.size() && i < StudioPlayer::maxTracks; ++i)
    {
        if (! rowShown (i)) continue;
        const auto& t = st.tracks[(size_t) i];
        auto& m = proc.studio.mix[(size_t) i];
        auto row = rowBounds (i);
        const bool picked = selectedTrack && selectedTrack() == i;
        if (picked || i == hoverRow)
        {
            g.setColour (Colours::white.withAlpha (picked ? 0.07f : 0.04f));
            g.fillRoundedRectangle (row.withRight (lx - 6.0f), 5.0f);
        }
        if (picked)
        {
            g.setColour (accent);
            g.fillRoundedRectangle (Rectangle<float> (row.getX() - 6.0f, row.getY() + 3.0f, 3.0f, row.getHeight() - 6.0f), 1.5f);
        }

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
        auto nameArea = Rectangle<float> (row.getX() + 68.0f, row.getY(), colBounds (i, Volume).getX() - 22.0f - row.getX() - 72.0f, row.getHeight());
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
            lp::ui::drawStereoMeter (g, Rectangle<float> (r.getX() - 16.0f, row.getY() + 2.0f, 9.0f, row.getHeight() - 4.0f), meterL[(size_t) i], meterR[(size_t) i]);
        }

        // its lane: a clip in each section it plays in, with its real waveform as a thin line
        const auto* env = envelopeOf (t.stem);
        const bool muted = m.mute.load();
        const Colour clipCol = (picked ? accent : accent.withSaturation (accent.getSaturation() * 0.6f)).withAlpha (muted ? 0.12f : picked ? 0.42f : 0.26f);
        const Colour waveCol = (picked ? Colours::white : th.grassLight).withAlpha (muted ? 0.2f : 0.7f);
        if (env == nullptr || env->peaks.empty() || env->rate <= 0.0)
        {
            g.setColour (Colours::white.withAlpha (0.04f));
            g.fillRoundedRectangle (Rectangle<float> (lx, row.getY() + 1.0f, lw, row.getHeight() - 2.0f), 4.0f);
            continue;
        }
        const double barSamples = 4.0 * 60.0 / std::max (40.0, st.song.bpm) * env->rate;
        const int n = (int) env->peaks.size();
        auto bucketOf = [&] (double bar) { return jlimit (0, n, (int) (bar * barSamples / (double) std::max<int64> (1, env->length) * n)); };
        float top = 0.0f;
        for (float v : env->peaks) top = std::max (top, v);
        if (top <= 0.0f) continue;
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
        for (size_t si = 0; si < st.song.sections.size(); ++si)
        {
            const auto& sc = st.song.sections[si];
            if (avg[si] < loudest * 0.12f) continue; // (only a tail or silence here)
            const float x0 = xOfBar (sc.startBar) + 1.0f, x1 = xOfBar (sc.startBar + sc.bars) - 1.0f;
            auto clip = Rectangle<float> (x0, row.getY() + 1.0f, x1 - x0, row.getHeight() - 2.0f);
            g.setColour (clipCol);
            g.fillRoundedRectangle (clip, 3.0f);
            // one thin line per pixel: the loudest moment under it
            g.setColour (waveCol);
            const float mid = clip.getCentreY(), half = clip.getHeight() * 0.5f - 2.0f;
            for (float x = clip.getX() + 1.0f; x < clip.getRight() - 1.0f; x += 1.0f)
            {
                const double barA = sc.startBar + (x - clip.getX()) / clip.getWidth() * sc.bars;
                const double barB = sc.startBar + (x + 1.0f - clip.getX()) / clip.getWidth() * sc.bars;
                const int ka = bucketOf (barA), kb = std::max (ka + 1, bucketOf (barB));
                float v = 0.0f;
                for (int k = ka; k < kb && k < n; ++k) v = std::max (v, env->peaks[(size_t) k]);
                const float h = std::max (0.5f, v / top * half);
                g.drawVerticalLine ((int) x, mid - h, mid + h);
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
                auto clip = Rectangle<float> (x0 + 1.0f, rb.getY() + 1.0f, std::max (8.0f, x1 - x0 - 2.0f), rb.getHeight() - 2.0f);
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

    // where the song is playing, down through the lanes
    if (proc.studio.length() > 0 && proc.songIsPlaying())
    {
        const float frac = (float) jlimit (0.0, 1.0, proc.playheadPos.load() / (double) proc.studio.length());
        g.setColour (Colours::white.withAlpha (0.9f));
        g.drawVerticalLine ((int) (lx + lw * frac), 22.0f, (float) getHeight() - 32.0f);
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
    return { (float) getWidth() - 7.0f, 26.0f, 5.0f, (float) visibleRows() * 24.0f - 2.0f };
}

void StudioView::mouseDown (const MouseEvent& e)
{
    const auto st = proc.songJob.getStatus();
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
        if (e.mods.isPopupMenu())
        {
            const auto& tr = st.tracks[(size_t) i];
            PopupMenu m;
            m.addSectionHeader (String (tr.name).toUpperCase());
            m.addItem (2, "Swap: another sound for this part", tr.canSwap);
            m.addItem (3, tr.reversed ? "Play it forwards again" : "Reverse it", tr.canReverse);
            m.addItem (4, "Pitch up a semitone");
            m.addItem (5, "Pitch down a semitone");
            m.addSeparator();
            m.addItem (1, "Remove \"" + tr.name + "\" from the song", st.tracks.size() > 1);
            m.showMenuAsync (PopupMenu::Options(), [this, i] (int r)
            {
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
    if (draggingFeel >= 0) { setFeel (draggingFeel, e.position.x); return; }
    if (dragRow >= 0) { setFromX (dragRow, dragCol, e.position.x); repaint(); }
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
