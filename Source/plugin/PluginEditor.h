#pragma once
#include "VoiceHelper.h"
#include "../song/Tune.h"
#include "../song/Stack.h"
#include <set>
#include "Meter.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"

namespace lp::ui
{
// where a full-window panel goes (the vocal chain, chop pads...): over the whole plugin, at its own scale
juce::Component* overlayHost (juce::Component* from);
}

namespace lp::ui
{
    enum Weight { Light, Medium, Bold };
    juce::Font font (float size, Weight w = Light, float kerning = 0.0f);
    // small uppercase labels, buttons and numbers
    juce::Font mono (float size, bool bold = false, float kerning = 0.08f);

    // the colour of the device body
    struct Theme
    {
        const char* name;
        juce::Colour body, bodyLight, bodyDark, ink, inkDim, line; // body = the birch bark slabs
        bool dark;
        juce::Colour grassLight, grass, grassDark, skyTop, skyBottom;
    };
    const Theme& theme (int index);
    constexpr int numThemes = 4;
    // the product's name, as shown in the wordmark (one place to change it)
    inline const char* productName() { return "LOOPWRIGHT"; }
}

class LivePlanetLookAndFeel : public juce::LookAndFeel_V4
{
public:
    LivePlanetLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end,
                           juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool over, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int) override;
    // menus, confirm boxes and tips, in the plugin's own look
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive, bool isHighlighted,
                            bool isTicked, bool hasSubMenu, const juce::String& text, const juce::String& shortcutKeyText,
                            const juce::Drawable* icon, const juce::Colour* textColour) override;
    void drawPopupMenuSectionHeader (juce::Graphics&, const juce::Rectangle<int>& area, const juce::String& name) override;
    juce::Font getPopupMenuFont() override;
    void getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardMenuItemHeight, int& idealWidth, int& idealHeight) override;
    int getPopupMenuBorderSize() override { return 7; }
    void drawAlertBox (juce::Graphics&, juce::AlertWindow&, const juce::Rectangle<int>& textArea, juce::TextLayout&) override;
    int getAlertBoxWindowFlags() override { return 0; } // no title bar: just the box
    juce::Font getAlertWindowTitleFont() override;
    juce::Font getAlertWindowMessageFont() override;
    juce::Font getAlertWindowFont() override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea) override;
    void applyThemeColours();   // the menus and boxes follow the colour you pick
    void drawCallOutBoxBackground (juce::CallOutBox&, juce::Graphics&, const juce::Path&, juce::Image&) override;
    int getCallOutBoxBorderSize (const juce::CallOutBox&) override { return 22; }
    float getCallOutBoxCornerSize (const juce::CallOutBox&) override { return 12.0f; }
    // text boxes draw their own box only when given a colour (the big prompt box is see-through)
    void fillTextEditorBackground (juce::Graphics&, int, int, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int, int, juce::TextEditor&) override;

    const lp::ui::Theme* th = &lp::ui::theme (0);
    std::map<std::pair<const lp::ui::Theme*, int>, juce::Image> balls; // grass knob images
    float displayScale = 1.0f;
};

// Two-way switch, like "live | manual"
class Segmented : public juce::Component, public juce::SettableTooltipClient
{
public:
    Segmented (juce::StringArray names, std::function<int()> get, std::function<void (int)> set,
               const lp::ui::Theme*& theme);
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::StringArray labels;
    std::function<int()> getter;
    std::function<void (int)> setter;
    const lp::ui::Theme*& th;
};

// A row of filter chips for a choice parameter ("AUTO  TRAP  HOUSE ...")
class ChoiceChips : public juce::Component
{
public:
    ChoiceChips (juce::RangedAudioParameter& p, const lp::ui::Theme*& theme) : param (p), th (theme) {}
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> chip (int i) const;
    juce::RangedAudioParameter& param;
    const lp::ui::Theme*& th;
};

// "<  value  >" stepper for a choice parameter
class Stepper : public juce::Component
{
public:
    Stepper (juce::RangedAudioParameter& p, const lp::ui::Theme*& theme);
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::RangedAudioParameter& param;
    const lp::ui::Theme*& th;
};

// The instrument keys: eight hardware buttons with coloured LEDs
class SoundKeys : public juce::Component
{
public:
    SoundKeys (LivePlanetProcessor&, const lp::ui::Theme*& theme);
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    static float hueFor (int index); // 0 = auto, 1..7 = sounds
    static juce::String nameFor (int index);

private:
    juce::Rectangle<float> keyBounds (int index) const;
    LivePlanetProcessor& proc;
    const lp::ui::Theme*& th;
};

// Round glass play / stop button on the screen (plays without the DAW running)
class PlayButton : public juce::Component
{
public:
    explicit PlayButton (LivePlanetProcessor& p) : proc (p) {}
    std::function<bool()> songMode; // in the song tab it plays the song preview
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override { toggle(); }
    void toggle(); // play / stop (the button, or the space bar)
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

private:
    LivePlanetProcessor& proc;
};

// "<  rainy window  >": step through presets, or click the name for the full list
class PresetBrowser : public juce::Component
{
public:
    PresetBrowser (LivePlanetProcessor& p, const lp::ui::Theme*& theme) : proc (p), th (theme) {}
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    LivePlanetProcessor& proc;
    const lp::ui::Theme*& th;
};

