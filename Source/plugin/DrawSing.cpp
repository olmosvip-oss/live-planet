#include "PluginEditor.h"
#include "../song/LyricMelody.h"
#include "../song/Transcribe.h"

using namespace juce;
using lp::ui::font;

// VOCALS, "Draw & sing": a piano roll over one section of the song. Draw notes, put a word on each (double-click,
// or fill them from your lyrics, or start from a melody written for your lyrics), hear them with the song,
// then the voice helper sings exactly those notes and words in your voice; it lands as a new vocal track.

namespace { constexpr int kMin = 36, kMax = 88, kRows = 32; } // C2 .. E6, 32 notes at a time

// ---- your voice, live (like Melodyne): the last sung take, cut at the notes it was sung on; each note you move or
// stretch is re-pitched (formants kept, so it stays your voice) and re-timed from its own piece of the take.
struct DrawSing::Voiced
{
    std::shared_ptr<const AudioBuffer<float>> take;      // the section, as sung (48 kHz, from the section's start)
    std::vector<lp::song::lyricmelody::Note> sungNotes; // the notes it was sung on
    double bpm = 120.0;
    struct Seg { String key; std::shared_ptr<AudioBuffer<float>> audio; int lead = 0; }; // lead: samples before the note's start
    std::mutex lock;
    std::map<int, Seg> cache;                           // by note id: its piece, re-pitched and re-timed
    std::atomic<int> generation { 0 };
    std::vector<float> env;                             // how loud the take is, 200 times a second (the notes' shapes)
    std::shared_ptr<AudioBuffer<float>> lastRender;     // the section in your voice, as the notes are now
    String renderedKey;                                 // (which notes it was rendered from)

