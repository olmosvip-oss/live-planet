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
    lyrics.onTextChange = [this] { refreshButtons(); repaint(); };
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

    section.onChange = [this] { refreshButtons(); repaint(); };
    for (auto* c : { &section, &voice })
    {
        c->setWantsKeyboardFocus (false);
        addAndMakeVisible (*c);
    }
    voice.onChange = [this] { if (voice.getText().isNotEmpty()) lp::setSetting ("voice", voice.getText()); refreshButtons(); }; // (your voice: the one picked everywhere next time)
    addVoice.setWantsKeyboardFocus (false);
    addAndMakeVisible (addVoice);
    addAndMakeVisible (takes);
    addAndMakeVisible (versions);
    who.setTooltip ("Write melody: Loopwright writes a melody for your words over the song's own chords, and the voice sings it note for "
                    "note (every word, in key, in time). I sing it: you rap or sing the lines over the section (rough is fine), and it keeps "
                    "your exact words and timing, then cleans, tunes and turns it into the voice.");
    addChildComponent (who); // (one way left: no switch to show)
    for (auto* b : { &recordTake, &useTake, &hearTake }) { b->setWantsKeyboardFocus (false); addChildComponent (*b); }
    recordTake.setTooltip ("Plays the section with a one-bar count-in and records your mic until it ends (in the Loopwright app; in a DAW, record there and drop it here)");
    useTake.setTooltip ("A recording of you over this section: from its first bar, or a bounce from bar 1 (it's cut to the section). You can also drop it on this page.");
    recordTake.onClick = [this] { if (takeRecording) finishTakeRecording(); else startTakeRecording(); };
    useTake.onClick = [this]
    {
        chooser = std::make_unique<FileChooser> ("Your take over the section", File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.m4a;*.caf");
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this] (const FileChooser& fc)
        {
            if (fc.getResult().existsAsFile()) useTakeFile (fc.getResult());
        });
    };
    hearTake.onClick = [this]
    {
        // your take with the song, from its section
        if (hearTake.getButtonText() == "stop") { proc.setVoicePreview (nullptr); proc.previewPlaying = false; hearTake.setButtonText ("hear it"); return; }
        const int c = section.getSelectedItemIndex();
        if (! takeFile.existsAsFile() || c < 0 || c >= (int) sectionInfo.size()) return;
        AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<AudioFormatReader> rd (fm.createReaderFor (takeFile));
        if (rd == nullptr || rd->sampleRate <= 0) return;
        const double start = sectionInfo[(size_t) c].start, ratio = rd->sampleRate / StudioPlayer::fileRate;
        const int len = (int) jmin<int64> (rd->lengthInSamples, (int64) (rd->sampleRate * 70.0)), pad = (int) (start * StudioPlayer::fileRate), outLen = (int) (len / ratio);
        AudioBuffer<float> raw (2, len);
        raw.clear();
        rd->read (&raw, 0, len, 0, true, rd->numChannels > 1);
        if (rd->numChannels == 1) raw.copyFrom (1, 0, raw, 0, 0, len);
        auto buf = std::make_shared<AudioBuffer<float>> (2, pad + outLen);
        buf->clear();
        for (int ch = 0; ch < 2; ++ch) { LagrangeInterpolator li; li.process (ratio, raw.getReadPointer (ch), buf->getWritePointer (ch, pad), outLen); }
        proc.setVoicePreview (buf);
        proc.previewPos = jmax (0.0, start - 0.5) * StudioPlayer::fileRate;
        proc.previewPlaying = true;
        hearTake.setButtonText ("stop");
    };
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
    // (your own recording: background noise removed, as "I sing it" always did; switch it off for a clean studio take)
    clean.setToggleState (true, dontSendNotification);
    clean.onClick = [this] { refreshButtons(); };
    consent.onClick = [this] { refreshButtons(); };
    create.getProperties().set ("primary", true);
    create.setWantsKeyboardFocus (false);
    create.onClick = [this] { startCreate(); };
    addAndMakeVisible (create);
    stop.setWantsKeyboardFocus (false);
    stop.onClick = [this] { if (! snap.jobs.empty()) proc.voiceClient.cancel (snap.jobs.front().id); };
    addChildComponent (stop);
    addVoice.setTooltip ("Add a voice: a clean recording of the singer (WAV, or a phone voice memo), 10 to 30 seconds, no music. It stays on this computer, with the voice helper.");
    addVoice.onClick = [this]
    {
        chooser = std::make_unique<FileChooser> ("A clean recording of the voice (no music)", File(), "*.wav;*.m4a;*.mp3;*.aif;*.aiff;*.flac;*.caf;*.aac");
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this] (const FileChooser& fc)
        {
            if (fc.getResult().existsAsFile()) proc.voiceClient.addVoice (fc.getResult());
        });
    };
    demo = std::getenv ("LP_VOICE_DEMO") != nullptr;
    addAndMakeVisible (voiceChip);
    startTimerHz (30);
}

