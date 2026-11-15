#include "PluginEditor.h"
#include "License.h"

using namespace juce;
using lp::ui::font;

// Help: the "?" bubbles next to confusing parts, and the first-run tour.

namespace
{
    const lp::ui::Theme& currentTheme() { return lp::ui::theme (0); }

    // what a "?" shows: a title and a few lines, in the plugin's look
    class HelpBubble : public Component
    {
    public:
        HelpBubble (const String& title, const String& text, const lp::ui::Theme& t) : th (t)
        {
            AttributedString s;
            s.setLineSpacing (3.0f);
            if (title.isNotEmpty()) s.append (title + "\n", font (15.0f, lp::ui::Medium), t.ink);
            s.append (text, font (13.0f), t.ink.withAlpha (0.82f));
            layout.createLayoutWithBalancedLineLengths (s, 300.0f);
            setSize ((int) layout.getWidth() + 8, (int) layout.getHeight() + 6);
        }
        void paint (Graphics& g) override { layout.draw (g, getLocalBounds().toFloat().reduced (4.0f, 3.0f)); }
    private:
        const lp::ui::Theme& th;
        TextLayout layout;
    };
}

void HelpDot::paint (Graphics& g)
{
    const auto* lnf = dynamic_cast<LivePlanetLookAndFeel*> (&getLookAndFeel());
    const auto& t = lnf != nullptr ? *lnf->th : currentTheme();
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    const bool over = isMouseOver();
    g.setColour (over ? t.grass : Colours::white.withAlpha (0.09f));
    g.fillEllipse (r);
    g.setColour (over ? Colours::white : t.grassLight.withAlpha (0.9f));
    g.drawEllipse (r.reduced (0.5f), 1.0f);
    g.setFont (font (r.getHeight() * 0.72f, lp::ui::Medium));
    g.drawText ("?", r.translated (0.0f, -0.5f), Justification::centred, false);
}

void HelpDot::mouseUp (const MouseEvent& e)
{
    if (! getLocalBounds().contains (e.getPosition())) return;
    if (onClick) { onClick(); return; }
    const auto* lnf = dynamic_cast<LivePlanetLookAndFeel*> (&getLookAndFeel());
    const auto& t = lnf != nullptr ? *lnf->th : currentTheme();
    CallOutBox::launchAsynchronously (std::make_unique<HelpBubble> (heading, body, t), getScreenBounds(), nullptr);
}

// ---- the tour

Tour::Tour (std::vector<Step> s, std::function<void (int)> showPage, std::function<void()> done)
    : steps (std::move (s)), setPage (std::move (showPage)), onDone (std::move (done))
{
    setWantsKeyboardFocus (true);
    setInterceptsMouseClicks (true, true);
    for (auto* b : { &back, &next }) { b->setWantsKeyboardFocus (false); addAndMakeVisible (*b); }
    next.getProperties().set ("primary", true);
    back.onClick = [this] { go (at - 1); };
    next.onClick = [this]
    {
        if (at + 1 < (int) steps.size()) { go (at + 1); return; }
        if (onDone) onDone();   // (this deletes the tour)
    };
}

bool Tour::keyPressed (const KeyPress& k)
{
    if (k == KeyPress::rightKey || k == KeyPress::returnKey) { next.triggerClick(); return true; }
    if (k == KeyPress::leftKey && at > 0) { go (at - 1); return true; }
    return true; // nothing else gets through while the tour is on
}

void Tour::go (int i)
{
    at = jlimit (0, (int) steps.size() - 1, i);
    if (steps[(size_t) at].page >= 0 && setPage) setPage (steps[(size_t) at].page);
    spotNow = steps[(size_t) at].spot ? steps[(size_t) at].spot() : Rectangle<int>();
    back.setVisible (at > 0);
    next.setButtonText (at + 1 < (int) steps.size() ? "next" : "start making music");
    resized();
    repaint();
    grabKeyboardFocus();
}