// Your sample's waveform: drag the edges to pick the part that plays, arrows to tune it
class SampleView : public juce::Component
{
public:
    explicit SampleView (LivePlanetProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> waveArea() const;
    juce::Rectangle<float> tuneDown() const, tuneUp() const;
    void setParam (const char* id, float value);
    LivePlanetProcessor& proc;
    int dragging = -1;        // 0 start edge, 1 end edge, 2 the whole region
    float grabOffset = 0.0f;
};

// Drag this into your DAW to get the notes Live Planet just played as a MIDI clip
class DragNotes : public juce::Component
{
public:
    explicit DragNotes (LivePlanetProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override { started = false; }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    juce::String message;

private:
    LivePlanetProcessor& proc;
    bool started = false;
};

// Song mode, right panel: drop a reference song, pick options, write a song
// A small "?" next to something that might be confusing: click it for a short explanation in a bubble.
class HelpDot : public juce::Component, public juce::SettableTooltipClient
{
public:
    HelpDot (juce::String title, juce::String text) : heading (std::move (title)), body (std::move (text)) { setSize (16, 16); }
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    std::function<void()> onClick;  // instead of the bubble (the top bar's "?" replays the tour)
    juce::String heading, body;
};

// The first-run tour: the screen dims, one part at a time is lit up with a short explanation, Next / Back.
class Tour : public juce::Component
{
public:
    struct Step { int page; std::function<juce::Rectangle<int>()> spot; juce::String title, text; };
    Tour (std::vector<Step> s, std::function<void (int)> showPage, std::function<void()> done);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override {} // the tour comes first: clicks don't reach the plugin
    bool keyPressed (const juce::KeyPress&) override;
    void start() { go (0); }

private:
    void go (int i);
    juce::Rectangle<int> card() const;
    std::vector<Step> steps;
    std::function<void (int)> setPage;
    std::function<void()> onDone;
    int at = 0;
    juce::Rectangle<int> spotNow;
    juce::TextButton back { "back" }, next { "next" };
};

// the license window: the price, the trial, a box for the code, Activate / Buy (a card over the plugin)
class LicensePanel : public juce::Component
{
public:
    LicensePanel (std::function<void()> closed, const juce::String& note = {});
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void focusCode() { code.grabKeyboardFocus(); }

private:
    void activate();
    void refreshState();
    juce::Rectangle<int> card() const;
    std::function<void()> onClosed;
    juce::String note, message;
    bool messageBad = false, licensed = false, busy = false;
    juce::TextEditor code;
    juce::TextButton activateBtn { "Activate" }, buyBtn { "Buy" }, closeBtn { "Close" };
    juce::Rectangle<int> closeX, lostLink, deactivateLink;
    void deactivate();
};

// A vocal track's chain (EQ, compress, saturate, de-ess, air & width, delay, reverb) with Analyze (VocalPanel.cpp).
// It plays live: every knob is heard at once; each module has its own live picture.
class VocalPanel : public juce::Component, private juce::Timer, private juce::AsyncUpdater
{
public:
    VocalPanel (LivePlanetProcessor&, int track, std::function<void()> closed);
    ~VocalPanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    struct Knob { juce::Slider s; int module = 0; float* value = nullptr; };
    void timerCallback() override;
    void handleAsyncUpdate() override;
    void takeAnalysis();
    void buildKnobs();
    void layoutDetail();
    void changed();
    void save();
    void readScope();
    void paintVisual (juce::Graphics&, juce::Rectangle<float>);
    void paintStatic (juce::Graphics&);
    juce::Image bg;
    bool bgDirty = true, hoverClose = false;
    int hoverRow = -1;
    juce::Rectangle<int> card() const, rowArea (int) const, detailArea() const;
    juce::Rectangle<float> visualArea() const;
    LivePlanetProcessor& proc;
    int track = 0, picked = 0, seenSerial = -1, dragPoint = -1;
    std::function<void()> onClosed;
    lp::song::VocalChain chain;
    std::vector<std::unique_ptr<Knob>> knobs;
    juce::TextButton analyzeBtn { "Analyze" }, powerBtn { "Chain: on" }, playBtn { "Play" }, soloBtn { "Solo vocal" }, pingBtn { "Ping-pong" }, noteBtns[3];
    juce::Rectangle<int> closeX;
    bool dirty = false, analyzing = false;
    juce::uint32 changedAt = 0;
    // what the chain sends out: the spectrum (smoothed), the stereo picture, the meters
    std::vector<float> spectrum, scopeL, scopeR;
    float compGr = 0.0f, deessGr = 0.0f, outDb = -90.0f;
    int lastScope = 0;
    bool livePicture = false;
};

// A vocal chops track's pads: its recording with the chops on it, 16 pads you play with the mouse, your keys or
// a MIDI controller, and recording what you play into the song (ChopPanel.cpp)
class ChopPanel : public juce::Component, private juce::Timer
{
public:
    ChopPanel (LivePlanetProcessor&, int track, std::function<void()> closed);
    ~ChopPanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;

private:
    void timerCallback() override;
    void load();
    void cutPads();
    void commitChops();
    void finishTake();
    double songBeat() const;
    float xOf (double sec) const;
    double secOf (float x) const;
    int chopAt (float x, int& edge) const;
    juce::Rectangle<int> card() const;
    juce::Rectangle<float> waveArea() const, padArea() const, padRect (int) const;
    LivePlanetProcessor& proc;
    int track = 0;
    std::function<void()> onClosed;
    SongJob::ChopView view;
    juce::AudioBuffer<float> audio;          // the vocal, at the pads' rate
    std::vector<float> peaks;
    int peaksWidth = -1;
    std::vector<lp::song::ChopSlice> chops;
    std::vector<int> origin;                 // each chop's place in the song's list (-1: new)
    bool chopsDirty = false;
    juce::uint32 changedAt = 0, lastPaint = 0;
    int selected = -1, dragChop = -1, dragEdge = 0, seenVersion = -1;
    double dragStartSec = 0.0;
    lp::song::ChopSlice dragOrig;
    std::array<juce::uint32, 16> flashAt {};
    std::set<int> heldKeys;
    bool recording = false, quantize = true, wasPlaying = false, mutedForTake = false;
    double takeFrom = -1.0, takeTo = -1.0;
    std::vector<LivePlanetProcessor::PadHit> take;
    juce::String message;
    juce::TextButton playBtn { "Play" }, recBtn { "Record" }, quantBtn { "Snap: 1/16" }, autoBtn { "Find chops again" };
    juce::Rectangle<int> closeX;
};

// A drum track's kit on pads you play with the mouse, your keys or a MIDI controller, and record (DrumPadPanel.cpp)
class DrumPadPanel : public juce::Component, private juce::Timer
{
public:
    DrumPadPanel (LivePlanetProcessor&, int track, std::function<void()> closed);
    ~DrumPadPanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;

private:
    void timerCallback() override;
    void finishTake();
    juce::Rectangle<int> card() const;
    juce::Rectangle<float> padRect (int) const;
    LivePlanetProcessor& proc;
    int track = 0;
    std::function<void()> onClosed;
    juce::String title, message;
    std::map<int, juce::uint32> flashAt;
    std::set<int> heldKeys;
    bool recording = false, quantize = true, wasPlaying = false;
    std::vector<LivePlanetProcessor::PadHit> take;
    juce::TextButton playBtn { "Play" }, recBtn { "Record" }, quantBtn { "Snap: 1/16" };
    juce::Rectangle<int> closeX;
};

// A tuned vocal's Auto-tune window (TunePanel.cpp)
class TunePanel : public juce::Component, private juce::Timer
{
public:
    TunePanel (LivePlanetProcessor&, int track, std::function<void()> closed);
    ~TunePanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void timerCallback() override;
    void syncControls();
    void recompute();
    void changed();
    double nowSec() const;
    lp::song::tune::Settings settings() const;
    juce::Rectangle<int> card() const;
    juce::Rectangle<float> meterArea() const, graphArea() const, keysArea() const;
    LivePlanetProcessor& proc;
    int track = 0, seenVersion = -1, lowNote = 52, highNote = 72;
    std::function<void()> onClosed;
    lp::song::VocalTune tune;
    juce::String name;
    std::vector<double> pitch, target;
    std::vector<float> corr;
    std::shared_ptr<std::atomic<bool>> aliveFlag = std::make_shared<std::atomic<bool>> (true);
    bool dirty = false, busy = false;
    juce::uint32 changedAt = 0;
    juce::TextButton naturalBtn { "Natural" }, extremeBtn { "Extreme" }, playBtn { "Play" };
    juce::ComboBox keyBox, scaleBox;
    juce::Slider speedKnob, humanKnob, amountKnob;
    juce::Rectangle<int> closeX;
};

// DAWs that can't import markers (FL Studio, Bitwig): the song's sections and their bars, to add by hand
class SectionsPanel : public juce::Component
{
public:
    SectionsPanel (std::vector<std::pair<juce::String, int>> sections, juce::String daw, std::function<void()> closed);
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    juce::Rectangle<int> card() const;
    std::vector<std::pair<juce::String, int>> secs;
    juce::String host;
    std::function<void()> onClosed;
};

// your songs on disk: how much room they take, and clearing out old versions of tracks
class StoragePanel : public juce::Component
{
public:
    StoragePanel (juce::File currentSong, std::function<void()> closed);
    ~StoragePanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    struct Usage { juce::int64 total = 0, removable = 0; int songs = 0; bool done = false; };

private:
    void measure();
    juce::Rectangle<int> card() const;
    juce::File current;
    std::function<void()> onClosed;
    Usage usage;
    juce::String message;
    bool busy = false;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
    juce::TextButton cleanBtn { "Remove old versions" }, openBtn { "Open songs folder" };
    juce::Rectangle<int> closeX;
};

class SongPanel : public juce::Component
{
public:
    SongPanel (LivePlanetProcessor& p, const lp::ui::Theme*& theme);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    juce::Rectangle<int> dropZone() const { return hero.withTrimmedBottom (96); }
    void refresh(); // update the button from the message thread (never while drawing)
    juce::TextEditor promptBox;     // "describe your song"
    juce::String autoDescription, describedRef; // what the box was filled with from your reference

private:
    LivePlanetProcessor& proc;
    const lp::ui::Theme*& th;
    juce::ToggleButton newKey { "new key" }, useSamples { "my samples" }, playWith { "play it with my song" }, samplesOnly { "samples only" }, dawTempo { "daw tempo" };
    Segmented length;
    Segmented soundsFrom; // where the sounds come from: your sample folders only, yours + built-in, built-in only
    std::unique_ptr<ChoiceChips> style;
    juce::ComboBox keyBox, modeBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> keyAtt, modeAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> useSamplesAtt, playWithAtt, samplesOnlyAtt, dawTempoAtt;
    juce::TextButton write { "write a song" };
    bool full = true, takes3 = false; // full song / sketch / three takes of a full song
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<float> scanLink, folderLink, sourcesLink;
    juce::Rectangle<float> licenseLink;
    void showActivate (const juce::String& message);
    std::unique_ptr<LicensePanel> licensePanel;
    std::unique_ptr<StoragePanel> storagePanel;
    juce::Rectangle<float> songsLink;
    juce::String songsSize;                 // how much room your songs take (measured now and then)
    juce::uint32 songsSizeAt = 0;
public:
    std::function<void()> onShowSources;
    juce::Rectangle<int> heroArea() const { return hero; }
    juce::Rectangle<int> settingsArea() const { return settings; }
private:
    HelpDot hTempo { "Follow my DAW's tempo", "On: the song is written at your project's tempo, so everything lines up when you drag it in. Off: it uses the tempo of the song you dropped, or the one you describe." },
            hSounds { "Sounds", "My samples: only loops and sounds from your own sample folders. Mine + built-in: your samples, filled out with the plugin's own instruments where your library has nothing that fits. Built-in: only the plugin's own instruments." },
            hPlay { "Play along with my DAW", "On: press play in your DAW and the plugin plays the song in time with it, from where your playhead is. Turn it off once you've dragged the tracks into your project, so you don't hear them twice." },
            hNewKey { "New key", "Off: the song stays in your reference's key (or the key you pick above). On: it moves to a different key, so it feels less like the song you dropped." },
            hLength { "Full song, sketch or 3 takes", "Full song: a whole arrangement, intro to outro. Sketch: a short idea, quick to write. 3 takes: three versions of the song to compare, and you can take a track from one into another." };
    juce::Rectangle<int> hero, settings;
};

// The Studio's arrangement: which bars are in view (zoomed in, scrolled along), shared by the sections ruler and the lanes
struct ArrView
{
    double start = 0.0; // the first bar shown
    double bars = 0.0;  // how many bars are shown (0: the whole song)
    std::function<void()> changed;
    double shown (int total) const { return bars <= 0.0 ? (double) juce::jmax (1, total) : juce::jmin (bars, (double) juce::jmax (1, total)); }
    double first (int total) const { return juce::jlimit (0.0, juce::jmax (0.0, total - shown (total)), start); }
    float xOf (double bar, float lx, float lw, int total) const { return lx + lw * (float) ((bar - first (total)) / shown (total)); }
    double barAt (float x, float lx, float lw, int total) const { return first (total) + (double) ((x - lx) / juce::jmax (1.0f, lw)) * shown (total); }
    bool zoomed (int total) const { return shown (total) < total - 1e-6; }
    // closer in (factor < 1) or further out, keeping the bar under the mouse where it is
    void zoom (double factor, double anchorBar, int total)
    {
        const double s = shown (total), f = first (total);
        const double ns = juce::jlimit (juce::jmin (2.0, (double) total), (double) juce::jmax (1, total), s * factor);
        const double t = (anchorBar - f) / s;
        bars = ns >= total - 1e-6 ? 0.0 : ns;
        start = juce::jlimit (0.0, juce::jmax (0.0, total - ns), anchorBar - t * ns);
        if (changed) changed();
    }
    void pan (double dBars, int total)
    {
        start = juce::jlimit (0.0, juce::jmax (0.0, total - shown (total)), first (total) + dBars);
        if (changed) changed();
    }
    void fit() { start = 0.0; bars = 0.0; if (changed) changed(); }
    // a wheel or trackpad over the timeline: cmd/ctrl zooms, sideways (or shift) scrolls along. true: it was used
    bool wheel (const juce::MouseEvent& e, const juce::MouseWheelDetails& w, float lx, float lw, int total)
    {
        if (e.mods.isCommandDown() || e.mods.isCtrlDown())
        {
            zoom (std::exp (-(double) w.deltaY * (w.isSmooth ? 1.6 : 2.4)), barAt (e.position.x, lx, lw, total), total);
            return true;
        }
        const double dx = e.mods.isShiftDown() && std::abs (w.deltaX) < 1e-4 ? w.deltaY : -w.deltaX;
        if (std::abs (dx) > 1e-4 && (e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY)))
        {
            if (zoomed (total)) pan (-dx * shown (total) * 0.6, total);
            return true;
        }
        return false;
    }
};

// Song mode, on the screen: the song's sections as a timeline, and progress
// The song's shape: its sections as blocks. Edit the structure here: click a section to pick it,
// drag it to move it, drag its right edge to make it longer or shorter; add, duplicate or remove
// sections; then "rebuild song" records it in its new shape.
class SongView : public juce::Component
{
public:
    explicit SongView (LivePlanetProcessor& p);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void refresh(); // every frame
    // the Studio: a slim ruler over the track lanes (each section full height, lined up with them)
    void setRuler (bool r) { if (ruler != r) { ruler = r; resized(); repaint(); } }
    // "+ AI layer" on the sections picked (the voice helper switched on): first section, how many
    std::function<void (int, int)> onLayer;
    bool layerAvailable = false;
    ArrView* view = nullptr; // the Studio: the bars in view (shared with the lanes below)
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float) override;
    void requestLayer() { layerBtn.onClick(); }   // over the sections picked (none: the whole song)
    bool layerReady() const { return ! dirty && ! waiting && ! draft.empty(); }
    int pickedCount() const { return (int) std::max<size_t> (picked.size(), selected >= 0 ? 1u : 0u); }

private:
    bool ruler = false;
    std::set<int> picked; // the sections picked (shift-click for more): remove takes them all out
    void pickOnly (int i) { picked.clear(); if (i >= 0) picked.insert (i); }
    juce::Rectangle<float> lane() const;
    juce::Rectangle<float> blockRect (int i) const;
    int totalDraftBars() const;
    juce::String shownName (int i) const;
    void showAddMenu();
    LivePlanetProcessor& proc;
    std::vector<lp::song::arrange::Item> draft; // the structure you're shaping
    bool dirty = false, waiting = false;
    juce::uint32 lastShapeEdit = 0;
    int version = -1, selected = -1, dragIndex = -1, dragOrigBars = 0;
    enum class Drag { None, Move, Resize } drag = Drag::None;
    float dragStartX = 0.0f, pxPerBarAtStart = 1.0f;
    juce::TextButton addBtn { "+ section" }, dupBtn { "duplicate" }, removeBtn { "remove" }, undoBtn { "undo" }, rebuildBtn { "rebuild song" },
                     layerBtn { "+ AI layer" };
};