void VoicePage::shown (bool isShown)
{
    if (! demo) proc.voiceClient.setWanted (isShown);
    if (! isShown) // (leaving: a version playing along stops)
        if (proc.hasVoicePreview()) { proc.setVoicePreview (nullptr); for (auto* r : rows) r->play.setButtonText ("play"); }
}

File VoicePage::voiceFolder() const { return songFolder.getChildFile ("voice"); }

// which of this song's jobs started where (so a version lines up with its section), kept with the song
void VoicePage::loadJobs()
{
    jobStarts.clear();
    const auto v = JSON::parse (voiceFolder().getChildFile ("jobs.json")); // (kept: the object lives in it)
    if (auto* o = v.getDynamicObject())
        for (auto& p : o->getProperties())
            if (lp::voice::validJobId (p.name.toString()) && p.value.isArray() && p.value.size() >= 2)
                jobStarts[p.name.toString()] = { (double) p.value[0], p.value[1].toString() };
}
void VoicePage::saveJobs()
{
    if (! songFolder.isDirectory()) return;
    auto* o = new DynamicObject();
    for (auto& [id, v] : jobStarts) o->setProperty (id, Array<var> { v.first, v.second });
    voiceFolder().createDirectory();
    voiceFolder().getChildFile ("jobs.json").replaceWithText (JSON::toString (var (o)));
}

// the singing model goes by the style most of all: if it says nothing about a voice, it may well hum or sing no
// words. So a style that doesn't is given one ("rap vocals" for hip hop, rap, trap or drill; else "lead vocals").
bool VoicePage::rapStyle (const String& styleText)
{
    const auto low = styleText.toLowerCase();
    return low.contains ("hip hop") || low.contains ("hip-hop") || low.contains ("hiphop") || low.contains ("rap")
        || low.contains ("trap") || low.contains ("drill") || low.contains ("grime");
}

String VoicePage::withVoiceWords (const String& styleText)
{
    auto st = styleText.trim();
    const auto low = st.toLowerCase();
    bool voiceWords = false;
    for (auto* w : { "vocal", "voice", "vox", "sing", "sung", "rapper", "rapping", "choir", "falsetto", "tenor", "baritone", "soprano", "alto" })
        voiceWords = voiceWords || low.contains (w);
    if (voiceWords) return st;
    return (st.isNotEmpty() ? st + ", " : String()) + (rapStyle (st) ? "rap vocals, clear lyrics" : "lead vocals, clear lyrics");
}

void VoicePage::startCreate()
{
    const int choice = section.getSelectedItemIndex();
    if (choice < 0 || choice >= (int) sectionInfo.size()) return;
    lp::voice::Client::Request r;
    r.lyrics = lyrics.getText();
    r.style = withVoiceWords (style.getText());
    r.voice = voice.getText();
    r.key = songKeyName;
    r.bpm = songBpmValue;
    r.takes = takesN;
    r.results = versionsN;
    if (whoSings == 1) { r.guide = takeFile; r.takes = 1; r.results = 1; }
    r.tune = (float) tuning.getValue();
    r.runs = roundToInt (runs.getValue());
    r.clean = clean.getToggleState();
    r.consent = consent.getToggleState();
    const auto sc = sectionInfo[(size_t) choice];
    r.tag = String (sc.start, 3) + "|" + sc.name;
    if (whoSings == 0)
    {
        // the melodies are written here (on the helper thread: hearing the chords takes a moment), then just sung
        r.takes = r.results = versionsN;
        r.clean = false;
        r.melodies = [&pr = proc, sc, words = r.lyrics, rap = rapStyle (style.getText()), n = versionsN]
                     { return pr.writeVoiceMelodies (sc.first, sc.count, words, rap, n); };
    }
    r.renderTrack = [&pr = proc, sc] { return pr.renderSectionForVoice (sc.first, sc.count); };
    proc.voiceClient.create (std::move (r));
    create.setEnabled (false);
}