    // where each word really is in the take: the singer doesn't keep exactly to the notes it was given (a word held a
    // little longer, the next one late), so each note's stretch of the take is found from the sound itself: between two
    // words, the quiet dip (or the jump in pitch) nearest where the notes meet; a phrase's first word from where the
    // voice comes in, its last until it fades. Then a note plays exactly its own word, nothing of the ones beside it.
    void align()
    {
        if (take == nullptr || sungNotes.empty()) return;
        const double sr = StudioPlayer::fileRate, spb = 60.0 / bpm;
        const int N = take->getNumSamples(), hop = (int) (sr / 200.0);
        // a smooth level (RMS, 10 ms), 200 times a second, 0..1
        std::vector<float> lv;
        float top = 1.0e-6f;
        for (int i = 0; i + hop <= N; i += hop)
        {
            const int a = jmax (0, i - hop / 2), len = jmin (N - a, hop * 2);
            const float r = 0.5f * (take->getRMSLevel (0, a, len) + take->getRMSLevel (jmin (1, take->getNumChannels() - 1), a, len));
            lv.push_back (r);
            top = jmax (top, r);
        }
        for (auto& v : lv) v /= top;
        const int F = (int) lv.size();
        auto fr = [hop] (double sample) { return (int) (sample / hop); };
        // the pitch, for words sung straight on from one to the next (no dip between them)
        std::vector<float> mono ((size_t) N);
        for (int i = 0; i < N; ++i) mono[(size_t) i] = take->getSample (0, i);
        const auto pitch = lp::song::tune::track (mono, sr, hop);
        auto jumpAt = [&pitch] (int f) { return f >= 3 && f < (int) pitch.size() && pitch[(size_t) f] > 0 && pitch[(size_t) f - 3] > 0 && std::abs (pitch[(size_t) f] - pitch[(size_t) f - 3]) >= 0.8; };

        std::vector<size_t> order (sungNotes.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::sort (order.begin(), order.end(), [this] (size_t a, size_t b) { return sungNotes[a].beat < sungNotes[b].beat; });
        const size_t n = order.size();
        std::vector<double> st (n), en (n); // (in samples)
        for (size_t k = 0; k < n; ++k) { st[k] = sungNotes[order[k]].beat * spb * sr; en[k] = (sungNotes[order[k]].beat + sungNotes[order[k]].len) * spb * sr; }
        std::vector<double> a0 = st, a1 = en;
        const double gapForPhrase = 0.3 * sr;
        for (size_t k = 0; k < n; ++k)
        {
            const bool phraseStart = k == 0 || st[k] - en[k - 1] > gapForPhrase;
            const bool phraseEnd = k + 1 == n || st[k + 1] - en[k] > gapForPhrase;
            if (phraseStart)
            {
                // where the voice comes in, near the note's start
                const int f0 = jmax (0, fr (st[k] - 0.25 * sr)), f1 = jmin (F - 1, fr (st[k] + 0.25 * sr));
                for (int f = f0; f <= f1; ++f) if (lv[(size_t) f] > 0.08f) { a0[k] = (double) jmax (0, f - 1) * hop; break; }
            }
            if (phraseEnd)
            {
                // until it fades, after the note's end (not into the next phrase)
                const double limit = k + 1 < n ? st[k + 1] - 0.05 * sr : (double) N;
                int f = jmax (0, fr (en[k] - 0.1 * sr));
                const int fEnd = jmin (F - 1, fr (jmin (limit, en[k] + 0.7 * sr)));
                while (f < fEnd && ! (lv[(size_t) f] < 0.05f && (f + 8 >= F || lv[(size_t) f + 8] < 0.05f))) ++f;
                a1[k] = jmax (a0[k] + 0.04 * sr, (double) f * hop);
            }
            else
            {
                // between this word and the next: the quiet dip (or the pitch jump) nearest where the notes meet
                const double m = 0.5 * (en[k] + st[k + 1]);
                const int lo = jmax (fr (st[k] + 0.04 * sr), fr (m - 0.22 * sr)), hi = jmin (fr (en[k + 1] - 0.04 * sr), fr (m + 0.22 * sr));
                int best = fr (m);
                float bestScore = 1.0e9f;
                for (int f = jmax (1, lo); f <= jmin (F - 2, hi); ++f)
                {
                    const bool valley = lv[(size_t) f] <= lv[(size_t) f - 1] && lv[(size_t) f] <= lv[(size_t) f + 1];
                    const float dist = (float) std::abs (f * hop - m) / (float) (0.22 * sr);
                    float score = 1.0e9f;
                    if (valley) score = lv[(size_t) f] * 1.6f + dist * 0.35f;
                    if (jumpAt (f)) score = jmin (score, 0.45f + dist * 0.35f);
                    if (score < bestScore) { bestScore = score; best = f; }
                }
                a1[k] = (double) best * hop;
                a0[k + 1] = a1[k];
            }
        }
        for (size_t k = 0; k < n; ++k)
        {
            if (a1[k] - a0[k] < 0.03 * sr) continue; // (couldn't tell: as given)
            auto& s = sungNotes[order[k]];
            s.beat = a0[k] / sr / spb;
            s.len = (a1[k] - a0[k]) / sr / spb;
        }
    }
    int leadOf (int id)
    {
        const std::lock_guard<std::mutex> l (lock);
        auto it = cache.find (id);
        return it != cache.end() ? it->second.lead : (int) (0.012 * StudioPlayer::fileRate);
    }
    const lp::song::lyricmelody::Note* sungOf (int id) const
    {
        for (auto& s : sungNotes) if (s.id == id) return &s;
        return nullptr;
    }
    // one note's piece of the take, at its new pitch and length (starting 12 ms early, for its fade in)
    std::shared_ptr<AudioBuffer<float>> piece (const lp::song::lyricmelody::Note& n)
    {
        const auto* s = sungOf (n.id);
        if (s == nullptr || take == nullptr) return nullptr;
        const double sr = StudioPlayer::fileRate, spb = 60.0 / bpm;
        const float semis = (float) (n.pitch - s->pitch);
        const double ratio = jlimit (0.25, 4.0, n.len / jmax (0.05, s->len));
        const String key = String (semis) + ":" + String (ratio, 4) + ":" + String (n.ringMs, 0);
        {
            const std::lock_guard<std::mutex> l (lock);
            if (auto it = cache.find (n.id); it != cache.end() && it->second.key == key) return it->second.audio;
        }
        const int N = take->getNumSamples();
        const double ctx = 0.2;
        const int a0 = (int) (s->beat * spb * sr), a1 = (int) ((s->beat + s->len) * spb * sr);
        // the whole word, as it was sung: its start (a consonant before the beat) and its end (it rings on past the note)
        // are found in the take, as far as the words either side of it
        int prevEnd = 0, nextStart = N;
        for (auto& o : sungNotes)
        {
            const int o0 = (int) (o.beat * spb * sr), o1 = (int) ((o.beat + o.len) * spb * sr);
            if (o.id == s->id) continue;
            if (o1 <= a0 + 1) prevEnd = jmax (prevEnd, o1);
            if (o0 >= a1 - 1) nextStart = jmin (nextStart, o0);
        }
        const double hopS = sr / 200.0;
        auto envAt = [this, hopS] (int sample) { const int i = (int) (sample / hopS); return i >= 0 && i < (int) env.size() ? env[(size_t) i] : 0.0f; };
        // (the word's stretch is already its own, found in the take: just a few ms either side, for the handovers)
        ignoreUnused (prevEnd, nextStart, envAt);
        const double fadeIn = 0.006, tail = n.ringMs >= 0.0f ? 0.006 + n.ringMs * 0.001 : 0.006; // (rings on: as long as you set)
        const int cs = jmax (0, a0 - (int) ((fadeIn + ctx) * sr)), ce = jmin (N, a1 + (int) ((tail + ctx) * sr));
        if (ce - cs < 256) return nullptr;
        const int inLen = ce - cs, outLen = jmax (1, (int) std::round (inLen * ratio));
        AudioBuffer<float> in (2, inLen), out (2, outLen);
        for (int ch = 0; ch < 2; ++ch) in.copyFrom (ch, 0, *take, jmin (ch, take->getNumChannels() - 1), cs, inLen);
        bool done = false;
        if (std::abs (semis) > 0.001f || std::abs (ratio - 1.0) > 0.001)
        {
            signalsmith::stretch::SignalsmithStretch<float> st;
            st.presetDefault (2, (float) sr);
            st.setTransposeSemitones (semis, (float) (8000.0 / sr));
            st.setFormantFactor (1.0f, true);
            done = st.exact (in.getArrayOfReadPointers(), inLen, out.getArrayOfWritePointers(), outLen);
        }
        if (! done) // (unchanged, or too short to stretch: as sung)
        {
            out.setSize (2, inLen, false, false, true);
            out.makeCopyOf (in);
        }
        const double r = (double) out.getNumSamples() / inLen;
        // the note itself, from just before it starts to just after it ends
        const int from = jmax (0, (int) ((a0 - cs - fadeIn * sr) * r)), to = jmin (out.getNumSamples(), (int) ((a1 - cs + tail * sr) * r));
        if (to - from < 64) return nullptr;
        auto seg = std::make_shared<AudioBuffer<float>> (2, to - from);
        for (int ch = 0; ch < 2; ++ch) seg->copyFrom (ch, 0, out, ch, from, to - from);
        const int fi = jmin (seg->getNumSamples() / 3, (int) (0.010 * sr)), fo = jmin (seg->getNumSamples() / 3, (int) (0.03 * sr));
        seg->applyGainRamp (0, fi, 0.0f, 1.0f);
        seg->applyGainRamp (seg->getNumSamples() - fo, fo, 1.0f, 0.0f);
        const std::lock_guard<std::mutex> l (lock);
        cache[n.id] = { key, seg, (int) ((a0 - cs) * r) - from };
        return seg;
    }
};


DrawSing::DrawSing (LivePlanetProcessor& p, const lp::ui::Theme*& theme) : proc (p), th (theme)
{
    section.onChange = [this] { notes.clear(); viewStart = 0.0; markerBeat = 0.0; selectedNote = -1; voiced = nullptr; leave(); repaint(); };
    voice.onChange = [this] { if (voice.getText().isNotEmpty()) lp::setSetting ("voice", voice.getText()); }; // (remembered for every page)
    addVoiceBtn.setTooltip ("Add your voice: 10 to 30 seconds of you singing or rapping, clean, with no music (a WAV). It stays on this computer "
                            "and is picked for you from then on.");
    addVoiceBtn.setWantsKeyboardFocus (false);
    addVoiceBtn.onClick = [this]
    {
        voiceChooser = std::make_unique<FileChooser> ("Your voice: 10 to 30 seconds of clean singing or rapping, no music", File(), "*.wav;*.m4a;*.mp3;*.aif;*.aiff;*.flac;*.caf;*.aac");
        voiceChooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this] (const FileChooser& fc)
        {
            if (! fc.getResult().existsAsFile()) return;
            proc.voiceClient.addVoice (fc.getResult());
            lp::setSetting ("voice", fc.getResult().getFileNameWithoutExtension()); // (picked as soon as the helper has it)
            message = "Adding your voice...";
            repaint();
        });
    };
    addAndMakeVisible (addVoiceBtn);
    for (auto* c : { &section, &voice }) { c->setWantsKeyboardFocus (false); addAndMakeVisible (*c); }
    addAndMakeVisible (voiceChip);
    lyrics.setMultiLine (true, true);
    lyrics.setReturnKeyStartsNewLine (true);
    lyrics.setFont (font (14.0f));
    lyrics.setTextToShowWhenEmpty ("your words, one line per phrase", Colours::grey);
    addAndMakeVisible (lyrics);
    struct B { TextButton* b; const char* tip; };
    for (auto [b, tip] : { B { &fromLyrics, "Writes a melody for your words over this section's chords (the same one Write melody uses), to change as you like" },
                           B { &fillWords, "Puts your words on the notes, one word per note, left to right" },
                           B { &clearAll, "Takes every note away" },
                           B { &prev, "The four bars before" }, B { &next, "The next four bars" },
                           B { &hear, "Your notes as plain tones, with the song, from this section" },
                           B { &sing, "The voice helper sings your notes and words in the chosen voice: it lands as a new vocal track" } })
    {
        b->setTooltip (tip);
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (*b);
    }
    sing.getProperties().set ("primary", true);
    for (auto* b : { &undoBtn, &redoBtn }) { b->setWantsKeyboardFocus (false); addAndMakeVisible (*b); }
    undoBtn.setTooltip ("Undo the last change to the notes (Cmd+Z)");
    redoBtn.setTooltip ("Redo it (Shift+Cmd+Z)");
    undoBtn.onClick = [this] { undo (true); };
    redoBtn.onClick = [this] { undo (false); };
    struct T { TextButton* b; const char* tip; };
    for (auto [b, tip] : { T { &octUp, "Show higher notes (or scroll)" }, T { &octDown, "Show lower notes (or scroll)" },
                           T { &holdBtn, "Hold word (H): this note keeps singing the word before it, on its own pitch (\"you\": \"yu\" on one note, \"oo\" on the next)" },
                           T { &vibBtn, "Vibrato (V): the selected note wavers once it's held, like a singer's" },
                           T { &slideBtn, "Slide in (S): the selected note slides up or down into its pitch from the note before" },
                           T { &splitBtn, "Split (E): cuts a note exactly at the mouse (press E with the mouse over it) or at the marker; the second part keeps singing the word (drag it to another pitch)" },
                           T { &recBtn, "Sing or hum your melody while the section plays (a bar to count in): it becomes the notes, "
                                        "and your words go on them. Click again to stop early." } })
    {
        b->setTooltip (tip);
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (*b);
    }
    octUp.onClick = [this] { top = jmin (kMax, top + 12); repaint(); };
    octDown.onClick = [this] { top = jmax (kMin + kRows - 1, top - 12); repaint(); };
    holdBtn.onClick = [this] { toggle (0); };
    vibBtn.onClick = [this] { toggle (1); };
    slideBtn.onClick = [this] { toggle (2); };
    splitBtn.onClick = [this] { splitNote(); };
    recBtn.onClick = [this] { toggleRecord(); };
    autoSing.setToggleState (true, dontSendNotification);
    autoSing.setTooltip ("On: a couple of seconds after you stop changing the notes, they're sung by themselves, and the section's drawn track "
                         "gets the new take. Off: only when you press the button.");
    autoSing.setWantsKeyboardFocus (false);
    addAndMakeVisible (autoSing);
    consent.setWantsKeyboardFocus (false);
    consent.onClick = [this] { repaint(); };
    addAndMakeVisible (consent);
    wordBox.setFont (font (13.0f));
    wordBox.setJustification (Justification::centred);
    wordBox.onReturnKey = [this] { commitWord(); };
    wordBox.onEscapeKey = [this] { wordBox.setVisible (false); };
    wordBox.onFocusLost = [this] { commitWord(); };
    addChildComponent (wordBox);
    prev.onClick = [this] { viewStart = jmax (0.0, viewStart - beatsShown); repaint(); };
    next.onClick = [this] { viewStart = jmin (jmax (0.0, sectionBeats() - beatsShown), viewStart + beatsShown); repaint(); };
    for (auto* b : { &zoomIn, &zoomOut }) { b->setWantsKeyboardFocus (false); addAndMakeVisible (*b); }
    zoomIn.setTooltip ("Zoom in, for timing (or Cmd/Ctrl + scroll). Notes snap to 32nds when zoomed in.");
    zoomOut.setTooltip ("Zoom out (or Cmd/Ctrl + scroll)");
    zoomIn.onClick = [this] { zoom (0.5, -1.0f); };
    zoomOut.onClick = [this] { zoom (2.0, -1.0f); };
    guide.setToggleState (true, dontSendNotification);
    guide.setTooltip ("Your notes play as a soft tone whenever the song plays (in the DAW or here), so you hear every change right away. "
                      "Off: just the song (and your sung track).");
    guide.setWantsKeyboardFocus (false);
    addAndMakeVisible (guide);
    clearAll.onClick = [this] { notes.clear(); selectedNote = -1; repaint(); };
    fillWords.onClick = [this] { fillFromLyrics(); };
    fromLyrics.onClick = [this] { writeFromLyrics(); };
    hear.onClick = [this] { hearIt(); };
    sing.onClick = [this] { singIt(); };
    setWantsKeyboardFocus (true);
}

DrawSing::~DrawSing() = default;

// everything about the notes that would change what's sung
String DrawSing::noteKey() const
{
    String k;
    k << section.getSelectedItemIndex() << ":" << voice.getText() << ":";
    for (auto& n : notes) k << n.beat << "," << n.len << "," << n.pitch << "," << n.word << "," << (int) n.slur << "," << n.vibrato << "," << (int) n.slide
                             << "," << n.gainDb << "," << n.ringMs << ";";
    return k;
}

double DrawSing::sectionBeats() const
{
    const auto st = proc.songJob.getStatus();
    const int c = section.getSelectedItemIndex();
    return st.haveSong && c >= 0 && c < (int) st.song.sections.size() ? st.song.sections[(size_t) c].bars * 4.0 : 16.0;
}

double DrawSing::sectionStartSeconds() const
{
    const auto st = proc.songJob.getStatus();
    const int c = section.getSelectedItemIndex();
    return st.haveSong && c >= 0 && c < (int) st.song.sections.size() ? st.song.sections[(size_t) c].startBar * 240.0 / st.song.bpm : 0.0;
}

