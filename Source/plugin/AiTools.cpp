#include "PluginEditor.h"

using namespace juce;
using lp::ui::font;

// The AI tools that work through the voice helper and land in the song by themselves: a layer (an instrument
// played over the sections you picked) and a drawn melody sung in your voice. Both are made in the background;
// when the helper is done, the result is lined up with the song and added as a new track.

namespace lp::ai
{
String helperKeyName (const lp::song::Song& song)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int minorKey = song.minor ? song.key : (song.key + 9) % 12; // (a major song: its relative minor, the same notes)
    return names[((minorKey % 12) + 12) % 12];
}

// the helper's file (it starts where the section starts), padded with silence from the song's first bar
static bool padToSongStart (const File& in, const File& out, double startSeconds, const std::vector<lp::song::stack::Bend>& bends = {},
                            double loopSeconds = 0.0, double totalSeconds = 0.0)
{
    AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<AudioFormatReader> rd (fm.createReaderFor (in));
    if (rd == nullptr || rd->sampleRate <= 0 || rd->lengthInSamples > (int64) (rd->sampleRate * 600)) return false;
    AudioBuffer<float> raw (2, (int) rd->lengthInSamples);
    raw.clear();
    rd->read (&raw, 0, raw.getNumSamples(), 0, true, rd->numChannels > 1);
    if (rd->numChannels == 1) raw.copyFrom (1, 0, raw, 0, 0, raw.getNumSamples());
    lp::song::stack::express (raw, rd->sampleRate, bends); // (a drawn melody's vibrato and slides)
    if (loopSeconds > 0.0 && totalSeconds > loopSeconds)
    {
        // a loop: its bars, exactly, repeated over the whole stretch (each repeat blended in over 5 ms)
        const int L = jmin (raw.getNumSamples(), (int) std::round (loopSeconds * rd->sampleRate));
        const int T = (int) std::round (totalSeconds * rd->sampleRate), xf = jmin (L / 4, (int) (0.005 * rd->sampleRate));
        if (L > 64 && T > L)
        {
            AudioBuffer<float> tiled (2, T);
            tiled.clear();
            for (int at = 0; at < T; at += L)
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < L + xf && at + i < T; ++i)
                    {
                        const int k = i % L == i ? i : i - L; // (past the end: the loop's start again, faded)
                        float v = raw.getSample (ch, i < L ? i : k);
                        if (i >= L) v *= 1.0f - (float) (i - L) / (float) xf;
                        if (at > 0 && i < xf) v *= (float) i / (float) xf;
                        tiled.addSample (ch, at + i, v);
                    }
            raw = std::move (tiled);
        }
    }
    const int len = raw.getNumSamples();
    const double ratio = rd->sampleRate / StudioPlayer::fileRate;
    const int pad = (int) std::round (startSeconds * StudioPlayer::fileRate), outLen = (int) (len / ratio);
    AudioBuffer<float> buf (2, pad + outLen);
    buf.clear();
    for (int ch = 0; ch < 2; ++ch) { LagrangeInterpolator li; li.process (ratio, raw.getReadPointer (ch), buf.getWritePointer (ch, pad), outLen); }
    out.deleteFile();
    WavAudioFormat wav;
    std::unique_ptr<AudioFormatWriter> w (wav.createWriterFor (new FileOutputStream (out), StudioPlayer::fileRate, 2, 24, {}, 0));
    return w != nullptr && w->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
}