void VoicePage::refresh()
{
    if (demo) return;
    // the song stopped: the version playing along stops too
    if (! proc.previewPlaying.load() && proc.hasVoicePreview())
    {
        proc.setVoicePreview (nullptr);
        for (auto* r : rows) r->play.setButtonText ("play");
    }
    // the song: its sections (the parts a voice can sing over), key and tempo
    const auto st = proc.songJob.getStatus();
    if (st.version != seenSongVersion)
    {
        seenSongVersion = st.version;
        songFolder = st.haveSong ? st.files.folder : File();
        loadJobs();
        StringArray names;
        sectionInfo.clear();
        int pick = 0;
        // each section, and two or three in a row (a verse and its chorus...), up to a minute: what the helper sings well
        if (st.haveSong)
        {
            auto cap = [] (String n) { return n.substring (0, 1).toUpperCase() + n.substring (1); };
            const auto& secs = st.song.sections;
            for (int count = 1; count <= 3; ++count)
                for (int i = 0; i + count <= (int) secs.size(); ++i)
                {
                    const auto& a = secs[(size_t) i];
                    const auto& b = secs[(size_t) (i + count - 1)];
                    const int bars = b.startBar + b.bars - a.startBar;
                    const double len = bars * 240.0 / st.song.bpm;
                    if (len < 4.0 || len > 60.0) continue;
                    String n = cap (String (a.name));
                    for (int k = 1; k < count; ++k) n << " + " << cap (String (secs[(size_t) (i + k)].name));
                    if (pick == 0 && count == 1 && n.startsWithIgnoreCase ("chorus")) pick = names.size();
                    names.add (n + "   (bars " + String (a.startBar + 1) + " to " + String (a.startBar + bars) + ", " + String (roundToInt (len)) + " s)");
                    sectionInfo.push_back ({ i, count, a.startBar * 240.0 / st.song.bpm, len, n });
                }
        }
        static const char* noteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const int minorKey = st.song.minor ? st.song.key : (st.song.key + 9) % 12; // (a major song: its relative minor, the same notes)
        songKeyName = noteNames[((minorKey % 12) + 12) % 12];
        songBpmValue = st.song.bpm;
        setSong (names, pick, ! st.haveSong ? String ("WRITE A SONG FIRST")
                                           : String (songKeyName) + " MINOR" + (st.song.minor ? String() : " (SAME NOTES AS " + String (noteNames[st.song.key % 12]) + " MAJOR)"),
                 String (roundToInt (st.song.bpm)) + " BPM");
    }
    // the helper's news
    const int ser = proc.voiceClient.serial();
    if (ser != seenSerial)
    {
        seenSerial = ser;
        auto s = proc.voiceClient.snapshot();
        bool changed = false;
        for (auto& [id, tag] : s.tags)
            if (! jobStarts.count (id))
            {
                jobStarts[id] = { tag.upToFirstOccurrenceOf ("|", false, false).getDoubleValue(), tag.fromFirstOccurrenceOf ("|", false, false) };
                changed = true;
            }
        if (changed) saveJobs();
        // each finished version of the newest song is fetched once, into the song's folder
        if (! s.jobs.empty() && songFolder.isDirectory())
            for (auto& r : s.jobs.front().results)
            {
                const auto key = s.jobs.front().id + "/" + String (r.n);
                if (asked.count (key) || s.files.count (key)) continue;
                asked.insert (key);
                proc.voiceClient.fetch (s.jobs.front().id, r.n, voiceFolder().getChildFile (s.jobs.front().id).getChildFile ("vocal_" + String (r.n) + ".wav"));
            }
        setSnapshot (s);
    }
}