void DrawSing::refresh()
{
    if (recordingMelody && (! proc.previewPlaying.load()
                            || proc.previewPos.load() / StudioPlayer::fileRate >= sectionStartSeconds() + sectionBeats() * 60.0 / jmax (40.0, proc.songJob.getStatus().song.bpm)))
        toggleRecord(); // (the section's over: what you sang becomes the notes)
    const auto st = proc.songJob.getStatus();
    if (st.version != songVersion)
    {
        songVersion = st.version;
        const int keep = section.getSelectedItemIndex();
        section.clear (dontSendNotification);
        int pick = 0;
        for (int i = 0; i < (int) st.song.sections.size(); ++i)
        {
            const auto& s = st.song.sections[(size_t) i];
            String n (s.name);
            n = n.substring (0, 1).toUpperCase() + n.substring (1);
            if (pick == 0 && n.startsWithIgnoreCase ("chorus")) pick = i;
            section.addItem (n + "   (bars " + String (s.startBar + 1) + " to " + String (s.startBar + s.bars) + ")", i + 1);
        }
        if (section.getNumItems() > 0) section.setSelectedItemIndex (keep >= 0 && keep < section.getNumItems() ? keep : pick, dontSendNotification);
    }
    const auto snap = proc.voiceClient.snapshot();
    if (const auto voices = lp::voice::usableVoices (snap.voices); voices != shownVoices)
    {
        shownVoices = voices;
        const auto was = voice.getText();
        voice.clear (dontSendNotification);
        for (int i = 0; i < voices.size(); ++i) voice.addItem (voices[i], i + 1);
        voice.setText (lp::voice::defaultVoice (voices, was, lp::setting ("voice", "").toString()), dontSendNotification);
    }
    connected = snap.connected;
    sing.setEnabled (connected && consent.getToggleState() && ! notes.empty() && voice.getText().isNotEmpty() && ! snap.sending
                     && (snap.jobs.empty() || ! snap.jobs.front().live()));
    sing.setButtonText (autoSing.getToggleState() ? "Sing it now" : "Sing it in my voice");
    if (hear.getButtonText() == "stop" && ! proc.previewPlaying.load()) hear.setButtonText ("play section");
    // sing as I draw: once the notes have stood still a moment
    if (const auto k = noteKey(); k != shownKey) { shownKey = k; changedAt = Time::getMillisecondCounter(); }
    // undo: each settled change remembered (a whole drag is one change)
    if (shownKey != committedKey && drag == Drag::None && ! wordBox.isVisible())
    {
        if (committedKey.isNotEmpty() || ! committed.empty())
        {
            undoStack.push_back (committed);
            if (undoStack.size() > 200) undoStack.erase (undoStack.begin());
            redoStack.clear();
        }
        committed = notes;
        committedKey = shownKey;
    }
    undoBtn.setEnabled (! undoStack.empty());
    redoBtn.setEnabled (! redoStack.empty());
    // only moved since the take: the edit goes into the track by itself, no singing (once it's rendered)
    if (autoSing.getToggleState() && ! autoPaused && ! notes.empty() && shownKey != sentKey && drag == Drag::None && ! wordBox.isVisible()
        && ! needsSinging() && voiced != nullptr && voiced->renderedKey == shownKey && Time::getMillisecondCounter() - changedAt > 2500)
        bake();
    if (autoSing.getToggleState() && ! autoPaused && consent.getToggleState() && connected && ! notes.empty() && shownKey != sentKey && drag == Drag::None
        && ! wordBox.isVisible() && ! snap.sending && Time::getMillisecondCounter() - changedAt > 2500 && voice.getText().isNotEmpty()
        && needsSinging() && (voiced == nullptr || voiced->renderedKey == shownKey))
    {
        // (a take of an older version still singing: stopped, the new one goes instead)
        bool waiting = false;
        for (auto& j : snap.jobs)
            if (j.live())
            {
                const auto t = snap.tags.find (j.id);
                if (t != snap.tags.end() && lastToken.isNotEmpty() && t->second.endsWith ("|" + lastToken)) proc.voiceClient.cancel (j.id);
                else waiting = true; // (someone else's job: wait for it)
            }
        if (! waiting) singIt();
    }
    // your voice, live: the newest sung take, once it's in the song; every edit re-rendered from it a moment later
    if (lastToken.isNotEmpty() && loadedToken != lastToken)
        if (auto f = proc.drawnFiles.find (lastToken); f != proc.drawnFiles.end()) loadTake (lastToken, f->second);
    if (voiced != nullptr && isShowing())
    {
        const auto k = noteKey();
        if (k != voicedKey && Time::getMillisecondCounter() - changedAt > 90)
        {
            voicedKey = k;
            guideKey.clear(); // (the guide: just the notes not sung yet)
            renderVoice();
        }
        // the section's drawn track is heard from this instead, while you're here
        const auto st3 = proc.songJob.getStatus();
        const int c = section.getSelectedItemIndex();
        int drawn = -1;
        if (c >= 0 && c < (int) st3.song.sections.size())
            for (int i = 0; i < (int) st3.tracks.size(); ++i)
                if (String (st3.tracks[(size_t) i].name) == "vocal (drawn, " + String (st3.song.sections[(size_t) c].name) + ")") drawn = i;
        if (proc.liveTrack.load() < 0 && proc.studio.skipTrack.load() != drawn) proc.studio.skipTrack = drawn;
    }
    prev.setEnabled (viewStart > 0.0);
    next.setEnabled (viewStart + beatsShown < sectionBeats());
    zoomIn.setEnabled (beatsShown > 2.0);
    zoomOut.setEnabled (beatsShown < 64.0);
    // the guide: your notes, at their place in the song, heard as you change them
    proc.guideOn = isShowing() && guide.getToggleState();
    {
        const auto st2 = proc.songJob.getStatus();
        String gk = shownKey + String (sectionStartSeconds()) + String (st2.song.bpm);
        if (gk != guideKey && st2.haveSong)
        {
            guideKey = gk;
            const double spb = 60.0 / st2.song.bpm, start = sectionStartSeconds();
            std::vector<LivePlanetProcessor::GuideNote> g;
            for (auto& n : notes)
                if (! isVoiced (n)) // (notes already sung play in your voice instead)
                    g.push_back ({ start + n.beat * spb, start + (n.beat + n.len) * spb - 0.02, n.pitch });
            proc.setGuide (std::move (g));
        }
    }
    repaint();
}

// ---- the roll's geometry
Rectangle<float> DrawSing::roll() const { return rollCard().toFloat().reduced (18.0f, 16.0f).withTrimmedTop (54.0f).withTrimmedBottom (40.0f); }
// the strip above the notes: click or drag to put the marker (where "play section" starts)
Rectangle<float> DrawSing::ruler() const { const auto r = roll(); return { r.getX() + 44.0f, r.getY() - 20.0f, r.getWidth() - 44.0f, 18.0f }; }
Rectangle<int> DrawSing::rollCard() const { return { 0, 0, getWidth() - 340, getHeight() }; }
Rectangle<int> DrawSing::sideCard() const { return { getWidth() - 326, 0, 326, getHeight() }; }
float DrawSing::rowH() const { return roll().getHeight() / (float) kRows; }
float DrawSing::xOf (double beat) const { const auto r = roll().withTrimmedLeft (44.0f); return r.getX() + r.getWidth() * (float) ((beat - viewStart) / beatsShown); }
double DrawSing::beatAt (float x) const { const auto r = roll().withTrimmedLeft (44.0f); return viewStart + (x - r.getX()) / r.getWidth() * beatsShown; }

// zoom in (factor < 1) or out, keeping the beat under the mouse (or the middle) where it is
void DrawSing::zoom (double factor, float aroundX)
{
    const auto r = roll().withTrimmedLeft (44.0f);
    const float x = aroundX < 0.0f ? r.getCentreX() : aroundX;
    const double at = beatAt (x);
    beatsShown = jlimit (2.0, 64.0, beatsShown * factor);
    viewStart = jlimit (0.0, jmax (0.0, sectionBeats() - beatsShown), at - (x - r.getX()) / r.getWidth() * beatsShown);
    viewStart = std::round (viewStart * 4.0) / 4.0;
    repaint();
}
float DrawSing::yOf (int pitch) const { return roll().getY() + (float) (top - pitch) * rowH(); }
int DrawSing::pitchAt (float y) const { return jlimit (top - kRows + 1, top, top - (int) std::floor ((y - roll().getY()) / rowH())); }

void DrawSing::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w)
{
    if (e.mods.isCommandDown() || e.mods.isCtrlDown()) { zoom (w.deltaY > 0 ? 0.8 : 1.25, e.position.x); return; }
    if (e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY))
    {
        // sideways through the section
        const float d = std::abs (w.deltaX) > std::abs (w.deltaY) ? w.deltaX : w.deltaY;
        viewStart = jlimit (0.0, jmax (0.0, sectionBeats() - beatsShown), viewStart - d * beatsShown * 0.5);
        repaint();
        return;
    }
    // up and down the octaves
    wheel += w.deltaY * (w.isSmooth ? 20.0f : 8.0f);
    const int rows = (int) wheel;
    if (rows == 0) return;
    wheel -= (float) rows;
    top = jlimit (kMin + kRows - 1, kMax, top + rows);
    repaint();
}

void DrawSing::toggle (int what)
{
    if (selectedNote < 0 || selectedNote >= (int) notes.size()) return;
    auto& n = notes[(size_t) selectedNote];
    if (what == 0)
    {
        n.slur = ! n.slur;
        // (it sings the word before it: find it)
        if (n.slur)
            for (int i = selectedNote - 1; i >= 0; --i)
                if (! notes[(size_t) i].slur) { n.word = notes[(size_t) i].word; break; }
    }
    else if (what == 1) n.vibrato = n.vibrato > 0.0f ? 0.0f : 1.0f;
    else n.slide = ! n.slide;
    showTools();
    repaint();
}