// a phrase sung again on its own, put into the rest of the take: the take as it is now everywhere else, the new
// phrase where it was sung again (with short crossfades at each end)
static bool mergeInto (const File& padded, const LivePlanetProcessor::DrawnMerge& m)
{
    AudioFormatManager fm;
    fm.registerBasicFormats();
    AudioBuffer<float> fresh;
    double rate = StudioPlayer::fileRate;
    {
        std::unique_ptr<AudioFormatReader> rd (fm.createReaderFor (padded));
        if (rd == nullptr) return false;
        rate = rd->sampleRate;
        fresh.setSize (2, (int) rd->lengthInSamples);
        rd->read (&fresh, 0, fresh.getNumSamples(), 0, true, true);
    }
    const auto& base = *m.base;
    const int a = (int) (m.start * rate);
    AudioBuffer<float> out (2, jmax (fresh.getNumSamples(), a + base.getNumSamples()));
    out.clear();
    for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, a, base, jmin (ch, base.getNumChannels() - 1), 0, base.getNumSamples());
    const int xf = (int) (0.03 * rate);
    for (auto [s0, s1] : m.ranges)
    {
        const int r0 = jmax (0, a + (int) (s0 * rate)), r1 = jmin (fresh.getNumSamples(), a + (int) (s1 * rate));
        for (int i = r0; i < r1; ++i)
        {
            const float w = jmin (1.0f, jmin ((float) (i - r0) / (float) xf, (float) (r1 - i) / (float) xf));
            for (int ch = 0; ch < 2; ++ch)
                out.setSample (ch, i, out.getSample (ch, i) * (1.0f - w) + fresh.getSample (ch, i) * w);
        }
    }
    padded.deleteFile();
    WavAudioFormat wav;
    std::unique_ptr<AudioFormatWriter> w (wav.createWriterFor (new FileOutputStream (padded), rate, 2, 24, {}, 0));
    return w != nullptr && w->writeFromAudioSampleBuffer (out, 0, out.getNumSamples());
}

// every frame: the helper's layer and drawn-melody jobs, fetched when done and added to the song once
void AutoAdd::tick (LivePlanetProcessor& proc)
{
    const auto snap = proc.voiceClient.snapshot();
    const auto st = proc.songJob.getStatus();
    if (! st.haveSong) return;
    const auto folder = st.files.folder.getChildFile ("voice");
    status.clear();
    pending.clear();
    bool newest = true; // (what the newest one is doing is what's shown)
    for (auto& j : snap.jobs)
    {
        const auto tagIt = snap.tags.find (j.id);
        if (tagIt == snap.tags.end()) continue;
        StringArray tag;
        tag.addTokens (tagIt->second, "|", "");
        if (tag.size() < 3 || (tag[0] != "layer" && tag[0] != "drawn")) continue;
        const bool layer = tag[0] == "layer";
        const double start = tag[1].getDoubleValue();
        const String what = tag[2];
        if (j.live() || (j.state == "done" && handled.count (j.id + "/1") == 0))
        {
            // still being made (or just made, on its way in): where it goes, pulsing in the arrangement
            double len = 0.0;
            if (layer && tag.size() > 3) len = tag[3].getDoubleValue();
            if (! layer)
                for (auto& sc : st.song.sections)
                    if (String (sc.name) == what) len = sc.bars * 240.0 / jmax (40.0, st.song.bpm);
            if (len > 0.0)
                pending.push_back ({ start, len, (layer ? "AI " + what.replace ("_", " ") : String ("singing your melody"))
                                                   + (j.note.isNotEmpty() ? String::fromUTF8 ("  \xc2\xb7  ") + j.note : String()) });
        }
        if (newest)
        {
            newest = false;
            if (j.live())
                status = (layer ? "AI " + what.replace ("_", " ") : String ("Singing your melody")) + ": " + (j.note.isNotEmpty() ? j.note : String ("starting")) + "...";
            else if (j.state == "error")
                status = (layer ? "The AI layer" : String ("Your melody")) + " didn't work: " + j.error.substring (0, 120);
        }
        if (j.state != "done") continue;
        for (auto& r : j.results)
        {
            const auto key = j.id + "/" + String (r.n);
            if (handled.count (key) > 0) continue;
            const auto f = snap.files.find (key);
            if (f == snap.files.end())
            {
                if (asked.insert (key).second)
                {
                    folder.createDirectory();
                    proc.voiceClient.fetch (j.id, r.n, folder.getChildFile ((layer ? "layer " : "drawn ") + j.id + " " + String (r.n) + ".wav"));
                }
                continue;
            }
            handled.insert (key);
            const auto out = folder.getChildFile ((layer ? what.replace ("_", " ") : String ("drawn")) + " v" + String (r.n) + " " + j.id.substring (0, 4) + ".wav");
            // (a drawn melody: one track per section, each new take replacing the last)
            const String name = layer ? "AI " + what.replace ("_", " ") + (j.results.size() > 1 ? " " + String (r.n) : String()) : "drawn, " + what;
            const String about = layer ? "played by the AI (voice helper) over your song: " + j.title : "your drawn melody, sung by the voice helper";
            auto* p = &proc;
            std::vector<lp::song::stack::Bend> bend;
            if (tag.size() > 3) if (auto b = bends.find (tag[3]); b != bends.end()) bend = b->second;
            LivePlanetProcessor::DrawnMerge merge;
            if (tag.size() > 3) if (auto m = proc.drawnMerge.find (tag[3]); m != proc.drawnMerge.end()) merge = m->second;
            // a loop layer: "layer|start|kind|length|loopSeconds"
            const double loopSecs = layer && tag.size() > 4 ? tag[4].getDoubleValue() : 0.0, totalSecs = layer && tag.size() > 3 ? tag[3].getDoubleValue() : 0.0;
            Thread::launch ([p, in = f->second, out, start, name, about, layer, bend, tag, merge, loopSecs, totalSecs]
            {
                if (! padToSongStart (in, out, start, bend, loopSecs, totalSecs)) return;
                if (merge.base != nullptr && ! mergeInto (out, merge)) return;
                MessageManager::callAsync ([p, out, name, about, layer, token = tag.size() > 3 ? tag[3] : String()]
                {
                    p->songJob.addSungVocal (out, name, about, ! layer, layer ? "AI layer" : "drawn and sung", ! layer);
                    if (token.isNotEmpty()) p->drawnFiles[token] = out; // (Draw & sing plays your voice live from it)
                });
            });
        }
    }
}

