#include "PluginEditor.h"

using namespace juce;
using lp::ui::font;

// VOCALS, "Your vocals": every vocal in the song in one place (its chain, its chop pads, its auto-tune, each a click
// away), and the ways to add one: chops from your sample folders, chops from your own vocal, your vocal auto-tuned.

VocalsHub::VocalsHub (LivePlanetProcessor& p, const lp::ui::Theme*& theme) : proc (p), th (theme)
{
    // add a vocal: tiles (an icon, what it is, a line about it)
    struct T { int icon; const char* head; const char* sub; const char* tip; bool main; };
    for (auto [icon, head, sub, tip, main] : {
             T { 0, "Draw & sing", "draw notes, type words, hear it in your voice", "A piano roll over a section: each note a word, sung in your voice (the voice helper)", true },
             T { 1, "Create with AI", "sing it yourself, hear it in another voice", "Record or drop in your own take, and the voice helper sings it again in the voice you picked", true },
             T { 2, "Vocal chops", "from the vocals in your samples", "Vocal chops from the vocals in your sample folders, sung to a new melody over your chords", false },
             T { 3, "Chops from my vocal", "your recording, chopped into a hook", "Your own vocal (a WAV or AIFF) chopped into a catchy hook for this song", false },
             T { 4, "Auto-tune my vocal", "natural: keeps your slides", "Your vocal, auto-tuned to the song's key: natural (keeps your slides)", false },
             T { 5, "Hard-tune my vocal", "the snapped, robotic sound", "Your vocal, auto-tuned to the song's key: hard (the snapped, robotic sound)", false } })
    {
        auto* t = tiles.add (new HubTile (th, icon, head, sub));
        t->setTooltip (tip);
        t->highlight = main;
        addAndMakeVisible (t);
    }
    auto pick = [this] (const String& title, std::function<void (const File&)> then)
    {
        chooser = std::make_unique<FileChooser> (title, File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.m4a");
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [then] (const FileChooser& fc)
        {
            if (fc.getResult().existsAsFile()) then (fc.getResult());
        });
    };
    tiles[0]->onClick = [this] { if (onGoTab) onGoTab (1); };
    tiles[1]->onClick = [this] { if (onGoTab) onGoTab (2); };
    tiles[2]->onClick = [this] { proc.songJob.editTrack (0, SongJob::EditOp::AddChops); };
    tiles[3]->onClick = [this, pick] { pick ("Your vocal: it becomes a chop hook for this song", [this] (const File& f) { proc.songJob.addChopHookFrom (f); }); };
    tiles[4]->onClick = [this, pick] { pick ("Your vocal: tuned to the song's key", [this] (const File& f) { proc.songJob.addTunedVocal (f, false); }); };
    tiles[5]->onClick = [this, pick] { pick ("Your vocal: tuned hard to the song's key", [this] (const File& f) { proc.songJob.addTunedVocal (f, true); }); };
    voiceList.onChange = [this] { if (voiceList.getText().isNotEmpty()) lp::setSetting ("voice", voiceList.getText()); };
    addChildComponent (voiceChip);
}

// a tile: an icon in a soft square, a title and a line
void HubTile::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool over = isMouseOver (true) && isEnabled();
    const float a = isEnabled() ? 1.0f : 0.4f;
    g.setColour ((highlight ? th->grass.withAlpha (over ? 0.22f : 0.14f) : Colours::white.withAlpha (over ? 0.08f : 0.04f)).withMultipliedAlpha (a));
    g.fillRoundedRectangle (r, 12.0f);
    g.setColour ((highlight ? th->grassLight.withAlpha (over ? 0.7f : 0.4f) : Colours::white.withAlpha (over ? 0.2f : 0.1f)).withMultipliedAlpha (a));
    g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, 1.0f);
    auto ic = Rectangle<float> (r.getX() + 14.0f, r.getCentreY() - 19.0f, 38.0f, 38.0f);
    g.setColour ((highlight ? th->grass : Colours::white.withAlpha (0.08f)).withMultipliedAlpha (highlight ? 0.85f * a : a));
    g.fillRoundedRectangle (ic, 10.0f);
    drawIcon (g, ic.reduced (9.0f), icon, Colours::white.withAlpha (0.92f * a));
    auto txt = r.withTrimmedLeft (ic.getRight() - r.getX() + 12.0f).withTrimmedRight (10.0f);
    g.setColour (th->ink.withMultipliedAlpha (a));
    g.setFont (font (14.5f, lp::ui::Medium));
    g.drawText (head, txt.removeFromTop (txt.getHeight() * 0.52f), Justification::bottomLeft, true);
    g.setColour (th->inkDim.withMultipliedAlpha (a));
    g.setFont (font (11.5f));
    g.drawText (line, txt, Justification::topLeft, true);
}

