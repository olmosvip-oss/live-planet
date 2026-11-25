#include "PluginEditor.h"

using namespace juce;
using lp::ui::font;

// VOCALS, "Your vocals": every vocal in the song in one place (its chain, its chop pads, its auto-tune, each a click
// away), and the ways to add one: chops from your sample folders, chops from your own vocal, your vocal auto-tuned.

VocalsHub::VocalsHub (LivePlanetProcessor& p, const lp::ui::Theme*& theme) : proc (p), th (theme)
{
    struct Add { TextButton* b; const char* tip; };
    for (auto [b, tip] : { Add { &addChops, "Vocal chops from the vocals in your sample folders, sung to a new melody over your chords" },
                           Add { &addMyChops, "Your own vocal (a WAV or AIFF) chopped into a catchy hook for this song" },
                           Add { &addTuned, "Your vocal, auto-tuned to the song's key: natural (keeps your slides)" },
                           Add { &addTunedHard, "Your vocal, auto-tuned to the song's key: hard (the snapped, robotic sound)" } })
    {
        b->setTooltip (tip);
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (*b);
    }
    addChops.onClick = [this] { proc.songJob.editTrack (0, SongJob::EditOp::AddChops); };
    auto pick = [this] (const String& title, std::function<void (const File&)> then)
    {
        chooser = std::make_unique<FileChooser> (title, File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.m4a");
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [then] (const FileChooser& fc)
        {
            if (fc.getResult().existsAsFile()) then (fc.getResult());
        });
    };
    addMyChops.onClick = [this, pick] { pick ("Your vocal: it becomes a chop hook for this song", [this] (const File& f) { proc.songJob.addChopHookFrom (f); }); };
    addTuned.onClick = [this, pick] { pick ("Your vocal: tuned to the song's key", [this] (const File& f) { proc.songJob.addTunedVocal (f, false); }); };
    addTunedHard.onClick = [this, pick] { pick ("Your vocal: tuned hard to the song's key", [this] (const File& f) { proc.songJob.addTunedVocal (f, true); }); };
}

VocalsHub::~VocalsHub() = default;

// the song's vocal tracks: a vocal, chops, a tuned vocal
std::vector<int> VocalsHub::vocalTracks() const
{
    std::vector<int> out;
    const auto st = proc.songJob.getStatus();
    for (int i = 0; i < (int) st.tracks.size(); ++i)
    {
        const auto& t = st.tracks[(size_t) i];
        if (t.vocal || t.tunable || String (t.name) == "vocal chops") out.push_back (i);
    }
    return out;
}

Rectangle<int> VocalsHub::listCard() const { return { 0, 0, getWidth() - 340, getHeight() }; }
Rectangle<int> VocalsHub::addCard() const  { return { getWidth() - 326, 0, 326, getHeight() }; }

void VocalsHub::rebuild()
{
    rows.clear();
    const auto st = proc.songJob.getStatus();
    for (int i : vocalTracks())
    {
        const auto& t = st.tracks[(size_t) i];
        auto r = std::make_unique<Row>();
        r->track = i;
        r->name = t.name;
        r->source = SongJob::niceSource (t.source);
        auto setup = [this] (TextButton& b, const char* tip, bool primary)
        {
            b.setTooltip (tip);
            b.setWantsKeyboardFocus (false);
            if (primary) b.getProperties().set ("primary", true);
            addAndMakeVisible (b);
        };
        setup (r->chain, "This vocal's chain: EQ, compression, saturation, de-ess, air and width, delay and reverb", false);
        r->chain.onClick = [this, i] { open (0, i); };
        if (String (t.name) == "vocal chops")
        {
            setup (r->chops, "Its chops as 16 pads: play them with your mouse, computer keys or a MIDI controller", true);
            r->chops.onClick = [this, i] { open (1, i); };
        }
        if (t.tunable)
        {
            setup (r->tune, "Auto-tune: key and scale, retune speed, humanize, its notes and pitch line", true);
            r->tune.onClick = [this, i] { open (2, i); };
        }
        setup (r->studio, "Open it in the Studio: its sample, its sound and effects", false);
        r->studio.onClick = [this, i] { if (onOpenInStudio) onOpenInStudio (i); };
        rows.push_back (std::move (r));
    }
    resized();
    repaint();
}