void DrawSing::splitNote()
{
    // where: the mouse, if it's over a note (that note); else the marker, if it's inside the picked note (or any note);
    // else the picked note's middle. Exactly there: no grid.
    const auto mouse = getMouseXYRelative().toFloat();
    double at = -1.0;
    if (const int under = noteAt (mouse); under >= 0 && roll().contains (mouse)) { selectedNote = under; at = beatAt (mouse.x); }
    if (at < 0.0)
    {
        auto inside = [this] (int i) { const auto& m = notes[(size_t) i]; return markerBeat > m.beat + 1e-6 && markerBeat < m.beat + m.len - 1e-6; };
        if (selectedNote >= 0 && selectedNote < (int) notes.size() && inside (selectedNote)) at = markerBeat;
        else for (int i = 0; i < (int) notes.size() && at < 0.0; ++i) if (inside (i)) { selectedNote = i; at = markerBeat; }
    }
    if (selectedNote < 0 || selectedNote >= (int) notes.size()) { message = "Pick a note, or put the marker inside one, then press split (E)"; repaint(); return; }
    auto& n = notes[(size_t) selectedNote];
    if (at < 0.0) at = n.beat + n.len * 0.5;
    const double half = at - n.beat, minLen = 1.0 / 64.0;
    if (half < minLen || n.len - half < minLen) { message = "Too close to the note's edge to split there"; repaint(); return; }
    autoPaused = false;
    Note b = n;
    const double fraction = half / n.len;
    n.len = half;
    b.beat = n.beat + half;
    b.len -= half;
    b.slur = true;          // (the same word, carried on: drag it up or down for a run)
    b.slide = false;
    b.vibrato = 0.0f;
    b.id = nextId++;
    // already sung: each part keeps its own stretch of the take (no new singing, the word just splits)
    if (voiced != nullptr)
    {
        const std::lock_guard<std::mutex> l (voiced->lock);
        for (auto& sn : voiced->sungNotes)
            if (sn.id == n.id)
            {
                auto child = sn;
                const double cut = sn.len * fraction;
                sn.len = cut;
                child.beat += cut; child.len -= cut;
                child.id = b.id; child.slur = true; child.slide = false; child.vibrato = 0.0f;
                voiced->sungNotes.push_back (child);
                voiced->cache.erase (n.id);
                break;
            }
    }
    notes.insert (notes.begin() + selectedNote + 1, b);
    selectedNote = selectedNote + 1;
    showTools();
    repaint();
}

// sing or hum the melody: the section plays (a bar early, to count in) while your mic records; when it's over
// (or you stop) the pitch is followed note by note, snapped to the key, and your words go on the notes
void DrawSing::toggleRecord()
{
    const auto st = proc.songJob.getStatus();
    if (! recordingMelody)
    {
        if (! st.haveSong) { message = "Write a song first"; repaint(); return; }
        const double bpm = jmax (40.0, st.song.bpm);
        recStartSec = jmax (0.0, sectionStartSeconds() - 240.0 / bpm); // (a bar to count in)
        proc.startRecording();
        proc.previewPos = recStartSec * StudioPlayer::fileRate;
        proc.previewPlaying = true;
        hear.setButtonText ("stop");
        recordingMelody = true;
        recBtn.setButtonText ("stop recording");
        recBtn.getProperties().set ("warn", true);
        message = "Recording: sing or hum your melody when the section starts (after a bar)";
        repaint();
        return;
    }
    recordingMelody = false;
    recBtn.setButtonText ("record melody");
    recBtn.getProperties().set ("warn", false);
    proc.recording = false;
    proc.previewPlaying = false;
    hear.setButtonText ("play section");
    const int len = proc.recPos.load();
    const double rate = proc.recRate;
    if (len < (int) (rate * 1.0)) { message = "Too short: let it record at least a bar of the section"; repaint(); return; }
    std::vector<float> mono ((size_t) len);
    for (int i = 0; i < len; ++i) mono[(size_t) i] = 0.5f * (proc.recBuf.getSample (0, i) + proc.recBuf.getSample (1, i));
    float pk = 0.0f;
    for (float v : mono) pk = jmax (pk, std::abs (v));
    if (pk < 0.003f)
    {
        message = proc.wrapperType == AudioProcessor::wrapperType_Standalone ? "No sound came in: Options > Audio settings, pick your mic and untick Mute audio input"
                                                                             : "No sound came in: set Loopwright's sidechain (audio from) to your mic's track";
        repaint();
        return;
    }
    for (auto& v : mono) v *= 0.9f / pk;
    message = "Listening to your melody...";
    repaint();
    lp::song::transcribe::Options o;
    o.bpm = jmax (40.0, st.song.bpm); o.key = st.song.key; o.minor = st.song.minor; o.lowest = 36; o.highest = 96;
    const double offsetBeats = (recStartSec - sectionStartSeconds()) * o.bpm / 60.0; // (the count-in: before the section)
    const double secBeats = sectionBeats();
    SafePointer<DrawSing> self (this);
    Thread::launch ([self, mono = std::move (mono), rate, o, offsetBeats, secBeats]
    {
        auto heard = lp::song::transcribe::melodyOf (mono.data(), (int) mono.size(), rate, o);
        MessageManager::callAsync ([self, heard, offsetBeats, secBeats]
        {
            if (self == nullptr) return;
            self->notes.clear();
            for (auto& h : heard)
            {
                Note n;
                n.beat = h.beat + offsetBeats;
                n.len = h.length;
                if (n.beat < 0.0 || n.beat >= secBeats) continue;
                n.len = jmin (n.len, secBeats - n.beat);
                n.pitch = h.pitch;
                while (n.pitch < kMin) n.pitch += 12;
                while (n.pitch > kMax) n.pitch -= 12;
                n.word = "la";
                n.id = self->nextId++;
                self->notes.push_back (n);
            }
            if (self->notes.empty()) { self->message = "Couldn't hear a melody: sing a little louder, closer to the mic"; self->repaint(); return; }
            const bool words = self->lyricWords().size() > 0;
            if (words) self->fillFromLyrics();
            int hi = kMin + kRows - 1;
            for (auto& n : self->notes) hi = std::max (hi, n.pitch + 4);
            self->top = jlimit (kMin + kRows - 1, kMax, hi);
            self->viewStart = 0.0;
            self->autoPaused = true;
            if (self->message.isEmpty() || ! words)
                self->message = String ((int) self->notes.size()) + " notes from your melody" + (words ? ", with your words on them" : ": type your words and press Fill in my words");
            self->repaint();
        });
    });
}

void DrawSing::showTools()
{
    const bool has = selectedNote >= 0 && selectedNote < (int) notes.size();
    for (auto* b : { &holdBtn, &vibBtn, &slideBtn }) b->setEnabled (has);
    splitBtn.setEnabled (! notes.empty());
    auto lit = [] (TextButton& b, bool on) { if ((bool) b.getProperties()["primary"] != on) { b.getProperties().set ("primary", on); b.repaint(); } };
    lit (holdBtn, has && notes[(size_t) selectedNote].slur);
    lit (vibBtn, has && notes[(size_t) selectedNote].vibrato > 0.0f);
    lit (slideBtn, has && notes[(size_t) selectedNote].slide);
}
Rectangle<float> DrawSing::noteRect (const Note& n) const { return { xOf (n.beat), yOf (n.pitch) + 1.0f, std::max (4.0f, xOf (n.beat + n.len) - xOf (n.beat)), rowH() - 2.0f }; }

int DrawSing::noteAt (Point<float> p) const
{
    for (int i = (int) notes.size() - 1; i >= 0; --i)
        if (noteRect (notes[(size_t) i]).expanded (2.0f, 0.0f).contains (p)) return i;
    return -1;
}



void DrawSing::mouseDown (const MouseEvent& e)
{
    setWantsKeyboardFocus (true); // (the window turns it off for everything but text boxes; the roll needs its keys)
    grabKeyboardFocus();
    if (wordBox.isVisible()) commitWord();
    if (ruler().expanded (0.0f, 2.0f).contains (e.position))
    {
        draggingMarker = true;
        setMarker (e);
        return;
    }
    if (! roll().withTrimmedLeft (44.0f).contains (e.position)) return;
    autoPaused = false; // (you're changing the notes: "sing as I draw" works again)
    const int i = noteAt (e.position);
    if (i >= 0 && (e.mods.isPopupMenu() || e.mods.isAltDown()))
    {
        notes.erase (notes.begin() + i);
        selectedNote = -1;
        repaint();
        return;
    }
    if (i >= 0 && e.getNumberOfClicks() > 1) { showWordEditor (i); return; }
    if (i >= 0)
    {
        selectedNote = i;
        previewNote (i);
        const auto r = noteRect (notes[(size_t) i]);
        drag = std::abs (e.position.x - r.getRight()) < 7.0f ? Drag::Resize : Drag::Move;
        dragBeatOffset = beatAt (e.position.x) - notes[(size_t) i].beat;
        repaint();
        return;
    }
    if (e.mods.isPopupMenu()) return;
    // a new note where you clicked, a beat long (drag to make it longer)
    Note n;
    n.beat = std::floor (beatAt (e.position.x) / (snap() * 2.0)) * snap() * 2.0;
    n.len = jmax (0.25, snap() * 4.0);
    n.pitch = pitchAt (e.position.y);
    n.word = nextWord();
    n.id = nextId++;
    if (n.beat < 0.0 || n.beat >= sectionBeats()) return;
    notes.push_back (n);
    selectedNote = (int) notes.size() - 1;
    drag = Drag::Resize;
    preview (n.pitch);
    repaint();
}