// little line icons: 0 a pencil on notes, 1 a sparkle, 2 a microphone, 3 scissors, 4 a tuning wave, 5 a stepped (hard-tuned) wave
void HubTile::drawIcon (Graphics& g, Rectangle<float> r, int kind, Colour c)
{
    g.setColour (c);
    const PathStrokeType stroke (1.7f, PathStrokeType::curved, PathStrokeType::rounded);
    Path p;
    const float x = r.getX(), y = r.getY(), w = r.getWidth(), h = r.getHeight();
    if (kind == 0)
    {
        // notes on a staff, a pencil
        g.fillRoundedRectangle (x, y + h * 0.62f, w * 0.34f, h * 0.16f, 2.0f);
        g.fillRoundedRectangle (x + w * 0.3f, y + h * 0.38f, w * 0.34f, h * 0.16f, 2.0f);
        p.startNewSubPath (x + w * 0.62f, y + h * 0.92f); p.lineTo (x + w, y + h * 0.18f);
        g.strokePath (p, PathStrokeType (2.4f, PathStrokeType::curved, PathStrokeType::rounded));
    }
    else if (kind == 1)
    {
        const float cx = r.getCentreX() - w * 0.08f, cy = r.getCentreY() + h * 0.06f, s = w * 0.42f;
        p.startNewSubPath (cx, cy - s); p.quadraticTo (cx, cy, cx + s, cy); p.quadraticTo (cx, cy, cx, cy + s);
        p.quadraticTo (cx, cy, cx - s, cy); p.quadraticTo (cx, cy, cx, cy - s); p.closeSubPath();
        g.fillPath (p);
        g.fillEllipse (x + w * 0.74f, y, w * 0.2f, h * 0.2f);
    }
    else if (kind == 2)
    {
        g.drawRoundedRectangle (x + w * 0.32f, y, w * 0.36f, h * 0.6f, w * 0.18f, 1.7f);
        p.addCentredArc (r.getCentreX(), y + h * 0.38f, w * 0.34f, h * 0.3f, 0.0f, MathConstants<float>::halfPi, MathConstants<float>::halfPi * 3.0f, true);
        p.startNewSubPath (r.getCentreX(), y + h * 0.7f); p.lineTo (r.getCentreX(), y + h);
        g.strokePath (p, stroke);
    }
    else if (kind == 3)
    {
        g.drawEllipse (x, y + h * 0.62f, w * 0.34f, h * 0.34f, 1.7f);
        g.drawEllipse (x + w * 0.66f, y + h * 0.62f, w * 0.34f, h * 0.34f, 1.7f);
        p.startNewSubPath (x + w * 0.28f, y + h * 0.66f); p.lineTo (x + w * 0.82f, y);
        p.startNewSubPath (x + w * 0.72f, y + h * 0.66f); p.lineTo (x + w * 0.18f, y);
        g.strokePath (p, stroke);
    }
    else if (kind == 4)
    {
        p.startNewSubPath (x, r.getCentreY());
        for (int i = 1; i <= 24; ++i) { const float t = (float) i / 24.0f; p.lineTo (x + w * t, r.getCentreY() - std::sin (t * MathConstants<float>::twoPi * 1.5f) * h * 0.35f); }
        g.strokePath (p, stroke);
    }
    else
    {
        p.startNewSubPath (x, y + h * 0.7f); p.lineTo (x + w * 0.25f, y + h * 0.7f); p.lineTo (x + w * 0.25f, y + h * 0.3f);
        p.lineTo (x + w * 0.55f, y + h * 0.3f); p.lineTo (x + w * 0.55f, y + h * 0.55f); p.lineTo (x + w * 0.8f, y + h * 0.55f);
        p.lineTo (x + w * 0.8f, y + h * 0.15f); p.lineTo (x + w, y + h * 0.15f);
        g.strokePath (p, PathStrokeType (1.7f, PathStrokeType::mitered, PathStrokeType::rounded));
    }
}