// ---- "+ AI layer": which instrument, how it should sound
// the library loop kinds a chip flips (its own first, then close ones)
static std::vector<lp::song::LoopKind> flipKindsFor (const String& id)
{
    using LK = lp::song::LoopKind;
    if (id == "drums") return { LK::Drums, LK::Perc };
    if (id == "bass") return { LK::Bass };
    if (id == "guitar") return { LK::Guitar, LK::Pluck };
    if (id == "keyboard") return { LK::Keys, LK::Mallet };
    if (id == "strings") return { LK::Strings, LK::Pad };
    if (id == "synth") return { LK::Synth, LK::Lead, LK::Pluck, LK::Pad };
    if (id == "brass") return { LK::Brass };
    if (id == "woodwinds") return { LK::Woodwind };
    if (id == "percussion") return { LK::Perc, LK::Drums };
    if (id == "fx") return { LK::FX };
    if (id == "backing_vocals") return { LK::Vocal };
    return { LK::Other };
}
LayerPopup::LayerPopup (LivePlanetProcessor& p, const lp::ui::Theme*& theme, int first, int count, std::function<void()> closed)
    : proc (p), th (theme), firstSection (first), sectionCount (count), onClosed (std::move (closed))
{
    static const std::pair<const char*, const char*> kinds[] = { { "Drums", "drums" }, { "Bass", "bass" }, { "Guitar", "guitar" }, { "Keys", "keyboard" },
        { "Strings", "strings" }, { "Synth", "synth" }, { "Brass", "brass" }, { "Woodwinds", "woodwinds" }, { "Percussion", "percussion" },
        { "FX", "fx" }, { "Backing vocals", "backing_vocals" } };
    for (auto& [label, id] : kinds)
    {
        auto* b = chips.add (new TextButton (label));
        b->setWantsKeyboardFocus (false);
        b->getProperties().set ("id", id);
        b->getProperties().set ("glass", true);
        b->onClick = [this, b] { pickKind (b); };
        addAndMakeVisible (b);
    }
    pickKind (chips[1]);
    struct T { TextButton* b; const char* tip; };
    for (auto [b, tip] : { T { &asPart, "A part played all the way through your bars, following the song" },
                           T { &asLoop, "A short loop that repeats over your bars, like a sample loop (quicker to make)" },
                           T { &loop1, "a 1-bar loop" }, T { &loop2, "a 2-bar loop" }, T { &loop4, "a 4-bar loop" } })
    {
        b->setTooltip (tip);
        b->getProperties().set ("glass", true);
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (*b);
    }
    for (auto [b, tip] : { T { &modeCreate, "Type what you want (\"a vibrant colorful synth\"): the sample in your library that matches it best, at the song's tempo, following the song's chords" },
                           T { &modeFlip, "A loop from your own sample library, in the song's key and tempo, chopped up and re-ordered so it's yours (quick, no AI)" },
                           T { &modeAi, "The AI makes new audio for it (needs the voice helper; slower, and hit or miss)" },
                           T { &amtLight, "A few slices moved: still close to the loop" }, T { &amtMed, "Re-ordered, some slices backwards, a stutter now and then" },
                           T { &amtHeavy, "Chopped hard: a new riff from the loop's pieces" } })
    {
        b->setTooltip (tip);
        b->getProperties().set ("glass", true);
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (*b);
    }
    modeCreate.onClick = [this] { setMode (0); };
    modeAi.setVisible (false); // (AI audio: retired here; generate sound plays real samples)
    {
        // whose notes the new sound plays: a part of the song with notes (keys, chords, a lead...), or a new part
        const auto st = proc.songJob.getStatus();
        notesFrom.addItem ("a new part, written on your chords (in key)", 1);
        int pick = 1;
        for (int i = 0; i < (int) st.tracks.size(); ++i)
        {
            const String n = st.tracks[(size_t) i].name;
            if (n.containsIgnoreCase ("drum") || n.containsIgnoreCase ("perc") || n.containsIgnoreCase ("transition") || n.containsIgnoreCase ("fx")
                || n == "vocal chops") continue;
            // (a vocal: the melody you sang, read from it note by note)
            const bool sung = st.tracks[(size_t) i].vocal || n.startsWithIgnoreCase ("vocal");
            notesFrom.addItem (sung ? "the melody you sang in " + n : "the notes of " + n, 100 + i);
            if (pick == 1 && (n.containsIgnoreCase ("key") || n.containsIgnoreCase ("piano") || n.containsIgnoreCase ("chord") || n.containsIgnoreCase ("rhodes")))
                pick = 100 + i;
        }
        notesFrom.setSelectedId (pick, dontSendNotification);
        notesFrom.onChange = [this] { repaint(); };
        addAndMakeVisible (notesFrom);
    }
    modeFlip.onClick = [this] { setMode (1); };
    modeAi.onClick = [this] { setMode (2); };
    amtLight.onClick = [this] { flipAmount = 1; showType(); };
    amtMed.onClick = [this] { flipAmount = 2; showType(); };
    amtHeavy.onClick = [this] { flipAmount = 3; showType(); };
    asPart.onClick = [this] { loopBars = 0; showType(); };
    asLoop.onClick = [this] { if (loopBars == 0) loopBars = 2; showType(); };
    loop1.onClick = [this] { loopBars = 1; showType(); };
    loop2.onClick = [this] { loopBars = 2; showType(); };
    loop4.onClick = [this] { loopBars = 4; showType(); };
    describe.setTextToShowWhenEmpty ("describe it: a vibrant colorful synth", Colours::grey);
    describe.setFont (font (14.0f));
    describe.setIndents (12, 9);
    addAndMakeVisible (describe);
    for (auto* b : { &go, &cancel }) { b->setWantsKeyboardFocus (false); addAndMakeVisible (*b); }
    go.getProperties().set ("ai", true);
    cancel.getProperties().set ("glass", true);
    describe.setColour (TextEditor::backgroundColourId, Colours::white.withAlpha (0.05f));
    describe.setColour (TextEditor::outlineColourId, Colours::white.withAlpha (0.14f));
    describe.setColour (TextEditor::focusedOutlineColourId, th->grassLight.withAlpha (0.8f));
    go.onClick = [this] { start(); };
    describe.onReturnKey = [this] { start(); };
    setMode (0);
    cancel.onClick = [this] { if (onClosed) onClosed(); };
}