// The studio: every track of the song, where its sound came from, and its mixer and remix tools
class StudioView : public juce::Component, public juce::TooltipClient
{
public:
    explicit StudioView (LivePlanetProcessor& p) : proc (p) { for (auto* h : { &hFeel, &hTracks }) addAndMakeVisible (*h); }
    void resized() override;
    HelpDot hFeel { "Feel", "Shape the whole song at once (double-click any of them to undo it). Pump: everything ducks under the kick and swells back. "
                            "Bright: brighter and airier, or darker and warmer. Sparse: the quieter sections (intro, verses, bridge) thin out to their core, so the choruses hit harder. "
                            "Punch: drums and bass forward. Auto mix: the bass dips under each kick, chords and melodies stay out of the low end and make room for a voice." },
            hTracks { "Your tracks", "Click a track (its name or its lane) to open it below. Right-click it to swap, reverse, re-pitch or remove it. M mutes, S solos, the padlock locks a track you love: 'write the rest again' then gives every unlocked track a new take. "
                                     "The bar by each volume shows what it's playing; each lane shows its sound in every section." };
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hoverRow = -1; hoverAdd = false; repaint(); }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    ArrView* view = nullptr;               // the bars in view (zoom, scroll along), shared with the sections ruler
    int rowStep = 34;                      // how tall each track's lane is (+ the gap)
    std::function<void (int)> onRowStep;   // (remembered)
    std::function<void()> onMixChanged;
    std::function<void (int)> onOpenTrack; // clicking a track's name opens it in the editor
    std::function<int()> selectedTrack;    // the track open below (lit in the list)
    std::function<juce::String()> statusText; // what the AI is making for the song right now
    struct Ghost { double start = 0, len = 0; juce::String label; }; // (song seconds) a track still being made
    std::function<std::vector<Ghost>()> ghosts;
    bool hadGhost = false;
    bool mixDirty = false;
    // where the lanes are (the sections ruler above lines up with them)
    float laneLeft() const { return 310.0f; }
    float laneRight() const { return (float) getWidth() - 14.0f; }
    juce::Rectangle<float> scrollBar() const;

