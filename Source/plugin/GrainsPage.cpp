#include "PluginEditor.h"
#include "AppFolder.h"

using namespace juce;
using lp::ui::font;
namespace G = lp::grains;

namespace
{
// your computer keys: A S D F G H J K are C3..C4, W E T Y U the black keys
int noteForKey (juce_wchar c)
{
    switch (CharacterFunctions::toLowerCase (c))
    {
        case 'a': return 60; case 'w': return 61; case 's': return 62; case 'e': return 63; case 'd': return 64;
        case 'f': return 65; case 't': return 66; case 'g': return 67; case 'y': return 68; case 'h': return 69;
        case 'u': return 70; case 'j': return 71; case 'k': return 72; case 'o': return 73; case 'l': return 74;
        default: return -1;
    }
}
const char* const keyChars = "awsedftgyhujkol";

File userPresetFolder() { return lp::appFolder().getChildFile ("Grains presets"); }

void styleDial (Slider& s, const String& name, int size)
{
    s.setName (name);
    s.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    s.setRotaryParameters (MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
    s.getProperties().set ("size", size);
    s.setMouseDragSensitivity (240);
    s.setWantsKeyboardFocus (false);
}

String noteName (int n)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return String (names[n % 12]) + String (n / 12 - 2);
}
} // namespace

GrainsPage::GrainsPage (LivePlanetProcessor& p, const lp::ui::Theme*& theme) : proc (p), th (theme)
{
    setWantsKeyboardFocus (true);
    onMessage = [this] (const String& m) { note = m; noteAt = Time::getMillisecondCounter(); repaint(); };
    static const char* names[] = { "grains", "length", "position", "space", "chaos", "tune" };
    for (int k = 0; k < G::numKnobs; ++k)
    {
        auto& kn = knobs[(size_t) k];
        kn.id = k;
        styleDial (kn.s, names[k], 68);
        if (k == G::Rate) kn.s.setRange (0.0, 1.0, 0.0);
        if (k == G::Length) { kn.s.setRange (10.0, 1000.0, 1.0); kn.s.setSkewFactorFromMidPoint (180.0); }
        if (k == G::Position || k == G::Space || k == G::Chaos) kn.s.setRange (0.0, 1.0, 0.0);
        if (k == G::Tune) { kn.s.setRange (-24.0, 24.0, 0.01); kn.s.setDoubleClickReturnValue (true, 0.0); }
        if (k == G::Position) kn.s.setDoubleClickReturnValue (true, 0.0);
        kn.s.textFromValueFunction = [this, k] (double v) { return knobText (k, v); };
        kn.s.onDragStart = [this] { beginChange(); };
        kn.s.onValueChange = [this, k]
        {
            set().set (k, selChop, (float) knobs[(size_t) k].s.getValue());
            changed (false);
        };
        addAndMakeVisible (kn.s);
    }
    auto side = [this] (Slider& s, const String& name, double lo, double hi, double mid, std::function<void (float)> to, std::function<String (double)> text)
    {
        styleDial (s, name, 44);
        s.setRange (lo, hi, 0.0);
        if (mid > 0.0) s.setSkewFactorFromMidPoint (mid);
        s.textFromValueFunction = std::move (text);
        s.onDragStart = [this] { beginChange(); };
        s.onValueChange = [this, &s, to] { to ((float) s.getValue()); changed (false); };
        addAndMakeVisible (s);
    };
    auto hz = [] (double v) { return v >= 1000.0 ? String (v / 1000.0, 1) + "k" : String (roundToInt (v)); };
    auto pc = [] (double v) { return String (roundToInt (v * 100.0)) + "%"; };
    side (hpF, "low cut", 20.0, 2000.0, 200.0, [this] (float v) { set().hpHz = v; }, hz);
    side (lpF, "high cut", 400.0, 20000.0, 4000.0, [this] (float v) { set().lpHz = v; }, hz);
    side (res, "res", 0.0, 1.0, 0.0, [this] (float v) { set().hpRes = set().lpRes = v; }, pc);
    side (drive, "drive", 0.0, 1.0, 0.0, [this] (float v) { set().drive = v; }, pc);
    side (fmix, "mix", 0.0, 1.0, 0.0, [this] (float v) { set().filterMix = v; }, pc);
    styleDial (output, "output", 56);
    output.setRange (-30.0, 6.0, 0.1);
    output.setDoubleClickReturnValue (true, 0.0);
    output.textFromValueFunction = [] (double v) { return (v > 0.05 ? "+" : "") + String (v, 1) + " dB"; };
    output.onDragStart = [this] { beginChange(); };
    output.onValueChange = [this] { set().outputDb = (float) output.getValue(); changed (false); };
    addAndMakeVisible (output);

    auto btn = [this] (TextButton& b, std::function<void()> f, const String& tip = {})
    {
        b.setWantsKeyboardFocus (false);
        b.setMouseClickGrabsKeyboardFocus (false);
        b.onClick = std::move (f);
        if (tip.isNotEmpty()) b.setTooltip (tip);
        addAndMakeVisible (b);
    };
    btn (presetBtn, [this] { showPresetMenu(); }, "Presets: Loopwright's starting points, then yours");
    btn (prevPreset, [this] { pickPreset (presetIndex - 1); });
    btn (nextPreset, [this] { pickPreset (presetIndex + 1); });
    btn (savePreset, [this]
    {
        auto* w = new AlertWindow ("Save preset", "A name for it:", MessageBoxIconType::NoIcon);
        w->addTextEditor ("name", presetBtn.getButtonText().trimCharactersAtEnd ("*").trim());
        w->addButton ("Save", 1, KeyPress (KeyPress::returnKey));
        w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
        SafePointer<GrainsPage> self (this);
        w->enterModalState (true, ModalCallbackFunction::create ([self, w] (int r)
        {
            if (self == nullptr || r != 1) return;
            const auto name = File::createLegalFileName (w->getTextEditorContents ("name").trim());
            if (name.isEmpty()) return;
            userPresetFolder().createDirectory();
            auto v = G::toVar (self->set());
            v.getDynamicObject()->setProperty ("name", name);
            userPresetFolder().getChildFile (name + ".json").replaceWithText (JSON::toString (v));
            self->presetBtn.setButtonText (name);
            self->presetEdited = false;
            if (self->onMessage) self->onMessage ("saved the preset \"" + name + "\"");
        }), true);
    }, "Save what you've made as a preset");
    btn (loadBtn, [this]
    {
        chooser = std::make_unique<FileChooser> ("A sound for Grains", File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
        SafePointer<GrainsPage> self (this);
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [self] (const FileChooser& fc)
        {
            if (self != nullptr && fc.getResult().existsAsFile()) self->loadFile (fc.getResult());
        });
    }, "Load a sound from your computer (or just drop one on the screen)");
    btn (songBtn, [this] { showSongMenu(); }, "Play one of your song's tracks as grains");
    btn (libBtn, [this] { showLibrary(); }, "Search your sample library");
    addBtn.getProperties().set ("primary", true);
    btn (addBtn, [this] { addToSong(); }, "Record it into your song as a new track (holding a note, in time with the song)");
    btn (holdBtn, [this] { hold (! holding); }, "Hold a note (C3) so you can hear it without a keyboard");
    btn (modeHold, [this] { beginChange(); set().mode = G::Hold; changed(); }, "hold: grains come from where position puts them; drag on the screen to move through the sound");
    btn (modeStep, [this] { beginChange(); set().mode = G::Step; changed(); }, "step: the cells play the chops in turn, in time with your song");
    btn (modeKeys, [this] { beginChange(); set().mode = G::Keys; changed(); }, "keys: C3 and up play one chop each, like a sampler");
    btn (chops8, [this] { beginChange(); set().chops = 8; selChop = std::min (selChop, 7); changed(); });
    btn (chops16, [this] { beginChange(); set().chops = 16; changed(); });
    btn (clockDown, [this] { beginChange(); set().clock = jlimit (0, 16, set().clock + 1); changed(); }, "slower steps");
    btn (clockUp, [this] { beginChange(); set().clock = jlimit (0, 16, set().clock - 1); changed(); }, "faster steps");
    btn (resetBtn, [this] { beginChange(); set().resetCells(); changed(); }, "every cell on, each on its own chop again");
    btn (undoBtn, [this]
    {
        if (undo.empty()) return;
        redo.push_back (set()); set() = undo.back(); undo.pop_back();
        changed (false); syncControls();
    });
    btn (redoBtn, [this]
    {
        if (redo.empty()) return;
        undo.push_back (set()); set() = redo.back(); redo.pop_back();
        changed (false); syncControls();
    });
    btn (syncBtn, [this]
    {
        beginChange();
        // the closest speed in the other mode, so it barely changes
        auto& s = set();
        const double bpm = proc.songJob.getStatus().haveSong ? proc.songJob.getStatus().song.bpm : 120.0;
        auto convert = [&] (float v)
        {
            if (s.sync) { const double hzNow = 1.0 / (G::divBeats (G::rateDiv (v)) * 60.0 / bpm); return (float) jlimit (0.0, 1.0, std::log (hzNow) / std::log (100.0)); }
            const double beats = 1.0 / G::rateHz (v) * bpm / 60.0;
            int best = 0; for (int i = 0; i < G::numDivs(); ++i) if (std::abs (std::log (G::divBeats (i) / beats)) < std::abs (std::log (G::divBeats (best) / beats))) best = i;
            return (16.0f - (float) best) / 16.0f;
        };
        s.knob[G::Rate] = convert (s.knob[G::Rate]);
        for (auto& c : s.chop) c[G::Rate] = convert (c[G::Rate]);
        s.sync = ! s.sync;
        changed (); syncControls();
    }, "sync: grains on your song's beat (a note value). free: so many a second.");
    btn (envPrev, [this] { beginChange(); set().env = (set().env + G::numEnvs - 1) % G::numEnvs; changed(); });
    btn (envNext, [this] { beginChange(); set().env = (set().env + 1) % G::numEnvs; changed(); });
    btn (revBtn, [this] { beginChange(); set().reverse = ! set().reverse; changed(); }, "every grain played backwards");
    btn (chokeBtn, [this] { beginChange(); set().choke = ! set().choke; changed(); }, "a new note cuts the one before");
    btn (hpOn, [this] { beginChange(); set().hpOn = ! set().hpOn; changed(); }, "the low cut (high-pass): clears the low end");
    btn (lpOn, [this] { beginChange(); set().lpOn = ! set().lpOn; changed(); }, "the high cut (low-pass): softens the top");
    addAndMakeVisible (help);
    presetBtn.setButtonText (G::presets()[0].name);
    syncControls();
}