// the card sits beside what's lit up (below it if there's room, else above; the middle when nothing is)
Rectangle<int> Tour::card() const
{
    const int w = 440, h = 210;
    if (spotNow.isEmpty()) return Rectangle<int> (w, h).withCentre (getLocalBounds().getCentre());
    int y = spotNow.getBottom() + 18;
    if (y + h > getHeight() - 12) y = spotNow.getY() - h - 18;
    if (y < 12) y = jlimit (12, getHeight() - h - 12, spotNow.getCentreY() - h / 2);
    const int x = jlimit (16, getWidth() - w - 16, spotNow.getCentreX() - w / 2);
    return { x, y, w, h };
}

void Tour::resized()
{
    if (steps.empty()) return;
    auto c = card().reduced (22, 18);
    auto row = c.removeFromBottom (34);
    next.setBounds (row.removeFromRight (at + 1 < (int) steps.size() ? 96 : 170));
    row.removeFromRight (8);
    back.setBounds (row.removeFromRight (80));
}

void Tour::paint (Graphics& g)
{
    const auto* lnf = dynamic_cast<LivePlanetLookAndFeel*> (&getLookAndFeel());
    const auto& t = lnf != nullptr ? *lnf->th : currentTheme();
    // the dimmed screen, with the part we're talking about cut out and outlined
    Path dim;
    dim.addRectangle (getLocalBounds().toFloat());
    if (! spotNow.isEmpty())
    {
        dim.addRoundedRectangle (spotNow.toFloat().expanded (6.0f), 14.0f);
        dim.setUsingNonZeroWinding (false);
    }
    g.setColour (Colours::black.withAlpha (0.72f));
    g.fillPath (dim);
    if (! spotNow.isEmpty())
    {
        g.setColour (t.grass);
        g.drawRoundedRectangle (spotNow.toFloat().expanded (6.0f), 14.0f, 2.0f);
    }
    // the card
    const auto c = card().toFloat();
    g.setColour (t.bodyDark.interpolatedWith (Colour (0xff101014), 0.55f));
    g.fillRoundedRectangle (c, 16.0f);
    g.setColour (Colours::white.withAlpha (0.1f));
    g.drawRoundedRectangle (c.reduced (0.5f), 16.0f, 1.0f);
    g.setColour (t.grass);
    g.fillRoundedRectangle (Rectangle<float> (c.getX() + 22.0f, c.getY() + 1.0f, 36.0f, 3.0f), 1.5f);
    auto in = c.reduced (22.0f, 18.0f);
    const auto& st = steps[(size_t) at];
    g.setFont (lp::ui::mono (9.5f, true, 0.16f));
    g.setColour (t.grassLight);
    g.drawText (String (at + 1) + " / " + String ((int) steps.size()), in.removeFromTop (14.0f), Justification::centredLeft, false);
    in.removeFromTop (6.0f);
    g.setFont (font (20.0f, lp::ui::Medium, -0.01f));
    g.setColour (t.ink);
    g.drawText (st.title, in.removeFromTop (26.0f), Justification::centredLeft, true);
    in.removeFromTop (6.0f);
    in.removeFromBottom (40.0f);
    AttributedString s;
    s.setLineSpacing (3.0f);
    s.append (st.text, font (13.5f), t.ink.withAlpha (0.85f));
    TextLayout tl;
    tl.createLayout (s, in.getWidth());
    tl.draw (g, in);
    // progress dots
    const float dy = c.getBottom() - 36.0f;
    for (int i = 0; i < (int) steps.size(); ++i)
    {
        g.setColour (i == at ? t.grass : Colours::white.withAlpha (0.18f));
        g.fillEllipse (c.getX() + 22.0f + (float) i * 12.0f, dy, 6.0f, 6.0f);
    }
}

// ---- the license window

