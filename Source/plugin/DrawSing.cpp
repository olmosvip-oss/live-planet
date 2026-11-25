#include "PluginEditor.h"
#include "../song/LyricMelody.h"

using namespace juce;
using lp::ui::font;

// VOCALS, "Draw & sing": a piano roll over one section of the song. Draw notes, put a word on each (double-click,
// or fill them from your lyrics, or start from a melody written for your lyrics), hear them with the song,
// then the voice helper sings exactly those notes and words in your voice; it lands as a new vocal track.

namespace { constexpr int kLo = 48, kHi = 79, kBeatsShown = 16; } // C3 .. G5, four bars at a time

DrawSing::DrawSing (LivePlanetProcessor& p, const lp::ui::Theme*& theme) : proc (p), th (theme)
{
    section.onChange = [this] { notes.clear(); page = 0; selectedNote = -1; repaint(); };
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
    consent.setWantsKeyboardFocus (false);
    consent.onClick = [this] { repaint(); };
    addAndMakeVisible (consent);
    wordBox.setFont (font (13.0f));
    wordBox.setJustification (Justification::centred);
    wordBox.onReturnKey = [this] { commitWord(); };
    wordBox.onEscapeKey = [this] { wordBox.setVisible (false); };
    wordBox.onFocusLost = [this] { commitWord(); };
    addChildComponent (wordBox);
    prev.onClick = [this] { page = jmax (0, page - 1); repaint(); };
    next.onClick = [this] { page = jmin (jmax (0, (int) std::ceil (sectionBeats() / kBeatsShown) - 1), page + 1); repaint(); };
    clearAll.onClick = [this] { notes.clear(); selectedNote = -1; repaint(); };
    fillWords.onClick = [this] { fillFromLyrics(); };
    fromLyrics.onClick = [this] { writeFromLyrics(); };
    hear.onClick = [this] { hearIt(); };
    sing.onClick = [this] { singIt(); };
    setWantsKeyboardFocus (true);
}

DrawSing::~DrawSing() = default;

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
    if (snap.voices != shownVoices)
    {
        shownVoices = snap.voices;
        const auto was = voice.getText();
        voice.clear (dontSendNotification);
        for (int i = 0; i < snap.voices.size(); ++i) voice.addItem (snap.voices[i], i + 1);
        const int at = snap.voices.indexOf (was);
        if (voice.getNumItems() > 0) voice.setSelectedItemIndex (at >= 0 ? at : 0, dontSendNotification);
    }
    connected = snap.connected;
    sing.setEnabled (connected && consent.getToggleState() && ! notes.empty() && voice.getText().isNotEmpty() && ! snap.sending
                     && (snap.jobs.empty() || ! snap.jobs.front().live()));
    if (hear.getButtonText() == "stop" && ! proc.previewPlaying.load()) hear.setButtonText ("hear it");
    prev.setEnabled (page > 0);
    next.setEnabled ((page + 1) * kBeatsShown < sectionBeats());
    repaint();
}

// ---- the roll's geometry
Rectangle<float> DrawSing::roll() const { return rollCard().toFloat().reduced (18.0f, 16.0f).withTrimmedTop (34.0f).withTrimmedBottom (40.0f); }
Rectangle<int> DrawSing::rollCard() const { return { 0, 0, getWidth() - 340, getHeight() }; }
Rectangle<int> DrawSing::sideCard() const { return { getWidth() - 326, 0, 326, getHeight() }; }
float DrawSing::rowH() const { return roll().getHeight() / (float) (kHi - kLo + 1); }
float DrawSing::xOf (double beat) const { const auto r = roll().withTrimmedLeft (44.0f); return r.getX() + r.getWidth() * (float) ((beat - page * kBeatsShown) / kBeatsShown); }
double DrawSing::beatAt (float x) const { const auto r = roll().withTrimmedLeft (44.0f); return page * kBeatsShown + (x - r.getX()) / r.getWidth() * kBeatsShown; }
float DrawSing::yOf (int pitch) const { return roll().getY() + (float) (kHi - pitch) * rowH(); }
int DrawSing::pitchAt (float y) const { return jlimit (kLo, kHi, kHi - (int) std::floor ((y - roll().getY()) / rowH())); }
Rectangle<float> DrawSing::noteRect (const Note& n) const { return { xOf (n.beat), yOf (n.pitch) + 1.0f, std::max (4.0f, xOf (n.beat + n.len) - xOf (n.beat)), rowH() - 2.0f }; }

int DrawSing::noteAt (Point<float> p) const
{
    for (int i = (int) notes.size() - 1; i >= 0; --i)
        if (noteRect (notes[(size_t) i]).expanded (2.0f, 0.0f).contains (p)) return i;
    return -1;
}

static double snapBeat (double b) { return std::round (b * 4.0) / 4.0; } // (16ths)

void DrawSing::mouseDown (const MouseEvent& e)
{
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
    n.beat = std::floor (beatAt (e.position.x) * 2.0) / 2.0;
    n.len = 0.5;
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
        n.len = jlimit (0.25, 16.0, snapBeat (beatAt (e.position.x) - n.beat));
    else
    {
        const int p = pitchAt (e.position.y);
        if (p != n.pitch) preview (p);
        n.pitch = p;
        n.beat = jlimit (0.0, std::max (0.0, sectionBeats() - n.len), snapBeat (beatAt (e.position.x) - dragBeatOffset));
    }
    repaint();
}

void DrawSing::mouseUp (const MouseEvent&)
{
    drag = Drag::None;
    std::sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.beat < b.beat; });
    selectedNote = -1;
    repaint();
}