void LayerPopup::useNotesOf (int track)
{
    // (any track you right-clicked is offered, even one the list leaves out)
    if (notesFrom.indexOfItemId (100 + track) < 0)
    {
        const auto st = proc.songJob.getStatus();
        if (track >= 0 && track < (int) st.tracks.size()) notesFrom.addItem ("the notes of " + String (st.tracks[(size_t) track].name), 100 + track);
    }
    if (notesFrom.indexOfItemId (100 + track) >= 0) notesFrom.setSelectedId (100 + track, dontSendNotification);
    setMode (0);
}

void LayerPopup::setMode (int m)
{
    mode = m;
    if (mode == 0) { kind = {}; for (auto* c : chips) { c->getProperties().set ("primary", false); c->repaint(); } } // (an instrument is just a hint here)
    else if (kind.isEmpty()) pickKind (chips[1]);
    describe.setTextToShowWhenEmpty (mode == 0 ? "describe it: a vibrant colorful synth" : "describe it: funky disco bass, syncopated", Colours::grey);
    showType();
    resized();
    repaint();
    if (mode != 1 && isShowing()) describe.grabKeyboardFocus();
}

void LayerPopup::showType()
{
    auto lit = [] (TextButton& b, bool on) { b.getProperties().set ("primary", on); b.repaint(); };
    lit (asPart, loopBars == 0);
    lit (asLoop, loopBars > 0);
    lit (loop1, loopBars == 1);
    lit (loop2, loopBars == 2);
    lit (loop4, loopBars == 4);
    for (auto* b : { &loop1, &loop2, &loop4 }) b->setVisible (mode == 2 && loopBars > 0);
    lit (modeCreate, mode == 0);
    lit (modeFlip, mode == 1);
    lit (modeAi, mode == 2);
    lit (amtLight, flipAmount == 1);
    lit (amtMed, flipAmount == 2);
    lit (amtHeavy, flipAmount == 3);
    for (auto* b : { &asPart, &asLoop }) b->setVisible (mode == 2);
    {
        // flip: how many loops your library has for each (none: greyed out)
        const auto st = proc.songJob.getStatus();
        for (auto* c : chips)
        {
            const auto id = c->getProperties()["id"].toString();
            const String base = c->getProperties().getWithDefault ("label", c->getButtonText()).toString();
            c->getProperties().set ("label", base);
            int n = 0;
            for (auto lk : flipKindsFor (id)) n += (size_t) lk < st.kindCounts.size() ? st.kindCounts[(size_t) lk] : 0;
            c->setButtonText (mode != 2 ? base + "  " + String (n) : base);
            c->setEnabled (mode == 2 || n > 0);
        }
        // (the one picked has nothing to flip: the first that has)
        for (auto* c : chips)
            if (mode == 1 && c->getProperties()["id"].toString() == kind && ! c->isEnabled())
            {
                for (auto* d : chips) if (d->isEnabled()) { pickKind (d); break; }
                break;
            }
    }
    for (auto* b : { &amtLight, &amtMed, &amtHeavy }) b->setVisible (mode == 1);
    notesFrom.setVisible (mode == 0);
    for (auto* c : chips) c->setVisible (mode != 0); // (generate sound: your words say what it is)
    describe.setVisible (mode != 1);
    go.setButtonText (mode == 0 ? "Generate sound" : mode == 1 ? "Flip it" : "Generate");
}

