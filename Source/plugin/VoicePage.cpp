#include "PluginEditor.h"

using namespace juce;
using lp::ui::font;

// The Voice page (experimental): your lyrics and a style, sung over a section of your song in a voice you
// have permission to use, by the voice helper (a separate app on this computer). Left: the words and the
// style. Right: which part of the song, which voice, how it's made; then what's happening, live, and the
// versions to hear with the song and drag out. Nothing here waits on the helper: when it isn't running,
// the page says so and the rest of Loopwright carries on.

namespace
{
    void card (Graphics& g, Rectangle<float> r, const lp::ui::Theme& t, bool glow)
    {
        g.setGradientFill (ColourGradient (Colour (0xff16161c), r.getX(), r.getY(), Colour (0xff0d0d12), r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, 16.0f);
        if (glow)
        {
            Graphics::ScopedSaveState s (g);
            Path clip; clip.addRoundedRectangle (r, 16.0f);
            g.reduceClipRegion (clip);
            g.setGradientFill (ColourGradient (t.grass.withAlpha (0.16f), r.getCentreX(), r.getBottom() + 40.0f,
                                               t.grass.withAlpha (0.0f), r.getCentreX(), r.getBottom() - r.getHeight() * 0.55f, true));
            g.fillRect (r);
        }
        g.setColour (Colours::white.withAlpha (0.07f));
        g.drawRoundedRectangle (r.reduced (0.5f), 16.0f, 1.0f);
    }
    void label (Graphics& g, const String& s, Rectangle<float> r, const lp::ui::Theme& t)
    {
        g.setFont (lp::ui::mono (9.5f, true, 0.16f));
        g.setColour (t.grassLight);
        g.drawText (s, r, Justification::centredLeft, false);
    }
    // a meter: a name, a bar, a number
    void meter (Graphics& g, Rectangle<float> r, const String& name, int pct, const lp::ui::Theme& t)
    {
        g.setFont (lp::ui::mono (9.0f, false, 0.12f));
        g.setColour (t.inkDim);
        g.drawText (name, r.removeFromLeft (GlyphArrangement::getStringWidth (g.getCurrentFont(), name) + 8.0f), Justification::centredLeft, false);
        g.drawText (String (pct) + "%", r.removeFromRight (34.0f), Justification::centredRight, false);
        auto bar = r.withSizeKeepingCentre (r.getWidth(), 5.0f);
        g.setColour (Colours::white.withAlpha (0.07f));
        g.fillRoundedRectangle (bar, 2.5f);
        g.setGradientFill (ColourGradient (t.grassDark, bar.getX(), 0, t.grassLight, bar.getRight(), 0, false));
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * (float) jlimit (0, 100, pct) / 100.0f), 2.5f);
    }
    String clock (double secs)
    {
        const int s = jmax (0, (int) secs);
        return String (s / 60) + ":" + String (s % 60).paddedLeft ('0', 2);
    }
}