namespace
{
    const lp::ui::Theme& themeOf (Component& c)
    {
        const auto* lnf = dynamic_cast<LivePlanetLookAndFeel*> (&c.getLookAndFeel());
        return lnf != nullptr ? *lnf->th : currentTheme();
    }
    // the site's "get your code again" page, from the Buy link (https://site/api/checkout -> https://site/license)
    String lostCodeUrl()
    {
        const auto buy = lp::license::buyUrl();
        if (buy.isEmpty()) return {};
        const URL u (buy);
        return u.getOrigin() + "/license";
    }
}

LicensePanel::LicensePanel (std::function<void()> closed, const String& n) : onClosed (std::move (closed)), note (n)
{
    setWantsKeyboardFocus (true);
    code.setFont (lp::ui::mono (13.0f, false, 0.02f));
    code.setIndents (14, 0);
    code.setJustification (Justification::centredLeft);
    code.setSelectAllWhenFocused (true);
    code.setColour (TextEditor::backgroundColourId, Colours::black.withAlpha (0.42f));
    code.setColour (TextEditor::outlineColourId, Colours::white.withAlpha (0.14f));
    code.setColour (TextEditor::textColourId, Colours::white);
    code.setTextToShowWhenEmpty ("Paste your code: LW1-...", Colours::white.withAlpha (0.32f));
    code.onReturnKey = [this] { activate(); };
    code.onTextChange = [this] { if (message.isNotEmpty()) { message = {}; repaint(); } };
    addAndMakeVisible (code);
    activateBtn.getProperties().set ("primary", true);
    activateBtn.onClick = [this] { activate(); };
    buyBtn.setButtonText ("Buy " + lp::license::price());
    buyBtn.onClick = [] { URL (lp::license::buyUrl()).launchInDefaultBrowser(); };
    closeBtn.getProperties().set ("primary", true);
    closeBtn.onClick = [this] { if (onClosed) onClosed(); };
    for (auto* b : { &activateBtn, &buyBtn, &closeBtn }) { b->setWantsKeyboardFocus (false); addAndMakeVisible (*b); }
    refreshState();
}

void LicensePanel::refreshState()
{
    licensed = lp::license::activatedTo().isNotEmpty();
    code.setVisible (! licensed);
    activateBtn.setVisible (! licensed);
    buyBtn.setVisible (! licensed && lp::license::buyUrl().isNotEmpty());
    closeBtn.setVisible (licensed);
    resized();
    repaint();
}

void LicensePanel::activate()
{
    const auto c = code.getText().trim();
    if (c.isEmpty()) { message = "Paste the code from your email or the thank-you page."; messageBad = true; repaint(); return; }
    if (! lp::license::activate (c)) { message = "That code didn't work. Copy the whole thing, starting with LW1-"; messageBad = true; repaint(); return; }
    message = {};
    refreshState();
}

Rectangle<int> LicensePanel::card() const
{
    const int w = juce::jmin (500, getWidth() - 40), h = licensed ? 300 : 438;
    return Rectangle<int> (w, h).withCentre (getLocalBounds().getCentre());
}

void LicensePanel::resized()
{
    auto c = card().reduced (34, 30);
    closeX = Rectangle<int> (card().getRight() - 50, card().getY() + 18, 30, 30);
    auto bottom = c.removeFromBottom (licensed ? 46 : 74);
    if (licensed) { closeBtn.setBounds (bottom.withSizeKeepingCentre (180, 44)); return; }
    lostLink = bottom.removeFromBottom (18);
    bottom.removeFromBottom (12);
    auto row = bottom.removeFromTop (44);
    if (buyBtn.isVisible())
    {
        activateBtn.setBounds (row.removeFromLeft (row.getWidth() / 2 - 6));
        row.removeFromLeft (12);
        buyBtn.setBounds (row);
    }
    else activateBtn.setBounds (row);
    c.removeFromBottom (36); // the message line
    code.setBounds (c.removeFromBottom (48));
}