void LayerPopup::pickKind (TextButton* b)
{
    if (mode == 0 && kind == b->getProperties()["id"].toString()) { kind = {}; b->getProperties().set ("primary", false); b->repaint(); return; } // (a hint: click again to drop it)
    kind = b->getProperties()["id"].toString();
    for (auto* c : chips) { c->getProperties().set ("primary", c == b); c->repaint(); }
}

void LayerPopup::start()
{
    const auto st = proc.songJob.getStatus();
    if (! st.haveSong || firstSection < 0 || firstSection + sectionCount > (int) st.song.sections.size()) return;
    if (mode == 0)
    {
        // what you typed: a sound from your library that fits it, playing a part's notes (or a new part on your chords)
        const String words = describe.getText().trim();
        if (words.isEmpty()) { message = "Type what it should sound like, e.g. \"a vibrant colorful synth\"."; repaint(); describe.grabKeyboardFocus(); return; }
        const int id = notesFrom.getSelectedId();
        proc.songJob.resound (id >= 100 ? id - 100 : -1, words, false, firstSection, sectionCount);
        if (onClosed) onClosed();
        return;
    }
    if (mode == 1)
    {
        // its own kind if your library has loops of it, else the closest kind that does
        int k = -1;
        for (auto lk : flipKindsFor (kind))
            if ((size_t) lk < st.kindCounts.size() && st.kindCounts[(size_t) lk] > 0) { k = (int) lk; break; }
        if (k < 0) { message = "No loops of that kind in your sample folders yet: add a folder (Write page), or try AI generate."; repaint(); return; }
        proc.songJob.flipLayer ((lp::song::LoopKind) k, firstSection, sectionCount, flipAmount);
        if (onClosed) onClosed();
        return;
    }
    const auto snap = proc.voiceClient.snapshot();
    if (! snap.connected) { message = "The voice helper isn't running: start it, then try again."; repaint(); return; }
    lp::voice::Client::Request r;
    const auto& a = st.song.sections[(size_t) firstSection];
    String label = String (chips[0]->getButtonText());
    for (auto* c : chips) if (c->getProperties()["id"].toString() == kind) label = c->getProperties().getWithDefault ("label", c->getButtonText()).toString();
    r.style = describe.getText().trim().isNotEmpty() ? describe.getText().trim() : label.toLowerCase();
    r.layer = kind;
    r.key = helperKeyName (st.song);
    r.bpm = st.song.bpm;
    r.takes = r.results = 1;
    r.consent = true; // (no voice is used)
    r.clean = false;
    int bars = 0;
    for (int i = firstSection; i < firstSection + sectionCount; ++i) bars += st.song.sections[(size_t) i].bars;
    // (the layer's place in the song: where it starts, what it is, how long: shown pulsing until it's made)
    r.tag = "layer|" + String (a.startBar * 240.0 / st.song.bpm, 3) + "|" + kind + "|" + String (bars * 240.0 / st.song.bpm, 3);
    if (loopBars > 0)
    {
        r.loopBars = jmin (loopBars, bars);
        r.tag << "|" << String (r.loopBars * 240.0 / st.song.bpm, 4); // (Loopwright repeats it over the bars)
    }
    r.renderTrack = [&pr = proc, f = firstSection, n = sectionCount] { return pr.renderSectionForVoice (f, n); };
    proc.voiceClient.create (std::move (r));
    if (onClosed) onClosed();
}