// one of the vocal windows, over the whole plugin
void VocalsHub::open (int which, int track)
{
    auto* top = lp::ui::overlayHost (this);
    if (top == nullptr) return;
    SafePointer<VocalsHub> self (this);
    auto closed = [self] { MessageManager::callAsync ([self] { if (self != nullptr) self->window.reset(); }); };
    if (which == 0) window = std::make_unique<VocalPanel> (proc, track, closed);
    else if (which == 1) window = std::make_unique<ChopPanel> (proc, track, closed);
    else window = std::make_unique<TunePanel> (proc, track, closed);
    top->addAndMakeVisible (*window);
    window->setBounds (top->getLocalBounds());
    window->toFront (true);
    if (which > 0) window->grabKeyboardFocus();
}

void VocalsHub::refresh()
{
    const auto st = proc.songJob.getStatus();
    String key;
    for (int i : vocalTracks()) key << i << st.tracks[(size_t) i].name << st.tracks[(size_t) i].source << (int) st.tracks[(size_t) i].tunable << ";";
    key << (int) st.haveSong;
    if (key != shownKey) { shownKey = key; rebuild(); }
    for (auto* b : { &addChops, &addMyChops, &addTuned, &addTunedHard }) b->setEnabled (st.haveSong && ! st.editing);
}

void VocalsHub::resized()
{
    auto l = listCard().reduced (22, 18);
    l.removeFromTop (60);
    for (auto& r : rows)
    {
        auto row = l.removeFromTop (52);
        l.removeFromTop (8);
        auto bt = row.withTrimmedLeft (row.getWidth() / 2).withTrimmedRight (12).reduced (0, 11);
        for (auto* b : { &r->studio, &r->chain, &r->chops, &r->tune })
        {
            if (! b->isVisible()) continue;
            b->setBounds (bt.removeFromRight (b == &r->studio ? 92 : 104));
            bt.removeFromRight (8);
        }
        r->area = row;
    }
    auto a = addCard().reduced (22, 18);
    a.removeFromTop (60);
    for (auto* b : { &addChops, &addMyChops, &addTuned, &addTunedHard })
    {
        b->setBounds (a.removeFromTop (40));
        a.removeFromTop (10);
    }
    nextNote = a.withTrimmedTop (10);
}

void VocalsHub::paint (Graphics& g)
{
    const auto& t = *th;
    auto card = [&] (Rectangle<int> r)
    {
        g.setColour (t.body);
        g.fillRoundedRectangle (r.toFloat(), 14.0f);
        g.setColour (t.line);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 14.0f, 1.0f);
    };
    auto title = [&] (Rectangle<int> r, const String& head, const String& sub)
    {
        auto in = r.reduced (22, 18);
        g.setFont (font (20.0f, lp::ui::Medium));
        g.setColour (t.ink);
        g.drawText (head, in.removeFromTop (26), Justification::centredLeft, false);
        g.setFont (font (12.0f));
        g.setColour (t.inkDim);
        g.drawText (sub, in.removeFromTop (20), Justification::centredLeft, true);
    };
    card (listCard());
    card (addCard());
    const auto st = proc.songJob.getStatus();
    title (listCard(), "Your vocals", st.haveSong ? "Every vocal in this song: its chain, its chop pads and its auto-tune" : "Write a song first, then add vocals to it here");
    title (addCard(), "Add a vocal", "In the song's key and tempo, as a new track");
    if (st.haveSong && rows.empty())
    {
        g.setFont (font (14.0f));
        g.setColour (t.inkDim);
        g.drawText ("No vocals in this song yet. Add one on the right.", listCard().withTrimmedTop (80), Justification::centred, false);
    }
    for (auto& r : rows)
    {
        g.setColour (Colours::white.withAlpha (0.04f));
        g.fillRoundedRectangle (r->area.toFloat(), 10.0f);
        auto txt = r->area.reduced (16, 8).withWidth (r->area.getWidth() / 2 - 16);
        g.setFont (font (15.0f, lp::ui::Medium));
        g.setColour (t.ink);
        g.drawText (r->name.substring (0, 1).toUpperCase() + r->name.substring (1), txt.removeFromTop (20), Justification::centredLeft, true);
        g.setFont (lp::ui::mono (9.5f, false, 0.1f));
        g.setColour (t.inkDim);
        g.drawText (r->source.toUpperCase(), txt, Justification::centredLeft, true);
    }
    g.setFont (font (12.0f));
    g.setColour (t.inkDim);
    g.drawFittedText ("Coming next on this tab: draw a melody and type the words for your voice to sing, and choirs, doubles and harmonies "
                      "from any vocal.", nextNote, Justification::topLeft, 4);
}