VoicePage::VoicePage (LivePlanetProcessor& p, const lp::ui::Theme*& theme) : proc (p), th (theme)
{
    auto setupEditor = [this] (TextEditor& e, bool multi, const String& hint)
    {
        e.setMultiLine (multi, true);
        e.setReturnKeyStartsNewLine (multi);
        e.setScrollbarsShown (multi);
        e.setTextToShowWhenEmpty (hint, Colours::white.withAlpha (0.28f));
        e.setIndents (14, 12);
        addAndMakeVisible (e);
    };
    setupEditor (lyrics, true, "[Chorus]\nOne line per phrase.\nStart each part with a tag like [Verse] or [Chorus].");
    lyrics.setFont (font (17.0f, lp::ui::Light));
    lyrics.onTextChange = [this] { repaint(); };
    setupEditor (style, false, "smooth R&B lead, catchy, long held notes");
    style.setFont (font (14.5f, lp::ui::Light));
    style.onTextChange = [this] { markStyles(); };

    for (auto* tag : { "[Verse]", "[Chorus]", "[Hook]", "[Bridge]" })
    {
        auto* b = tagChips.add (new TextButton (tag));
        b->setWantsKeyboardFocus (false);
        b->onClick = [this, b]
        {
            auto v = lyrics.getText();
            lyrics.setText (v + (v.isNotEmpty() && ! v.endsWith ("\n") ? "\n\n" : "") + b->getButtonText() + "\n");
            lyrics.moveCaretToEnd();
            lyrics.grabKeyboardFocus();
        };
        addAndMakeVisible (b);
    }
    for (auto* s : { "R&B", "soulful", "breathy", "falsetto", "rhythmic", "long held notes", "tenor", "baritone", "stacked hook" })
    {
        auto* b = styleChips.add (new TextButton (s));
        b->setWantsKeyboardFocus (false);
        b->onClick = [this, b]
        {
            StringArray parts;
            parts.addTokens (style.getText(), ",", "");
            parts.trim();
            parts.removeEmptyStrings();
            const auto w = b->getButtonText();
            if (parts.contains (w)) parts.removeString (w); else parts.add (w);
            style.setText (parts.joinIntoString (", "));
        };
        addAndMakeVisible (b);
    }

    for (auto* c : { &section, &voice })
    {
        c->setWantsKeyboardFocus (false);
        addAndMakeVisible (*c);
    }
    addVoice.setWantsKeyboardFocus (false);
    addAndMakeVisible (addVoice);
    addAndMakeVisible (takes);
    addAndMakeVisible (versions);
    auto knob = [this] (Slider& s, const char* name, double lo, double hi, double v, std::function<String (double)> text)
    {
        s.setName (name);
        s.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
        s.setRotaryParameters (MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
        s.setRange (lo, hi, hi > 1.0 ? 1.0 : 0.0);
        s.setValue (v, dontSendNotification);
        s.getProperties().set ("size", 52);
        s.textFromValueFunction = std::move (text);
        s.setWantsKeyboardFocus (false);
        addAndMakeVisible (s);
    };
    knob (tuning, "tuning", 0.0, 1.0, 0.95, [] (double v) { return String (roundToInt (v * 100.0)) + "%"; });
    knob (runs, "runs", 0.0, 8.0, 3.0, [] (double v) { return String (roundToInt (v)); });
    for (auto* b : { &clean, &consent })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (*b);
    }
    clean.setToggleState (true, dontSendNotification);
    consent.onClick = [this] { refreshButtons(); };
    create.getProperties().set ("primary", true);
    create.setWantsKeyboardFocus (false);
    addAndMakeVisible (create);
    stop.setWantsKeyboardFocus (false);
    addChildComponent (stop);
    startTimerHz (30);
}

VoicePage::~VoicePage() = default;

void VoicePage::markStyles()
{
    StringArray parts;
    parts.addTokens (style.getText(), ",", "");
    parts.trim();
    for (auto* b : styleChips)
    {
        const bool on = parts.contains (b->getButtonText());
        if ((bool) b->getProperties()["primary"] != on) { b->getProperties().set ("primary", on); b->repaint(); }
    }
}

void VoicePage::refreshButtons()
{
    const bool working = ! snap.jobs.empty() && snap.jobs.front().live();
    create.setEnabled (snap.connected && consent.getToggleState() && ! working);
    stop.setVisible (working);
}

void VoicePage::setSnapshot (const lp::voice::Snapshot& s)
{
    snap = s;
    StringArray had;
    for (int i = 0; i < voice.getNumItems(); ++i) had.add (voice.getItemText (i));
    if (had != snap.voices)
    {
        const auto keep = voice.getText();
        voice.clear (dontSendNotification);
        for (int i = 0; i < snap.voices.size(); ++i) voice.addItem (snap.voices[i], i + 1);
        voice.setText (snap.voices.contains (keep) ? keep : snap.voices[0], dontSendNotification);
    }
    refreshButtons();
    resized();
    repaint();
}

void VoicePage::setSong (const juce::StringArray& sectionNames, int pick, const String& keyText, const String& bpmText)
{
    section.clear (dontSendNotification);
    for (int i = 0; i < sectionNames.size(); ++i) section.addItem (sectionNames[i], i + 1);
    section.setSelectedItemIndex (jlimit (0, jmax (0, sectionNames.size() - 1), pick), dontSendNotification);
    songKey = keyText;
    songBpm = bpmText;
    repaint();
}