GrainsPage::~GrainsPage()
{
    proc.grainSynth.allOff();
    proc.grainsActive = false;
}

void GrainsPage::shown (bool on)
{
    if (on == isShown) return;
    isShown = on;
    proc.grainsActive = on;
    if (on) { startTimerHz (30); syncControls(); grabKeyboardFocus(); }
    else
    {
        stopTimer();
        for (int n : keysDown) proc.grainSynth.uiNote (n, 0.0f, false);
        keysDown.clear();
        if (holding) hold (false);
    }
}

String GrainsPage::knobText (int k, double v) const
{
    switch (k)
    {
        case G::Rate:     return set().sync ? String (G::divName (G::rateDiv ((float) v))) : String (roundToInt (G::rateHz ((float) v))) + " /s";
        case G::Length:   return String (roundToInt (v)) + " ms";
        case G::Tune:
        {
            const int st = (int) std::trunc (v), ct = roundToInt ((v - st) * 100.0);
            return (v > 0.004 ? "+" : "") + String (st) + " st" + (ct != 0 ? " " + String (ct > 0 ? "+" : "") + String (ct) + "c" : String());
        }
        default:          return String (roundToInt (v * 100.0)) + " %";
    }
}

void GrainsPage::beginChange()
{
    undo.push_back (set());
    if (undo.size() > 120) undo.erase (undo.begin());
    redo.clear();
}

void GrainsPage::changed (bool pushUndo)
{
    ignoreUnused (pushUndo);
    proc.sendGrainSettings();
    presetEdited = true;
    syncControls();
    repaint();
}

void GrainsPage::syncControls()
{
    auto& s = set();
    for (auto& kn : knobs)
    {
        kn.s.setValue (s.value (kn.id, selChop), dontSendNotification);
        kn.s.repaint();
    }
    hpF.setValue (s.hpHz, dontSendNotification);
    lpF.setValue (s.lpHz, dontSendNotification);
    res.setValue (s.lpRes, dontSendNotification);
    drive.setValue (s.drive, dontSendNotification);
    fmix.setValue (s.filterMix, dontSendNotification);
    output.setValue (s.outputDb, dontSendNotification);
    auto lit = [] (TextButton& b, bool on) { b.getProperties().set ("primary", on); b.repaint(); };
    lit (modeHold, s.mode == G::Hold); lit (modeStep, s.mode == G::Step); lit (modeKeys, s.mode == G::Keys);
    lit (chops8, s.chops != 16); lit (chops16, s.chops == 16);
    lit (syncBtn, s.sync); syncBtn.setButtonText (s.sync ? "sync" : "free");
    lit (revBtn, s.reverse); lit (chokeBtn, s.choke);
    lit (hpOn, s.hpOn); lit (lpOn, s.lpOn);
    lit (holdBtn, holding);
    hpF.setEnabled (s.hpOn); lpF.setEnabled (s.lpOn);
    for (auto* b : { &clockDown, &clockUp }) b->setEnabled (s.mode == G::Step);
    undoBtn.setEnabled (! undo.empty()); redoBtn.setEnabled (! redo.empty());
    presetBtn.setButtonText (presetBtn.getButtonText().trimCharactersAtEnd ("*").trim() + (presetEdited ? " *" : ""));
    addBtn.setEnabled (proc.grainSource != nullptr && proc.songJob.getStatus().haveSong && ! recordingNow.load());
}

void GrainsPage::pickPreset (int i)
{
    auto ps = G::presets();
    Array<File> mine = userPresetFolder().findChildFiles (File::findFiles, false, "*.json");
    mine.sort();
    const int total = (int) ps.size() + mine.size();
    if (total == 0) return;
    i = (i % total + total) % total;
    beginChange();
    const float out = set().outputDb; // (a preset never changes your level or your sound)
    const float t0 = set().trimStart, t1 = set().trimEnd;
    String name;
    if (i < (int) ps.size()) { set() = ps[(size_t) i].s; name = ps[(size_t) i].name; }
    else
    {
        const auto v = JSON::parse (mine[i - (int) ps.size()]);
        set() = G::fromVar (v);
        name = v.getProperty ("name", mine[i - (int) ps.size()].getFileNameWithoutExtension()).toString();
    }
    set().outputDb = out; set().trimStart = t0; set().trimEnd = t1;
    presetIndex = i;
    selChop = jmin (selChop, chopsNow() - 1);
    proc.sendGrainSettings();
    presetEdited = false;
    presetBtn.setButtonText (name);
    syncControls();
    repaint();
}