VocalsHub::~VocalsHub() = default;

// a double, a harmony or a choir from one of the vocals: made in the background from its track, then added
void VocalsHub::makeStack (int track, int kind)
{
    const auto st = proc.songJob.getStatus();
    if (busy->load() || track < 0 || track >= (int) st.tracks.size()) return;
    const auto& t = st.tracks[(size_t) track];
    const auto k = (lp::song::stack::Kind) jlimit (0, 3, kind);
    lp::song::tune::Settings s;
    s.key = st.song.minor ? st.song.key : (st.song.key + 9) % 12; // (a major song: its relative minor, the same notes)
    s.scale = 1;
    const auto in = t.stem;
    const auto out = st.files.folder.getChildFile ("voice").getChildFile (String (lp::song::stack::nameOf (k)) + " of " + String (t.name).retainCharacters (
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 ()-") + " " + String (Time::currentTimeMillis() % 100000) + ".wav");
    String base = t.name;
    if (base.startsWith ("vocal (") && base.endsWith (")")) base = base.substring (7, base.length() - 1); // ("vocal (tuned)" -> "tuned")
    const String name = String (lp::song::stack::nameOf (k)) + ", " + base;
    working = "Making the " + String (lp::song::stack::nameOf (k)) + " of " + t.name + "...";
    repaint();
    busy->store (true);
    auto flag = busy;
    SafePointer<VocalsHub> self (this);
    auto* p = &proc;
    Thread::launch ([flag, self, p, in, out, s, k, name]
    {
        bool ok = false;
        AudioFormatManager fm;
        fm.registerBasicFormats();
        if (std::unique_ptr<AudioFormatReader> rd { fm.createReaderFor (in) }; rd != nullptr && rd->lengthInSamples > 0)
        {
            AudioBuffer<float> buf ((int) rd->numChannels, (int) rd->lengthInSamples);
            rd->read (&buf, 0, buf.getNumSamples(), 0, true, true);
            const auto stack = lp::song::stack::make (buf, rd->sampleRate, s, k);
            out.getParentDirectory().createDirectory();
            WavAudioFormat wav;
            if (std::unique_ptr<AudioFormatWriter> w { wav.createWriterFor (new FileOutputStream (out), rd->sampleRate, 2, 24, {}, 0) })
                ok = w->writeFromAudioSampleBuffer (stack, 0, stack.getNumSamples());
        }
        MessageManager::callAsync ([flag, self, p, out, ok, name]
        {
            flag->store (false);
            if (ok) p->songJob.addSungVocal (out, name, "made here from your vocal: " + name, true, "stacked from your vocal");
            if (self != nullptr) { self->working = ok ? String() : "Couldn't make the " + name; self->repaint(); }
        });
    });
}

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

Rectangle<int> VocalsHub::listCard() const  { return { 0, 0, getWidth() - 400, getHeight() }; }
Rectangle<int> VocalsHub::voiceCard() const { return { getWidth() - 386, 0, 386, 126 }; }
Rectangle<int> VocalsHub::sideCard() const  { return { getWidth() - 386, 140, 386, getHeight() - 140 }; }

// a vocal's loudness along the song, for its card (read once per file version, in the background)
const std::vector<float>* VocalsHub::waveOf (const File& f)
{
    if (! f.existsAsFile()) return nullptr;
    const auto key = f.getFullPathName() + "|" + String (f.getLastModificationTime().toMilliseconds());
    {
        const std::lock_guard<std::mutex> l (waves->lock);
        if (auto it = waves->done.find (key); it != waves->done.end()) return &it->second;
        if (waves->pending.count (key)) return nullptr;
        waves->pending.insert (key);
    }
    auto shared = waves;
    SafePointer<VocalsHub> self (this);
    Thread::launch ([shared, f, key, self]
    {
        std::vector<float> v (900, 0.0f);
        AudioFormatManager fm;
        fm.registerBasicFormats();
        if (std::unique_ptr<AudioFormatReader> rd { fm.createReaderFor (f) })
        {
            const int64 per = std::max<int64> (1, rd->lengthInSamples / (int64) v.size());
            AudioBuffer<float> b ((int) rd->numChannels, (int) std::min<int64> (per, 1 << 16));
            for (size_t i = 0; i < v.size(); ++i)
            {
                const int len = (int) std::min<int64> ({ per, rd->lengthInSamples - (int64) i * per, (int64) b.getNumSamples() });
                if (len <= 0) break;
                rd->read (&b, 0, len, (int64) i * per, true, true);
                v[i] = b.getMagnitude (0, len);
            }
        }
        { const std::lock_guard<std::mutex> l (shared->lock); shared->done[key] = std::move (v); shared->pending.erase (key); }
        MessageManager::callAsync ([self] { if (self != nullptr) self->repaint(); });
    });
    return nullptr;
}

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
        r->stem = t.stem;
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
        setup (r->stack, "Doubles, harmonies or a choir from this vocal, made here in a few seconds, as a new track", true);
        r->stack.onClick = [this, i, b = &r->stack]
        {
            PopupMenu m;
            m.addSectionHeader ("STACK THIS VOCAL (A NEW TRACK)");
            m.addItem (1, "Double it (sung again, left and right)");
            m.addItem (2, "Harmony a third above (follows the melody, in key)");
            m.addItem (3, "Harmony a fifth below (in key)");
            m.addItem (4, "Choir (six voices: doubles and both harmonies)");
            m.showMenuAsync (PopupMenu::Options().withTargetComponent (b), [this, i] (int r) { if (r > 0) makeStack (i, r - 1); });
        };
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
    const bool helper = voiceAvailable && voiceAvailable();
    for (int i = 0; i < tiles.size(); ++i) tiles[i]->setEnabled (i < 2 ? helper && st.haveSong : st.haveSong && ! st.editing);
    // the voices (for the chip), from the voice helper
    if (helper)
    {
        proc.voiceClient.setWanted (true); // (the helper's voices, for the chip)
        const auto snap = proc.voiceClient.snapshot();
        if (const auto vs = lp::voice::usableVoices (snap.voices); vs != shownVoices)
        {
            shownVoices = vs;
            voiceList.clear (dontSendNotification);
            for (int i = 0; i < vs.size(); ++i) voiceList.addItem (vs[i], i + 1);
            voiceList.setText (lp::voice::defaultVoice (vs, {}, lp::setting ("voice", "").toString()), dontSendNotification);
        }
        else if (const auto want = lp::setting ("voice", "").toString(); want.isNotEmpty() && want != voiceList.getText() && vs.contains (want))
            voiceList.setText (want, dontSendNotification); // (picked on another page)
    }
    if (voiceChip.isVisible() != helper) { voiceChip.setVisible (helper); repaint(); }
}

void VocalsHub::resized()
{
    auto l = listCard().reduced (22, 18);
    l.removeFromTop (64);
    for (auto& r : rows)
    {
        auto row = l.removeFromTop (92);
        l.removeFromTop (10);
        // its actions, on the right, in a column of two rows
        auto bt = row.withTrimmedLeft (row.getWidth() - 250).reduced (12, 12);
        auto top = bt.removeFromTop (bt.getHeight() / 2).reduced (0, 2), bottom = bt.reduced (0, 2);
        int k = 0;
        for (auto* b : { &r->stack, &r->chain, &r->tune, &r->chops, &r->studio })
        {
            if (! b->isVisible()) continue;
            auto& line = k < 2 ? top : bottom;
            b->setBounds (line.removeFromLeft (110));
            line.removeFromLeft (6);
            ++k;
        }
        r->area = row;
    }
    {
        auto v = voiceCard().reduced (20, 16);
        v.removeFromTop (34);
        voiceChip.setBounds (v.removeFromTop (60));
    }
    auto a = sideCard().reduced (20, 16);
    a.removeFromTop (28);
    const int th = jlimit (44, 64, (a.getHeight() - 8 * (tiles.size() - 1)) / jmax (1, tiles.size()));
    for (auto* t : tiles)
    {
        t->setBounds (a.removeFromTop (th));
        a.removeFromTop (8);
    }
}

void VocalsHub::paint (Graphics& g)
{
    const auto& t = *th;
    auto card = [&] (Rectangle<int> r)
    {
        g.setColour (t.body);
        g.fillRoundedRectangle (r.toFloat(), 14.0f);
        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.035f), 0.0f, (float) r.getY(), Colours::transparentWhite, 0.0f, (float) r.getY() + 120.0f, false));
        g.fillRoundedRectangle (r.toFloat(), 14.0f);
        g.setColour (t.line);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 14.0f, 1.0f);
    };
    auto label = [&] (Rectangle<int> r, const String& text)
    {
        g.setFont (lp::ui::mono (10.0f, true, 0.16f));
        g.setColour (t.grassLight);
        g.drawText (text, r.reduced (20, 16).removeFromTop (16), Justification::centredLeft, false);
    };
    card (listCard());
    card (voiceCard());
    card (sideCard());
    const auto st = proc.songJob.getStatus();
    {
        auto in = listCard().reduced (22, 18);
        g.setFont (font (22.0f, lp::ui::Medium, -0.01f));
        g.setColour (t.ink);
        g.drawText ("Your vocals", in.removeFromTop (30), Justification::centredLeft, false);
        if (! rows.empty())
        {
            // how many, as a little pill by the title
            const String n = String ((int) rows.size()) + ((int) rows.size() == 1 ? " vocal" : " vocals");
            const float w = GlyphArrangement::getStringWidth (lp::ui::mono (10.0f, true, 0.12f), n.toUpperCase()) + 20.0f;
            auto pill = Rectangle<float> ((float) in.getX() + 140.0f, (float) in.getY() - 26.0f, w, 20.0f);
            g.setColour (t.grass.withAlpha (0.25f));
            g.fillRoundedRectangle (pill, 10.0f);
            g.setColour (t.grassLight);
            g.setFont (lp::ui::mono (10.0f, true, 0.12f));
            g.drawText (n.toUpperCase(), pill, Justification::centred, false);
        }
        g.setFont (font (12.5f));
        g.setColour (t.inkDim);
        g.drawText (st.haveSong ? "Every vocal in this song: stack it, its chain, its pads and its auto-tune" : "Write a song first, then add vocals to it here",
                    in.removeFromTop (20), Justification::centredLeft, true);
    }
    label (voiceCard(), "YOUR VOICE");
    if (! voiceChip.isVisible())
    {
        g.setFont (font (12.5f));
        g.setColour (t.inkDim);
        g.drawFittedText ("Set up the voice helper (the ? at the top) to sing in your voice: Draw & sing and Create with AI.",
                          voiceCard().reduced (20, 16).withTrimmedTop (30), Justification::topLeft, 3);
    }
    label (sideCard(), "ADD A VOCAL");
    if (st.haveSong && rows.empty())
    {
        // nothing yet: a microphone and where to start
        auto e = listCard().withTrimmedTop (90).toFloat();
        auto ic = Rectangle<float> (64.0f, 64.0f).withCentre ({ e.getCentreX(), e.getCentreY() - 40.0f });
        g.setColour (t.grass.withAlpha (0.18f));
        g.fillEllipse (ic.expanded (18.0f));
        HubTile::drawIcon (g, ic.reduced (14.0f), 2, t.grassLight);
        g.setFont (font (17.0f, lp::ui::Medium));
        g.setColour (t.ink);
        g.drawText ("No vocals in this song yet", e.withY (ic.getBottom() + 30.0f).withHeight (24.0f), Justification::centred, false);
        g.setFont (font (13.0f));
        g.setColour (t.inkDim);
        g.drawText ("Draw & sing a melody, write one with AI, or bring your own on the right.", e.withY (ic.getBottom() + 56.0f).withHeight (20.0f), Justification::centred, false);
    }
    for (auto& r : rows)
    {
        const auto ar = r->area.toFloat();
        g.setColour (Colours::white.withAlpha (0.04f));
        g.fillRoundedRectangle (ar, 12.0f);
        g.setColour (Colours::white.withAlpha (0.07f));
        g.drawRoundedRectangle (ar.reduced (0.5f), 12.0f, 1.0f);
        // its art, its name and where it came from
        const auto art = Rectangle<float> (ar.getX() + 12.0f, ar.getY() + 12.0f, ar.getHeight() - 24.0f, ar.getHeight() - 24.0f);
        lp::ai::VoicePicker::drawArt (g, art, r->name + r->source);
        auto txt = Rectangle<float> (art.getRight() + 14.0f, ar.getY() + 14.0f, 260.0f, ar.getHeight() - 28.0f);
        g.setFont (font (16.0f, lp::ui::Medium));
        g.setColour (t.ink);
        g.drawText (r->name.substring (0, 1).toUpperCase() + r->name.substring (1), txt.removeFromTop (22.0f), Justification::centredLeft, true);
        g.setFont (lp::ui::mono (9.5f, false, 0.1f));
        g.setColour (t.inkDim);
        g.drawText (r->source.toUpperCase(), txt.removeFromTop (16.0f), Justification::centredLeft, true);
        // where it sings in the song: its loudness, start to end
        auto wv = Rectangle<float> (art.getRight() + 14.0f, ar.getBottom() - 30.0f, ar.getWidth() - (art.getRight() - ar.getX()) - 14.0f - 262.0f, 18.0f);
        if (wv.getWidth() > 40.0f)
        {
            g.setColour (Colours::white.withAlpha (0.04f));
            g.fillRoundedRectangle (wv, 4.0f);
            if (const auto* w = waveOf (r->stem); w != nullptr && ! w->empty())
            {
                float top = 0.0f;
                for (float v : *w) top = std::max (top, v);
                if (top > 0.0f)
                {
                    g.setColour (t.grassLight.withAlpha (0.8f));
                    for (float x = wv.getX() + 1.0f; x < wv.getRight() - 1.0f; x += 1.0f)
                    {
                        const size_t k = (size_t) jlimit (0, (int) w->size() - 1, (int) ((x - wv.getX()) / wv.getWidth() * (float) w->size()));
                        const float hh = std::max (0.4f, (*w)[k] / top * (wv.getHeight() * 0.5f - 2.0f));
                        g.drawVerticalLine ((int) x, wv.getCentreY() - hh, wv.getCentreY() + hh);
                    }
                }
            }
        }
    }
    if (working.isNotEmpty())
    {
        g.setFont (font (12.5f, lp::ui::Medium));
        g.setColour (working.startsWith ("Couldn't") ? Colour (0xffff8f8f) : t.grassLight);
        g.drawText (working, listCard().reduced (22, 14).removeFromBottom (20), Justification::centredLeft, true);
    }
}
