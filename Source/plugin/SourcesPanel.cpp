#include "PluginEditor.h"

using namespace juce;
using lp::ui::font;

void SourcesPanel::open()
{
    rows = proc.songJob.packRows();
    scroll = 0;
    setVisible (true);
    toFront (false);
    repaint();
}

Rectangle<float> SourcesPanel::panel() const
{
    return getLocalBounds().toFloat().withSizeKeepingCentre (720.0f, 560.0f);
}

std::vector<SongJob::PackRow> SourcesPanel::shown() const
{
    std::vector<SongJob::PackRow> out;
    for (auto& r : rows)
        if (filter == 0 || r.source == filter - 1) out.push_back (r);
    return out;
}

Rectangle<float> SourcesPanel::rowAt (int i) const
{
    auto p = panel().reduced (28.0f, 0.0f);
    return { p.getX(), panel().getY() + 176.0f + (float) (i - scroll) * 34.0f, p.getWidth(), 32.0f };
}

void SourcesPanel::paint (Graphics& g)
{
    const auto& t = *th;
    g.setColour (Colours::black.withAlpha (0.6f));
    g.fillAll();
    auto p = panel();
    g.setColour (t.body);
    g.fillRoundedRectangle (p, 14.0f);
    g.setColour (t.line);
    g.drawRoundedRectangle (p.reduced (0.5f), 14.0f, 1.0f);
    auto in = p.reduced (28.0f, 22.0f);
    auto head = in.removeFromTop (30.0f);
    g.setFont (font (22.0f, lp::ui::Medium));
    g.setColour (t.ink);
    g.drawText ("Sample sources", head, Justification::centredLeft, false);
    g.setFont (font (20.0f));
    g.drawText ("x", head.removeFromRight (30.0f), Justification::centred, false);
    g.setFont (font (13.0f));
    g.setColour (t.inkDim);
    g.drawText ("Songs only use the packs switched on. WAVS packs start off: switch on the ones marked RF (royalty-free) in WAVS.",
                in.removeFromTop (34.0f), Justification::centredLeft, true);
    // filters and all on / off
    static const char* names[] = { "ALL", "SPLICE", "WAVS", "FOLDERS" };
    auto bar = Rectangle<float> (in.getX(), p.getY() + 120.0f, in.getWidth(), 30.0f);
    float x = bar.getX();
    for (int i = 0; i < 4; ++i)
    {
        const float w = GlyphArrangement::getStringWidth (lp::ui::mono (10.5f, false, 0.14f), names[i]) + 28.0f;
        auto c = Rectangle<float> (x, bar.getY(), w, bar.getHeight());
        g.setColour (i == filter ? Colour (0xffe9e9ec) : t.bodyLight);
        g.fillRoundedRectangle (c, 6.0f);
        g.setFont (lp::ui::mono (10.5f, false, 0.14f));
        g.setColour (i == filter ? Colour (0xff141416) : t.ink.withAlpha (0.8f));
        g.drawText (names[i], c, Justification::centred, false);
        x += w + 8.0f;
    }
    for (auto [label, dx] : { std::pair { "ALL OFF", 0.0f }, { "ALL ON", 96.0f } })
    {
        auto c = Rectangle<float> (bar.getRight() - 86.0f - dx, bar.getY(), 86.0f, bar.getHeight());
        g.setColour (t.bodyLight);
        g.fillRoundedRectangle (c, 6.0f);
        g.setColour (t.ink.withAlpha (0.8f));
        g.drawText (label, c, Justification::centred, false);
    }
    // the packs
    const auto list = shown();
    Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (Rectangle<float> (p.getX(), p.getY() + 172.0f, p.getWidth(), p.getHeight() - 186.0f).toNearestInt());
    if (list.empty())
    {
        g.setFont (font (14.0f));
        g.setColour (t.inkDim);
        g.drawText (filter == 2 ? "No WAVS samples found yet. They're read from Documents > WAVS > Samples (press SCAN after downloading)."
                                : "Nothing here yet.", rowAt (0).withHeight (60.0f), Justification::centredLeft, true);
    }
    for (int i = 0; i < (int) list.size(); ++i)
    {
        auto r = rowAt (i);
        if (r.getBottom() < p.getY() + 172.0f || r.getY() > p.getBottom()) continue;
        const auto& row = list[(size_t) i];
        g.setColour (i % 2 == 0 ? t.bodyDark.withAlpha (0.6f) : Colours::transparentBlack);
        g.fillRoundedRectangle (r, 6.0f);
        static const char* src[] = { "SPLICE", "WAVS", "FOLDER" };
        g.setFont (lp::ui::mono (9.5f, false, 0.12f));
        g.setColour (t.inkDim);
        g.drawText (src[jlimit (0, 2, row.source)], r.withTrimmedLeft (12.0f).withWidth (70.0f), Justification::centredLeft, false);
        g.setFont (font (13.5f, lp::ui::Medium));
        g.setColour (row.on ? t.ink : t.inkDim);
        g.drawText (row.name, r.withTrimmedLeft (86.0f).withTrimmedRight (150.0f), Justification::centredLeft, true);
        g.setFont (lp::ui::mono (10.0f));
        g.setColour (t.inkDim);
        g.drawText (String (row.count) + " SOUNDS", r.withTrimmedRight (56.0f).withLeft (r.getRight() - 150.0f), Justification::centredRight, false);
        auto sw = Rectangle<float> (r.getRight() - 44.0f, r.getCentreY() - 8.5f, 30.0f, 17.0f);
        g.setColour (row.on ? t.grass : Colour (0xff3a3a40));
        g.fillRoundedRectangle (sw, 8.5f);
        g.setColour (Colours::white);
        g.fillEllipse (row.on ? sw.getRight() - 15.0f : sw.getX() + 2.0f, sw.getY() + 2.0f, 13.0f, 13.0f);
    }
}