void LicensePanel::paint (Graphics& g)
{
    const auto& t = themeOf (*this);
    g.fillAll (Colours::black.withAlpha (0.66f));
    const auto cf = card().toFloat();
    // the card: dark, a soft glow of the theme colour from the top corner
    {
        Graphics::ScopedSaveState s (g);
        Path clip; clip.addRoundedRectangle (cf, 20.0f);
        g.reduceClipRegion (clip);
        g.setColour (t.bodyDark.interpolatedWith (Colour (0xff0d0d11), 0.6f));
        g.fillRect (cf);
        ColourGradient glow (t.grass.withAlpha (0.32f), cf.getRight() - 40.0f, cf.getY(), t.grass.withAlpha (0.0f), cf.getRight() - 300.0f, cf.getY() + 260.0f, true);
        g.setGradientFill (glow);
        g.fillRect (cf);
    }
    g.setColour (Colours::white.withAlpha (0.1f));
    g.drawRoundedRectangle (cf.reduced (0.5f), 20.0f, 1.0f);

    auto r = cf.reduced (34.0f, 30.0f);
    // the label, and a close cross
    g.setFont (lp::ui::mono (10.0f, true, 0.18f));
    g.setColour (t.grassLight);
    g.drawText ("LOOPWRIGHT LICENSE", r.removeFromTop (14.0f), Justification::centredLeft, false);
    {
        auto x = closeX.toFloat().reduced (9.0f);
        g.setColour (Colours::white.withAlpha (isMouseOver() && closeX.contains (getMouseXYRelative()) ? 0.9f : 0.45f));
        g.drawLine (x.getX(), x.getY(), x.getRight(), x.getBottom(), 1.6f);
        g.drawLine (x.getRight(), x.getY(), x.getX(), x.getBottom(), 1.6f);
    }
    r.removeFromTop (14.0f);

    if (licensed)
    {
        g.setFont (font (30.0f, lp::ui::Medium, -0.02f));
        g.setColour (t.ink);
        g.drawText ("You're all set.", r.removeFromTop (38.0f), Justification::centredLeft, false);
        r.removeFromTop (8.0f);
        g.setFont (font (14.5f));
        g.setColour (t.ink.withAlpha (0.75f));
        g.drawText ("Licensed to " + lp::license::activatedTo(), r.removeFromTop (22.0f), Justification::centredLeft, true);
        g.drawText ("Yours forever, with free updates. Thank you!", r.removeFromTop (22.0f), Justification::centredLeft, true);
        return;
    }

    const int left = lp::license::trialDaysLeft();
    const bool ended = left <= 0;
    g.setFont (font (30.0f, lp::ui::Medium, -0.02f));
    g.setColour (t.ink);
    g.drawText (ended ? "Your free trial has ended." : "Make it yours.", r.removeFromTop (38.0f), Justification::centredLeft, false);
    r.removeFromTop (4.0f);
    g.setFont (font (14.5f));
    g.setColour (t.ink.withAlpha (0.7f));
    g.drawText (ended ? "Activate Loopwright to keep writing songs." : "You're on the free trial. Everything is unlocked.", r.removeFromTop (22.0f), Justification::centredLeft, false);
    r.removeFromTop (18.0f);

    // the price, and what you get
    auto priceRow = r.removeFromTop (70.0f);
    g.setColour (Colours::white.withAlpha (0.045f));
    g.fillRoundedRectangle (priceRow, 14.0f);
    g.setColour (Colours::white.withAlpha (0.08f));
    g.drawRoundedRectangle (priceRow.reduced (0.5f), 14.0f, 1.0f);
    auto pr = priceRow.reduced (18.0f, 0.0f);
    g.setFont (font (36.0f, lp::ui::Medium, -0.03f));
    g.setColour (t.ink);
    const auto price = lp::license::price();
    const float pw = GlyphArrangement::getStringWidth (g.getCurrentFont(), price) + 10.0f;
    g.drawText (price, pr.removeFromLeft (pw), Justification::centredLeft, false);
    pr.removeFromLeft (14.0f);
    g.setFont (lp::ui::mono (10.0f, false, 0.12f));
    g.setColour (t.ink.withAlpha (0.8f));
    const char* perks[] = { "ONE-TIME PAYMENT", "YOURS FOREVER", "FREE UPDATES" };
    auto perkArea = pr.withSizeKeepingCentre (pr.getWidth(), 48.0f);
    for (auto* p : perks)
    {
        auto line = perkArea.removeFromTop (16.0f);
        g.setColour (t.grass);
        g.fillEllipse (line.getX(), line.getCentreY() - 2.5f, 5.0f, 5.0f);
        g.setColour (t.ink.withAlpha (0.8f));
        g.drawText (p, line.withTrimmedLeft (13.0f), Justification::centredLeft, false);
    }
    // the trial, as a little pill on the right of the price
    {
        const String txt = ended ? "TRIAL ENDED" : String (left) + (left == 1 ? " DAY LEFT" : " DAYS LEFT");
        g.setFont (lp::ui::mono (9.5f, true, 0.12f));
        const float w = GlyphArrangement::getStringWidth (g.getCurrentFont(), txt) + 22.0f;
        auto pill = Rectangle<float> (priceRow.getRight() - 16.0f - w, priceRow.getCentreY() - 12.0f, w, 24.0f);
        const auto col = ended ? Colour (0xfff2a33a) : t.grass;
        g.setColour (col.withAlpha (0.16f));
        g.fillRoundedRectangle (pill, 12.0f);
        g.setColour (col.withAlpha (0.9f));
        g.drawText (txt, pill, Justification::centred, false);
    }

    // the message under the box (wrong code, empty box)
    if (message.isNotEmpty())
    {
        auto m = code.getBounds().toFloat().translated (0.0f, (float) code.getHeight() + 8.0f).withHeight (20.0f);
        g.setFont (font (13.0f));
        g.setColour (messageBad ? Colour (0xfff2a33a) : t.grassLight);
        g.drawText (message, m, Justification::centredLeft, true);
    }
    // lost your code?
    const auto lost = lostCodeUrl();
    if (lost.isNotEmpty())
    {
        g.setFont (font (12.5f));
        g.setColour (t.ink.withAlpha (lostLink.contains (getMouseXYRelative()) ? 0.9f : 0.5f));
        g.drawText ("Lost your code? Get it again", lostLink, Justification::centred, false);
    }
    else
    {
        g.setFont (font (12.5f));
        g.setColour (t.ink.withAlpha (0.45f));
        g.drawText ("Your code is in your purchase email.", lostLink, Justification::centred, false);
    }
}