private:
    enum Col { None, Mute, Solo, Lock, Volume, Pan, Tone, Swap, Reverse, PitchDown, PitchUp, Remove };
    static constexpr float rowsTop = 30.0f;  // (the bar numbers above)
    struct ClipHit { int track, section; juce::Rectangle<float> r; };
    std::vector<ClipHit> clipHits;           // where each clip was drawn (for clicks)
    std::set<std::pair<int, int>> pickedClips; // (track, section): Delete silences them
    bool trackPicked = false;                // a track's name was clicked: Delete removes the track
    juce::Rectangle<float> zoomBtn (int i) const;   // 0 out, 1 in, 2 the whole song
    juce::Rectangle<float> heightBtn (int i) const; // lane height: small, medium, large
    int sectionAt (float x) const;
    void deletePicked();
    double wheelZoomRows = 0.0;
    double cuedBar = -1.0;
    // a clip being dragged to another section of its track (option: a copy)
    int dragClipTrack = -1, dragClipSection = -1, dropSection = -1;
    bool clipDragging = false, dragCopy = false;                   // clicked in the bar numbers: where play starts
    juce::Rectangle<float> rewriteBounds (int numTracks) const;
    bool hoverRewrite = false;
    void askRemove (int track);
    juce::Point<float> lastMouse;
    bool hoverRemove = false;
    juce::Rectangle<float> rowBounds (int row) const;
    juce::Rectangle<float> colBounds (int row, Col c) const;
    juce::Rectangle<float> addBounds (int numTracks) const;
    void showAddMenu();
    bool hoverAdd = false;
    int scroll = 0;                 // first track row shown
    double wheelRows = 0.0;         // scrolling, a bit at a time
    int visibleRows() const;
    bool rowShown (int row) const { return row >= scroll && row < scroll + visibleRows(); }
    Col colAt (int row, juce::Point<float> p) const;
    void setFromX (int row, Col c, float x);
    std::unique_ptr<juce::FileChooser> chooser;
    LivePlanetProcessor& proc;
    int hoverRow = -1, dragRow = -1;
    std::array<lp::ui::MeterBallistics, StudioPlayer::maxTracks> meterL {}, meterR {};
    juce::Rectangle<float> pumpBounds() const;
    bool draggingPump = false;
    // each track's stem as a loudness line, read in the background
    struct Env { std::vector<float> peaks; double rate = 0.0; juce::int64 length = 0; };
    struct Envs { std::mutex lock; std::map<juce::String, Env> done; std::set<juce::String> pending; };
    std::shared_ptr<Envs> envs = std::make_shared<Envs>();
    const Env* envelopeOf (const juce::File&);
    // the song's feel, along the bottom: pump, bright, sparse, punch, and the auto mix switch
    juce::Rectangle<float> feelBar (int i) const;     // 0 pump, 1 bright, 2 sparse, 3 punch
    juce::Rectangle<float> autoMixBounds() const;
    float feelValue (int i) const;
    void setFeel (int i, float x);
    int draggingFeel = -1;
public:
    juce::String getTooltip() override;
private:
    Col dragCol = None;
};

// EDIT: one track at a time, like a sampler: its sample (choose which bars play), its own effects and remix tools
class FilePill : public juce::Component, public juce::SettableTooltipClient
{
public:
    FilePill (juce::String text, std::function<juce::StringArray()> files) : label (std::move (text)), getFiles (std::move (files)) {}
    // optional: the files actually handed over when dragged (e.g. recorded with effects), if not getFiles
    std::function<juce::StringArray()> filesToDrag;
    std::function<void()> onClick; // optional: a click does this (instead of showing the file in Finder)
    void setLabel (const juce::String& t) { if (label != t) { label = t; repaint(); } }
    void paint (juce::Graphics&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

private:
    juce::String label;
    std::function<juce::StringArray()> getFiles;
    bool dragged = false;
};

// EDIT > Drums: draw your own drum pattern on a grid (add, erase, paint, how hard each hit is)
// EDIT > Drums: the drum pattern on a timeline you can scroll and zoom, like a DAW's clip editor.
// Draw hits (click / drag), erase them, set how hard they play; the overview shows the whole song.
class DrumGrid : public juce::Component
{
public:
    DrumGrid (LivePlanetProcessor& p, const lp::ui::Theme*& theme);
    void load (int track);   // the track's pattern (keeps what you're drawing while it re-records)
    void refresh();          // every frame: sends your drawing once you pause
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;
    void mouseMagnify (const juce::MouseEvent&, float scale) override;
    void stopLoop();          // leaving the drum editor
    void forgetKit() { kitKey.clear(); } // another editor used the live player: load the kit again next time
    std::function<void()> onToggleBig; // the "bigger" button: the grid takes the whole editor
    void setBig (bool b) { bigBtn.setToggleState (b, juce::dontSendNotification); }
    // click a drum's name: its own effects on the knobs below (-1: the whole drum track)
    std::function<void (int note)> onPickVoice;
    void setPickedVoice (int note) { if (picked != note) { picked = note; repaint(); } }
    int pickedVoice() const { return picked; }

private:
    // the timeline
    double totalBeats() const { return totalBars * 4.0; }
    juce::Rectangle<float> overviewArea() const;
    juce::Rectangle<float> rulerArea() const;
    juce::Rectangle<float> sectionLane() const;
    juce::uint32 lastViewChange = 0;
    juce::Rectangle<float> gridArea() const;
    float xOf (double beat) const;
    double beatAt (float x) const;
    void setView (double start, double len);
    void zoomAround (double factor, float x);
    void loopRange (double& a, double& b) const; // the bars that play / get copied / cleared

    void loadKit();
    int picked = -1;          // the drum whose effects the knobs show (-1: all of them)
    std::set<int> withFx;     // drums with their own effects (marked with a dot)
    juce::Rectangle<float> swapArrow (size_t row, int which) const;
    bool canSwapRows = false;
    int auditionAfterSwap = -1, kitReloadsAtSwap = 0, seenSwaps = 0;
    std::atomic<int> kitReloads { 0 };
    juce::String swapMessage;
    juce::uint32 swapMessageUntil = 0;
    void sendLoop();          // what the loop plays right now (your drawing, live)
    juce::String kitKey;
    bool cellAt (juce::Point<float> p, int& row, long& step) const;
    std::vector<size_t> hitsIn (int row, long step) const;
    void setCell (int row, long step, bool on);
    void copyAcross (bool everySectionOfKind);
    void pushUndo();
    void changed();