void LayerPopup::resized()
{
    auto c = cardBounds().reduced (24, 20);
    c.removeFromTop (54);
    {
        auto t = c.removeFromTop (32);
        modeCreate.setBounds (t.removeFromLeft (150));
        t.removeFromLeft (8);
        modeFlip.setBounds (t.removeFromLeft (140));
        c.removeFromTop (14);
    }
    if (mode == 0)
    {
        // what it should sound like (typed), and whose notes it plays
        describe.setBounds (c.removeFromTop (42));
        c.removeFromTop (12);
        notesFrom.setBounds (c.removeFromTop (34));
        c.removeFromTop (18);
        auto bt = c.removeFromTop (40);
        cancel.setBounds (bt.removeFromRight (100));
        bt.removeFromRight (10);
        go.setBounds (bt);
        return;
    }
    else if (mode == 1)
    {
        auto t = c.removeFromTop (30);
        amtLight.setBounds (t.removeFromLeft (100)); t.removeFromLeft (6);
        amtMed.setBounds (t.removeFromLeft (84)); t.removeFromLeft (6);
        amtHeavy.setBounds (t.removeFromLeft (80));
        c.removeFromTop (14);
    }
    else
    {
        // what kind: a part played through, or a loop (and how long)
        auto t = c.removeFromTop (30);
        asPart.setBounds (t.removeFromLeft (120));
        t.removeFromLeft (8);
        asLoop.setBounds (t.removeFromLeft (80));
        t.removeFromLeft (16);
        for (auto* b : { &loop1, &loop2, &loop4 }) { b->setBounds (t.removeFromLeft (64)); t.removeFromLeft (6); }
        c.removeFromTop (14);
    }
    auto row = c.removeFromTop (30);
    int x = row.getX();
    for (auto* b : chips)
    {
        const int w = (int) GlyphArrangement::getStringWidth (font (13.0f), b->getButtonText()) + 28;
        if (x + w > c.getRight()) { row = c.removeFromTop (38).withTrimmedTop (8); x = row.getX(); }
        b->setBounds (x, row.getY(), w, 30);
        x += w + 8;
    }
    c.removeFromTop (16);
    if (mode == 2) { describe.setBounds (c.removeFromTop (38)); c.removeFromTop (16); }
    auto bt = c.removeFromTop (40);
    cancel.setBounds (bt.removeFromRight (100));
    bt.removeFromRight (10);
    go.setBounds (bt);
}

Rectangle<int> LayerPopup::cardBounds() const { return getLocalBounds().withSizeKeepingCentre (600, mode == 0 ? 286 : mode == 1 ? 310 : 400); }

