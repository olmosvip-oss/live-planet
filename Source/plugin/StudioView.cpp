#include "PluginEditor.h"

using namespace juce;
using lp::ui::font;

// The studio: one row per track, showing where its sound came from, with a mixer and remix tools.

Rectangle<float> StudioView::rowBounds (int row) const
{
    return { 10.0f, 30.0f + (float) row * 27.0f, (float) getWidth() - 20.0f, 25.0f };
}

Rectangle<float> StudioView::colBounds (int row, Col c) const
{
    auto r = rowBounds (row);
    const float x = r.getX(), y = r.getY(), h = r.getHeight();
    switch (c)
    {
        case Mute:      return { x + 2, y + 3, 19, 19 };
        case Solo:      return { x + 24, y + 3, 19, 19 };
        case Volume:    return { x + 262, y + 8, 86, 9 };
        case Pan:       return { x + 358, y + 8, 46, 9 };
        case Tone:      return { x + 414, y + 8, 56, 9 };
        case Swap:      return { x + 480, y + 2, 38, h - 4 };
        case Reverse:   return { x + 522, y + 2, 34, h - 4 };
        case PitchDown: return { x + 560, y + 2, 20, h - 4 };
        case PitchUp:   return { r.getRight() - 22, y + 2, 20, h - 4 };
        default:        return {};
    }
}

StudioView::Col StudioView::colAt (int row, Point<float> p) const
{
    for (Col c : { Mute, Solo, Volume, Pan, Tone, Swap, Reverse, PitchDown, PitchUp })
        if (colBounds (row, c).expanded (2.0f, 4.0f).contains (p)) return c;
    return None;
}