void SourcesPanel::mouseUp (const MouseEvent& e)
{
    auto p = panel();
    const auto pos = e.position;
    if (! p.contains (pos) || Rectangle<float> (p.getRight() - 60.0f, p.getY(), 60.0f, 60.0f).contains (pos))
    {
        setVisible (false);
        return;
    }
    // filters
    static const char* names[] = { "ALL", "SPLICE", "WAVS", "FOLDERS" };
    float x = p.getX() + 28.0f;
    const float by = p.getY() + 120.0f;
    for (int i = 0; i < 4; ++i)
    {
        const float w = GlyphArrangement::getStringWidth (lp::ui::mono (10.5f, false, 0.14f), names[i]) + 28.0f;
        if (Rectangle<float> (x, by, w, 30.0f).contains (pos)) { filter = i; scroll = 0; repaint(); return; }
        x += w + 8.0f;
    }
    // all on / all off (for what's showing)
    const float right = p.getRight() - 28.0f;
    for (auto [on, dx] : { std::pair { false, 0.0f }, { true, 96.0f } })
        if (Rectangle<float> (right - 86.0f - dx, by, 86.0f, 30.0f).contains (pos))
        {
            for (auto& r : shown()) proc.songJob.setPackOn (r.source, r.name, on);
            rows = proc.songJob.packRows();
            repaint();
            return;
        }
    // a pack: flip it
    const auto list = shown();
    for (int i = 0; i < (int) list.size(); ++i)
        if (rowAt (i).contains (pos) && pos.y > p.getY() + 172.0f)
        {
            proc.songJob.setPackOn (list[(size_t) i].source, list[(size_t) i].name, ! list[(size_t) i].on);
            rows = proc.songJob.packRows();
            repaint();
            return;
        }
}

void SourcesPanel::mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w)
{
    const int visible = (int) ((panel().getHeight() - 190.0f) / 34.0f);
    scroll = jlimit (0, std::max (0, (int) shown().size() - visible), scroll + (w.deltaY < 0 ? 2 : -2));
    repaint();
}