void GrainsPage::showPresetMenu()
{
    PopupMenu m;
    auto ps = G::presets();
    m.addSectionHeader ("Loopwright");
    for (int i = 0; i < (int) ps.size(); ++i) m.addItem (1 + i, ps[(size_t) i].name, true, i == presetIndex);
    Array<File> mine = userPresetFolder().findChildFiles (File::findFiles, false, "*.json");
    mine.sort();
    if (! mine.isEmpty())
    {
        m.addSectionHeader ("Yours");
        for (int i = 0; i < mine.size(); ++i) m.addItem (1 + (int) ps.size() + i, mine[i].getFileNameWithoutExtension(), true, (int) ps.size() + i == presetIndex);
    }
    SafePointer<GrainsPage> self (this);
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&presetBtn), [self] (int r) { if (self != nullptr && r > 0) self->pickPreset (r - 1); });
}

void GrainsPage::showSongMenu()
{
    const auto st = proc.songJob.getStatus();
    PopupMenu m;
    if (! st.haveSong) m.addItem (-1, "Write a song first: then its tracks are here", false);
    for (int i = 0; i < (int) st.tracks.size(); ++i)
        m.addItem (1 + i, String (st.tracks[(size_t) i].name), st.tracks[(size_t) i].stem.existsAsFile());
    SafePointer<GrainsPage> self (this);
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&songBtn), [self] (int r) { if (self != nullptr && r > 0) self->loadTrack (r - 1); });
}

void GrainsPage::showLibrary()
{
    auto* w = new AlertWindow ("Your library", "Search for a sound: a word from its name or pack (pad, vocal, guitar, texture...)", MessageBoxIconType::NoIcon);
    w->addTextEditor ("q", {});
    w->addButton ("Search", 1, KeyPress (KeyPress::returnKey));
    w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
    SafePointer<GrainsPage> self (this);
    w->enterModalState (true, ModalCallbackFunction::create ([self, w] (int r)
    {
        if (self == nullptr || r != 1) return;
        const auto found = self->proc.songJob.librarySearch (w->getTextEditorContents ("q"), 40);
        PopupMenu m;
        if (found.isEmpty()) m.addItem (-1, "nothing in your library has that in its name", false);
        for (int i = 0; i < found.size(); ++i) m.addItem (1 + i, File (found[i]).getFileNameWithoutExtension());
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (&self->libBtn), [self, found] (int k)
        {
            if (self != nullptr && k > 0) self->loadFile (File (found[k - 1]));
        });
    }), true);
}

void GrainsPage::loadFile (const File& f)
{
    if (! proc.loadGrainSound (f))
    {
        if (onMessage) onMessage ("couldn't play " + f.getFileName() + ": WAV, AIFF, FLAC, MP3 and OGG work");
        return;
    }
    beginChange();
    set().trimStart = 0.0f; set().trimEnd = 1.0f;
    proc.sendGrainSettings();
    syncControls();
    repaint();
}

// a track of your song: its file runs the whole song, so just the bars where it plays (up to 8) are taken
void GrainsPage::loadTrack (int track)
{
    const auto st = proc.songJob.getStatus();
    if (track < 0 || track >= (int) st.tracks.size()) return;
    const auto stem = st.tracks[(size_t) track].stem;
    AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<AudioFormatReader> rd (fm.createReaderFor (stem));
    if (rd == nullptr) { if (onMessage) onMessage ("couldn't read that track"); return; }
    const double bpm = st.song.bpm > 0.0 ? st.song.bpm : 120.0;
    const int barLen = (int) (rd->sampleRate * 4.0 * 60.0 / bpm);
    const int bars = (int) (rd->lengthInSamples / barLen);
    if (bars < 1) { loadFile (stem); return; }
    // how loud each bar is
    std::vector<float> loud ((size_t) bars, 0.0f);
    AudioBuffer<float> b ((int) rd->numChannels, barLen);
    float top = 0.0f;
    for (int i = 0; i < bars; ++i)
    {
        rd->read (&b, 0, barLen, (juce::int64) i * barLen, true, true);
        float e = 0.0f;
        for (int ch = 0; ch < b.getNumChannels(); ++ch) e += b.getRMSLevel (ch, 0, barLen);
        loud[(size_t) i] = e;
        top = jmax (top, e);
    }
    if (top <= 0.0f) { if (onMessage) onMessage ("that track is silent"); return; }
    int first = 0;
    while (first < bars && loud[(size_t) first] < top * 0.15f) ++first;
    int count = 0;
    while (first + count < bars && count < 8 && loud[(size_t) (first + count)] >= top * 0.08f) ++count;
    if (count >= 4) count -= count % 4; // (whole phrases: 4 or 8 bars)
    count = jmax (1, count);
    AudioBuffer<float> part (2, count * barLen);
    rd->read (&part, 0, count * barLen, (juce::int64) first * barLen, true, true);
    if (rd->numChannels < 2) part.copyFrom (1, 0, part, 0, 0, part.getNumSamples());
    const auto folder = st.files.folder.getChildFile ("grains");
    folder.createDirectory();
    const auto name = String (st.tracks[(size_t) track].name) + " (bars " + String (first + 1) + "-" + String (first + count) + ")";
    const auto out = folder.getChildFile (File::createLegalFileName (name) + ".wav");
    out.deleteFile();
    {
        WavAudioFormat wav;
        std::unique_ptr<AudioFormatWriter> w (wav.createWriterFor (new FileOutputStream (out), rd->sampleRate, 2, 24, {}, 0));
        if (w == nullptr || ! w->writeFromAudioSampleBuffer (part, 0, part.getNumSamples())) { if (onMessage) onMessage ("couldn't take that track"); return; }
    }
    loadFile (out);
    if (proc.grainSource != nullptr) proc.grainSource->bpm = bpm; // (it's in the song's tempo)
    if (onMessage) onMessage ("took " + name + " from your song");
}

void GrainsPage::hold (bool on)
{
    if (on == holding) return;
    holding = on;
    proc.grainSynth.uiNote (60, 0.85f, on);
    syncControls();
}

// ---- where things are
Rectangle<float> GrainsPage::screenArea() const
{
    auto r = getLocalBounds().toFloat();
    return { r.getX(), 56.0f, r.getWidth() - 300.0f, jmax (160.0f, r.getHeight() * 0.40f) };
}
Rectangle<float> GrainsPage::waveArea() const { return screenArea().reduced (18.0f, 0.0f).withTrimmedTop (52.0f).withTrimmedBottom (26.0f); }
Rectangle<float> GrainsPage::cellsArea() const
{
    const auto s = screenArea();
    return { s.getX(), s.getBottom() + 58.0f, s.getWidth(), 58.0f };
}
Rectangle<float> GrainsPage::envArea() const
{
    const auto s = screenArea();
    return { s.getRight() + 16.0f, s.getY(), (float) getWidth() - s.getRight() - 16.0f, 196.0f };
}
Rectangle<float> GrainsPage::filterArea() const
{
    const auto e = envArea();
    return { e.getX(), e.getBottom() + 14.0f, e.getWidth(), 268.0f };
}
Rectangle<float> GrainsPage::cellRect (int i) const
{
    const auto a = cellsArea();
    const int n = chopsNow();
    const float gap = n == 16 ? 5.0f : 8.0f, w = (a.getWidth() - gap * (n - 1)) / n;
    return { a.getX() + i * (w + gap), a.getY(), w, a.getHeight() };
}
int GrainsPage::cellAt (Point<float> p) const
{
    for (int i = 0; i < chopsNow(); ++i) if (cellRect (i).contains (p)) return i;
    return -1;
}
float GrainsPage::xOfFrac (float f) const { const auto w = waveArea(); return w.getX() + w.getWidth() * f; }
float GrainsPage::fracOfX (float x) const { const auto w = waveArea(); return jlimit (0.0f, 1.0f, (x - w.getX()) / w.getWidth()); }

