#pragma once
#include <set>
#include "Meter.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"

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
    void fillTextEditorBackground (juce::Graphics&, int, int, juce::TextEditor&) override {}
    void drawTextEditorOutline (juce::Graphics&, int, int, juce::TextEditor&) override {}

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
    void mouseUp (const juce::MouseEvent&) override;
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
    void refresh(); // every frame

private:
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
    juce::TextButton addBtn { "+ section" }, dupBtn { "duplicate" }, removeBtn { "remove" }, undoBtn { "undo" }, rebuildBtn { "rebuild song" };
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
            hTracks { "Your tracks", "Click a track's name to open it in Edit. M mutes, S solos, the padlock locks a track you love: 'write the rest again' then gives every unlocked track a new take. "
                                     "Swap: another sound for the same part. Rev: backwards. - / +: pitch. x deletes the track (undo brings it back). The green bar by each volume shows what it's playing." };
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hoverRow = -1; hoverAdd = false; repaint(); }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    std::function<void()> onMixChanged;
    std::function<void (int)> onOpenTrack; // clicking a track's name opens it in the editor
    bool mixDirty = false;

private:
    enum Col { None, Mute, Solo, Lock, Volume, Pan, Tone, Swap, Reverse, PitchDown, PitchUp, Remove };
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
    void mouseMagnify (const juce::MouseEvent&, float scale) override;
    void stopLoop();          // leaving the drum editor
    void forgetKit() { kitKey.clear(); } // another editor used the live player: load the kit again next time
    std::function<void()> onToggleBig; // the "bigger" button: the grid takes the whole editor
    void setBig (bool b) { bigBtn.setToggleState (b, juce::dontSendNotification); }

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
    bool bigNow() const { return notesTrack && ! showingDrums() ? noteBig : drumBig; }  // a played part (808, hook, bass line): its notes can be edited
    juce::TextButton sliceBtn { "Slice into hits" }; // a drum loop -> a drum track you can edit
    bool drumBig = false;
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
    SourcesPanel (LivePlanetProcessor& p, const lp::ui::Theme*& theme) : proc (p), th (theme) {}
    void open();
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

class LivePlanetEditor : public juce::AudioProcessorEditor,
                         public juce::FileDragAndDropTarget,
                         private juce::Timer
{
public:
    explicit LivePlanetEditor (LivePlanetProcessor&);
    ~LivePlanetEditor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override; // clicking anywhere else hands the keyboard back to your DAW
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int, int) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragging = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragging = false; repaint(); }

private:
    void timerCallback() override;
    void runSearch();
    void setPage (int p);
    void applyTheme();
    enum Page { Write = 0, Studio, Edit, Sound };
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
    TrackEditor trackEditor;
    juce::ComboBox studioKey, studioMode;   // the song's key, at the top of the studio
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
    bool dragging = false;

    juce::String cityText, headerText, nowText, statusText;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LivePlanetEditor)
};