void VoicePage::timerCallback()
{
    phase += 1.0f / 30.0f;
    // only the moving parts are drawn again
    repaint (progressCard().toNearestInt());
}

Rectangle<float> VoicePage::lyricsCard() const   { auto b = getLocalBounds().toFloat(); return { b.getX(), b.getY(), b.getWidth() * 0.47f, b.getHeight() - 196.0f }; }
Rectangle<float> VoicePage::styleCard() const    { auto l = lyricsCard(); return { l.getX(), l.getBottom() + 12.0f, l.getWidth(), (float) getHeight() - l.getBottom() - 12.0f }; }
Rectangle<float> VoicePage::setupCard() const    { auto l = lyricsCard(); const float x = l.getRight() + 14.0f; return { x, 0.0f, (float) getWidth() - x, 324.0f }; }
Rectangle<float> VoicePage::progressCard() const { auto s = setupCard(); return { s.getX(), s.getBottom() + 12.0f, s.getWidth(), (float) getHeight() - s.getBottom() - 12.0f }; }

void VoicePage::resized()
{
    // ---- words
    {
        auto c = lyricsCard().reduced (18.0f, 16.0f).toNearestInt();
        c.removeFromTop (24);
        auto chips = c.removeFromBottom (30);
        c.removeFromBottom (30); // (how many lines, and how long a section suits them)
        lyrics.setBounds (c);
        for (auto* b : tagChips) { b->setBounds (chips.removeFromLeft (84)); chips.removeFromLeft (6); }
    }
    {
        auto c = styleCard().reduced (18.0f, 16.0f).toNearestInt();
        c.removeFromTop (24);
        style.setBounds (c.removeFromTop (40));
        c.removeFromTop (10);
        auto row = c.removeFromTop (28);
        for (auto* b : styleChips)
        {
            const int w = (int) GlyphArrangement::getStringWidth (font (13.0f, lp::ui::Medium), b->getButtonText()) + 28;
            if (w > row.getWidth()) { c.removeFromTop (6); row = c.removeFromTop (28); }
            b->setBounds (row.removeFromLeft (w));
            row.removeFromLeft (6);
        }
    }
    // ---- the song, the voice, how
    {
        auto c = setupCard().reduced (18.0f, 16.0f).toNearestInt();
        c.removeFromTop (24);
        auto r1 = c.removeFromTop (36);
        section.setBounds (r1.removeFromLeft (r1.getWidth() / 2 - 5));
        r1.removeFromLeft (10);
        addVoice.setBounds (r1.removeFromRight (64));
        r1.removeFromRight (6);
        voice.setBounds (r1);
        c.removeFromTop (30); // key / tempo / length, written in
        auto r3 = c.removeFromTop (92);
        auto segs = r3.removeFromLeft (r3.getWidth() / 2);
        takes.setBounds (segs.removeFromTop (30).withTrimmedLeft (70));
        segs.removeFromTop (12);
        versions.setBounds (segs.removeFromTop (30).withTrimmedLeft (70));
        r3.removeFromLeft (10);
        tuning.setBounds (r3.removeFromLeft (r3.getWidth() / 2));
        runs.setBounds (r3);
        c.removeFromTop (2);
        clean.setBounds (c.removeFromTop (26));
        consent.setBounds (c.removeFromTop (26));
        c.removeFromTop (8);
        auto r6 = c.removeFromTop (40);
        stop.setBounds (r6.removeFromRight (90));
        if (stop.isVisible()) r6.removeFromRight (8);
        create.setBounds (r6);
    }
    // ---- the versions: a row each
    {
        const auto area = resultsArea();
        for (auto* r : rows) r->setVisible (false);
        int i = 0;
        if (! snap.jobs.empty())
            for (auto& res : snap.jobs.front().results)
            {
                if (i >= rows.size()) addAndMakeVisible (rows.add (new ResultRow (proc, th)));
                auto* row = rows[i];
                row->set (res);
                row->setBounds (area.withY (area.getY() + (float) i * 62.0f).withHeight (56.0f).toNearestInt());
                row->setVisible (true);
                ++i;
            }
    }
}