void DrawSing::setMarker (const MouseEvent& e)
{
    // on the grid (the zoom's step), or anywhere with cmd held
    double b = beatAt (e.position.x); // (anywhere: hold shift to snap it to the grid)
    if (e.mods.isShiftDown()) b = std::round (b / (snap() * 2.0)) * snap() * 2.0;
    markerBeat = jlimit (0.0, sectionBeats(), b);
    // (while it plays, the song jumps there)
    if (proc.previewPlaying.load())
        proc.previewPos = (sectionStartSeconds() + markerBeat * 60.0 / jmax (40.0, proc.songJob.getStatus().song.bpm)) * StudioPlayer::fileRate;
    repaint();
}

void DrawSing::mouseDrag (const MouseEvent& e)
{
    if (draggingMarker) { setMarker (e); return; }
    if (selectedNote < 0 || selectedNote >= (int) notes.size() || drag == Drag::None) return;
    auto& n = notes[(size_t) selectedNote];
    if (drag == Drag::Resize)
        n.len = jlimit (snap(), 16.0, std::round ((beatAt (e.position.x) - n.beat) / snap()) * snap());
    else
    {
        const int p = pitchAt (e.position.y);
        const bool moved = p != n.pitch;
        n.pitch = p;
        if (moved) previewNote (selectedNote); // (heard at its new note, in your voice once it's sung)
        n.beat = jlimit (0.0, std::max (0.0, sectionBeats() - n.len), std::round ((beatAt (e.position.x) - dragBeatOffset) / snap()) * snap());
    }
    repaint();
}

void DrawSing::mouseUp (const MouseEvent&)
{
    if (draggingMarker) { draggingMarker = false; return; }
    drag = Drag::None;
    // (sorted left to right; the note you had stays picked, for hold, vibrato and slide)
    const bool had = selectedNote >= 0 && selectedNote < (int) notes.size();
    const Note was = had ? notes[(size_t) selectedNote] : Note();
    std::sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.beat < b.beat; });
    selectedNote = -1;
    if (had)
        for (int i = 0; i < (int) notes.size(); ++i)
            if (notes[(size_t) i].beat == was.beat && notes[(size_t) i].pitch == was.pitch) { selectedNote = i; break; }
    showTools();
    repaint();
}

bool DrawSing::keyPressed (const KeyPress& k)
{
    if ((k == KeyPress::deleteKey || k == KeyPress::backspaceKey) && selectedNote >= 0 && selectedNote < (int) notes.size())
    {
        notes.erase (notes.begin() + selectedNote);
        selectedNote = -1;
        showTools();
        repaint();
        return true;
    }
    if (k.isKeyCode ('Z') && k.getModifiers().isCommandDown()) { undo (! k.getModifiers().isShiftDown()); return true; }
    if (k.getTextCharacter() == 'h' || k.getTextCharacter() == 'H') { toggle (0); return true; }
    if (k.getTextCharacter() == 'v' || k.getTextCharacter() == 'V') { toggle (1); return true; }
    if (k.getTextCharacter() == 's' || k.getTextCharacter() == 'S') { toggle (2); return true; }
    if (k.getTextCharacter() == 'e' || k.getTextCharacter() == 'E') { splitNote(); return true; }
    return false;
}

// a word, up close (double-click it): what it says, how loud it is, and how long it rings on after its note
namespace
{
struct WordEditor : public Component
{
    WordEditor (const String& word, float gainDb, float ringMs, std::function<void (String, float, float)> changed)
        : onChange (std::move (changed))
    {
        text.setText (word, dontSendNotification);
        text.setFont (lp::ui::font (15.0f, lp::ui::Medium));
        text.onTextChange = [this] { send(); };
        addAndMakeVisible (text);
        auto dial = [this] (Slider& s, double lo, double hi, double v, std::function<String (double)> fmt)
        {
            s.setSliderStyle (Slider::LinearHorizontal);
            s.setTextBoxStyle (Slider::TextBoxRight, false, 64, 22);
            s.setRange (lo, hi, 0.0);
            s.setValue (v, dontSendNotification);
            s.textFromValueFunction = std::move (fmt);
            s.updateText();
            s.onValueChange = [this] { send(); };
            s.setColour (Slider::thumbColourId, Colour (0xffb9a4ff));
            s.setColour (Slider::trackColourId, Colour (0xff7c5cff));
            addAndMakeVisible (s);
        };
        dial (volume, -18.0, 6.0, gainDb, [] (double v) { return (v > 0.05 ? "+" : "") + String (v, 1) + " dB"; });
        volume.setDoubleClickReturnValue (true, 0.0);
        dial (ring, -50.0, 1500.0, ringMs < 0.0f ? -50.0 : ringMs, [] (double v) { return v < 0.0 ? String ("as sung") : String (roundToInt (v)) + " ms"; });
        ring.setDoubleClickReturnValue (true, -50.0);
        setSize (320, 150);
    }
    void send() { if (onChange) onChange (text.getText().trim().upToFirstOccurrenceOf (" ", false, false), (float) volume.getValue(), ring.getValue() < 0.0 ? -1.0f : (float) ring.getValue()); }
    void resized() override
    {
        auto r = getLocalBounds().reduced (12, 10);
        text.setBounds (r.removeFromTop (30));
        r.removeFromTop (12);
        auto row = [&r] (Slider& s) { auto a = r.removeFromTop (30); a.removeFromLeft (78); s.setBounds (a); r.removeFromTop (8); };
        row (volume);
        row (ring);
    }
    void paint (Graphics& g) override
    {
        g.setColour (Colours::white.withAlpha (0.7f));
        g.setFont (lp::ui::mono (9.5f, true, 0.12f));
        g.drawText ("VOLUME", volume.getBounds().withX (12).withWidth (76), Justification::centredLeft, false);
        g.drawText ("RINGS ON", ring.getBounds().withX (12).withWidth (76), Justification::centredLeft, false);
    }
    TextEditor text;
    Slider volume, ring;
    std::function<void (String, float, float)> onChange;
};
}

void DrawSing::showWordEditor (int i)
{
    if (i < 0 || i >= (int) notes.size()) return;
    selectedNote = i;
    const auto n = notes[(size_t) i];
    const int id = n.id;
    SafePointer<DrawSing> self (this);
    auto ed = std::make_unique<WordEditor> (n.word, n.gainDb, n.ringMs, [self, id] (String w, float db, float ring)
    {
        if (self == nullptr) return;
        for (auto& m : self->notes)
            if (m.id == id)
            {
                if (w.isNotEmpty()) m.word = w;
                m.gainDb = db;
                m.ringMs = ring;
            }
        self->changedAt = Time::getMillisecondCounter();
        self->repaint();
    });
    auto& box = CallOutBox::launchAsynchronously (std::move (ed), noteRect (n).toNearestInt(), this);
    ignoreUnused (box);
}

void DrawSing::editWord (int i)
{
    editing = i;
    const auto r = noteRect (notes[(size_t) i]);
    wordBox.setBounds (Rectangle<int> ((int) r.getX(), (int) r.getY() - 6, jmax (90, (int) r.getWidth()), (int) r.getHeight() + 12));
    wordBox.setText (notes[(size_t) i].word, dontSendNotification);
    wordBox.setVisible (true);
    wordBox.grabKeyboardFocus();
    wordBox.selectAll();
}

void DrawSing::commitWord()
{
    if (! wordBox.isVisible()) return;
    wordBox.setVisible (false);
    if (editing >= 0 && editing < (int) notes.size())
    {
        const auto w = wordBox.getText().trim().upToFirstOccurrenceOf (" ", false, false);
        notes[(size_t) editing].word = w.isNotEmpty() ? w : String ("la");
    }
    editing = -1;
    repaint();
}

// the words of your lyrics, in order
StringArray DrawSing::lyricWords() const
{
    StringArray words;
    for (auto& line : lp::song::lyricmelody::sungLines (lyrics.getText()))
    {
        StringArray w;
        w.addTokens (line, " \t", "");
        w.removeEmptyStrings();
        words.addArray (w);
    }
    return words;
}

String DrawSing::nextWord() const
{
    const auto words = lyricWords();
    return (int) notes.size() < words.size() ? words[(int) notes.size()] : String ("la");
}

void DrawSing::fillFromLyrics()
{
    const auto words = lyricWords();
    if (words.isEmpty()) return;
    std::sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.beat < b.beat; });
    // one word per note, left to right; a held note ("hold word") keeps singing the word before it
    int w = 0;
    String last = "la";
    for (auto& n : notes)
    {
        if (n.slur) { n.word = last; continue; }
        n.word = last = w < words.size() ? words[w] : String ("la");
        ++w;
    }
    message = w < words.size() ? String (words.size() - w) + " of your words have no note yet: draw more notes, or split some (E)"
                               : w > words.size() ? String ("more notes than words: the extra notes sing \"la\"") : String();
    repaint();
}