void LicensePanel::mouseUp (const MouseEvent& e)
{
    const auto p = e.getPosition();
    if (closeX.contains (p) || ! card().contains (p)) { if (onClosed) onClosed(); return; }
    if (! licensed && lostLink.contains (p) && lostCodeUrl().isNotEmpty()) URL (lostCodeUrl()).launchInDefaultBrowser();
}

bool LicensePanel::keyPressed (const KeyPress& k)
{
    if (k == KeyPress::escapeKey) { if (onClosed) onClosed(); return true; }
    return false;
}

// ---- your songs on disk

namespace
{
    // what in a song's folder can go: old versions of tracks that were replaced, and the "printed"
    // copies (tracks with their effects recorded in, made again whenever you drag one)
    std::vector<File> removableIn (const File& song)
    {
        std::vector<File> out;
        const auto text = song.getChildFile ("song.json").loadFileAsString() + song.getChildFile ("plan.json").loadFileAsString();
        if (text.isEmpty()) return out; // not a Loopwright song we understand: leave it alone
        std::vector<File> unused;
        bool anyUsed = false;
        for (const auto& e : RangedDirectoryIterator (song.getChildFile ("stems"), false, "*.wav", File::findFiles))
        {
            if (text.contains (e.getFile().getFullPathName()) || text.contains (e.getFile().getFileName())) anyUsed = true;
            else unused.push_back (e.getFile());
        }
        if (! anyUsed) return out; // its file doesn't name its tracks (an older song?): don't guess
        out = std::move (unused);
        for (const auto& e : RangedDirectoryIterator (song.getChildFile ("printed"), true, "*", File::findFiles))
            out.push_back (e.getFile());
        return out;
    }
    int64 sizeOf (const File& dir)
    {
        int64 b = 0;
        for (const auto& e : RangedDirectoryIterator (dir, true, "*", File::findFiles)) b += e.getFileSize();
        return b;
    }
    String gb (int64 bytes) { return File::descriptionOfSizeInBytes (bytes); }
}