// a fetched version: made to start at bar 1 (silent until its section), for dragging, as a track, and to play along
void VoicePage::prepare (ResultRow& row, const File& fetched)
{
    if (row.preparing || row.vocalFile.existsAsFile()) return;
    row.preparing = true;
    const auto it = jobStarts.find (row.jobId);
    const double start = it != jobStarts.end() ? it->second.first : 0.0;
    const String name = it != jobStarts.end() ? it->second.second : String ("vocal");
    const auto title = (snap.jobs.empty() ? String() : snap.jobs.front().title).retainCharacters ("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 '-").substring (0, 30).trim();
    const auto out = fetched.getSiblingFile ((title.isNotEmpty() ? title : String ("vocal")) + " v" + String (row.res.n) + " (" + name + ").wav");
    SafePointer<VoicePage> self (this);
    Component::SafePointer<ResultRow> rp (&row);
    Thread::launch ([self, rp, fetched, out, start]
    {
        AudioFormatManager fm;
        fm.registerBasicFormats();
        std::shared_ptr<AudioBuffer<float>> buf;
        if (std::unique_ptr<AudioFormatReader> rd (fm.createReaderFor (fetched)); rd != nullptr && rd->sampleRate > 0 && rd->lengthInSamples < (int64) (rd->sampleRate * 600))
        {
            const int len = (int) rd->lengthInSamples;
            AudioBuffer<float> raw (2, len);
            raw.clear();
            rd->read (&raw, 0, len, 0, true, rd->numChannels > 1);
            if (rd->numChannels == 1) raw.copyFrom (1, 0, raw, 0, 0, len);
            const double ratio = rd->sampleRate / StudioPlayer::fileRate;
            const int pad = (int) std::round (start * StudioPlayer::fileRate), outLen = (int) (len / ratio);
            buf = std::make_shared<AudioBuffer<float>> (2, pad + outLen);
            buf->clear();
            for (int ch = 0; ch < 2; ++ch) { LagrangeInterpolator li; li.process (ratio, raw.getReadPointer (ch), buf->getWritePointer (ch, pad), outLen); }
            out.deleteFile();
            WavAudioFormat wav;
            if (std::unique_ptr<AudioFormatWriter> w (wav.createWriterFor (new FileOutputStream (out), StudioPlayer::fileRate, 2, 24, {}, 0)); w != nullptr)
                w->writeFromAudioSampleBuffer (*buf, 0, buf->getNumSamples());
        }
        MessageManager::callAsync ([self, rp, out, buf, start]
        {
            if (rp == nullptr) return;
            rp->preparing = false;
            if (buf == nullptr || ! out.existsAsFile()) return;
            rp->vocalFile = out;
            rp->preview = buf;
            rp->startSec = start;
            rp->repaint();
            if (self != nullptr) self->refreshButtons();
        });
    });
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

int VoicePage::lyricLines() const
{
    StringArray lines;
    lines.addLines (lyrics.getText());
    int n = 0;
    for (auto& l : lines) if (l.trim().isNotEmpty() && ! l.trim().startsWith ("[")) ++n;
    return n;
}
double VoicePage::barSeconds() const { return 240.0 / jmax (40.0, songBpmValue); }
double VoicePage::chosenSeconds() const
{
    const int c = section.getSelectedItemIndex();
    return c >= 0 && c < (int) sectionInfo.size() ? sectionInfo[(size_t) c].secs : 0.0;
}

// from the helper's own timings on an Apple Silicon Mac: a take ~45 s, checking words ~30 s, noise removal ~2 min
// a version, the voice ~1.5x the length, loading the models ~40 s
int VoicePage::minutesEstimate() const
{
    const double secs = chosenSeconds() > 0.0 ? chosenSeconds() : 20.0;
    const bool mine = whoSings == 1;
    if (whoSings == 0) // (the voice read once, then each version sung at about its own length, finished and mixed)
        return jmax (1, (int) std::ceil ((40.0 + 30.0 + (lyricLines() > 0 ? 30.0 : 0.0) + versionsN * (secs * 1.2 + 35.0)) / 60.0));
    const int versionsMade = mine ? 1 : versionsN;
    double t = 40.0 + (mine ? (lyricLines() > 0 ? 30.0 : 0.0) : takesN * 45.0 + 30.0);
    t += versionsMade * ((clean.getToggleState() ? 120.0 : 0.0) + secs * 1.5 + 35.0);
    return jmax (1, (int) std::ceil (t / 60.0));
}

void VoicePage::refreshButtons()
{
    const bool working = snap.connected && ! snap.jobs.empty() && snap.jobs.front().live(); // (no helper: nothing is running)
    const bool mine = whoSings == 1;
    const bool haveTake = takeFile.existsAsFile() && takeChoice == section.getSelectedItemIndex() && ! takeRecording;
    const bool ready = snap.connected && consent.getToggleState() && ! working && ! snap.sending
                    && voice.getText().isNotEmpty() && section.getNumItems() > 0
                    && (mine ? haveTake // (your own take: the words are yours, however many)
                             : lyrics.getText().trim().isNotEmpty()
                               // (freestyle: under 2 bars a line, words come out mumbled; a written melody fits a line a bar)
                               && (demo || chosenSeconds() >= lyricLines() * (whoSings == 0 ? 1.0 : 2.0) * barSeconds() - 0.05));
    takes.setVisible (whoSings == 2);
    for (Component* c : { (Component*) &tuning, (Component*) &runs, (Component*) &clean })
        c->setVisible (whoSings != 0); // (a written melody is sung on its notes already; nothing to tune or clean)
    versions.setVisible (! mine);
    recordTake.setVisible (mine);
    useTake.setVisible (mine);
    hearTake.setVisible (mine && haveTake);
    recordTake.setEnabled (proc.canRecordMic());
    recordTake.setButtonText (takeRecording ? "Stop" : proc.canRecordMic() ? "Record my take" : "Record (in the app)");
    create.setEnabled (ready);
    // (greyed out: say what's missing, so it never just sits there)
    notReady = ready || working || snap.sending ? String()
             : ! snap.connected ? String ("Start the voice helper on this computer to create")
             : mine && ! haveTake ? String ("Record your take in the app, or drop / pick a recording of you over this section")
             : ! mine && lyrics.getText().trim().isEmpty() ? String ("Type your words first")
             : voice.getText().isEmpty() ? String ("Pick a voice")
             : ! consent.getToggleState() ? String ("Switch on \"this is my voice, or I have the singer's permission\" to create")
             : String();
    create.setButtonText (snap.sending ? "Sending your section..."
                          : (mine ? String ("Create from my take") : whoSings == 0 ? String ("Write & sing") : String ("Create vocal")) + String::fromUTF8 ("  \xc2\xb7  about ") + String (minutesEstimate()) + " min");
    if (stop.isVisible() != working) { stop.setVisible (working); resized(); }
    for (auto* r : rows)
    {
        r->play.setEnabled (r->preview != nullptr);
        r->toTrack.setEnabled (r->vocalFile.existsAsFile() && r->toTrack.getButtonText() != "added");
    }
}

void VoicePage::setSnapshot (const lp::voice::Snapshot& s)
{
    snap = s;
    if (! snap.message.isEmpty() && snap.messageBad) create.setEnabled (true);
    StringArray had;
    for (int i = 0; i < voice.getNumItems(); ++i) had.add (voice.getItemText (i));
    if (const auto voices = lp::voice::usableVoices (snap.voices); had != voices)
    {
        const auto keep = voice.getText();
        voice.clear (dontSendNotification);
        for (int i = 0; i < voices.size(); ++i) voice.addItem (voices[i], i + 1);
        voice.setText (lp::voice::defaultVoice (voices, keep, lp::setting ("voice", "").toString()), dontSendNotification);
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
    if (takeRecording)
    {
        const double now = proc.playheadPos.load() / StudioPlayer::fileRate;
        if (now >= takeStopAt || ! proc.previewPlaying.load()) finishTakeRecording();
        else repaint (setupCard().toNearestInt());
    }
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
        who.setBounds (c.getRight() - 300, c.getY() - 4, 300, 26);
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
        voiceChip.setBounds (voice.getBounds().getUnion (addVoice.getBounds()).withHeight (44).translated (0, -4));
        voice.setVisible (false);
        addVoice.setVisible (false);
        c.removeFromTop (30); // key / tempo / length, written in
        auto r3 = c.removeFromTop (92);
        auto segs = r3.removeFromLeft (r3.getWidth() / 2);
        {
            // (I sing it: your take instead of takes and versions)
            auto t = segs;
            auto row = t.removeFromTop (30);
            recordTake.setBounds (row.removeFromLeft (row.getWidth() / 2 - 3));
            row.removeFromLeft (6);
            useTake.setBounds (row);
            hearTake.setBounds (t.withTrimmedTop (12).removeFromTop (30).removeFromRight (70));
        }
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
                if (i >= rows.size())
                {
                    auto* nr = rows.add (new ResultRow (proc, th));
                    addAndMakeVisible (nr);
                    nr->play.onClick = [this, nr]
                    {
                        // play it with the song from its section (again: stop)
                        if (nr->play.getButtonText() == "stop") { proc.setVoicePreview (nullptr); proc.previewPlaying = false; nr->play.setButtonText ("play"); return; }
                        if (nr->preview == nullptr) return;
                        for (auto* r : rows) r->play.setButtonText ("play");
                        proc.setVoicePreview (nr->preview);
                        proc.previewPos = jmax (0.0, nr->startSec - 0.5) * StudioPlayer::fileRate;
                        proc.previewPlaying = true;
                        nr->play.setButtonText ("stop");
                    };
                    nr->toTrack.onClick = [this, nr]
                    {
                        if (! nr->vocalFile.existsAsFile()) return;
                        proc.songJob.addSungVocal (nr->vocalFile, "sung " + String (nr->res.n),
                                                   "sung by the voice helper (version " + String (nr->res.n) + "): words " + String (nr->res.words) + "%, in key " + String (nr->res.key) + "%");
                        nr->toTrack.setButtonText ("added");
                        nr->toTrack.setEnabled (false);
                    };
                }
                auto* row = rows[i];
                row->set (res, snap.jobs.front().id);
                if (auto f = snap.files.find (snap.jobs.front().id + "/" + String (res.n)); f != snap.files.end()) prepare (*row, f->second);
                row->setBounds (area.withY (area.getY() + (float) i * 52.0f).withHeight (47.0f).toNearestInt());
                row->setVisible (true);
                ++i;
            }
    }
}