// a melody for your words over the section's chords, written in the background (the chords take a moment to hear)
void DrawSing::writeFromLyrics()
{
    const int c = section.getSelectedItemIndex();
    const auto text = lyrics.getText();
    if (c < 0 || lp::song::lyricmelody::sungLines (text).isEmpty()) { message = "Type your words first (one line per phrase)"; repaint(); return; }
    message = "Writing a melody for your words...";
    repaint();
    SafePointer<DrawSing> self (this);
    auto* p = &proc;
    Thread::launch ([self, p, c, text]
    {
        const auto lines = p->writeVoiceLines (c, 1, text, false, (juce::uint32) Random::getSystemRandom().nextInt());
        MessageManager::callAsync ([self, lines]
        {
            if (self == nullptr) return;
            self->notes.clear();
            for (auto& l : lines)
                for (auto& n : l.notes)
                {
                    if (n.slur && ! self->notes.empty()) { self->notes.back().len += n.len; continue; } // (a held word: one note here)
                    Note m = n;
                    m.beat = std::max (0.0, m.beat);
                    while (m.pitch < kMin) m.pitch += 12;
                    while (m.pitch > kMax) m.pitch -= 12;
                    m.id = self->nextId++;
                    self->notes.push_back (m);
                }
            // (the melody in view)
            int hi = kMin + kRows - 1;
            for (auto& n : self->notes) hi = std::max (hi, n.pitch + 4);
            self->top = jlimit (kMin + kRows - 1, kMax, hi);
            self->message = self->notes.empty() ? String ("Couldn't write a melody here")
                                                : String ("Here's a melody: change any note, or press Sing to hear it in your voice");
            self->autoPaused = true; // (not sung by itself: you choose when)
            self->viewStart = 0.0;
            self->repaint();
        });
    });
}

// a short tone for a note you place or move (live, even while the song plays)
void DrawSing::preview (int pitch)
{
    proc.auditionNote (pitch);
}

// the song from this section (your notes play along as the guide, your sung track with it); again: stop
void DrawSing::hearIt()
{
    if (hear.getButtonText() == "stop") { proc.previewPlaying = false; hear.setButtonText ("play section"); return; }
    if (! proc.songJob.getStatus().haveSong) return;
    proc.setVoicePreview (nullptr);
    proc.previewPos = jmax (0.0, sectionStartSeconds() + markerBeat * 60.0 / jmax (40.0, proc.songJob.getStatus().song.bpm)) * StudioPlayer::fileRate;
    proc.previewPlaying = true;
    hear.setButtonText ("stop");
}

// the notes, as the helper reads them (a phrase where there's a gap of a beat or more)
void DrawSing::singIt()
{
    autoPaused = false;
    const auto st = proc.songJob.getStatus();
    const int c = section.getSelectedItemIndex();
    if (notes.empty() || ! st.haveSong || c < 0 || c >= (int) st.song.sections.size()) return;
    std::sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.beat < b.beat; });
    std::vector<lp::song::lyricmelody::Line> lines;
    double end = -100.0;
    StringArray words;
    std::vector<lp::song::stack::Bend> bends;
    const double spb = 60.0 / st.song.bpm;
    for (size_t i = 0; i < notes.size(); ++i)
    {
        const auto& n = notes[i];
        if (lines.empty() || n.beat - end >= 1.0) lines.push_back ({});
        lines.back().notes.push_back (n);
        end = std::max (end, n.beat + n.len);
        if (! n.slur) words.add (n.word);
        bends.push_back ({ n.beat * spb, (n.beat + n.len) * spb, n.vibrato, n.slide && i > 0 ? (float) (notes[i - 1].pitch - n.pitch) : 0.0f });
    }
    const String token = String::toHexString (Random::getSystemRandom().nextInt64()).substring (0, 10);
    // a take already there: only the phrases with new notes or new words are sung, and put into it
    auto sendLines = lines;
    if (voiced != nullptr && voiced->lastRender != nullptr && voiced->renderedKey == noteKey())
    {
        std::vector<lp::song::lyricmelody::Line> changed;
        std::vector<std::pair<double, double>> ranges;
        for (auto& l : lines)
        {
            bool any = false;
            for (auto& n : l.notes) any = any || ! isVoiced (n);
            if (! any || l.notes.empty()) continue;
            changed.push_back (l);
            const double t0 = l.notes.front().beat * spb - 0.15;
            double t1 = 0;
            for (auto& n : l.notes) t1 = jmax (t1, (n.beat + n.len) * spb);
            ranges.push_back ({ jmax (0.0, t0), t1 + 0.3 });
        }
        if (! changed.empty() && changed.size() < lines.size())
        {
            sendLines = changed;
            proc.drawnMerge[token] = { voiced->lastRender, sectionStartSeconds(), ranges };
        }
    }
    sentKey = noteKey();
    sentNotes[token] = notes; // (the notes this take is sung from: your voice is played live from it once it's in)
    lastToken = token;
    if (autoAdd != nullptr) autoAdd->bends[token] = bends;
    auto score = lp::song::lyricmelody::toScore (sendLines, st.song.bpm);
    {
        // the whole melody's middle note: a phrase sung on its own lands in the same octave as the rest
        std::vector<int> ps;
        for (auto& n : notes) ps.push_back (n.pitch);
        std::nth_element (ps.begin(), ps.begin() + (long) ps.size() / 2, ps.end());
        if (auto* o = score.getDynamicObject()) o->setProperty ("reference_pitch", ps[ps.size() / 2]);
    }
    lp::voice::Client::Request r;
    r.lyrics = words.joinIntoString (" ");
    r.style = "";
    r.voice = voice.getText();
    r.key = lp::ai::helperKeyName (st.song);
    r.bpm = st.song.bpm;
    r.takes = r.results = 1;
    r.consent = consent.getToggleState();
    r.clean = false;
    r.quick = true;
    r.tag = "drawn|" + String (sectionStartSeconds(), 3) + "|" + String (st.song.sections[(size_t) c].name) + "|" + token;
    r.melodies = [score] { return var (Array<var> { score }); };
    r.renderTrack = [&pr = proc, c] { return pr.renderSectionForVoice (c, 1); };
    proc.voiceClient.create (std::move (r));
    message = sendLines.size() < lines.size() ? "Singing just the changed phrase" + String (sendLines.size() > 1 ? "s" : "") + "..."
                                              : String ("Singing it: the section's drawn track gets the new take when it's done.");
    repaint();
}

void DrawSing::resized()
{
    auto row = rollCard().reduced (18, 16).removeFromTop (28);
    next.setBounds (row.removeFromRight (34));
    row.removeFromRight (6);
    prev.setBounds (row.removeFromRight (34));
    row.removeFromRight (10);
    zoomIn.setBounds (row.removeFromRight (30));
    row.removeFromRight (4);
    zoomOut.setBounds (row.removeFromRight (30));
    row.removeFromRight (10);
    octUp.setBounds (row.removeFromRight (56));
    row.removeFromRight (6);
    octDown.setBounds (row.removeFromRight (56));
    row.removeFromRight (14);
    for (auto* b : { &splitBtn, &slideBtn, &vibBtn, &holdBtn })
    {
        b->setBounds (row.removeFromRight (b == &holdBtn ? 92 : b == &splitBtn ? 60 : 80));
        row.removeFromRight (6);
    }
    showTools();
    auto bottom = rollCard().reduced (18, 16).removeFromBottom (32);
    for (auto* b : { &fromLyrics, &recBtn, &fillWords, &clearAll, &undoBtn, &redoBtn })
    {
        b->setBounds (bottom.removeFromLeft (b == &fromLyrics ? 150 : b == &recBtn ? 128 : b == &undoBtn || b == &redoBtn ? 60 : b == &clearAll ? 70 : 120));
        bottom.removeFromLeft (8);
    }
    hear.setBounds (bottom.removeFromRight (100));
    // the side, in three groups: the voice and the section; your words; while you draw (and sing it)
    auto s = sideCard().reduced (20, 18);
    s.removeFromTop (30);
    voiceChip.setBounds (s.removeFromTop (58));
    voice.setBounds (voiceChip.getBounds());
    addVoiceBtn.setBounds (voiceChip.getBounds()); // (both live in the voice picker now)
    voice.setVisible (false);
    addVoiceBtn.setVisible (false);
    s.removeFromTop (10);
    section.setBounds (s.removeFromTop (36));
    s.removeFromTop (26);
    wordsTop = s.getY() - 20;
    lyrics.setBounds (s.removeFromTop (jlimit (90, 190, s.getHeight() - 250)));
    s.removeFromTop (26);
    liveTop = s.getY() - 20;
    autoSing.setBounds (s.removeFromTop (30));
    guide.setBounds (s.removeFromTop (30));
    consent.setBounds (s.removeFromTop (40));
    s.removeFromTop (10);
    sing.setBounds (s.removeFromTop (44));
    statusArea = s.withTrimmedTop (10);
}

