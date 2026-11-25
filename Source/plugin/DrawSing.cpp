#include "PluginEditor.h"
#include "../song/LyricMelody.h"

using namespace juce;
using lp::ui::font;

// VOCALS, "Draw & sing": a piano roll over one section of the song. Draw notes, put a word on each (double-click,
// or fill them from your lyrics, or start from a melody written for your lyrics), hear them with the song,
// then the voice helper sings exactly those notes and words in your voice; it lands as a new vocal track.

namespace { constexpr int kMin = 36, kMax = 88, kRows = 32; } // C2 .. E6, 32 notes at a time

DrawSing::DrawSing (LivePlanetProcessor& p, const lp::ui::Theme*& theme) : proc (p), th (theme)
{
    section.onChange = [this] { notes.clear(); viewStart = 0.0; selectedNote = -1; repaint(); };
    voice.onChange = [this] { if (voice.getText().isNotEmpty()) lp::setSetting ("voice", voice.getText()); }; // (remembered for every page)
    addVoiceBtn.setTooltip ("Add your voice: 10 to 30 seconds of you singing or rapping, clean, with no music (a WAV). It stays on this computer "
                            "and is picked for you from then on.");
    addVoiceBtn.setWantsKeyboardFocus (false);
    addVoiceBtn.onClick = [this]
    {
        voiceChooser = std::make_unique<FileChooser> ("Your voice: 10 to 30 seconds of clean singing or rapping, no music", File(), "*.wav;*.aif;*.aiff");
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
    struct T { TextButton* b; const char* tip; };
    for (auto [b, tip] : { T { &octUp, "Show higher notes (or scroll)" }, T { &octDown, "Show lower notes (or scroll)" },
                           T { &holdBtn, "Hold word (H): this note keeps singing the word before it, on its own pitch (\"you\": \"yu\" on one note, \"oo\" on the next)" },
                           T { &vibBtn, "Vibrato (V): the selected note wavers once it's held, like a singer's" },
                           T { &slideBtn, "Slide in (S): the selected note slides up or down into its pitch from the note before" } })
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
    for (auto& n : notes) k << n.beat << "," << n.len << "," << n.pitch << "," << n.word << "," << (int) n.slur << "," << n.vibrato << "," << (int) n.slide << ";";
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
    if (autoSing.getToggleState() && consent.getToggleState() && connected && ! notes.empty() && shownKey != sentKey && drag == Drag::None
        && ! wordBox.isVisible() && ! snap.sending && Time::getMillisecondCounter() - changedAt > 2500 && voice.getText().isNotEmpty())
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
            for (auto& n : notes) g.push_back ({ start + n.beat * spb, start + (n.beat + n.len) * spb - 0.02, n.pitch });
            proc.setGuide (std::move (g));
        }
    }
    repaint();
}

// ---- the roll's geometry
Rectangle<float> DrawSing::roll() const { return rollCard().toFloat().reduced (18.0f, 16.0f).withTrimmedTop (34.0f).withTrimmedBottom (40.0f); }
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

void DrawSing::showTools()
{
    const bool has = selectedNote >= 0 && selectedNote < (int) notes.size();
    for (auto* b : { &holdBtn, &vibBtn, &slideBtn }) b->setEnabled (has);
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
    if (! roll().withTrimmedLeft (44.0f).contains (e.position)) return;
    const int i = noteAt (e.position);
    if (i >= 0 && (e.mods.isPopupMenu() || e.mods.isAltDown()))
    {
        notes.erase (notes.begin() + i);
        selectedNote = -1;
        repaint();
        return;
    }
    if (i >= 0 && e.getNumberOfClicks() > 1) { editWord (i); return; }
    if (i >= 0)
    {
        selectedNote = i;
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
    if (n.beat < 0.0 || n.beat >= sectionBeats()) return;
    notes.push_back (n);
    selectedNote = (int) notes.size() - 1;
    drag = Drag::Resize;
    preview (n.pitch);
    repaint();
}

void DrawSing::mouseDrag (const MouseEvent& e)
{
    if (selectedNote < 0 || selectedNote >= (int) notes.size() || drag == Drag::None) return;
    auto& n = notes[(size_t) selectedNote];
    if (drag == Drag::Resize)
        n.len = jlimit (snap(), 16.0, std::round ((beatAt (e.position.x) - n.beat) / snap()) * snap());
    else
    {
        const int p = pitchAt (e.position.y);
        if (p != n.pitch) preview (p);
        n.pitch = p;
        n.beat = jlimit (0.0, std::max (0.0, sectionBeats() - n.len), std::round ((beatAt (e.position.x) - dragBeatOffset) / snap()) * snap());
    }
    repaint();
}

void DrawSing::mouseUp (const MouseEvent&)
{
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
    if (k.getTextCharacter() == 'h' || k.getTextCharacter() == 'H') { toggle (0); return true; }
    if (k.getTextCharacter() == 'v' || k.getTextCharacter() == 'V') { toggle (1); return true; }
    if (k.getTextCharacter() == 's' || k.getTextCharacter() == 'S') { toggle (2); return true; }
    return false;
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
    for (size_t i = 0; i < notes.size(); ++i) notes[i].word = (int) i < words.size() ? words[(int) i] : String ("la");
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
                    self->notes.push_back (m);
                }
            // (the melody in view)
            int hi = kMin + kRows - 1;
            for (auto& n : self->notes) hi = std::max (hi, n.pitch + 4);
            self->top = jlimit (kMin + kRows - 1, kMax, hi);
            self->message = self->notes.empty() ? String ("Couldn't write a melody here") : String();
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
    proc.previewPos = jmax (0.0, sectionStartSeconds() + viewStart * 60.0 / jmax (40.0, proc.songJob.getStatus().song.bpm) - 0.5) * StudioPlayer::fileRate;
    proc.previewPlaying = true;
    hear.setButtonText ("stop");
}