void GrainsPage::resized()
{
    const auto sc = screenArea();
    // the top bar: presets on the left, where sounds come from in the middle, record on the right
    int x = 0;
    prevPreset.setBounds (x, 8, 30, 34); x += 34;
    presetBtn.setBounds (x, 8, 200, 34); x += 204;
    nextPreset.setBounds (x, 8, 30, 34); x += 38;
    savePreset.setBounds (x, 8, 64, 34); x += 80;
    help.setTopLeftPosition (x, 17); x += 32;
    loadBtn.setBounds (x, 8, 112, 34); x += 118;
    songBtn.setBounds (x, 8, 132, 34); x += 138;
    libBtn.setBounds (x, 8, 92, 34);
    addBtn.setBounds (getWidth() - 140, 8, 140, 34);
    holdBtn.setBounds (addBtn.getX() - 8 - 104, 8, 104, 34);

    // the step bar, between the screen and the cells
    int y = (int) sc.getBottom() + 14;
    x = (int) sc.getX();
    for (auto* b : { &modeHold, &modeStep, &modeKeys }) { b->setBounds (x, y, 64, 30); x += 68; }
    x += 14;
    chops8.setBounds (x, y, 40, 30); x += 44;
    chops16.setBounds (x, y, 40, 30); x += 58;
    clockDown.setBounds (x, y, 30, 30); x += 30 + 70;   // (the clock's value is drawn between)
    clockUp.setBounds (x, y, 30, 30); x += 46;
    resetBtn.setBounds (x, y, 64, 30);
    redoBtn.setBounds ((int) sc.getRight() - 60, y, 60, 30);
    undoBtn.setBounds (redoBtn.getX() - 64, y, 60, 30);

    // the knobs, under the cells
    const auto ca = cellsArea();
    const int ky = (int) ca.getBottom() + 34, kh = jmax (120, getHeight() - ky - 8);
    const float kw = sc.getWidth() / (float) G::numKnobs;
    for (int k = 0; k < G::numKnobs; ++k)
    {
        auto& kn = knobs[(size_t) k];
        kn.s.setBounds ((int) (sc.getX() + k * kw), ky, (int) kw, kh - 18);
        kn.linkBox = Rectangle<float> (sc.getX() + k * kw + kw * 0.5f - 7.0f, (float) (ky + kh - 16), 14.0f, 14.0f);
    }
    syncBtn.setBounds ((int) (sc.getX() + kw * 0.5f - 30.0f), ky - 30, 60, 24);

    // the right: the grain shape, then the filter, then the output
    const auto ea = envArea();
    envPrev.setBounds ((int) ea.getX() + 14, (int) ea.getBottom() - 44, 30, 30);
    envNext.setBounds ((int) ea.getRight() - 44, (int) ea.getBottom() - 44, 30, 30);
    revBtn.setBounds ((int) ea.getX() + 14, (int) ea.getY() + 12, 76, 26);
    chokeBtn.setBounds ((int) ea.getRight() - 90, (int) ea.getY() + 12, 76, 26);
    const auto fa = filterArea();
    hpOn.setBounds ((int) fa.getX() + 14, (int) fa.getY() + 12, 80, 26);
    lpOn.setBounds ((int) fa.getRight() - 94, (int) fa.getY() + 12, 80, 26);
    const int fy = (int) fa.getY() + 128, fw = (int) (fa.getWidth() - 20) / 5;
    int i = 0;
    for (auto* s : { &hpF, &lpF, &res, &drive, &fmix }) s->setBounds ((int) fa.getX() + 10 + fw * i++, fy, fw, 128);
    output.setBounds ((int) fa.getX() + 10, (int) fa.getBottom() + 10, (int) fa.getWidth() - 60, jmax (100, getHeight() - (int) fa.getBottom() - 14));
}