bool DrawSing::keyPressed (const KeyPress& k)
{
    if ((k == KeyPress::deleteKey || k == KeyPress::backspaceKey) && selectedNote >= 0 && selectedNote < (int) notes.size())
    {
        notes.erase (notes.begin() + selectedNote);
        selectedNote = -1;
        repaint();
        return true;
    }
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
                    while (m.pitch < kLo) m.pitch += 12;
                    while (m.pitch > kHi) m.pitch -= 12;
                    self->notes.push_back (m);
                }
            self->message = self->notes.empty() ? String ("Couldn't write a melody here") : String();
            self->page = 0;
            self->repaint();
        });
    });
}

// a short tone when you place or move a note (through the song's preview)
void DrawSing::preview (int pitch)
{
    if (proc.songIsPlaying()) return;
    const double sr = StudioPlayer::fileRate;
    auto buf = std::make_shared<AudioBuffer<float>> (2, (int) (sr * 0.35));
    const double f = 440.0 * std::pow (2.0, (pitch - 69) / 12.0);
    for (int i = 0; i < buf->getNumSamples(); ++i)
    {
        const double t = i / sr;
        const float v = (float) (0.2 * std::sin (2 * MathConstants<double>::pi * f * t) * std::min (1.0, t / 0.01) * std::exp (-t * 6.0));
        buf->setSample (0, i, v); buf->setSample (1, i, v);
    }
    proc.setVoicePreview (buf);
    proc.previewPos = 0.0;
}

// your notes as tones, with the song from the section
void DrawSing::hearIt()
{
    if (hear.getButtonText() == "stop") { proc.setVoicePreview (nullptr); proc.previewPlaying = false; hear.setButtonText ("hear it"); return; }
    const auto st = proc.songJob.getStatus();
    if (notes.empty() || ! st.haveSong) return;
    const double sr = StudioPlayer::fileRate, start = sectionStartSeconds(), spb = 60.0 / st.song.bpm;
    const int pad = (int) (start * sr), len = (int) ((sectionBeats() * spb + 1.0) * sr);
    auto buf = std::make_shared<AudioBuffer<float>> (2, pad + len);
    buf->clear();
    for (auto& n : notes)
    {
        const double f = 440.0 * std::pow (2.0, (n.pitch - 69) / 12.0), dur = n.len * spb;
        const int a = pad + (int) (n.beat * spb * sr), m = (int) (dur * sr);
        for (int i = 0; i < m && a + i < buf->getNumSamples(); ++i)
        {
            const double t = i / sr, x = (double) i / m;
            const float v = (float) (0.16 * (std::sin (2 * MathConstants<double>::pi * f * t) + 0.3 * std::sin (4 * MathConstants<double>::pi * f * t))
                                     * std::min (1.0, t / 0.02) * std::min (1.0, (1.0 - x) * 8.0));
            buf->addSample (0, a + i, v); buf->addSample (1, a + i, v);
        }
    }
    proc.setVoicePreview (buf);
    proc.previewPos = jmax (0.0, start - 0.5) * sr;
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
    for (auto& n : notes)
    {
        if (lines.empty() || n.beat - end >= 1.0) lines.push_back ({});
        lines.back().notes.push_back (n);
        end = std::max (end, n.beat + n.len);
        words.add (n.word);
    }
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
    r.tag = "drawn|" + String (sectionStartSeconds(), 3) + "|" + String (st.song.sections[(size_t) c].name);
    r.melodies = [score] { return var (Array<var> { score }); };
    r.renderTrack = [&pr = proc, c] { return pr.renderSectionForVoice (c, 1); };
    proc.voiceClient.create (std::move (r));
    message = "Sent: it lands as a new vocal track when it's sung.";
    repaint();
}

void DrawSing::resized()
{
    auto top = rollCard().reduced (18, 16).removeFromTop (28);
    next.setBounds (top.removeFromRight (34));
    top.removeFromRight (6);
    prev.setBounds (top.removeFromRight (34));
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
    voice.setBounds (s.removeFromTop (34));
    s.removeFromTop (16);
    lyrics.setBounds (s.removeFromTop (150));
    s.removeFromTop (12);
    consent.setBounds (s.removeFromTop (40));
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
    const int bar0 = page * kBeatsShown / 4 + 1;
    const String dot = String::fromUTF8 ("  \xc2\xb7  ");
    label ("DRAW A MELODY" + dot + "DOUBLE-CLICK A NOTE FOR ITS WORD" + dot + "BARS " + String (bar0) + " TO " + String (bar0 + 3)
           + dot + "ALT-CLICK OR DELETE REMOVES", rollCard().reduced (18, 16).removeFromTop (28).withTrimmedRight (90));
    label ("THE SECTION AND THE VOICE", sideCard().reduced (20, 18).removeFromTop (24));

    // ---- the roll
    const auto r = roll();
    g.setColour (Colour (0xff0b0b0e));
    g.fillRoundedRectangle (r, 8.0f);
    const auto st = proc.songJob.getStatus();
    const int key = st.song.key;
    for (int p = kLo; p <= kHi; ++p)
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
    for (int b = 0; b <= kBeatsShown; ++b)
    {
        const float x = xOf (page * kBeatsShown + b);
        g.setColour (Colours::white.withAlpha (b % 4 == 0 ? 0.14f : 0.05f));
        g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
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
        g.drawText (n.word, nr.reduced (4.0f, 0.0f), Justification::centredLeft, true);
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