    LivePlanetProcessor& proc;
    const lp::ui::Theme*& th;
    int track = -1, totalBars = 1, stepsPerBeat = 4, paintRow = -1;
    double viewStart = 0.0, viewLen = 16.0;      // in beats
    double selA = -1.0, selB = -1.0;             // bars you picked on the ruler (beats; -1 = none)
    enum class Drag { None, Paint, Overview, Ruler } drag = Drag::None;
    double rulerFrom = 0.0, overviewGrab = 0.0;
    bool painting = true, dirty = false, waitingForRender = false;
    juce::uint32 lastChange = 0;
    juce::String songTitle;
    std::vector<lp::song::Section> sections;
    std::vector<lp::song::Note> notes;
    std::vector<std::vector<lp::song::Note>> undo;
    std::vector<int> rows; // drum notes, top to bottom
    // playing the kit from your keys or a MIDI controller (and recording it into the loop)
    juce::TextButton recBtn { "rec" };
    bool recording = false;
    std::set<int> heldKeys;
    std::map<int, juce::uint32> rowFlash;
    void takeHits();
    juce::TextButton playBtn { "play" }, zoomOut { "-" }, zoomIn { "+" }, fitBtn { "whole song" }, resBtn { "1/16" },
                     fillSection { "copy to section" }, fillAll { "every chorus" }, clearBtn { "clear" }, undoBtn { "undo" },
                     bigBtn { "bigger" }, feelBtn { "feel 100%" };
    lp::song::Groove groove; // the song's drum feel (heard in the live loop too)
    void updateFeelText();
};

// EDIT > a played part (an 808, a hook, a bass line): its notes on a piano roll, to move, stretch, add and erase
class NoteGrid : public juce::Component
{
public:
    NoteGrid (LivePlanetProcessor& p, const lp::ui::Theme*& theme);
    void load (int track);
    void forgetSound() { soundKey.clear(); }
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float scale) override;
    bool keyPressed (const juce::KeyPress&) override;
    void stopLoop();
    std::function<void()> onToggleBig;
    void setBig (bool b) { bigBtn.setButtonText (b ? "smaller" : "bigger"); }

private:
    double totalBeats() const { return totalBars * 4.0; }
    juce::Rectangle<float> overviewArea() const;
    juce::Rectangle<float> sectionLane() const;
    juce::Rectangle<float> rulerArea() const;
    juce::Rectangle<float> gridArea() const;
    float xOf (double beat) const;
    double beatAt (float x) const;
    float yOf (int pitch) const;       // the top of a note's row
    int pitchAt (float y) const;
    float rowH() const { return juce::jlimit (8.0f, 16.0f, gridArea().getHeight() / 15.0f); } // at least 15 notes in view
    int rowsShown() const;
    void setView (double start, double len);
    void zoomAround (double factor, float x);
    void setPitchView (int low);
    void loopRange (double& a, double& b) const;
    double snap (double beat) const;
    double gridLen() const { return 1.0 / stepsPerBeat; }
    bool inKey (int pitch) const;
    int toKey (int pitch, int dir) const;  // the nearest note of the key (dir: which way to look first)
    int noteAt (juce::Point<float> p, bool& onEdge) const;
    void loadSound();
    void sendLoop();
    void pushUndo();
    void changed();
    void copyAcross (bool everySectionOfKind);
    void hear (int pitch, double beats);

    LivePlanetProcessor& proc;
    const lp::ui::Theme*& th;
    int track = -1, totalBars = 1, stepsPerBeat = 4, key = 0, lowPitch = 36;
    bool minor = false, bass = false, keySnap = true;
    double viewStart = 0.0, viewLen = 16.0, selA = -1.0, selB = -1.0;
    enum class Drag { None, Move, Resize, Draw, Box, Overview, Ruler } drag = Drag::None;
    juce::Point<float> downAt, boxTo;
    double grabBeat = 0.0, rulerFrom = 0.0, overviewGrab = 0.0, lastLen = 0.5;
    int grabPitch = 0, drawn = -1, lastHeard = -1;
    double wheelRows = 0.0;   // scrolling up and down, a bit at a time
    std::vector<lp::song::Note> notes, before;  // before: the notes as they were when a drag started
    std::set<size_t> picked;
    bool dirty = false, waitingForRender = false, moved = false;
    juce::uint32 lastChange = 0, lastViewChange = 0;
    juce::String songTitle, partName, soundKey;
    std::vector<lp::song::Section> sections;
    std::vector<std::vector<lp::song::Note>> undo;
    juce::TextButton playBtn { "play" }, zoomOut { "-" }, zoomIn { "+" }, fitBtn { "whole song" }, resBtn { "1/16" },
                     keyBtn { "in key" }, fillSection { "copy to section" }, fillAll { "every chorus" }, clearBtn { "clear" },
                     undoBtn { "undo" }, bigBtn { "bigger" };
};

// VOICE (experimental): lyrics and a style, sung over a part of your song by the local voice helper,
// with every stage shown live and the versions to hear and drag out (VoicePage.cpp)
namespace lp::ai
{
juce::String helperKeyName (const lp::song::Song&); // the key the voice helper is told (the minor key with the song's notes)
// the voice helper's layers and drawn melodies, added to the song by themselves once they're made
struct AutoAdd
{
    void tick (LivePlanetProcessor&);
    juce::String status; // what's being made right now (or what went wrong)
    struct Pending { double start = 0, len = 0; juce::String label; }; // (song seconds) a layer or melody still being made
    std::vector<Pending> pending;
    // a drawn melody's vibrato and slides (by the token in its tag), put on the sung result when it arrives
    std::map<juce::String, std::vector<lp::song::stack::Bend>> bends;

private:
    std::set<juce::String> handled, asked;
};
// "+ AI layer": an instrument the AI plays over the sections you picked
class LayerPopup : public juce::Component
{
public:
    LayerPopup (LivePlanetProcessor&, const lp::ui::Theme*& theme, int firstSection, int sectionCount, std::function<void()> closed);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override { if (! cardBounds().contains (e.getPosition()) && onClosed) onClosed(); }
    void setBackdrop (juce::Image img); // what's behind it (blurred, seen through the glass)
private:
    juce::Image backdrop;
    juce::Rectangle<int> cardBounds() const;
    void pickKind (juce::TextButton*);
    void start();
    LivePlanetProcessor& proc;
    const lp::ui::Theme*& th;
    int firstSection, sectionCount;
    std::function<void()> onClosed;
    juce::OwnedArray<juce::TextButton> chips;
    juce::String kind, message;
    int loopBars = 0; // 0: a part played through; else a loop this many bars long, repeated
    juce::TextButton asPart { "Instrumental" }, asLoop { "Loop" }, loop1 { "1 bar" }, loop2 { "2 bars" }, loop4 { "4 bars" };
    // flip a sample from your library (chopped into something new) or have the AI play it
    bool flipMode = true;
    int flipAmount = 2;
    juce::TextButton modeFlip { "Flip a sample" }, modeAi { "AI generate" }, amtLight { "Light flip" }, amtMed { "Medium" }, amtHeavy { "Heavy" };
    void showType();
    juce::TextEditor describe;
    juce::TextButton go { "Generate" }, cancel { "Cancel" };
};
}

// VOCALS, "Draw & sing": a piano roll over a section; notes with words, sung in your voice by the voice helper
class DrawSing : public juce::Component
{
public:
    DrawSing (LivePlanetProcessor&, const lp::ui::Theme*& theme);
    ~DrawSing() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void refresh(); // every frame while showing
    std::function<juce::String()> statusText; // what the helper is doing with it
    lp::ai::AutoAdd* autoAdd = nullptr;        // where a melody's vibrato and slides are left for its result
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float scale) override; // (pinch on a trackpad: zoom)