void DrawSing::paint (Graphics& g)
{
    const auto& t = *th;
    for (auto r : { rollCard(), sideCard() })
    {
        g.setColour (t.body);
        g.fillRoundedRectangle (r.toFloat(), 14.0f);
        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.035f), 0.0f, (float) r.getY(), Colours::transparentWhite, 0.0f, (float) r.getY() + 120.0f, false));
        g.fillRoundedRectangle (r.toFloat(), 14.0f);
        g.setColour (t.line);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 14.0f, 1.0f);
    }
    {
        // the side's groups, each with its name
        auto sl = [&] (const String& txt, int y)
        {
            g.setFont (lp::ui::mono (9.5f, true, 0.16f));
            g.setColour (t.grassLight);
            g.drawText (txt, Rectangle<int> (sideCard().getX() + 20, y, sideCard().getWidth() - 40, 16), Justification::centredLeft, false);
        };
        sl ("YOUR WORDS", wordsTop);
        sl ("WHILE YOU DRAW", liveTop);
        // (a soft box around the live switches and the sing button)
        const auto box = Rectangle<int> (sideCard().getX() + 12, liveTop + 18, sideCard().getWidth() - 24, sing.getBottom() + 10 - (liveTop + 18)).toFloat();
        g.setColour (Colours::white.withAlpha (0.03f));
        g.fillRoundedRectangle (box, 10.0f);
    }
    auto label = [&] (const String& s, Rectangle<int> r)
    {
        g.setFont (lp::ui::mono (9.5f, true, 0.16f));
        g.setColour (t.grassLight);
        g.drawText (s, r, Justification::centredLeft, false);
    };
    const int bar0 = (int) (viewStart / 4.0) + 1, bar1 = (int) std::ceil ((viewStart + beatsShown) / 4.0);
    const String dot = String::fromUTF8 ("  \xc2\xb7  ");
    label (bar1 > bar0 ? "BARS " + String (bar0) + " TO " + String (bar1) : "BAR " + String (bar0), rollCard().reduced (18, 16).removeFromTop (28).withRight (holdBtn.getX() - 8));
    g.setFont (font (11.0f));
    g.setColour (t.inkDim);
    g.drawText ("Click to draw a note, drag to move it, drag its end to make it longer. Double-click: its word. Alt-click or Delete: gone.",
                rollCard().reduced (18, 16).removeFromBottom (32).withLeft (redoBtn.getRight() + 12).withRight (hear.getX() - 10), Justification::centredLeft, true);
    label ("YOUR VOICE AND THE SECTION", sideCard().reduced (20, 18).removeFromTop (24));

    // ---- the roll
    const auto r = roll();
    g.setColour (Colour (0xff0b0b0e));
    g.fillRoundedRectangle (r, 8.0f);
    const auto st = proc.songJob.getStatus();
    const int key = st.song.key;
    for (int p = top - kRows + 1; p <= top; ++p)
    {
        const float y = yOf (p);
        const int pc = ((p % 12) + 12) % 12;
        const bool black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
        const bool inKey = lp::song::tune::allowed (p, [&] { lp::song::tune::Settings s; s.key = st.song.minor ? key : (key + 9) % 12; s.scale = 1; return s; }());
        g.setColour (inKey ? Colours::white.withAlpha (0.035f) : Colours::transparentBlack);
        g.fillRect (Rectangle<float> (r.getX() + 44.0f, y, r.getWidth() - 44.0f, rowH()));
        g.setColour (black ? Colour (0xff1a1a1f) : Colour (0xff2a2a31));
        g.fillRect (Rectangle<float> (r.getX(), y, 40.0f, rowH() - 1.0f));
        if (pc == 0)
        {
            g.setFont (lp::ui::mono (8.0f));
            g.setColour (t.inkDim);
            g.drawText ("C" + String (p / 12 - 1), Rectangle<float> (r.getX() + 2.0f, y, 36.0f, rowH()), Justification::centredRight, false);
        }
    }
    {
        // beats (bars brighter), and their 16ths when zoomed in
        const double step = beatsShown <= 8.0 ? 0.25 : 1.0;
        for (double b = std::floor (viewStart); b <= viewStart + beatsShown + 0.001; b += step)
        {
            const float x = xOf (b);
            if (x < r.getX() + 44.0f) continue;
            const bool beat = std::abs (b - std::round (b)) < 0.001, bar = beat && ((int) std::round (b)) % 4 == 0;
            g.setColour (Colours::white.withAlpha (bar ? 0.16f : beat ? 0.07f : 0.03f));
            g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
        }
    }
    {
        // past the section's end: shaded
        const float xe = xOf (sectionBeats());
        if (xe < r.getRight()) { g.setColour (Colours::black.withAlpha (0.5f)); g.fillRect (Rectangle<float> (xe, r.getY(), r.getRight() - xe, r.getHeight())); }
    }
    {
    Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (r.withTrimmedLeft (44.0f).toNearestInt());
    for (int i = 0; i < (int) notes.size(); ++i)
    {
        const auto& n = notes[(size_t) i];
        const auto nr = noteRect (n);
        g.setColour (i == selectedNote ? t.grassLight : t.grass);
        g.fillRoundedRectangle (nr, 3.0f);
        if (isVoiced (n))
        {
            // its sound, as sung: a blob of your voice inside the note (stretched with it)
            const auto* s = voiced->sungOf (n.id);
            const double spb = 60.0 / jmax (40.0, voiced->bpm);
            const int e0 = (int) (s->beat * spb * 200.0), e1 = (int) ((s->beat + s->len) * spb * 200.0);
            Path blob;
            const float mid = nr.getCentreY(), half = nr.getHeight() * 0.5f;
            const int steps = jmax (2, (int) nr.getWidth() / 2);
            blob.startNewSubPath (nr.getX(), mid);
            for (int k = 0; k <= steps; ++k)
            {
                const int ei = jlimit (0, jmax (0, (int) voiced->env.size() - 1), e0 + (e1 - e0) * k / steps);
                const float a = voiced->env.empty() ? 0.0f : voiced->env[(size_t) ei];
                blob.lineTo (nr.getX() + nr.getWidth() * k / steps, mid - half * 0.9f * a);
            }
            for (int k = steps; k >= 0; --k)
            {
                const int ei = jlimit (0, jmax (0, (int) voiced->env.size() - 1), e0 + (e1 - e0) * k / steps);
                const float a = voiced->env.empty() ? 0.0f : voiced->env[(size_t) ei];
                blob.lineTo (nr.getX() + nr.getWidth() * k / steps, mid + half * 0.9f * a);
            }
            blob.closeSubPath();
            g.setColour (Colours::white.withAlpha (0.28f));
            g.fillPath (blob);
        }
        g.setFont (font (10.5f, lp::ui::Medium));
        g.setColour (Colours::white);
        g.drawText (n.slur ? String::fromUTF8 ("\xe2\x80\xbf ") + n.word : n.word, nr.reduced (4.0f, 0.0f), Justification::centredLeft, true);
        if (n.vibrato > 0.0f)
        {
            // a wavy line along its bottom
            Path w;
            const float y = nr.getBottom() - 2.5f;
            w.startNewSubPath (nr.getX() + nr.getWidth() * 0.3f, y);
            for (float x = nr.getX() + nr.getWidth() * 0.3f; x < nr.getRight() - 2.0f; x += 1.5f)
                w.lineTo (x, y - 1.5f * std::sin ((x - nr.getX()) * 0.6f));
            g.strokePath (w, PathStrokeType (1.0f));
        }
        if (n.slide && i > 0)
        {
            // a line from the note before into this one
            const auto pr = noteRect (notes[(size_t) i - 1]);
            g.setColour (Colours::white.withAlpha (0.6f));
            g.drawLine (pr.getRight(), pr.getCentreY(), nr.getX() + 6.0f, nr.getCentreY(), 1.2f);
        }
    }
    // where the song is (when it's playing over this section)
    if (proc.songIsPlaying())
    {
        const double beat = (proc.playheadPos.load() / StudioPlayer::fileRate - sectionStartSeconds()) * st.song.bpm / 60.0;
        const float x = xOf (beat);
        if (x > r.getX() + 44.0f && x < r.getRight()) { g.setColour (Colours::white); g.drawVerticalLine ((int) x, r.getY(), r.getBottom()); }
    }
    }

    // the strip above: bar and beat numbers, and the marker (where play starts)
    if (st.haveSong)
    {
        const auto ru = ruler();
        g.setColour (Colours::white.withAlpha (0.04f));
        g.fillRoundedRectangle (ru, 4.0f);
        g.setFont (lp::ui::mono (9.0f, false, 0.08f));
        const int c = section.getSelectedItemIndex();
        const int firstBar = st.haveSong && c >= 0 && c < (int) st.song.sections.size() ? st.song.sections[(size_t) c].startBar : 0;
        for (int b = (int) std::ceil (viewStart); b <= (int) (viewStart + beatsShown); ++b)
        {
            const float x = xOf (b);
            if (x < ru.getX() || x > ru.getRight()) continue;
            const bool bar = b % 4 == 0;
            g.setColour (Colours::white.withAlpha (bar ? 0.35f : 0.14f));
            g.drawVerticalLine ((int) x, ru.getBottom() - (bar ? 9.0f : 5.0f), ru.getBottom());
            if (bar)
            {
                g.setColour (t.inkDim);
                g.drawText (String (firstBar + b / 4 + 1), Rectangle<float> (x + 3.0f, ru.getY(), 40.0f, 11.0f), Justification::centredLeft, false);
            }
        }
        const float mx = xOf (markerBeat);
        if (mx >= ru.getX() - 1.0f && mx <= ru.getRight() + 1.0f)
        {
            const Colour cyan (0xff3fd2e8);
            g.setColour (cyan.withAlpha (0.55f));
            g.drawVerticalLine ((int) mx, ru.getBottom(), roll().getBottom());
            Path tri;
            tri.addTriangle (mx - 6.0f, ru.getY() + 4.0f, mx + 6.0f, ru.getY() + 4.0f, mx, ru.getBottom() - 1.0f);
            g.setColour (cyan);
            g.fillPath (tri);
        }
    }
    // ---- what's happening
    String s = message;
    if (statusText) if (const auto a = statusText(); a.isNotEmpty()) s = a;
    if (! connected) s = "The voice helper isn't running: you can draw and hear notes; start it to have them sung.";
    g.setFont (font (12.0f));
    g.setColour (s.contains ("didn't work") || s.contains ("isn't running") ? Colour (0xffff8f8f) : t.inkDim);
    g.drawFittedText (s, statusArea, Justification::topLeft, 4);
}

bool DrawSing::isVoiced (const Note& n) const
{
    if (voiced == nullptr) return false;
    const auto* s = voiced->sungOf (n.id);
    return s != nullptr && s->word == n.word && s->slur == n.slur && s->vibrato == n.vibrato && s->slide == n.slide;
}

// back to how the notes were before the last change (or forward again)
void DrawSing::undo (bool back)
{
    auto& from = back ? undoStack : redoStack;
    auto& to = back ? redoStack : undoStack;
    if (from.empty()) return;
    to.push_back (notes);
    notes = from.back();
    from.pop_back();
    committed = notes;
    committedKey = noteKey();
    selectedNote = -1;
    showTools();
    repaint();
}