StoragePanel::StoragePanel (File currentSong, std::function<void()> closed) : current (currentSong), onClosed (std::move (closed))
{
    setWantsKeyboardFocus (true);
    cleanBtn.getProperties().set ("primary", true);
    cleanBtn.onClick = [this]
    {
        if (busy || usage.removable <= 0) return;
        busy = true;
        message = "Clearing out old versions...";
        repaint();
        Component::SafePointer<StoragePanel> self (this);
        Thread::launch ([self, cur = current, alive = alive]
        {
            int64 freed = 0;
            for (const auto& e : RangedDirectoryIterator (SongJob::songsFolder(), false, "*", File::findDirectories))
            {
                if (e.getFile() == cur) continue; // the song that's open keeps everything (its undo needs them)
                for (auto& f : removableIn (e.getFile())) { const auto sz = f.getSize(); if (f.deleteFile()) freed += sz; }
            }
            MessageManager::callAsync ([self, freed]
            {
                if (self == nullptr) return;
                self->busy = false;
                self->message = "Done: freed " + gb (freed) + ".";
                self->measure();
            });
        });
    };
    openBtn.onClick = [] { SongJob::songsFolder().revealToUser(); };
    for (auto* b : { &cleanBtn, &openBtn }) { b->setWantsKeyboardFocus (false); addAndMakeVisible (*b); }
    measure();
}

StoragePanel::~StoragePanel() { *alive = false; }

void StoragePanel::measure()
{
    Component::SafePointer<StoragePanel> self (this);
    Thread::launch ([self, cur = current]
    {
        Usage u;
        for (const auto& e : RangedDirectoryIterator (SongJob::songsFolder(), false, "*", File::findDirectories))
        {
            ++u.songs;
            u.total += sizeOf (e.getFile());
            if (e.getFile() != cur)
                for (auto& f : removableIn (e.getFile())) u.removable += f.getSize();
        }
        u.done = true;
        MessageManager::callAsync ([self, u] { if (self != nullptr) { self->usage = u; self->resized(); self->repaint(); } });
    });
}

Rectangle<int> StoragePanel::card() const
{
    return Rectangle<int> (jmin (520, getWidth() - 40), 420).withCentre (getLocalBounds().getCentre());
}

void StoragePanel::resized()
{
    auto c = card().reduced (34, 30);
    closeX = Rectangle<int> (card().getRight() - 50, card().getY() + 18, 30, 30);
    auto row = c.removeFromBottom (44);
    cleanBtn.setBounds (row.removeFromLeft (row.getWidth() / 2 - 6));
    row.removeFromLeft (12);
    openBtn.setBounds (row);
    cleanBtn.setEnabled (usage.done && usage.removable > 1024 * 1024 && ! busy);
    cleanBtn.setButtonText (usage.done && usage.removable > 1024 * 1024 ? "Free " + gb (usage.removable) : String ("Nothing to clear"));
}