// ---- drawing
void GrainsPage::paint (Graphics& g)
{
    const auto& t = *th;
    auto& s = set();
    const auto* src = proc.grainSource.get();
    const auto sc = screenArea();
    auto card = [&] (Rectangle<float> r)
    {
        g.setColour (t.body);
        g.fillRoundedRectangle (r, 14.0f);
        g.setColour (t.line);
        g.drawRoundedRectangle (r.reduced (0.5f), 14.0f, 1.0f);
    };

    // ---- the screen
    {
        ColourGradient bg (Colour (0xff0c0b10), sc.getX(), sc.getY(), Colour (0xff0a0a0c).interpolatedWith (t.grassDark, 0.18f), sc.getX(), sc.getBottom(), false);
        g.setGradientFill (bg);
        g.fillRoundedRectangle (sc, 16.0f);
        g.setColour (Colours::white.withAlpha (0.06f));
        g.drawRoundedRectangle (sc.reduced (0.5f), 16.0f, 1.0f);
        // a faint grid, like a scope
        g.setColour (Colours::white.withAlpha (0.025f));
        for (float gx = sc.getX() + 40.0f; gx < sc.getRight(); gx += 40.0f) g.drawVerticalLine ((int) gx, sc.getY() + 46.0f, sc.getBottom() - 8.0f);
        auto head = sc.reduced (18.0f, 12.0f).removeFromTop (34.0f);
        if (src == nullptr)
        {
            g.setColour (dropping ? t.grassLight : t.ink);
            g.setFont (font (22.0f, lp::ui::Medium, -0.01f));
            g.drawText (dropping ? "Let go to play it" : "Drop a sound here", sc.withTrimmedBottom (sc.getHeight() * 0.18f), Justification::centred, false);
            g.setFont (font (13.0f));
            g.setColour (t.inkDim);
            g.drawText ("any sample, loop or vocal from your computer or your DAW, a track of your song, or one from your library",
                        sc.withTrimmedTop (sc.getHeight() * 0.22f), Justification::centred, false);
        }
        else
        {
            g.setColour (t.ink);
            g.setFont (font (16.0f, lp::ui::Medium));
            g.drawText (src->name, head.removeFromTop (19.0f), Justification::centredLeft, true);
            String info = String (src->sampleRate / 1000.0, src->sampleRate == 44100.0 ? 1 : 0) + " kHz   /   "
                        + String (src->audio.getNumSamples() / src->sampleRate, 1) + " s";
            if (src->bpm > 0.0) info << "   /   " << String (roundToInt (src->bpm)) << " bpm";
            if (src->key.isNotEmpty()) info << "   /   " << src->key;
            g.setFont (lp::ui::mono (10.0f, false, 0.12f));
            g.setColour (t.inkDim);
            g.drawText (info.toUpperCase(), head, Justification::centredLeft, true);
            // the note being played (so you can see your keys arrive)
            if (const int held = proc.grainSynth.heldNow.load(); held > 0 && proc.grainSynth.lastNote.load() >= 0)
            {
                auto chip = sc.reduced (18.0f, 12.0f).removeFromTop (20.0f).removeFromRight (420.0f).removeFromLeft (110.0f);
                g.setColour (t.grass.withAlpha (0.25f));
                g.fillRoundedRectangle (chip, 10.0f);
                g.setColour (t.grassLight);
                g.setFont (lp::ui::mono (10.0f, true, 0.12f));
                g.drawText ("PLAYING " + noteName (proc.grainSynth.lastNote.load()) + (held > 1 ? " +" + String (held - 1) : String()), chip, Justification::centred, false);
            }
            const int live = proc.grainSynth.activeGrains.load();
            g.setColour (live > 0 ? t.grassLight : t.inkDim);
            g.drawText (live > 0 ? String (live) + " GRAINS" : (s.mode == G::Step ? "PLAY: KEYS A-K, MIDI OR HOLD NOTE  (PLAY THE SONG TO STEP)" : "PLAY: KEYS A-K, MIDI OR HOLD NOTE"),
                        sc.reduced (18.0f, 14.0f).removeFromTop (16.0f), Justification::centredRight, false);

            const auto wa = waveArea();
            const float mid = wa.getCentreY();
            const int n = chopsNow();
            const float t0 = s.trimStart, t1 = s.trimEnd;
            // the chop picked
            if (s.mode != G::Hold)
            {
                const float cw = (t1 - t0) / n;
                g.setColour (t.grass.withAlpha (0.13f));
                g.fillRect (Rectangle<float>::leftTopRightBottom (xOfFrac (t0 + cw * selChop), wa.getY(), xOfFrac (t0 + cw * (selChop + 1)), wa.getBottom()));
            }
            // the waveform (mirrored), brighter inside the trim
            const int cols = jmax (1, (int) wa.getWidth() / 2);
            for (int c = 0; c < cols; ++c)
            {
                const float f = (float) c / cols;
                const size_t pi = (size_t) jlimit (0, (int) src->peaks.size() - 1, (int) (f * src->peaks.size()));
                const float h = jmax (0.8f, src->peaks[pi] * wa.getHeight() * 0.46f);
                const bool in = f >= t0 && f <= t1;
                g.setColour (in ? Colours::white.withAlpha (0.62f) : Colours::white.withAlpha (0.14f));
                g.fillRect (wa.getX() + c * 2.0f, mid - h, 1.2f, h * 2.0f);
            }
            // chops: thin lines and numbers
            for (int c = 0; c <= n; ++c)
            {
                const float x = xOfFrac (t0 + (t1 - t0) * c / n);
                g.setColour (Colours::white.withAlpha (c == 0 || c == n ? 0.35f : 0.12f));
                g.drawVerticalLine ((int) x, wa.getY(), wa.getBottom() + 4.0f);
                if (c < n)
                {
                    g.setFont (lp::ui::mono (9.0f, c == selChop && s.mode != G::Hold, 0.1f));
                    g.setColour (c == selChop && s.mode != G::Hold ? t.grassLight : t.inkDim.withAlpha (0.7f));
                    g.drawText (String (c + 1), Rectangle<float> (x, wa.getBottom() + 6.0f, xOfFrac (t0 + (t1 - t0) * (c + 1) / n) - x, 14.0f), Justification::centred, false);
                }
            }
            // the trim handles
            for (float f : { t0, t1 })
            {
                const float x = xOfFrac (f);
                g.setColour (t.ink.withAlpha (0.85f));
                g.fillRoundedRectangle (x - 3.0f, wa.getY() - 6.0f, 6.0f, 16.0f, 3.0f);
            }
            // where it reads from
            {
                const float pos = s.value (G::Position, selChop);
                const float f = s.mode == G::Hold ? t0 + (t1 - t0) * pos : t0 + (t1 - t0) * (selChop + pos) / n;
                const float x = xOfFrac (f);
                g.setColour (t.grassLight);
                g.fillRect (x - 1.0f, wa.getY() - 2.0f, 2.0f, wa.getHeight() + 4.0f);
                Path tri; tri.addTriangle (x - 6.0f, wa.getY() - 10.0f, x + 6.0f, wa.getY() - 10.0f, x, wa.getY() - 2.0f);
                g.fillPath (tri);
            }
            // the grains, glowing where they read
            for (size_t k = 0; k < proc.grainSynth.activity.size(); ++k)
            {
                const float a = proc.grainSynth.activity[k].load();
                if (a < 0.0f) { trailA[k] *= 0.8f; }
                else { trailX[k] = xOfFrac (a); trailA[k] = jlimit (0.15f, 1.0f, proc.grainSynth.activityAmp[k].load() * 1.6f); }
                if (trailA[k] < 0.03f) continue;
                const float x = trailX[k], h = wa.getHeight() * (0.25f + 0.55f * trailA[k]);
                ColourGradient glow (t.grassLight.withAlpha (0.55f * trailA[k]), x, mid, t.grass.withAlpha (0.0f), x + 14.0f, mid, true);
                g.setGradientFill (glow);
                g.fillEllipse (x - 14.0f, mid - h * 0.5f, 28.0f, h);
                g.setColour (Colours::white.withAlpha (0.8f * trailA[k]));
                g.fillRoundedRectangle (x - 1.0f, mid - h * 0.32f, 2.0f, h * 0.64f, 1.0f);
            }
            if (dropping)
            {
                g.setColour (t.grass.withAlpha (0.2f));
                g.fillRoundedRectangle (sc, 16.0f);
                g.setColour (t.ink);
                g.setFont (font (18.0f, lp::ui::Medium));
                g.drawText ("Let go to play it", sc, Justification::centred, false);
            }
        }
    }

    // ---- the step bar: the clock's value
    {
        const auto cb = clockDown.getBounds().toFloat();
        g.setFont (lp::ui::mono (11.0f, true, 0.1f));
        g.setColour (s.mode == G::Step ? t.ink : t.inkDim.withAlpha (0.5f));
        g.drawText (G::divName (s.clock), Rectangle<float> (cb.getRight(), cb.getY(), 70.0f, cb.getHeight()), Justification::centred, false);
        g.setFont (lp::ui::mono (9.0f, false, 0.14f));
        g.setColour (t.inkDim);
        g.drawText ("CLOCK", Rectangle<float> (cb.getRight(), cb.getY() - 14.0f, 70.0f, 12.0f), Justification::centred, false);
        g.drawText ("MODE", modeHold.getBounds().toFloat().withY ((float) modeHold.getY() - 14.0f).withHeight (12.0f), Justification::centredLeft, false);
        g.drawText ("CHOPS", chops8.getBounds().toFloat().withY ((float) chops8.getY() - 14.0f).withHeight (12.0f).withWidth (84.0f), Justification::centredLeft, false);
    }

    // ---- the cells
    {
        const int n = chopsNow(), cur = proc.grainSynth.currentStep.load();
        const float dim = s.mode == G::Hold ? 0.35f : 1.0f;
        for (int i = 0; i < n; ++i)
        {
            const auto r = cellRect (i);
            const bool on = s.mode == G::Keys || s.cellOn[(size_t) i];
            const int chop = s.mode == G::Keys ? i : jlimit (0, n - 1, s.cellChop[(size_t) i]);
            const bool now = s.mode == G::Step && cur == i;
            g.setColour ((now ? t.grass.withAlpha (0.55f) : on ? t.bodyLight : t.bodyDark).withMultipliedAlpha (dim));
            g.fillRoundedRectangle (r, 8.0f);
            if (i == selChop && s.mode != G::Hold)
            {
                g.setColour (t.grassLight);
                g.drawRoundedRectangle (r.reduced (0.75f), 8.0f, 1.5f);
            }
            else
            {
                g.setColour (t.line.withMultipliedAlpha (dim));
                g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
            }
            if (! on)
            {
                g.setColour (t.inkDim.withAlpha (0.35f));
                g.drawLine (r.getX() + 8.0f, r.getBottom() - 8.0f, r.getRight() - 8.0f, r.getY() + 8.0f, 1.0f);
                continue;
            }
            // its chop's waveform, small
            if (src != nullptr)
            {
                const auto wr = r.reduced (6.0f, 0.0f).withTrimmedTop (18.0f).withTrimmedBottom (6.0f);
                const float f0 = s.trimStart + (s.trimEnd - s.trimStart) * chop / n, f1 = s.trimStart + (s.trimEnd - s.trimStart) * (chop + 1) / n;
                const int cols = jmax (2, (int) wr.getWidth() / 2);
                g.setColour (Colours::white.withAlpha (0.45f * dim));
                for (int c = 0; c < cols; ++c)
                {
                    const float f = f0 + (f1 - f0) * c / cols;
                    const float h = jmax (0.5f, src->peaks[(size_t) jlimit (0, (int) src->peaks.size() - 1, (int) (f * src->peaks.size()))] * wr.getHeight() * 0.5f);
                    g.fillRect (wr.getX() + c * 2.0f, wr.getCentreY() - h, 1.0f, h * 2.0f);
                }
            }
            g.setFont (lp::ui::mono (9.5f, true, 0.08f));
            g.setColour ((now ? t.ink : t.inkDim).withMultipliedAlpha (dim));
            g.drawText (s.mode == G::Keys ? noteName (60 + i) : String (chop + 1), r.reduced (7.0f, 4.0f).removeFromTop (12.0f), Justification::centredLeft, false);
        }
        g.setFont (lp::ui::mono (9.0f, false, 0.14f));
        g.setColour (t.inkDim);
        g.drawText (s.mode == G::Hold ? "CELLS  (USED IN STEP AND KEYS)" : s.mode == G::Keys ? "CELLS  (C3 AND UP PLAY THEM)" : "CELLS  (CLICK: PICK   DRAG SIDEWAYS: ANOTHER CHOP   RIGHT-CLICK: MORE)",
                    cellsArea().withY (cellsArea().getY() - 14.0f).withHeight (12.0f), Justification::centredLeft, false);
    }

    // ---- the link dots under the knobs
    for (auto& kn : knobs)
    {
        const bool linked = s.link[(size_t) kn.id];
        const auto r = kn.linkBox;
        g.setColour (linked ? t.grass : t.inkDim.withAlpha (0.6f));
        if (linked) g.fillEllipse (r.reduced (3.0f));
        else g.drawEllipse (r.reduced (3.0f), 1.5f);
        if (! linked)
        {
            g.setFont (lp::ui::mono (8.5f, true, 0.1f));
            g.setColour (t.grassLight);
            g.drawText ("CHOP " + String (selChop + 1), r.withSizeKeepingCentre (70.0f, 12.0f).translated (0.0f, -13.0f), Justification::centred, false);
        }
    }

    // ---- the grain shape
    {
        const auto ea = envArea();
        card (ea);
        g.setFont (lp::ui::mono (9.5f, false, 0.14f));
        g.setColour (t.inkDim);
        g.drawText ("GRAIN SHAPE", ea.withTrimmedTop (44.0f).withHeight (14.0f).reduced (16.0f, 0.0f), Justification::centredLeft, false);
        auto pr = ea.reduced (20.0f, 0.0f).withTrimmedTop (64.0f).withTrimmedBottom (54.0f);
        Path shape;
        const int pts = 80;
        for (int i = 0; i <= pts; ++i)
        {
            const float x = (float) i / pts;
            float v;
            if (s.env == G::Square || s.env == G::Trapezoid) { const float rmp = s.env == G::Square ? 0.03f : 0.12f; v = jmin (1.0f, x / rmp, (1.0f - x) / rmp); }
            else if (s.env == G::Hann) v = 0.5f - 0.5f * std::cos (MathConstants<float>::twoPi * x);
            else if (s.env == G::Triangle) v = 1.0f - std::abs (2.0f * x - 1.0f);
            else { const float e = std::exp (-0.5f * std::pow (0.5f / 0.16f, 2.0f)); v = (std::exp (-0.5f * std::pow ((x - 0.5f) / 0.16f, 2.0f)) - e) / (1.0f - e); }
            const Point<float> p (pr.getX() + pr.getWidth() * x, pr.getBottom() - pr.getHeight() * jlimit (0.0f, 1.0f, v));
            i == 0 ? shape.startNewSubPath (p) : shape.lineTo (p);
        }
        Path fill (shape);
        fill.lineTo (pr.getBottomRight()); fill.lineTo (pr.getBottomLeft()); fill.closeSubPath();
        g.setGradientFill (ColourGradient (t.grass.withAlpha (0.45f), pr.getCentreX(), pr.getY(), t.grass.withAlpha (0.02f), pr.getCentreX(), pr.getBottom(), false));
        g.fillPath (fill);
        g.setColour (t.grassLight);
        g.strokePath (shape, PathStrokeType (2.0f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setFont (lp::ui::mono (11.0f, true, 0.12f));
        g.setColour (t.ink);
        g.drawText (String (G::envName (s.env)).toUpperCase(), Rectangle<float> (ea.getX() + 50.0f, ea.getBottom() - 44.0f, ea.getWidth() - 100.0f, 30.0f), Justification::centred, false);
    }

    // ---- the filter
    {
        const auto fa = filterArea();
        card (fa);
        auto cr = fa.reduced (16.0f, 0.0f).withTrimmedTop (48.0f).withHeight (70.0f);
        g.setColour (Colours::black.withAlpha (0.25f));
        g.fillRoundedRectangle (cr, 8.0f);
        Path curve;
        const float q = 0.707f + s.lpRes * 6.0f;
        for (int i = 0; i <= 100; ++i)
        {
            const double f = 20.0 * std::pow (1000.0, i / 100.0);
            double m = 1.0;
            auto lpMag = [] (double fr, double fc, double Q) { const double x = fr / fc; return 1.0 / std::sqrt (std::pow (1.0 - x * x, 2.0) + std::pow (x / Q, 2.0)); };
            auto hpMag = [] (double fr, double fc, double Q) { const double x = fc / fr; return 1.0 / std::sqrt (std::pow (1.0 - x * x, 2.0) + std::pow (x / Q, 2.0)); };
            if (s.hpOn) m *= hpMag (f, s.hpHz, q);
            if (s.lpOn) m *= lpMag (f, s.lpHz, q);
            m = m * s.filterMix + (1.0 - s.filterMix);
            const float db = (float) jlimit (-30.0, 18.0, 20.0 * std::log10 (jmax (1e-4, m)));
            const Point<float> p (cr.getX() + cr.getWidth() * i / 100.0f, cr.getY() + cr.getHeight() * (1.0f - (db + 30.0f) / 48.0f));
            i == 0 ? curve.startNewSubPath (p) : curve.lineTo (p);
        }
        g.setColour (s.hpOn || s.lpOn ? t.grassLight : t.inkDim);
        g.strokePath (curve, PathStrokeType (1.8f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setFont (lp::ui::mono (9.5f, false, 0.14f));
        g.setColour (t.inkDim);
        g.drawText ("FILTER", fa.withTrimmedTop (16.0f).withHeight (14.0f), Justification::centred, false);
    }

    // ---- the output meter
    {
        const auto ob = output.getBounds().toFloat();
        auto m = Rectangle<float> (ob.getRight() + 16.0f, ob.getY() + 10.0f, 8.0f, ob.getHeight() - 30.0f);
        g.setColour (Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (m, 4.0f);
        const float lvl = jlimit (0.0f, 1.0f, (Decibels::gainToDecibels (proc.grainSynth.meter.load(), -60.0f) + 60.0f) / 60.0f);
        auto f = m.withTrimmedTop (m.getHeight() * (1.0f - lvl));
        g.setColour (proc.grainSynth.meter.load() > 0.98f ? Colour (0xffff5a5a) : t.grass);
        g.fillRoundedRectangle (f, 4.0f);
    }
    if (note.isNotEmpty() && Time::getMillisecondCounter() - noteAt < 7000)
    {
        g.setFont (font (12.5f, lp::ui::Medium));
        g.setColour (t.grassLight);
        g.drawText (note, Rectangle<float> ((float) libBtn.getRight() + 16.0f, 8.0f, (float) holdBtn.getX() - libBtn.getRight() - 28.0f, 34.0f), Justification::centredLeft, true);
    }
    if (recordingNow.load())
    {
        g.setColour (t.grassLight);
        g.setFont (lp::ui::mono (10.0f, true, 0.12f));
        g.drawText ("RECORDING INTO YOUR SONG...", addBtn.getBounds().toFloat().translated (0.0f, 40.0f).withWidth (260.0f).withX ((float) getWidth() - 260.0f), Justification::centredRight, false);
    }
}

void GrainsPage::timerCallback()
{
    if (! isShown) return;
    // your computer keys reach this page: it takes the keyboard back after a click on a knob or a button
    // (never from a text box, or while a window like the library search is open)
    if (isShowing() && ! hasKeyboardFocus (true) && Component::getCurrentlyModalComponent() == nullptr)
    {
        auto* f = Component::getCurrentlyFocusedComponent();
        if (f == nullptr || (dynamic_cast<TextEditor*> (f) == nullptr && getTopLevelComponent()->isParentOf (f)))
            grabKeyboardFocus();
    }
    // keys let go (some hosts don't tell the page): checked here too
    for (auto it = keysDown.begin(); it != keysDown.end();)
    {
        bool down = false;
        for (const char* c = keyChars; *c != 0; ++c)
            if (noteForKey ((juce_wchar) *c) == *it)
                down = down || KeyPress::isKeyCurrentlyDown ((int) *c) || KeyPress::isKeyCurrentlyDown ((int) CharacterFunctions::toUpperCase ((juce_wchar) *c));
        if (! down) { proc.grainSynth.uiNote (*it, 0.0f, false); it = keysDown.erase (it); }
        else ++it;
    }
    repaint (screenArea().toNearestInt());
    repaint (cellsArea().expanded (2.0f).toNearestInt());
    repaint (output.getBounds().expanded (40, 0));
    if (note.isNotEmpty() && Time::getMillisecondCounter() - noteAt > 7000) { note = {}; repaint(); }
    const bool canAdd = proc.grainSource != nullptr && proc.songJob.getStatus().haveSong && ! recordingNow.load();
    if (addBtn.isEnabled() != canAdd) addBtn.setEnabled (canAdd);
}

// ---- the mouse
void GrainsPage::mouseMove (const MouseEvent& e)
{
    const auto p = e.position;
    const auto wa = waveArea();
    const auto* src = proc.grainSource.get();
    MouseCursor c = MouseCursor::NormalCursor;
    if (src != nullptr && wa.expanded (0.0f, 12.0f).contains (p))
    {
        if (std::abs (p.x - xOfFrac (set().trimStart)) < 6.0f || std::abs (p.x - xOfFrac (set().trimEnd)) < 6.0f) c = MouseCursor::LeftRightResizeCursor;
        else c = MouseCursor::IBeamCursor;
    }
    else if (cellAt (p) >= 0 && set().mode == G::Step) c = MouseCursor::PointingHandCursor;
    setMouseCursor (c);
}

void GrainsPage::mouseDown (const MouseEvent& e)
{
    grabKeyboardFocus();
    const auto p = e.position;
    auto& s = set();
    // the link dots
    for (auto& kn : knobs)
        if (kn.linkBox.expanded (4.0f).contains (p))
        {
            beginChange();
            const bool link = ! s.link[(size_t) kn.id];
            if (! link) for (auto& c : s.chop) c[(size_t) kn.id] = s.knob[(size_t) kn.id]; // (each chop starts from where it was)
            s.link[(size_t) kn.id] = link;
            changed();
            return;
        }
    if (const int c = cellAt (p); c >= 0)
    {
        if (e.mods.isPopupMenu()) { showCellMenu (c); return; }
        selChop = s.mode == G::Keys ? c : c;
        syncControls();
        drag = Drag::Cell;
        dragCell = c;
        dragCellFrom = s.cellChop[(size_t) c];
        dragStartX = p.x;
        if (s.mode == G::Keys) proc.grainSynth.uiNote (60 + c, 0.85f, true); // (hear it)
        repaint();
        return;
    }
    const auto* src = proc.grainSource.get();
    const auto wa = waveArea();
    if (src != nullptr && wa.expanded (0.0f, 14.0f).contains (p))
    {
        beginChange();
        if (std::abs (p.x - xOfFrac (s.trimStart)) < 7.0f) { drag = Drag::TrimStart; return; }
        if (std::abs (p.x - xOfFrac (s.trimEnd)) < 7.0f) { drag = Drag::TrimEnd; return; }
        drag = Drag::Position;
        mouseDrag (e);
    }
}

void GrainsPage::mouseDrag (const MouseEvent& e)
{
    auto& s = set();
    const float f = fracOfX (e.position.x);
    if (drag == Drag::TrimStart) { s.trimStart = jlimit (0.0f, s.trimEnd - 0.01f, f); changed(); return; }
    if (drag == Drag::TrimEnd) { s.trimEnd = jlimit (s.trimStart + 0.01f, 1.0f, f); changed(); return; }
    if (drag == Drag::Position)
    {
        const float t0 = s.trimStart, t1 = s.trimEnd, rel = jlimit (0.0f, 1.0f, (f - t0) / jmax (1e-4f, t1 - t0));
        if (s.mode == G::Hold) s.set (G::Position, -1, rel);
        else
        {
            // in step and keys: the picked chop, where in it (clicking another chop picks it)
            const int n = chopsNow();
            const int chop = jlimit (0, n - 1, (int) (rel * n));
            if (chop != selChop) selChop = chop;
            s.set (G::Position, selChop, jlimit (0.0f, 0.999f, rel * n - (float) chop));
        }
        changed();
        return;
    }
    if (drag == Drag::Cell && s.mode == G::Step && dragCell >= 0)
    {
        // sideways: this cell plays another chop
        const float w = cellRect (0).getWidth();
        const int n = chopsNow();
        const int to = jlimit (0, n - 1, dragCellFrom + roundToInt ((e.position.x - dragStartX) / jmax (8.0f, w * 0.6f)));
        if (to != s.cellChop[(size_t) dragCell])
        {
            if (undo.empty() || undo.back().cellChop[(size_t) dragCell] == s.cellChop[(size_t) dragCell]) beginChange();
            s.cellChop[(size_t) dragCell] = to;
            changed();
        }
    }
}

void GrainsPage::mouseUp (const MouseEvent&)
{
    if (drag == Drag::Cell && set().mode == G::Keys && dragCell >= 0) proc.grainSynth.uiNote (60 + dragCell, 0.0f, false);
    drag = Drag::None;
    dragCell = -1;
}

void GrainsPage::showCellMenu (int cell)
{
    auto& s = set();
    PopupMenu m, copyM;
    copyM.addItem (10, "Everything");
    copyM.addItem (11, "Only space");
    copyM.addItem (12, "Only chaos");
    copyM.addItem (13, "Only position");
    copyM.addItem (14, "Only grain (rate and length)");
    m.addSubMenu ("Copy", copyM);
    m.addItem (20, "Paste", copied.has_value());
    m.addItem (21, "Copy to all", true);
    m.addSeparator();
    m.addItem (30, s.cellOn[(size_t) cell] ? "Disable step" : "Enable step", s.mode == G::Step);
    SafePointer<GrainsPage> self (this);
    m.showMenuAsync (PopupMenu::Options(), [self, cell] (int r)
    {
        if (self == nullptr || r <= 0) return;
        auto& st = self->set();
        const int chop = st.mode == G::Keys ? cell : jlimit (0, G::maxChops - 1, st.cellChop[(size_t) cell]);
        std::array<float, G::numKnobs> vals {};
        for (int k = 0; k < G::numKnobs; ++k) vals[(size_t) k] = st.value (k, chop);
        if (r >= 10 && r <= 14)
        {
            self->copied = vals;
            self->copiedWhich.fill (r == 10);
            if (r == 11) self->copiedWhich[G::Space] = true;
            if (r == 12) self->copiedWhich[G::Chaos] = true;
            if (r == 13) self->copiedWhich[G::Position] = true;
            if (r == 14) { self->copiedWhich[G::Rate] = true; self->copiedWhich[G::Length] = true; }
            return;
        }
        self->beginChange();
        auto pasteInto = [&] (int c, const std::array<float, G::numKnobs>& v, const std::array<bool, G::numKnobs>& which)
        {
            for (int k = 0; k < G::numKnobs; ++k)
                if (which[(size_t) k]) { st.link[(size_t) k] = false; st.chop[(size_t) c][(size_t) k] = v[(size_t) k]; }
        };
        if (r == 20 && self->copied) pasteInto (chop, *self->copied, self->copiedWhich);
        if (r == 21)
        {
            std::array<bool, G::numKnobs> all; all.fill (true);
            for (int k = 0; k < G::numKnobs; ++k) st.set (k, -1, vals[(size_t) k]);
            ignoreUnused (all);
        }
        if (r == 30) st.cellOn[(size_t) cell] = ! st.cellOn[(size_t) cell];
        self->changed();
    });
}

// ---- your computer keys
bool GrainsPage::keyPressed (const KeyPress& k)
{
    if (k.getModifiers().isCommandDown() || k.getModifiers().isCtrlDown()) return false;
    juce_wchar c = k.getTextCharacter();
    if (c == 0) c = (juce_wchar) k.getKeyCode();
    const int note = noteForKey (c);
    if (note < 0) return false;
    // (the press plays it at once; letting go is heard by keyStateChanged, or the timer if the host keeps it)
    if (keysDown.insert (note).second) proc.grainSynth.uiNote (note, 0.85f, true);
    return true;
}

bool GrainsPage::keyStateChanged (bool)
{
    bool used = false;
    for (const char* c = keyChars; *c != 0; ++c)
    {
        const int note = noteForKey ((juce_wchar) *c);
        const bool down = KeyPress::isKeyCurrentlyDown ((int) *c) || KeyPress::isKeyCurrentlyDown ((int) CharacterFunctions::toUpperCase ((juce_wchar) *c));
        const bool was = keysDown.count (note) > 0;
        if (down && ! was) { keysDown.insert (note); proc.grainSynth.uiNote (note, 0.85f, true); used = true; }
        if (! down && was) { keysDown.erase (note); proc.grainSynth.uiNote (note, 0.0f, false); used = true; }
    }
    return used;
}

bool GrainsPage::isInterestedInFileDrag (const StringArray& files)
{
    for (auto& f : files) if (File (f).hasFileExtension ("wav;aif;aiff;flac;mp3;ogg")) return true;
    return false;
}

void GrainsPage::filesDropped (const StringArray& files, int, int)
{
    dropping = false;
    for (auto& f : files) if (File (f).hasFileExtension ("wav;aif;aiff;flac;mp3;ogg")) { loadFile (File (f)); break; }
    repaint();
}

// ---- record it into the song: a note held through the sections, in time with the song, as a new track
void GrainsPage::addToSong()
{
    const auto st = proc.songJob.getStatus();
    if (! st.haveSong || proc.grainSource == nullptr || recordingNow.load()) return;
    PopupMenu m;
    m.addSectionHeader ("Record it into your song");
    m.addItem (1, "The whole song");
    for (int i = 0; i < (int) st.song.sections.size(); ++i)
        m.addItem (100 + i, "Only " + String (st.song.sections[(size_t) i].name) + "  (bars " + String (st.song.sections[(size_t) i].startBar + 1) + "-"
                                + String (st.song.sections[(size_t) i].startBar + st.song.sections[(size_t) i].bars) + ")");
    SafePointer<GrainsPage> self (this);
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&addBtn), [self, st] (int r)
    {
        if (self == nullptr || r <= 0) return;
        const double bpm = st.song.bpm > 0.0 ? st.song.bpm : 120.0;
        int fromBar = 0, bars = st.song.totalBars();
        String where = "the whole song";
        if (r >= 100 && r - 100 < (int) st.song.sections.size())
        {
            const auto& sec = st.song.sections[(size_t) (r - 100)];
            fromBar = sec.startBar; bars = sec.bars; where = String (sec.name);
        }
        auto src = self->proc.grainSource;
        const auto settings = self->set();
        const auto folder = st.files.folder.getChildFile ("grains");
        folder.createDirectory();
        const auto name = src->name.substring (0, 24);
        const auto out = folder.getChildFile (File::createLegalFileName ("grains " + name + " " + String (Time::getMillisecondCounter() % 100000) + ".wav"));
        self->recordingNow = true;
        self->syncControls();
        self->repaint();
        LivePlanetProcessor* p = &self->proc;
        Thread::launch ([self, p, src, settings, bpm, fromBar, bars, out, name, where]
        {
            const double sr = 48000.0;
            const int block = 512;
            const double secPerBar = 4.0 * 60.0 / bpm;
            const int start = (int) (fromBar * secPerBar * sr), length = (int) ((fromBar + bars) * secPerBar * sr + 2.0 * sr); // (a tail for the last grains)
            juce::AudioBuffer<float> buf (2, length);
            buf.clear();
            G::Synth syn;
            syn.prepare (sr, block);
            syn.setSource (src);
            syn.setSettings (settings);
            G::Transport t;
            t.playing = true; t.bpm = bpm;
            const int endNote = (int) ((fromBar + bars) * secPerBar * sr);
            bool on = false;
            for (int i = start; i < length; i += block)
            {
                const int n = std::min (block, length - i);
                t.ppq = (double) i / sr * bpm / 60.0;
                if (! on && i >= start) { syn.noteOn (60, 0.85f); on = true; }
                if (on && i >= endNote) { syn.noteOff (60); on = false; }
                syn.process (buf.getWritePointer (0, i), buf.getWritePointer (1, i), n, t);
            }
            // as loud as the sound it came from (grains with gaps between them stay a little quieter, as they sound)
            {
                const auto& a = src->audio;
                const int s0 = (int) (settings.trimStart * a.getNumSamples()), s1 = std::max (s0 + 1, (int) (settings.trimEnd * a.getNumSamples()));
                const float want = 0.5f * (a.getRMSLevel (0, s0, s1 - s0) + a.getRMSLevel (1, s0, s1 - s0));
                const int r1 = std::min (length, endNote) - start;
                const float got = r1 > 0 ? 0.5f * (buf.getRMSLevel (0, start, r1) + buf.getRMSLevel (1, start, r1)) : 0.0f;
                if (got > 1e-6f && want > 1e-6f) buf.applyGain (jlimit (0.25f, 6.0f, want / got));
            }
            const float pk = buf.getMagnitude (0, length);
            if (pk > 0.97f) buf.applyGain (0.97f / pk);
            bool ok = false;
            {
                WavAudioFormat wav;
                out.deleteFile();
                if (auto* os = new FileOutputStream (out); os->openedOk())
                {
                    std::unique_ptr<AudioFormatWriter> w (wav.createWriterFor (os, sr, 2, 24, {}, 0));
                    if (w != nullptr) ok = w->writeFromAudioSampleBuffer (buf, 0, length);
                    else delete os;
                }
                else delete os;
            }
            MessageManager::callAsync ([self, p, ok, out, name, where]
            {
                if (ok) p->songJob.addSungVocal (out, "grains, " + name, "grains of " + name + " over " + where + ", made on the Grains page", false, "grains", false);
                if (self == nullptr) return;
                self->recordingNow = false;
                self->syncControls();
                self->repaint();
                if (self->onMessage) self->onMessage (ok ? "recording the grains into your song (" + where + "): a new track" : String ("couldn't record the grains"));
            });
        });
    });
}