private:
    using Note = lp::song::lyricmelody::Note;
    int top = 79;                              // the highest note shown (32 notes at a time; scroll for more)
    juce::TextButton octUp { "oct +" }, octDown { "oct -" }, holdBtn { "hold word" }, vibBtn { "vibrato" }, slideBtn { "slide in" };
    void toggle (int what);                    // 0 hold, 1 vibrato, 2 slide, on the selected note
    void showTools();
    LivePlanetProcessor& proc;
    const lp::ui::Theme*& th;
    std::vector<Note> notes; // beats from the section's start
    int page = 0, selectedNote = -1, editing = -1, songVersion = -1;
    float wheel = 0.0f;
    // sing as I draw: a moment after the notes stop changing they're sung (a take still singing an older version is stopped)
    juce::ToggleButton autoSing { "sing as I draw" };
    juce::String noteKey() const, shownKey, sentKey, lastToken;
    juce::uint32 changedAt = 0;
    // the view: how many beats across (zoom), from which beat
    double beatsShown = 16.0, viewStart = 0.0;
    juce::TextButton zoomIn { "+" }, zoomOut { "-" };
    juce::ToggleButton guide { "guide tone with the song" };
    juce::TextButton addVoiceBtn { "add" };
    std::unique_ptr<juce::FileChooser> voiceChooser;
    double snap() const { return beatsShown <= 8.0 ? 0.125 : 0.25; }
    void zoom (double factor, float aroundX);
    juce::String guideKey;
    // your voice, live: the last sung take and the notes it was sung on; moved notes are re-pitched and re-timed from it
    struct Voiced;
    std::shared_ptr<Voiced> voiced;
    std::map<juce::String, std::vector<Note>> sentNotes; // by token: the notes each take was sung from
    juce::String loadedToken, voicedKey;
    int nextId = 1;
    juce::uint32 voicedChangedAt = 0;
    void loadTake (const juce::String& token, const juce::File& file);
    void renderVoice();          // (in the background) the section in your voice, from the notes as they are now
    void previewNote (int index); // the note being moved, in your voice (or the tone)
    bool isVoiced (const Note&) const;
    bool needsSinging() const;   // notes the take doesn't have yet (new ones, or new words): those phrases are sung
    void bake();                 // only moved, stretched or re-pitched: the edited take goes into the track, no AI
    juce::String bakedKey;
    // undo / redo: the notes as they were before each change (a drag is one change)
    std::vector<std::vector<Note>> undoStack, redoStack;
    std::vector<Note> committed;
    juce::String committedKey;
    juce::TextButton undoBtn { "undo" }, redoBtn { "redo" };
    void undo (bool back);
public:
    void leave();                // the page is left: the song plays the sung track again
private:
    enum class Drag { None, Move, Resize } drag = Drag::None;
    double dragBeatOffset = 0.0;
    bool connected = false;
    juce::String message;
    juce::StringArray shownVoices;
    juce::ComboBox section, voice;
    juce::TextEditor lyrics, wordBox;
    juce::TextButton fromLyrics { "Start from a melody" }, fillWords { "Fill in my words" }, clearAll { "Clear" },
                     prev { "<" }, next { ">" }, hear { "play section" }, sing { "Sing it in my voice" };
    juce::ToggleButton consent { "this is my voice, or I have the singer's permission" };
    juce::Rectangle<int> statusArea;
    double sectionBeats() const, sectionStartSeconds() const;
    juce::Rectangle<float> roll() const;
    juce::Rectangle<int> rollCard() const, sideCard() const;
    float rowH() const, xOf (double beat) const, yOf (int pitch) const;
    double beatAt (float x) const;
    int pitchAt (float y) const;
    juce::Rectangle<float> noteRect (const Note&) const;
    int noteAt (juce::Point<float>) const;
    void editWord (int), commitWord(), fillFromLyrics(), writeFromLyrics(), preview (int pitch), hearIt(), singIt();
    juce::StringArray lyricWords() const;
    juce::String nextWord() const;
};

// VOCALS, "Your vocals": every vocal in the song (its chain, chop pads, auto-tune) and ways to add one
class VocalsHub : public juce::Component
{
public:
    VocalsHub (LivePlanetProcessor&, const lp::ui::Theme*& theme);
    ~VocalsHub() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh(); // every frame while showing
    std::function<void (int)> onOpenInStudio;

private:
    struct Row { int track = 0; juce::String name, source; juce::Rectangle<int> area;
                 juce::TextButton chain { "Vocal chain" }, chops { "Chop pads" }, tune { "Auto-tune" }, studio { "In Studio" }, stack { "Stack it..." }; };
    void makeStack (int track, int kind); // a double, a harmony or a choir from a vocal, as a new track
    juce::String working;                 // what's being made right now
    std::shared_ptr<std::atomic<bool>> busy = std::make_shared<std::atomic<bool>> (false);
    std::vector<int> vocalTracks() const;
    void rebuild();
    void open (int which, int track); // 0 chain, 1 chop pads, 2 auto-tune
    juce::Rectangle<int> listCard() const, addCard() const;
    LivePlanetProcessor& proc;
    const lp::ui::Theme*& th;
    std::vector<std::unique_ptr<Row>> rows;
    juce::String shownKey;
    juce::TextButton addChops { "+ Vocal chops from my samples" }, addMyChops { "+ Chops from my own vocal..." },
                     addTuned { "+ My vocal, auto-tuned..." }, addTunedHard { "+ My vocal, hard-tuned..." };
    juce::Rectangle<int> nextNote;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::Component> window;
};

class VoicePage : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int, int) override;
    VoicePage (LivePlanetProcessor&, const lp::ui::Theme*& theme);
    ~VoicePage() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void setSnapshot (const lp::voice::Snapshot&);
    void setSong (const juce::StringArray& sectionNames, int pick, const juce::String& keyText, const juce::String& bpmText);
    void refresh();                 // (every frame while it shows) the helper's news and the song's sections
    void shown (bool isShown);      // keep up with the helper only while it shows (or while it sings)

private:
    struct ResultRow : public juce::Component, public juce::SettableTooltipClient
    {
        ResultRow (LivePlanetProcessor&, const lp::ui::Theme*&);
        void set (const lp::voice::Result&, const juce::String& jobId);
        void paint (juce::Graphics&) override;
        void resized() override;
        LivePlanetProcessor& proc;
        const lp::ui::Theme*& th;
        lp::voice::Result res;
        juce::String jobId;
        juce::File vocalFile;                                 // the version, starting at bar 1 (dragged, made a track)
        std::shared_ptr<juce::AudioBuffer<float>> preview;    // ...at 48 kHz, to play with the song
        double startSec = 0.0;
        bool preparing = false;
        juce::TextButton play { "play" }, toTrack { "add as track" };
        FilePill drag;
    };
    void timerCallback() override;
    void markStyles();
    void refreshButtons();
    void startCreate();
    void prepare (ResultRow&, const juce::File& fetched);
    juce::File voiceFolder() const;
    void loadJobs();
    void saveJobs();
    juce::Rectangle<float> lyricsCard() const, styleCard() const, setupCard() const, progressCard() const, resultsArea() const;
    LivePlanetProcessor& proc;
    const lp::ui::Theme*& th;
    lp::voice::Snapshot snap;
    int seenSerial = -1, seenSongVersion = -1;
    bool demo = false;
    juce::String songKey, songBpm, songKeyName;  // songKeyName: the minor key sent to the helper ("A")
    double songBpmValue = 120.0;
    juce::File songFolder;
    struct SectionChoice { int first = 0, count = 1; double start = 0.0, secs = 0.0; juce::String name; };
    std::vector<SectionChoice> sectionInfo; // what each choice in the list is (one section, or two or three in a row)
    int lyricLines() const;                  // sung lines (tags like [Chorus] don't count)
    double chosenSeconds() const;
    double barSeconds() const;               // one bar of the song, in seconds
    std::map<juce::String, std::pair<double, juce::String>> jobStarts; // job id -> (its section's start, its name)
    std::set<juce::String> asked;   // versions asked for already
    float phase = 0.0f;
    juce::TextEditor lyrics, style;
    juce::OwnedArray<juce::TextButton> tagChips, styleChips;
    juce::ComboBox section, voice;
    juce::TextButton addVoice { "add" }, create { "Create vocal" }, stop { "Stop" };
    int takesN = 4, versionsN = 1;
    int minutesEstimate() const; // roughly how long the helper takes, from what's picked
    // who makes the melody: 0, Loopwright writes it over the song's chords (sung note for note); 1, you, from your own
    // take; 2, the AI improvises one (takes, the best kept)
    int whoSings = 0;
    Segmented who { { "Write melody", "I sing it", "AI freestyle" }, [this] { return whoSings; }, [this] (int i) { whoSings = i; clean.setToggleState (i == 1, juce::dontSendNotification); resized(); refreshButtons(); repaint(); }, th };
    juce::TextButton recordTake { "Record my take" }, useTake { "Use a recording..." }, hearTake { "hear it" };
    juce::File takeFile;          // your take, cut to the section
    double takeSecs = 0.0;
    bool takeRecording = false;
    double takeFrom = 0.0, takeStopAt = 0.0; // (song seconds: the count-in bar, and the section's end)
    int takeChoice = -1;          // the section choice it was made for
    juce::String takeMessage;
    void startTakeRecording();
    void finishTakeRecording();
    void useTakeFile (const juce::File&);
    static juce::String withVoiceWords (const juce::String& style);
    static bool rapStyle (const juce::String& style); // hip hop, rap, trap, drill, grime: a rap flow
    Segmented takes { { "2", "4", "6", "8" }, [this] { return takesN / 2 - 1; }, [this] (int i) { takesN = (i + 1) * 2; refreshButtons(); repaint(); }, th },
              versions { { "1", "2", "3" }, [this] { return versionsN - 1; }, [this] (int i) { versionsN = i + 1; refreshButtons(); repaint(); }, th };
    juce::Slider tuning, runs;
    juce::ToggleButton clean { "remove background noise" }, consent { "this is my voice, or I have the singer's permission" };
    juce::OwnedArray<ResultRow> rows;
    std::unique_ptr<juce::FileChooser> chooser;
};