Rectangle<float> VoicePage::resultsArea() const
{
    auto c = progressCard().reduced (18.0f, 16.0f);
    c.removeFromTop (24);
    c.removeFromTop (78 + 8 + 46 + 10); // the wave, then the steps
    return c;
}

void VoicePage::paint (Graphics& g)
{
    const auto& t = *th;
    // ---- lyrics
    {
        const auto r = lyricsCard();
        card (g, r, t, true);
        auto in = r.reduced (18.0f, 16.0f);
        label (g, "LYRICS", in.removeFromTop (18.0f), t);
        StringArray lines;
        lines.addLines (lyrics.getText());
        int n = 0;
        for (auto& l : lines) if (l.trim().isNotEmpty() && ! l.trim().startsWith ("[")) ++n;
        g.setFont (font (12.5f, lp::ui::Light));
        g.setColour (t.inkDim);
        const auto fit = n == 0 ? String ("Type your words: one sung phrase per line.")
                                : String (n) + (n == 1 ? " line" : " lines") + String::fromUTF8 (" \xc2\xb7 about ") + String (n * 8)
                                  + " seconds of song suits them, so no word is rushed or repeated";
        g.drawText (fit, in.withTrimmedBottom (30.0f).removeFromBottom (24.0f), Justification::centredLeft, true);
    }
    // ---- style
    {
        const auto r = styleCard();
        card (g, r, t, false);
        label (g, "VOCAL STYLE", r.reduced (18.0f, 16.0f).removeFromTop (18.0f), t);
    }
    // ---- set-up
    {
        const auto r = setupCard();
        card (g, r, t, false);
        auto in = r.reduced (18.0f, 16.0f);
        auto head = in.removeFromTop (18.0f);
        label (g, "THE SONG AND THE VOICE", head, t);
        // the lab badge
        auto badge = Rectangle<float> (head.getRight() - 96.0f, head.getY(), 96.0f, 18.0f);
        g.setColour (t.grass.withAlpha (0.16f));
        g.fillRoundedRectangle (badge, 9.0f);
        g.setFont (lp::ui::mono (8.5f, true, 0.14f));
        g.setColour (t.grassLight);
        g.drawText ("EXPERIMENTAL", badge, Justification::centred, false);
        in.removeFromTop (6.0f + 36.0f);
        auto facts = in.removeFromTop (30.0f);
        g.setFont (lp::ui::mono (10.0f, false, 0.1f));
        g.setColour (t.inkDim);
        g.drawText (songKey + String::fromUTF8 ("   \xc2\xb7   ") + songBpm + String::fromUTF8 ("   \xc2\xb7   sung in the song's key, at its tempo"),
                    facts, Justification::centredLeft, true);
        g.setFont (lp::ui::mono (9.0f, false, 0.12f));
        auto segs = in.removeFromTop (92.0f);
        g.drawText ("TAKES", segs.withHeight (30.0f), Justification::centredLeft, false);
        g.drawText ("VERSIONS", segs.withY (segs.getY() + 42.0f).withHeight (30.0f), Justification::centredLeft, false);
    }
    // ---- what's happening
    {
        const auto r = progressCard();
        card (g, r, t, true);
        auto in = r.reduced (18.0f, 16.0f);
        const lp::voice::Job* j = snap.jobs.empty() ? nullptr : &snap.jobs.front();
        auto head = in.removeFromTop (18.0f);
        String title = ! snap.connected ? String ("VOICE HELPER ISN'T RUNNING")
                     : j == nullptr ? String ("READY")
                     : j->state == "done" ? "READY: " + j->title.toUpperCase()
                     : j->state == "error" ? String ("SOMETHING WENT WRONG")
                     : j->state == "cancelled" ? String ("STOPPED")
                     : j->state == "queued" ? String ("WAITING FOR THE ONE BEFORE")
                     : "SINGING: " + j->title.toUpperCase();
        label (g, title, head.withTrimmedRight (70.0f), t);
        if (j != nullptr && j->live())
        {
            g.setFont (lp::ui::mono (10.0f, true, 0.1f));
            g.setColour (t.ink);
            g.drawText (clock (Time::getCurrentTime().toMilliseconds() / 1000.0 - j->created), head, Justification::centredRight, false);
        }
        // the wave: four lines, faster and bigger as the song gets further along
        auto wave = in.removeFromTop (78.0f);
        {
            const bool live = j != nullptr && j->live();
            int at = 0;
            if (j != nullptr) for (int k = 0; k < (int) snap.stages.size(); ++k) if (snap.stages[(size_t) k].first == j->stage) at = k;
            const float amp = live ? 0.25f + 0.12f * (float) at : j != nullptr && j->state == "done" ? 0.55f : 0.08f;
            const Colour cols[4] = { t.grassLight, t.grass, Colour (0xff5fbf8f), t.grassDark };
            for (int n = 0; n < 4; ++n)
            {
                Path p;
                for (float x = 0.0f; x <= wave.getWidth(); x += 4.0f)
                {
                    const float q = x / wave.getWidth(), env = std::sin (MathConstants<float>::pi * q);
                    const float y = wave.getCentreY() + std::sin (q * (7.0f + n * 2.0f) + phase * (live ? 2.4f : 0.5f) * (n % 2 ? 1.0f : -1.0f) + n)
                                    * std::cos (q * 3.0f + n) * env * wave.getHeight() * 0.48f * amp * (1.0f - n * 0.15f);
                    if (x == 0.0f) p.startNewSubPath (wave.getX() + x, y); else p.lineTo (wave.getX() + x, y);
                }
                g.setColour (cols[n].withAlpha (0.85f - n * 0.15f));
                g.strokePath (p, PathStrokeType (2.6f - n * 0.4f, PathStrokeType::curved, PathStrokeType::rounded));
            }
        }
        in.removeFromTop (8.0f);
        if (! snap.connected)
        {
            g.setFont (font (13.5f, lp::ui::Light));
            g.setColour (t.ink.withAlpha (0.8f));
            g.drawFittedText ("Voice needs the Loopwright Voice helper running on this computer (it does the singing; the plugin never "
                              "does). Start it, and this page finds it by itself. Everything else in Loopwright works without it.",
                              in.withHeight (80.0f).toNearestInt(), Justification::topLeft, 4, 1.0f);
            if (snap.problem.isNotEmpty())
            {
                g.setFont (lp::ui::mono (9.5f, false, 0.08f));
                g.setColour (t.inkDim);
                g.drawText (snap.problem, in.withY (in.getY() + 88.0f).withHeight (16.0f), Justification::centredLeft, true);
            }
            return;
        }
        // the stages: a row of steps, each lit as it's reached
        int at = -1;
        if (j != nullptr) for (int k = 0; k < (int) snap.stages.size(); ++k) if (snap.stages[(size_t) k].first == j->stage) at = k;
        std::vector<int> shown;
        for (int k = 0; k < (int) snap.stages.size(); ++k)
            if (! (snap.stages[(size_t) k].first == "clean" && j != nullptr && ! j->clean)) shown.push_back (k);
        auto steps = in.removeFromTop (46.0f);
        const float sw = steps.getWidth() / (float) jmax (1, (int) shown.size());
        for (size_t s = 0; s < shown.size(); ++s)
        {
            const int k = shown[s];
            // (our own short names for the helper's stages; its own words for any we don't know)
            const auto& key = snap.stages[(size_t) k].first;
            const String text = key == "sing" ? "Melody" : key == "score" ? "Words & key" : key == "clean" ? "Clean up"
                              : key == "voice" ? "Your voice" : key == "mix" ? "Mix" : snap.stages[(size_t) k].second;
            auto cell = Rectangle<float> (steps.getX() + (float) s * sw, steps.getY(), sw, steps.getHeight());
            const bool done = j != nullptr && (j->state == "done" || (at >= 0 && k < at));
            const bool now = j != nullptr && j->live() && k == at;
            if (s + 1 < shown.size()) // the line on to the next step
            {
                g.setColour (done ? Colour (0xff5fbf8f).withAlpha (0.6f) : Colours::white.withAlpha (0.08f));
                g.fillRect (Rectangle<float> (cell.getCentreX() + 10.0f, cell.getY() + 8.0f, sw - 20.0f, 2.0f));
            }
            auto dot = Rectangle<float> (12.0f, 12.0f).withCentre ({ cell.getCentreX(), cell.getY() + 9.0f });
            if (now)
            {
                const float pulse = 0.5f + 0.5f * std::sin (phase * 5.0f);
                g.setColour (t.grass.withAlpha (0.3f * pulse));
                g.fillEllipse (dot.expanded (6.0f));
            }
            g.setColour (done ? Colour (0xff5fbf8f) : now ? t.grass : Colours::white.withAlpha (0.12f));
            g.fillEllipse (dot);
            g.setFont (font (11.5f, now ? lp::ui::Medium : lp::ui::Light));
            g.setColour (now || done ? t.ink : t.inkDim);
            g.drawText (text, cell.withTrimmedTop (20.0f).withHeight (14.0f), Justification::centred, true);
            if (now && j->note.isNotEmpty())
            {
                g.setFont (lp::ui::mono (8.5f, false, 0.08f));
                g.setColour (t.inkDim);
                g.drawText (j->note.toUpperCase(), cell.withTrimmedTop (34.0f), Justification::centred, true);
            }
        }
        auto list = in;
        if (j != nullptr && j->state == "error")
        {
            g.setFont (font (12.5f, lp::ui::Light));
            g.setColour (Colour (0xffff8f8f));
            g.drawFittedText (j->error, list.removeFromTop (48.0f).toNearestInt(), Justification::topLeft, 3, 1.0f);
        }
        if (j == nullptr || j->results.empty())
        {
            g.setFont (font (13.0f, lp::ui::Light));
            g.setColour (t.inkDim);
            g.drawFittedText (j == nullptr ? String ("Type your words, pick a part of your song and a voice, and press Create.")
                                           : String ("Your versions appear here as each one is finished."),
                              resultsArea().withHeight (40.0f).toNearestInt(), Justification::topLeft, 2, 1.0f);
        }
    }
}