void StudioView::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (Colour (0xea070a0e));
    g.fillRoundedRectangle (b, 14.0f);
    const auto st = proc.songJob.getStatus();
    const Colour txt (0xffe8eef3), dim (0x99e8eef3), accent (0xff27b4ea);

    g.setFont (font (11.0f, lp::ui::Medium, 0.12f));
    g.setColour (dim);
    auto head = Rectangle<float> (12.0f, 8.0f, b.getWidth() - 24.0f, 16.0f);
    g.drawText ("TRACKS", head, Justification::centredLeft, false);
    const std::pair<const char*, float> headers[] = { { "volume", 272.0f }, { "pan", 368.0f }, { "tone", 424.0f },
                                                      { "swap", 490.0f }, { "rev", 534.0f }, { "pitch", 578.0f } };
    for (auto& [label, x] : headers)
        g.drawText (label, Rectangle<float> (x, 8.0f, 60.0f, 16.0f), Justification::centredLeft, false);
    if (st.editing)
    {
        g.setColour (accent);
        g.drawText ("updating...", head, Justification::centredRight, false);
    }

    for (int i = 0; i < (int) st.tracks.size() && i < StudioPlayer::maxTracks; ++i)
    {
        const auto& t = st.tracks[(size_t) i];
        auto& m = proc.studio.mix[(size_t) i];
        auto row = rowBounds (i);
        if (i == hoverRow) { g.setColour (Colours::white.withAlpha (0.05f)); g.fillRoundedRectangle (row, 6.0f); }

        auto pill = [&g] (Rectangle<float> r, const String& label, bool on, Colour onCol)
        {
            g.setColour (on ? onCol : Colours::white.withAlpha (0.08f));
            g.fillRoundedRectangle (r, 5.0f);
            g.setColour (on ? Colour (0xff0b1015) : Colours::white.withAlpha (0.75f));
            g.setFont (font (11.0f, lp::ui::Medium));
            g.drawText (label, r, Justification::centred, false);
        };
        pill (colBounds (i, Mute), "M", m.mute.load(), Colour (0xfff2a33a));
        pill (colBounds (i, Solo), "S", m.solo.load(), Colour (0xff7fdcff));

        auto nameArea = Rectangle<float> (row.getX() + 50, row.getY(), 204, row.getHeight());
        g.setColour (txt);
        g.setFont (font (12.0f, lp::ui::Medium));
        String name = t.name;
        if (t.reversed) name << "  -  reversed";
        if (t.pitch != 0) name << "  -  " << (t.pitch > 0 ? "+" : "") << t.pitch << " st";
        g.drawText (name, nameArea.removeFromTop (13.0f), Justification::bottomLeft, true);
        g.setColour (t.source.startsWith ("splice") ? accent.brighter (0.3f) : dim);
        g.setFont (font (10.0f));
        g.drawText (t.source, nameArea, Justification::topLeft, true);

        auto bar = [&g, accent] (Rectangle<float> r, float v, bool centred)
        {
            g.setColour (Colours::white.withAlpha (0.1f));
            g.fillRoundedRectangle (r, 4.5f);
            g.setColour (accent);
            if (centred)
            {
                const float mid = r.getCentreX(), x = r.getX() + r.getWidth() * (v * 0.5f + 0.5f);
                g.fillRoundedRectangle (Rectangle<float> (std::min (mid, x), r.getY(), std::max (2.0f, std::abs (x - mid)), r.getHeight()), 4.0f);
            }
            else
                g.fillRoundedRectangle (r.withWidth (std::max (r.getHeight(), r.getWidth() * v)), 4.5f);
        };
        bar (colBounds (i, Volume), jmap (m.gainDb.load(), -30.0f, 6.0f, 0.0f, 1.0f), false);
        bar (colBounds (i, Pan), m.pan.load(), true);
        bar (colBounds (i, Tone), m.tone.load(), true);

        auto button = [&g] (Rectangle<float> r, const String& label, bool enabled, bool on)
        {
            g.setColour (Colours::white.withAlpha (! enabled ? 0.03f : (on ? 0.25f : 0.09f)));
            g.fillRoundedRectangle (r, 5.0f);
            g.setColour (Colours::white.withAlpha (enabled ? 0.85f : 0.25f));
            g.setFont (font (11.0f, lp::ui::Medium));
            g.drawText (label, r, Justification::centred, false);
        };
        button (colBounds (i, Swap), "swap", t.canSwap, false);
        button (colBounds (i, Reverse), "rev", t.canReverse, t.reversed);
        button (colBounds (i, PitchDown), "-", true, false);
        button (colBounds (i, PitchUp), "+", true, false);
    }

    // how the hovered track was made
    if (hoverRow >= 0 && hoverRow < (int) st.tracks.size())
    {
        g.setColour (dim);
        g.setFont (font (11.0f));
        g.drawText ("how it was made: " + st.tracks[(size_t) hoverRow].remix, Rectangle<float> (12.0f, b.getBottom() - 20.0f, b.getWidth() - 24.0f, 16.0f),
                    Justification::centredLeft, true);
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

void StudioView::mouseDown (const MouseEvent& e)
{
    const auto st = proc.songJob.getStatus();
    dragRow = -1;
    for (int i = 0; i < (int) st.tracks.size() && i < StudioPlayer::maxTracks; ++i)
    {
        if (! rowBounds (i).expanded (0.0f, 1.0f).contains (e.position)) continue;
        const auto c = colAt (i, e.position);
        auto& m = proc.studio.mix[(size_t) i];
        switch (c)
        {
            case Mute: m.mute = ! m.mute.load(); mixDirty = true; break;
            case Solo: m.solo = ! m.solo.load(); mixDirty = true; break;
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
            default: break;
        }
        if (onMixChanged) onMixChanged();
        break;
    }
    repaint();
}

void StudioView::mouseDrag (const MouseEvent& e)
{
    if (dragRow >= 0) { setFromX (dragRow, dragCol, e.position.x); repaint(); }
}

void StudioView::mouseMove (const MouseEvent& e)
{
    int h = -1;
    for (int i = 0; i < StudioPlayer::maxTracks; ++i)
        if (rowBounds (i).contains (e.position)) h = i;
    if (h != hoverRow) { hoverRow = h; repaint(); }
}