class TrackEditor : public juce::Component
{
public:
    TrackEditor (LivePlanetProcessor& p, const lp::ui::Theme*& theme);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void select (int track);
    void refresh();     // message thread, every frame while showing
    int selected() const { return track; }
    void stopLive() { drumGrid.stopLoop(); }
    // docked under the arrangement (the Studio): one view at a time, 0 the sample / its notes, 1 its sound (dials)
    void setDockPage (int p) { if (dockPage != p) { dockPage = p; resized(); repaint(); } }
    int getDockPage() const { return dockPage; }
    // docked, showing a drum grid or notes made bigger: the panel wants more of the window
    bool wantsTall() const { return dockPage == 0 && (showingDrums() || notesTrack) && (notesTrack && ! showingDrums() ? noteBig : drumBig); }

private:
    // a dial is either live (the mixer: plays as you turn it) or re-records the track when you let go (grains)
    struct Dial { juce::Slider s; std::function<std::atomic<float>& (int)> value; float* local = nullptr; int row = 0; };
    Dial& addDial (const char* name, double lo, double hi, std::function<std::atomic<float>& (int)> value, int size, int row = 0);
    Dial& addGrainDial (const char* name, double lo, double hi, float* local, int size);
    SongJob::GrainSettings grains;
    float grainRateF = 2.0f;
    const std::vector<float>& peaksFor (const juce::File& f, bool wholeSong);
    juce::Rectangle<float> screen() const;
    int barAt (float x) const;

    LivePlanetProcessor& proc;
    const lp::ui::Theme*& th;
    int track = 0, version = -1;
    std::vector<std::unique_ptr<Dial>> dials;
    juce::TextButton prev { "<" }, next { ">" }, swap { "Swap sample" }, rev { "Reverse" }, pitchDown { "-" }, pitchUp { "+" },
                     mute { "M" }, solo { "S" };
    std::map<juce::String, std::vector<float>> peakCache;
    int dragFrom = -1, dragTo = -1;
    juce::TextButton printFx { "FX: dry" }, bypassFx { "Bypass" };
    HelpDot hFx { "Effects and dragging", "Bypass: hear this track without its effects; your settings stay. FX: printed means dragging the track gives it to you with its effects "
                                          "(and the song's feel) recorded in, as you hear it; click it to get the dry track instead. Drag midi gives a played part's notes." },
            hGrains { "Grains", "Re-plays the sample in tiny slices, in time with your song, for texture and movement. Grains: how much. Size: how long each slice is. "
                                "Rate: how often they play. Chaos: how random. Position: where in the sample they come from." };
    FilePill dragMidi;          // a played part's notes as MIDI
    juce::String fxKey;                 // the track's effect settings last seen (to record them once you stop turning)
    juce::uint32 fxKeyAt = 0;
    bool fxPrinted = false;
    FilePill dragTrack;
    juce::TextButton love { "Love it" }, notThis { "Not this" }; // your taste: remembered for every later song
    DrumGrid drumGrid;
    NoteGrid noteGrid;
    bool notesTrack = false;
    bool noteBig = true;      // the note editor opens big (notes need the room); "bigger" toggles it
    juce::Rectangle<int> toolSlot (int i) const;
    bool bigNow() const { return dockPage < 0 && (notesTrack && ! showingDrums() ? noteBig : drumBig); }
    int dockPage = -1; // -1: the whole editor at once (its own page)  // a played part (808, hook, bass line): its notes can be edited
    juce::TextButton sliceBtn { "Slice into hits" }; // a drum loop -> a drum track you can edit
    juce::TextButton vocalBtn { "Vocal chain" };     // a vocal track: its chain, in its own window
    std::unique_ptr<VocalPanel> vocalPanel;
    juce::TextButton chopBtn { "Chop pads" };      // vocal chops: its pads, in their own window
    std::unique_ptr<ChopPanel> chopPanel;
    juce::TextButton tuneBtn { "Auto-tune" };      // a tuned vocal: its Auto-tune window
    std::unique_ptr<TunePanel> tunePanel;
    juce::TextButton drumPadsBtn { "Drum pads" };  // a drum track: its kit on pads
    std::unique_ptr<DrumPadPanel> drumPadPanel;
    bool drumBig = true; // (docked: the drum grid opens tall, the arrangement shrinking to make room)
    // one drum's own effects (picked by clicking its name in the drum editor): the mix dials show it instead
    int voice = -1;
    std::array<std::atomic<float>, 8> voiceVals {}; // volume, pan, tone, space, echo, warmth, nostalgia, chorus
    bool voiceDirty = false, voiceLiveDirty = false;
    std::shared_ptr<std::atomic<bool>> voiceRendering = std::make_shared<std::atomic<bool>> (false);
    juce::uint32 voiceChangedAt = 0;
    juce::TextButton voiceChip;
    void pickVoice (int note);
    void sendVoice();
    bool showingDrums() const;
};