void StoragePanel::paint (Graphics& g)
{
    const auto* lnf = dynamic_cast<LivePlanetLookAndFeel*> (&getLookAndFeel());
    const auto& t = lnf != nullptr ? *lnf->th : currentTheme();
    g.fillAll (Colours::black.withAlpha (0.66f));
    const auto cf = card().toFloat();
    {
        Graphics::ScopedSaveState s (g);
        Path clip; clip.addRoundedRectangle (cf, 20.0f);
        g.reduceClipRegion (clip);
        g.setColour (t.bodyDark.interpolatedWith (Colour (0xff0d0d11), 0.6f));
        g.fillRect (cf);
        ColourGradient glow (t.grass.withAlpha (0.28f), cf.getRight() - 40.0f, cf.getY(), t.grass.withAlpha (0.0f), cf.getRight() - 300.0f, cf.getY() + 260.0f, true);
        g.setGradientFill (glow);
        g.fillRect (cf);
    }
    g.setColour (Colours::white.withAlpha (0.1f));
    g.drawRoundedRectangle (cf.reduced (0.5f), 20.0f, 1.0f);
    {
        auto x = closeX.toFloat().reduced (9.0f);
        g.setColour (Colours::white.withAlpha (0.5f));
        g.drawLine (x.getX(), x.getY(), x.getRight(), x.getBottom(), 1.6f);
        g.drawLine (x.getRight(), x.getY(), x.getX(), x.getBottom(), 1.6f);
    }
    auto r = cf.reduced (34.0f, 30.0f);
    g.setFont (lp::ui::mono (10.0f, true, 0.18f));
    g.setColour (t.grassLight);
    g.drawText ("YOUR SONGS ON THIS COMPUTER", r.removeFromTop (14.0f), Justification::centredLeft, false);
    r.removeFromTop (14.0f);
    g.setFont (font (30.0f, lp::ui::Medium, -0.02f));
    g.setColour (t.ink);
    g.drawText (usage.done ? gb (usage.total) : String ("Measuring..."), r.removeFromTop (38.0f), Justification::centredLeft, false);
    g.setFont (font (14.5f));
    g.setColour (t.ink.withAlpha (0.7f));
    g.drawText (usage.done ? String (usage.songs) + (usage.songs == 1 ? " song, " : " songs, ") + "kept so you can open them again from your DAW projects" : String(),
                r.removeFromTop (22.0f), Justification::centredLeft, true);
    r.removeFromTop (16.0f);
    AttributedString s;
    s.setLineSpacing (3.0f);
    s.append ("Every song keeps its tracks as audio, plus the older versions of tracks you swapped or edited. "
              "Clearing them keeps every song's current tracks: only the replaced versions go, and the copies with "
              "effects recorded in (made again when you drag a track). The song you have open isn't touched.\n\n"
              "Heads up: a DAW project that uses an older version of a track, and didn't keep its own copy "
              "(in Ableton: File > Collect All and Save), would lose it. To remove whole songs, open the folder.",
              font (13.0f), t.ink.withAlpha (0.78f));
    TextLayout tl;
    tl.createLayout (s, r.getWidth());
    tl.draw (g, r.removeFromTop (tl.getHeight()));
    if (message.isNotEmpty())
    {
        g.setFont (font (13.0f));
        g.setColour (t.grassLight);
        g.drawText (message, Rectangle<float> (cf.getX() + 34.0f, (float) cleanBtn.getY() - 28.0f, cf.getWidth() - 68.0f, 20.0f), Justification::centredLeft, false);
    }
}

void StoragePanel::mouseUp (const MouseEvent& e)
{
    if (closeX.contains (e.getPosition()) || ! card().contains (e.getPosition())) { if (onClosed) onClosed(); }
}

bool StoragePanel::keyPressed (const KeyPress& k)
{
    if (k == KeyPress::escapeKey) { if (onClosed) onClosed(); return true; }
    return false;
}
