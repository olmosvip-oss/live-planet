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
static bool padToSongStart (const File& in, const File& out, double startSeconds, const std::vector<lp::song::stack::Bend>& bends = {})
{
    AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<AudioFormatReader> rd (fm.createReaderFor (in));
    if (rd == nullptr || rd->sampleRate <= 0 || rd->lengthInSamples > (int64) (rd->sampleRate * 600)) return false;
    const int len = (int) rd->lengthInSamples;
    AudioBuffer<float> raw (2, len);
    raw.clear();
    rd->read (&raw, 0, len, 0, true, rd->numChannels > 1);
    if (rd->numChannels == 1) raw.copyFrom (1, 0, raw, 0, 0, len);
    lp::song::stack::express (raw, rd->sampleRate, bends); // (a drawn melody's vibrato and slides)
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
            Thread::launch ([p, in = f->second, out, start, name, about, layer, bend, tag, merge]
            {
                if (! padToSongStart (in, out, start, bend)) return;
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
        b->onClick = [this, b] { pickKind (b); };
        addAndMakeVisible (b);
    }
    pickKind (chips[1]);
    describe.setTextToShowWhenEmpty ("describe it: funky disco bass, syncopated", Colours::grey);
    describe.setFont (font (14.0f));
    describe.setIndents (12, 9);
    addAndMakeVisible (describe);
    for (auto* b : { &go, &cancel }) { b->setWantsKeyboardFocus (false); addAndMakeVisible (*b); }
    go.getProperties().set ("primary", true);
    go.onClick = [this] { start(); };
    cancel.onClick = [this] { if (onClosed) onClosed(); };
}

void LayerPopup::pickKind (TextButton* b)
{
    kind = b->getProperties()["id"].toString();
    for (auto* c : chips) { c->getProperties().set ("primary", c == b); c->repaint(); }
}

void LayerPopup::start()
{
    const auto st = proc.songJob.getStatus();
    if (! st.haveSong || firstSection < 0 || firstSection + sectionCount > (int) st.song.sections.size()) return;
    const auto snap = proc.voiceClient.snapshot();
    if (! snap.connected) { message = "The voice helper isn't running: start it, then try again."; repaint(); return; }
    lp::voice::Client::Request r;
    const auto& a = st.song.sections[(size_t) firstSection];
    String label = String (chips[0]->getButtonText());
    for (auto* c : chips) if (c->getProperties()["id"].toString() == kind) label = c->getButtonText();
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
    r.renderTrack = [&pr = proc, f = firstSection, n = sectionCount] { return pr.renderSectionForVoice (f, n); };
    proc.voiceClient.create (std::move (r));
    if (onClosed) onClosed();
}

void LayerPopup::resized()
{
    auto c = cardBounds().reduced (24, 20);
    c.removeFromTop (54);
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
    describe.setBounds (c.removeFromTop (38));
    c.removeFromTop (16);
    auto bt = c.removeFromTop (40);
    cancel.setBounds (bt.removeFromRight (100));
    bt.removeFromRight (10);
    go.setBounds (bt);
}

Rectangle<int> LayerPopup::cardBounds() const { return getLocalBounds().withSizeKeepingCentre (560, 330); }

void LayerPopup::paint (Graphics& g)
{
    g.fillAll (Colours::black.withAlpha (0.55f));
    const auto r = cardBounds().toFloat();
    g.setColour (th->body);
    g.fillRoundedRectangle (r, 16.0f);
    g.setColour (th->line);
    g.drawRoundedRectangle (r.reduced (0.5f), 16.0f, 1.0f);
    auto in = r.reduced (24.0f, 20.0f);
    g.setFont (font (20.0f, lp::ui::Medium));
    g.setColour (th->ink);
    g.drawText ("Generate a layer", in.removeFromTop (26.0f), Justification::centredLeft, false);
    const auto st = proc.songJob.getStatus();
    String where;
    for (int i = firstSection; i < firstSection + sectionCount && i < (int) st.song.sections.size(); ++i)
        where << (where.isEmpty() ? "" : " + ") << String (st.song.sections[(size_t) i].name);
    g.setFont (font (12.0f));
    g.setColour (th->inkDim);
    g.drawText ("Played by the AI over " + where + ", in key and in time. It lands as a new track.", in.removeFromTop (20.0f), Justification::centredLeft, true);
    if (message.isNotEmpty())
    {
        g.setColour (Colour (0xffff8f8f));
        g.drawText (message, r.reduced (24.0f, 8.0f).removeFromBottom (18.0f), Justification::centredLeft, true);
    }
}
} // namespace lp::ai