// ---- one finished version: hear it with the song, see how it did, drag it out, or make it a track
VoicePage::ResultRow::ResultRow (LivePlanetProcessor& p, const lp::ui::Theme*& theme)
    : proc (p), th (theme), drag ("drag vocal", [this] { return vocalFile.existsAsFile() ? StringArray (vocalFile.getFullPathName()) : StringArray(); })
{
    play.setWantsKeyboardFocus (false);
    play.getProperties().set ("primary", true);
    addAndMakeVisible (play);
    toTrack.setWantsKeyboardFocus (false);
    addAndMakeVisible (toTrack);
    addAndMakeVisible (drag);
}

void VoicePage::ResultRow::set (const lp::voice::Result& r)
{
    res = r;
    repaint();
}

void VoicePage::ResultRow::resized()
{
    auto b = getLocalBounds().reduced (12, 10);
    play.setBounds (b.removeFromLeft (38).withSizeKeepingCentre (38, 38));
    toTrack.setBounds (b.removeFromRight (104).withSizeKeepingCentre (104, 30));
    b.removeFromRight (6);
    drag.setBounds (b.removeFromRight (110).withSizeKeepingCentre (110, 30));
}

void VoicePage::ResultRow::paint (Graphics& g)
{
    const auto& t = *th;
    auto r = getLocalBounds().toFloat();
    g.setColour (Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (r, 12.0f);
    g.setColour (Colours::white.withAlpha (0.06f));
    g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, 1.0f);
    auto in = r.reduced (12.0f, 9.0f).withTrimmedLeft (52.0f).withTrimmedRight (236.0f);
    g.setFont (font (14.0f, lp::ui::Medium));
    g.setColour (t.ink);
    g.drawText ("Version " + String (res.n), in.removeFromTop (18.0f), Justification::centredLeft, false);
    auto m = in.removeFromTop (20.0f);
    meter (g, m.removeFromLeft (m.getWidth() / 2 - 6.0f), "WORDS", res.words, t);
    m.removeFromLeft (12.0f);
    meter (g, m, "IN KEY", res.key, t);
}