// SOUND: a little screen of the sound being played, with its grains appearing as they would
// (where they come from = position, how spread = scatter, how wide = grain size, bright high ones = sparkle)
class GrainView : public juce::Component
{
public:
    explicit GrainView (LivePlanetProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;

private:
    struct G { float x, w, born, life, high; };
    LivePlanetProcessor& proc;
    std::vector<G> grains;
    std::vector<float> peaks;
    int peaksFor = -1;
    juce::Random rng;
    double last = 0;
};

// Which sample packs songs may use: Splice packs start on, WAVS packs start off (switch on the
// royalty-free ones), your own folders start on
class SourcesPanel : public juce::Component
{
public:
    SourcesPanel (LivePlanetProcessor& p, const lp::ui::Theme*& theme);
    void open();
    void resized() override;
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    juce::Rectangle<float> panel() const;
    juce::Rectangle<float> rowAt (int i) const;
    std::vector<SongJob::PackRow> shown() const;
    LivePlanetProcessor& proc;
    const lp::ui::Theme*& th;
    std::vector<SongJob::PackRow> rows;
    juce::TextEditor search;    // pack, artist or sample names
    juce::String matchIn (const SongJob::PackRow&) const; // the sample a search found in a pack ("" = its name matched)
    int filter = 2, scroll = 0; // filter: 0 all, 1 splice, 2 wavs, 3 folders
    double wheelRows = 0.0;     // scrolling, a bit at a time
};

// A pill you drag into your DAW (or click to show in Finder)

class ThemeDots : public juce::Component
{
public:
    ThemeDots (LivePlanetProcessor& p, std::function<void()> changed) : proc (p), onChange (std::move (changed)) {}
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    LivePlanetProcessor& proc;
    std::function<void()> onChange;
};

class LivePlanetEditor : public juce::Component,
                         public juce::FileDragAndDropTarget,
                         private juce::Timer
{
public:
    explicit LivePlanetEditor (LivePlanetProcessor&);
    ~LivePlanetEditor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override; // clicking anywhere else hands the keyboard back to your DAW
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void resized() override;
    // the Studio's divider between the arrangement and the open track: drag it, double-click to hide the track
    int studioSplit = 358;
    bool dockCollapsed = false, draggingSplit = false;
    int splitDragFrom = 0;
    juce::Rectangle<int> splitBounds() const;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int, int) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragging = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragging = false; repaint(); }

private:
    void timerCallback() override;
    void runSearch();
public:
    void setPage (int p);
    enum Page { Write = 0, Studio, Edit, Sound, Voice };
private:
    void applyTheme();
    juce::Rectangle<int> tabBounds (int i) const;

    struct Knob
    {
        juce::Slider slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    Knob& addKnob (const char* id, const juce::String& name, float hue, int size);

    LivePlanetProcessor& proc;
    LivePlanetLookAndFeel lnf;
    double startMs = 0;

    juce::TextEditor cityBox;
    Segmented liveSwitch, keySwitch;
    SoundKeys keys;
    PlayButton playButton;
    PresetBrowser presetBrowser;
    SampleView sampleView;
    DragNotes dragNotes;
    SongPanel songPanel;
    SongView songView;
    StudioView studioView;
public:
    void togglePlay() { playButton.toggle(); }
private:
    TrackEditor trackEditor;
    VoicePage voicePage;
    VocalsHub vocalsHub;
    lp::ai::AutoAdd autoAdd;
    std::unique_ptr<lp::ai::LayerPopup> layerPopup;
    DrawSing drawSing;
    int vocalTab = 0; // the Vocals tab: 0 your vocals, 1 draw & sing, 2 create with AI (both through the voice helper)
    Segmented vocalTabs { { "Your vocals", "Draw & sing", "Create with AI" }, [this] { return vocalTab; }, [this] (int i) { vocalTab = i; setPage (Voice); }, lnf.th };
    bool voiceOn() const; // the experimental Voice page is switched on
    juce::ComboBox studioKey, studioMode;   // the song's key, at the top of the studio
    juce::TextButton aiLayer { "AI layer" }; // the Studio: an instrument the AI plays over the sections picked
    ArrView arrView;                         // the arrangement's zoom (bars in view)
    juce::ComboBox masterBox;               // mastering: off, or a platform's loudness
    juce::TextButton masterAB { "after" };  // hear the master, or the mix before it
    juce::Label masterInfo;                 // what the master came out at
    struct MasterMeter : juce::Component
    {
        lp::ui::MeterBallistics l, r;
        void paint (juce::Graphics& g) override { lp::ui::drawStereoMeter (g, getLocalBounds().toFloat(), l, r); }
    } masterMeter;                          // everything the plugin plays
    // takes: switch between them (A/B), and mix tracks from one into another
    juce::TextButton takeBtn[3] { juce::TextButton ("Take 1"), juce::TextButton ("Take 2"), juce::TextButton ("Take 3") };
    juce::TextButton mixTakes { "mix takes" };
    void showMixTakes();
    int seenMasterResets = 0;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> studioKeyAtt, studioModeAtt;
    GrainView grainView;
    SourcesPanel sourcesPanel;
    FilePill pillMidi, pillChords, pillStems, pillMix, pillHelper, pillFolder;
    // help: the tour (the first time, and from the "?" by the logo) and the "?"s by the drag buttons
    HelpDot tourBtn { "", "" },
            hMidi { "Drag MIDI", "Every MIDI part of the song in one drag: the drums, the 808, the hook, every played part, and the chords and bass your loops play (read from them). Drop it in your DAW and each part lands on its own MIDI track, with the song's sections as markers." },
            hStems { "Drag stems or the mix", "Stems: every track as its own audio file, at bar 1, as you hear it (a track's effects are recorded in when it has any). Mix: the whole song in one file; with a master picked, it's the mastered version." },
            hLocators { "Locators", "DAWs can't take locators from a dropped file, so this is a small helper device for Ableton Live Suite. Drag it onto any track once and press 'place song locators': your sections appear as named locators, and they follow when you change sections here. 'build song tracks' brings every track into Live." };
    std::unique_ptr<Tour> tour;
    std::unique_ptr<SectionsPanel> sectionsPanel;
    void startTour();
    std::unique_ptr<Stepper> keyStepper, scaleStepper;
    ThemeDots themeDots;
    juce::TextButton loadButton { "Load sample" };
    juce::TextButton oneShotButton { "One-shot instrument" }; // a library one-shot across the keyboard (Play mode)
    juce::TextButton chopButton { "Chop my vocal" };          // your recording cut into syllables, one per key
    void chopFromFile();
    juce::String chopMessage;
    juce::uint32 chopMessageUntil = 0;
    struct InMeter : juce::Component
    {
        lp::ui::MeterBallistics l, r;
        void paint (juce::Graphics& g) override { lp::ui::drawStereoMeter (g, getLocalBounds().toFloat(), l, r); }
    } inMeter;                                                // your mic, while recording
    void showOneShotMenu();
    juce::ToggleButton dayToggle { "daylight" }, thunderToggle { "thunder" };

    std::vector<std::unique_ptr<Knob>> knobs;
    std::map<juce::String, Knob*> knobById;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> dayAtt, thunderAtt;
    std::unique_ptr<juce::FileChooser> chooser;

    // cards on each page
    juce::Rectangle<float> undoChip;   // the top bar's "undo ..." (empty when there's nothing to undo)
    juce::String undoShown;
    bool keyPressed (const juce::KeyPress&) override;
    juce::Rectangle<int> cardTimeline, cardInstrument, cardSky, cardWeather, plateMix, plateTex, plateFx;
    int page = 0;
    static int tabPage (int tab) { static const int pages[] = { Write, Studio, Voice, Sound }; return pages[juce::jlimit (0, 3, tab)]; }
    juce::Rectangle<int> cardDock; // the Studio: the open track, under the arrangement
    int dockPageShown = 0;
    bool dockTall = false; // the open track's grid made bigger: the arrangement shrinks to its sections and a few lanes
    Segmented dockTabs { { "Sample", "Sound", "Grains" }, [this] { return dockPageShown; }, [this] (int i) { dockPageShown = i; trackEditor.setDockPage (i); repaint(); }, lnf.th };
    bool dragging = false;

    juce::String cityText, headerText, nowText, statusText;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LivePlanetEditor)
};

// The plugin's window: the whole of Loopwright, drawn at any size you drag the window to (it keeps its shape)
class LivePlanetWindow : public juce::AudioProcessorEditor
{
public:
    explicit LivePlanetWindow (LivePlanetProcessor&);
    void resized() override;
    // the app on its own: the space bar plays and stops (in a DAW, the space bar stays your DAW's)
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xff0d0d10)); }

private:
    LivePlanetEditor view;
    bool ready = false;
    double lastSaved = -1.0;
    int lastW = 0, lastH = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LivePlanetWindow)
};