Rectangle<float> VoicePage::resultsArea() const
{
    auto c = progressCard().reduced (18.0f, 16.0f);
    c.removeFromTop (24);
    c.removeFromTop (54 + 8 + 46 + 8); // the wave, then the steps
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
        const int n = lyricLines();
        const double have = chosenSeconds();
        // how many bars each line gets: under 2 they'd be crammed in and come out mumbled (blocked); 2 to 4 is rap
        // pace (fine, just said); 4 or more suits sung lines
        const double bar = barSeconds();
        const double perLine = n > 0 && bar > 0.0 ? have / bar / n : 99.0;
        const double least = whoSings == 0 ? 1.0 : 2.0; // (a written melody fits a line a bar; freestyle needs two)
        const bool tooMany = whoSings != 1 && n > 0 && have > 0.0 && ! demo && perLine < least - 0.01;
        const bool quick = whoSings != 1 && ! tooMany && n > 0 && have > 0.0 && ! demo && perLine < 2.0 * least - 0.01;
        // (too few: a line has to fill many bars, so the singer stretches, repeats or makes up words, and takes fail the words check)
        const bool tooFew = whoSings != 1 && n > 0 && have > 0.0 && ! demo && perLine > 6.0;
        g.setFont (font (12.5f, tooMany ? lp::ui::Medium : lp::ui::Light));
        g.setColour (tooMany ? Colour (0xffff8f8f) : quick || tooFew ? Colour (0xfff2c14e) : t.inkDim);
        const int fits = jmax (1, (int) (have / (least * bar)));
        const int bars = roundToInt (have / jmax (0.01, bar));
        const auto fit = whoSings == 1 ? String ("Your words (optional): only used to check how many of them come through")
                       : n == 0 ? String ("Type your words: one line per phrase (about 2 bars each for rap, 4 for singing).")
                       : tooMany ? String (n) + " lines need at least " + String (roundToInt (n * least)) + " bars; this part is " + String (bars)
                                   + ". Keep " + String (fits) + (fits == 1 ? " line" : " lines") + " or pick a longer part"
                       : quick ? String (n) + " lines in " + String (bars) + " bars: rap pace. For slow sung lines, pick a longer part"
                       : tooFew ? String (n) + (n == 1 ? " line" : " lines") + " for " + String (bars) + " bars: too few words, the singer will stretch or "
                                  "repeat them. Write about " + String (jmax (n + 1, bars / 2)) + " lines, or pick a shorter part"
                       : String (n) + (n == 1 ? " line" : " lines") + " in " + String (bars) + " bars: room for every word";
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
        if (whoSings != 1)
        {
            if (whoSings == 2) g.drawText ("TAKES", segs.withHeight (30.0f), Justification::centredLeft, false);
            g.drawText ("VERSIONS", segs.withY (segs.getY() + 42.0f).withHeight (30.0f), Justification::centredLeft, false);
        }
        else
        {
            // your take: how it's going, or what you have
            auto line = segs.withTrimmedTop (42.0f).withHeight (30.0f).withWidth (segs.getWidth() / 2.0f - 76.0f);
            String txt;
            Colour col = t.inkDim;
            if (takeRecording)
            {
                const double now = proc.playheadPos.load() / StudioPlayer::fileRate;
                const double start = takeFrom + barSeconds();
                txt = now < start ? "COUNT-IN..." : String::fromUTF8 ("\xe2\x97\x8f  RECORDING ") + String (roundToInt (now - start)) + " / " + String (roundToInt (takeStopAt - start)) + " s";
                col = Colour (0xffff5a6e);
            }
            else if (takeMessage.isNotEmpty()) { txt = takeMessage; col = takeMessage.startsWith ("Your take") ? t.ink : Colour (0xffff8f8f); }
            else txt = proc.canRecordMic() ? "Rap or sing the lines over the section" : "Record in your DAW from the section's first bar, then drop it here";
            g.setFont (font (11.5f, takeRecording ? lp::ui::Medium : lp::ui::Light));
            g.setColour (col);
            g.drawFittedText (txt, line.toNearestInt(), Justification::centredLeft, 2, 1.0f);
        }
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
        if (snap.connected && j != nullptr && j->live())
        {
            g.setFont (lp::ui::mono (10.0f, true, 0.1f));
            g.setColour (t.ink);
            g.drawText (clock (Time::getCurrentTime().toMilliseconds() / 1000.0 - j->created), head, Justification::centredRight, false);
        }
        // the wave: four lines, faster and bigger as the song gets further along
        auto wave = in.removeFromTop (54.0f);
        {
            const bool live = snap.connected && j != nullptr && j->live();
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
        {
            const auto& sk = snap.stages[(size_t) k].first;
            const bool written = j != nullptr ? j->written : whoSings == 0; // (before a job: what this mode will do)
            if (sk == "clean" && (written || (j != nullptr && ! j->clean))) continue;
            if (sk == "voice" && written) continue; // (sung in the voice already)
            shown.push_back (k);
        }
        auto steps = in.removeFromTop (46.0f);
        const float sw = steps.getWidth() / (float) jmax (1, (int) shown.size());
        for (size_t s = 0; s < shown.size(); ++s)
        {
            const int k = shown[s];
            // (our own short names for the helper's stages; its own words for any we don't know)
            const auto& key = snap.stages[(size_t) k].first;
            const String text = key == "sing" ? (j != nullptr && j->guide ? "Your take" : (j != nullptr ? j->written : whoSings == 0) ? "Singing" : "Melody") : key == "score" ? "Words & key" : key == "clean" ? "Clean up"
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
        if (snap.message.isNotEmpty() && (snap.messageBad || j == nullptr || snap.sending))
        {
            g.setFont (font (12.0f, lp::ui::Light));
            g.setColour (snap.messageBad ? Colour (0xffff8f8f) : t.inkDim);
            g.drawText (snap.message, r.reduced (18.0f, 12.0f).removeFromBottom (16.0f), Justification::centredLeft, true);
        }
        if (j == nullptr || (j->results.empty() && j->state != "error" && j->state != "cancelled"))
        {
            g.setFont (font (13.0f, lp::ui::Light));
            g.setColour (t.inkDim);
            // (a round with none kept: the helper keeps a take only when most of your words come through, and tries again)
            const bool retrying = j != nullptr && j->note.containsIgnoreCase ("round") && ! j->note.containsIgnoreCase ("round 1:")
                               && j->note.trimStart().fromFirstOccurrenceOf (": ", false, false).startsWith ("0 of");
            g.drawFittedText (j == nullptr ? String ("Type your words, pick a part of your song and a voice, and press Create.")
                              : retrying ? String ("No take so far sang your words clearly enough (a take is kept when most of your words come through), "
                                                   "so it's trying again. Few words for a long part makes this likely: Stop, add more lines (about one "
                                                   "for every 2 bars) or pick a shorter part, and create again.")
                                         : String ("Your versions appear here as each one is finished."),
                              resultsArea().withHeight (retrying ? 64.0f : 40.0f).toNearestInt(), Justification::topLeft, 4, 1.0f);
        }
    }

    if (notReady.isNotEmpty() && create.isVisible())
    {
        // (why Create is greyed out, just under it)
        g.setFont (font (11.5f, lp::ui::Medium));
        g.setColour (Colour (0xfff2c14e));
        g.drawText (notReady, create.getBounds().toFloat().translated (0.0f, (float) create.getHeight() + 2.0f).withHeight (16.0f), Justification::centredLeft, true);
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

void VoicePage::ResultRow::set (const lp::voice::Result& r, const String& id)
{
    if (id != jobId || r.n != res.n)
    {
        // another version: nothing of the old one carries over
        vocalFile = File(); preview = nullptr; preparing = false;
        toTrack.setButtonText ("add as track"); toTrack.setEnabled (true);
        play.setButtonText ("play");
    }
    res = r;
    jobId = id;
    setTooltip (r.heard.isNotEmpty() ? "The words the guide was heard singing: \"" + r.heard + "\"" : String());
    play.setEnabled (preview != nullptr);
    toTrack.setEnabled (vocalFile.existsAsFile() && toTrack.getButtonText() != "added");
    repaint();
}

void VoicePage::ResultRow::resized()
{
    auto b = getLocalBounds().reduced (10, 6);
    play.setBounds (b.removeFromLeft (36).withSizeKeepingCentre (35, 35));
    toTrack.setBounds (b.removeFromRight (104).withSizeKeepingCentre (104, 28));
    b.removeFromRight (6);
    drag.setBounds (b.removeFromRight (110).withSizeKeepingCentre (110, 28));
}

void VoicePage::ResultRow::paint (Graphics& g)
{
    const auto& t = *th;
    auto r = getLocalBounds().toFloat();
    g.setColour (Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (r, 12.0f);
    g.setColour (Colours::white.withAlpha (0.06f));
    g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, 1.0f);
    auto in = r.reduced (10.0f, 6.0f).withTrimmedLeft (48.0f).withTrimmedRight (234.0f);
    g.setFont (font (13.5f, lp::ui::Medium));
    g.setColour (t.ink);
    g.drawText ("Version " + String (res.n), in.removeFromTop (17.0f), Justification::centredLeft, false);
    auto m = in.removeFromTop (17.0f);
    if (res.words == 0 && res.key == 0 && res.heard.isEmpty())
    {
        // (your own take with no lyrics typed: nothing to check it against)
        g.setFont (lp::ui::mono (9.0f, false, 0.12f));
        g.setColour (t.inkDim);
        g.drawText ("YOUR TAKE, AS YOU SANG IT", m, Justification::centredLeft, false);
        return;
    }
    meter (g, m.removeFromLeft (m.getWidth() / 2 - 6.0f), "WORDS", res.words, t);
    m.removeFromLeft (12.0f);
    meter (g, m, "IN KEY", res.key, t);
}

// ---- "I sing it": your take over the section

void VoicePage::startTakeRecording()
{
    const int c = section.getSelectedItemIndex();
    if (c < 0 || c >= (int) sectionInfo.size() || ! proc.canRecordMic()) return;
    const auto& sc = sectionInfo[(size_t) c];
    // a bar of count-in, then the section, recorded from your mic until it ends
    takeFrom = jmax (0.0, sc.start - barSeconds());
    takeStopAt = sc.start + sc.secs;
    takeChoice = c;
    takeMessage = {};
    proc.setVoicePreview (nullptr);
    proc.previewPos = takeFrom * StudioPlayer::fileRate;
    proc.startRecording();
    proc.previewPlaying = true;
    takeRecording = true;
    refreshButtons();
    repaint();
}

void VoicePage::finishTakeRecording()
{
    takeRecording = false;
    proc.previewPlaying = false;
    const int c = takeChoice;
    if (c < 0 || c >= (int) sectionInfo.size()) { proc.recording = false; return; }
    const auto& sc = sectionInfo[(size_t) c];
    const auto dest = voiceFolder().getChildFile ("takes").getChildFile ("take " + Time::getCurrentTime().formatted ("%Y-%m-%d %H.%M.%S") + ".wav");
    String msg;
    if (proc.saveGuideTake (dest, sc.start - takeFrom, sc.secs, msg))
    {
        takeFile = dest;
        takeSecs = sc.secs;
        takeMessage = "Your take: " + String (roundToInt (sc.secs)) + " s over " + sc.name;
    }
    else takeMessage = msg;
    refreshButtons();
    repaint();
}

// a recording you made elsewhere (your DAW): cut to the section. One that starts at bar 1 (a bounce of the whole
// song) is cut from the section's start; anything shorter is taken from its start.
void VoicePage::useTakeFile (const File& f)
{
    const int c = section.getSelectedItemIndex();
    if (c < 0 || c >= (int) sectionInfo.size()) return;
    const auto& sc = sectionInfo[(size_t) c];
    AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<AudioFormatReader> rd (fm.createReaderFor (f));
    if (rd == nullptr || rd->sampleRate <= 0) { takeMessage = "That file can't be read: use a WAV, AIFF, MP3 or M4A"; repaint(); return; }
    const double dur = (double) rd->lengthInSamples / rd->sampleRate;
    const double from = dur >= sc.start + sc.secs - 0.5 && sc.start > 0.5 ? sc.start : 0.0;
    const int a = (int) (from * rd->sampleRate), n = (int) jmin<int64> (rd->lengthInSamples - a, (int64) (sc.secs * rd->sampleRate));
    if (n < (int) rd->sampleRate) { takeMessage = "That recording is too short for this section"; repaint(); return; }
    AudioBuffer<float> b (2, n);
    b.clear();
    rd->read (&b, 0, n, a, true, rd->numChannels > 1);
    if (rd->numChannels == 1) b.copyFrom (1, 0, b, 0, 0, n);
    const float pk = jmax (b.getMagnitude (0, 0, n), b.getMagnitude (1, 0, n));
    if (pk < 0.003f) { takeMessage = "That recording is silent in this section"; repaint(); return; }
    b.applyGain (0.89f / pk);
    const auto dest = voiceFolder().getChildFile ("takes").getChildFile (f.getFileNameWithoutExtension().substring (0, 40) + " (" + sc.name + ").wav");
    dest.getParentDirectory().createDirectory();
    dest.deleteFile();
    WavAudioFormat wav;
    std::unique_ptr<AudioFormatWriter> w (wav.createWriterFor (new FileOutputStream (dest), rd->sampleRate, 2, 24, {}, 0));
    if (w == nullptr || ! w->writeFromAudioSampleBuffer (b, 0, n)) { takeMessage = "Couldn't save your take"; repaint(); return; }
    w.reset();
    takeFile = dest;
    takeSecs = n / rd->sampleRate;
    takeChoice = c;
    takeMessage = "Your take: " + String (roundToInt (takeSecs)) + " s over " + sc.name + (from > 0.0 ? " (cut from the full song)" : "");
    whoSings = 1;
    refreshButtons();
    resized();
    repaint();
}

bool VoicePage::isInterestedInFileDrag (const StringArray& files)
{
    for (auto& f : files)
        for (auto* ext : { ".wav", ".aif", ".aiff", ".flac", ".mp3", ".m4a", ".caf" })
            if (f.endsWithIgnoreCase (ext)) return true;
    return false;
}

void VoicePage::filesDropped (const StringArray& files, int, int)
{
    for (auto& f : files)
        if (isInterestedInFileDrag (StringArray (f))) { useTakeFile (File (f)); return; }
}