// the notes, as the helper reads them (a phrase where there's a gap of a beat or more)
void DrawSing::singIt()
{
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
    sentKey = noteKey();
    lastToken = token;
    if (autoAdd != nullptr) autoAdd->bends[token] = bends;
    const auto score = lp::song::lyricmelody::toScore (lines, st.song.bpm);
    lp::voice::Client::Request r;
    r.lyrics = words.joinIntoString (" ");
    r.style = "";
    r.voice = voice.getText();
    r.key = lp::ai::helperKeyName (st.song);
    r.bpm = st.song.bpm;
    r.takes = r.results = 1;
    r.consent = consent.getToggleState();
    r.clean = false;
    r.tag = "drawn|" + String (sectionStartSeconds(), 3) + "|" + String (st.song.sections[(size_t) c].name) + "|" + token;
    r.melodies = [score] { return var (Array<var> { score }); };
    r.renderTrack = [&pr = proc, c] { return pr.renderSectionForVoice (c, 1); };
    proc.voiceClient.create (std::move (r));
    message = "Singing it: the section's drawn track gets the new take when it's done.";
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
    for (auto* b : { &slideBtn, &vibBtn, &holdBtn })
    {
        b->setBounds (row.removeFromRight (b == &holdBtn ? 92 : 80));
        row.removeFromRight (6);
    }
    showTools();
    auto bottom = rollCard().reduced (18, 16).removeFromBottom (32);
    for (auto* b : { &fromLyrics, &fillWords, &clearAll })
    {
        b->setBounds (bottom.removeFromLeft (b == &fromLyrics ? 170 : 120));
        bottom.removeFromLeft (8);
    }
    hear.setBounds (bottom.removeFromRight (100));
    auto s = sideCard().reduced (20, 18);
    s.removeFromTop (34);
    section.setBounds (s.removeFromTop (34));
    s.removeFromTop (10);
    {
        auto vr = s.removeFromTop (34);
        addVoiceBtn.setBounds (vr.removeFromRight (60));
        vr.removeFromRight (8);
        voice.setBounds (vr);
    }
    s.removeFromTop (16);
    lyrics.setBounds (s.removeFromTop (150));
    s.removeFromTop (12);
    consent.setBounds (s.removeFromTop (40));
    autoSing.setBounds (s.removeFromTop (30));
    guide.setBounds (s.removeFromTop (30));
    s.removeFromTop (8);
    sing.setBounds (s.removeFromTop (40));
    statusArea = s.withTrimmedTop (10);
}

void DrawSing::paint (Graphics& g)
{
    const auto& t = *th;
    for (auto r : { rollCard(), sideCard() })
    {
        g.setColour (t.body);
        g.fillRoundedRectangle (r.toFloat(), 14.0f);
        g.setColour (t.line);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 14.0f, 1.0f);
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
                rollCard().reduced (18, 16).removeFromBottom (32).withLeft (clearAll.getRight() + 12).withRight (hear.getX() - 10), Justification::centredLeft, true);
    label ("THE SECTION AND THE VOICE", sideCard().reduced (20, 18).removeFromTop (24));

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

    // ---- what's happening
    String s = message;
    if (statusText) if (const auto a = statusText(); a.isNotEmpty()) s = a;
    if (! connected) s = "The voice helper isn't running: you can draw and hear notes; start it to have them sung.";
    g.setFont (font (12.0f));
    g.setColour (s.contains ("didn't work") || s.contains ("isn't running") ? Colour (0xffff8f8f) : t.inkDim);
    g.drawFittedText (s, statusArea, Justification::topLeft, 4);
}