bool DrawSing::needsSinging() const
{
    if (voiced == nullptr) return true;
    for (auto& n : notes) if (! isVoiced (n)) return true;
    return false;
}

void DrawSing::mouseMagnify (const MouseEvent& e, float scale)
{
    if (scale > 0.0f) zoom (1.0 / scale, e.position.x);
}

// only moved, stretched or re-pitched since the take: the edited version (already heard live) goes into the
// section's drawn track, straight away, with no singing
void DrawSing::bake()
{
    if (voiced == nullptr || voiced->lastRender == nullptr) return;
    const auto st = proc.songJob.getStatus();
    const int c = section.getSelectedItemIndex();
    if (! st.haveSong || c < 0 || c >= (int) st.song.sections.size()) return;
    const String token = String::toHexString (Random::getSystemRandom().nextInt64()).substring (0, 10);
    const auto file = st.files.folder.getChildFile ("voice").getChildFile ("drawn " + String (st.song.sections[(size_t) c].name) + " " + token + ".wav");
    const String name = "drawn, " + String (st.song.sections[(size_t) c].name);
    sentNotes[token] = notes;
    lastToken = loadedToken = token; // (your voice keeps coming from the take as sung: it isn't loaded again)
    sentKey = bakedKey = shownKey;
    auto render = voiced->lastRender;
    const double start = sectionStartSeconds();
    auto* p = &proc;
    Thread::launch ([p, render, start, file, name, token]
    {
        const int a = (int) (start * StudioPlayer::fileRate);
        AudioBuffer<float> out (2, a + render->getNumSamples());
        out.clear();
        for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, a, *render, ch, 0, render->getNumSamples());
        file.getParentDirectory().createDirectory();
        WavAudioFormat wav;
        bool ok = false;
        if (std::unique_ptr<AudioFormatWriter> w { wav.createWriterFor (new FileOutputStream (file), StudioPlayer::fileRate, 2, 24, {}, 0) })
            ok = w->writeFromAudioSampleBuffer (out, 0, out.getNumSamples());
        if (ok)
            MessageManager::callAsync ([p, file, name, token]
            {
                p->drawnFiles[token] = file;
                p->songJob.addSungVocal (file, name, "your drawn melody, edited", true, "drawn and sung", true);
            });
    });
    message = "Your changes are in the track (no singing needed: only notes moved).";
    repaint();
}

// a sung take has come in: kept (just this section of it) to play your voice from as you edit
void DrawSing::loadTake (const String& token, const File& file)
{
    loadedToken = token;
    const auto it = sentNotes.find (token);
    if (it == sentNotes.end()) return;
    auto v = std::make_shared<Voiced>();
    v->sungNotes = it->second;
    v->bpm = proc.songJob.getStatus().song.bpm;
    const double start = sectionStartSeconds(), len = sectionBeats() * 60.0 / jmax (40.0, v->bpm) + 2.0;
    SafePointer<DrawSing> self (this);
    Thread::launch ([self, v, file, start, len]
    {
        AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<AudioFormatReader> rd (fm.createReaderFor (file));
        if (rd == nullptr) return;
        const auto a = (int64) (start * rd->sampleRate), n = (int64) (len * rd->sampleRate);
        auto b = std::make_shared<AudioBuffer<float>> (2, (int) jmax<int64> (1, jmin (n, rd->lengthInSamples - a)));
        rd->read (b.get(), 0, b->getNumSamples(), a, true, true);
        v->take = b;
        const int hop = (int) (rd->sampleRate / 200.0);
        float top = 1.0e-6f;
        for (int i = 0; i + hop <= b->getNumSamples(); i += hop)
        {
            v->env.push_back (b->getMagnitude (0, i, hop));
            top = jmax (top, v->env.back());
        }
        for (auto& e : v->env) e = std::sqrt (e / top);
        v->align(); // (each note's own word, found in the take)
        MessageManager::callAsync ([self, v]
        {
            if (self == nullptr) return;
            self->voiced = v;
            self->voicedKey.clear(); // (rendered from it straight away)
        });
    });
}

// the section in your voice, from the notes as they are now (only the notes you changed are worked out again)
void DrawSing::renderVoice()
{
    if (voiced == nullptr || voiced->take == nullptr) return;
    auto v = voiced;
    auto list = notes;
    std::vector<bool> use;
    for (auto& n : list) use.push_back (isVoiced (n));
    const double bpm = v->bpm, start = sectionStartSeconds(), len = sectionBeats() * 60.0 / bpm + 2.0;
    const int gen = ++v->generation;
    auto* p = &proc;
    const String key = noteKey();
    Thread::launch ([v, list, use, bpm, start, len, gen, p, key]
    {
        const double sr = StudioPlayer::fileRate, spb = 60.0 / bpm;
        auto out = std::make_shared<AudioBuffer<float>> (2, (int) (len * sr));
        out->clear();
        // each word's piece, where it starts in the section (its first consonant can come before the beat)
        struct P { std::shared_ptr<AudioBuffer<float>> seg; int at = 0, start = 0, end = 0; float gain = 1.0f; };
        std::vector<P> ps;
        std::vector<size_t> order;
        for (size_t i = 0; i < list.size(); ++i) if (use[i]) order.push_back (i);
        std::sort (order.begin(), order.end(), [&list] (size_t a, size_t b) { return list[a].beat < list[b].beat; });
        for (size_t i : order)
        {
            if (v->generation.load() != gen) return; // (a newer edit: that one's rendered instead)
            const auto seg = v->piece (list[i]);
            if (seg == nullptr) continue;
            P p;
            p.seg = seg;
            p.start = (int) (list[i].beat * spb * sr);
            p.end = (int) ((list[i].beat + list[i].len) * spb * sr);
            p.at = p.start - v->leadOf (list[i].id);
            p.gain = Decibels::decibelsToGain (list[i].gainDb);
            ps.push_back (p);
        }
        // each word owns its own stretch: where two meet (a word's ring-out against the next one's first consonant),
        // one hands over to the other in a short crossfade, so the same voice is never heard twice (no jumps in level)
        const int xf = (int) (0.008 * sr);
        std::vector<int> from (ps.size()), to (ps.size());
        for (size_t k = 0; k < ps.size(); ++k) { from[k] = ps[k].at; to[k] = ps[k].at + ps[k].seg->getNumSamples(); }
        for (size_t k = 0; k + 1 < ps.size(); ++k)
        {
            auto& a = ps[k]; auto& b = ps[k + 1];
            if (to[k] <= from[k + 1]) continue; // (a gap: nothing to share)
            const int boundary = b.start >= a.end ? jmax (a.end, b.at) : b.start;
            to[k] = jmin (to[k], boundary + xf / 2);
            from[k + 1] = jmax (from[k + 1], boundary - xf / 2);
        }
        for (size_t k = 0; k < ps.size(); ++k)
        {
            const auto& p = ps[k];
            const int n = p.seg->getNumSamples();
            for (int x = jmax (0, from[k] - p.at); x < jmin (n, to[k] - p.at); ++x)
            {
                const int o = p.at + x;
                if (o < 0 || o >= out->getNumSamples()) continue;
                float g = p.gain;
                if (k > 0 && from[k] > p.at) g *= jlimit (0.0f, 1.0f, (float) (o - from[k]) / (float) xf);          // (handed over from the word before)
                if (k + 1 < ps.size() && to[k] < p.at + n) g *= jlimit (0.0f, 1.0f, (float) (to[k] - o) / (float) xf); // (handing over to the next)
                for (int ch = 0; ch < 2; ++ch) out->addSample (ch, o, p.seg->getSample (ch, x) * g);
            }
        }
        MessageManager::callAsync ([v, out, gen, p, start, key]
        {
            if (v->generation.load() != gen) return;
            p->setVoicePreview (out, (int64) (start * StudioPlayer::fileRate));
            v->lastRender = out;
            v->renderedKey = key;
        });
    });
}

// the note you're moving: in your voice when it was sung, else the tone
void DrawSing::previewNote (int index)
{
    if (index < 0 || index >= (int) notes.size()) return;
    const auto n = notes[(size_t) index];
    if (! isVoiced (n)) { proc.auditionNote (n.pitch); return; }
    auto v = voiced;
    auto* p = &proc;
    Thread::launch ([v, n, p]
    {
        if (auto seg = v->piece (n))
        {
            auto copy = std::make_shared<AudioBuffer<float>> (*seg);
            MessageManager::callAsync ([p, copy] { p->auditionBuffer (copy); ++p->auditionSerial; });
        }
    });
}

void DrawSing::leave()
{
    proc.setVoicePreview (nullptr);
    if (proc.liveTrack.load() < 0) proc.studio.skipTrack = -1;
    proc.guideOn = false;
}

// a drawn vocal double-clicked in the Studio: its section, here (the notes you drew are kept while Loopwright is open)
void DrawSing::openSection (const String& sectionName)
{
    const auto st = proc.songJob.getStatus();
    for (int i = 0; i < (int) st.song.sections.size(); ++i)
        if (String (st.song.sections[(size_t) i].name).equalsIgnoreCase (sectionName.trim()))
        {
            if (section.getSelectedItemIndex() != i) section.setSelectedItemIndex (i, sendNotification);
            // (the notes this section's take was sung from, when they're still known)
            if (notes.empty())
                for (auto& [token, list] : sentNotes)
                    if (auto f = proc.drawnFiles.find (token); f != proc.drawnFiles.end() && f->second.getFileName().containsIgnoreCase (sectionName) && ! list.empty())
                        notes = list;
            repaint();
            return;
        }
}