void LayerPopup::setBackdrop (Image img)
{
    // a soft blur: shrunk a lot, then smoothly stretched back (twice, so no blocks show)
    if (! img.isValid()) return;
    const int w = jmax (8, img.getWidth() / 6), h = jmax (8, img.getHeight() / 6);
    Image small (Image::ARGB, w, h, true);
    {
        Graphics sg (small);
        sg.setImageResamplingQuality (Graphics::highResamplingQuality);
        sg.drawImage (img, 0, 0, w, h, 0, 0, img.getWidth(), img.getHeight());
    }
    Image mid (Image::ARGB, w * 3, h * 3, true);
    {
        Graphics mg (mid);
        mg.setImageResamplingQuality (Graphics::highResamplingQuality);
        mg.drawImage (small, 0, 0, w * 3, h * 3, 0, 0, w, h);
    }
    Image again (Image::ARGB, w / 2 + 1, h / 2 + 1, true);
    {
        Graphics ag (again);
        ag.setImageResamplingQuality (Graphics::highResamplingQuality);
        ag.drawImage (mid, 0, 0, again.getWidth(), again.getHeight(), 0, 0, mid.getWidth(), mid.getHeight());
    }
    backdrop = again;
    repaint();
}

void LayerPopup::paint (Graphics& g)
{
    // frosted glass: the song behind it, blurred and dimmed; the card a light pane over it
    if (backdrop.isValid())
    {
        g.setImageResamplingQuality (Graphics::highResamplingQuality);
        g.drawImage (backdrop, getLocalBounds().toFloat(), RectanglePlacement::stretchToFit);
    }
    g.fillAll (Colours::black.withAlpha (backdrop.isValid() ? 0.42f : 0.6f));
    const auto r = cardBounds().toFloat();
    const float rad = 18.0f;
    {
        Path p;
        p.addRoundedRectangle (r, rad);
        DropShadow (Colours::black.withAlpha (0.55f), 40, { 0, 14 }).drawForPath (g, p);
    }
    g.setColour (th->body.withAlpha (0.72f));
    g.fillRoundedRectangle (r, rad);
    {
        // a sheen: lighter at the top, a faint coral glow from the top-left corner
        ColourGradient sheen (Colours::white.withAlpha (0.10f), 0.0f, r.getY(), Colours::white.withAlpha (0.015f), 0.0f, r.getY() + r.getHeight() * 0.6f, false);
        g.setGradientFill (sheen);
        g.fillRoundedRectangle (r, rad);
        ColourGradient glow (Colour (0xffff7a6a).withAlpha (0.13f), r.getX(), r.getY(), Colour (0xffff7a6a).withAlpha (0.0f), r.getX() + 260.0f, r.getY() + 200.0f, true);
        g.setGradientFill (glow);
        g.fillRoundedRectangle (r, rad);
    }
    // a crisp rim: bright along the top edge, fading down the sides
    {
        ColourGradient rim (Colours::white.withAlpha (0.34f), 0.0f, r.getY(), Colours::white.withAlpha (0.07f), 0.0f, r.getBottom(), false);
        g.setGradientFill (rim);
        g.drawRoundedRectangle (r.reduced (0.5f), rad, 1.0f);
    }
    auto in = r.reduced (24.0f, 20.0f);
    g.setFont (font (21.0f, lp::ui::Medium, -0.01f));
    g.setColour (Colours::white);
    g.drawText ("Generate a layer", in.removeFromTop (26.0f), Justification::centredLeft, false);
    const auto st = proc.songJob.getStatus();
    String where;
    if (firstSection == 0 && sectionCount >= (int) st.song.sections.size()) where = "the whole song";
    else
    for (int i = firstSection; i < firstSection + sectionCount && i < (int) st.song.sections.size(); ++i)
        where << (where.isEmpty() ? "" : " + ") << String (st.song.sections[(size_t) i].name);
    g.setFont (font (12.0f));
    g.setColour (th->inkDim);
    g.drawText (mode == 0 ? (notesFrom.getSelectedId() >= 100 ? notesFrom.getText().replace ("the notes of", "The notes of").replace ("the melody you sang", "The melody you sang") + " over " + where + ", played on a sound from your library that fits your words."
                                                             : "A sound from your library that fits your words, over " + where + ", in time and following your chords.")
                : mode == 1 ? "A loop from your library flipped over " + where + ": in key, in time, chopped so it's yours."
                            : "New audio made by the AI over " + where + ", in key and in time (needs the voice helper).", in.removeFromTop (20.0f), Justification::centredLeft, true);
    if (message.isNotEmpty())
    {
        g.setColour (Colour (0xffff8f8f));
        g.drawText (message, r.reduced (24.0f, 8.0f).removeFromBottom (18.0f), Justification::centredLeft, true);
    }
}
} // namespace lp::ai
