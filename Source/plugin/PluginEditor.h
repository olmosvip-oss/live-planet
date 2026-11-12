#pragma once
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
    void fillTextEditorBackground (juce::Graphics&, int, int, juce::TextEditor&) override {}
    void drawTextEditorOutline (juce::Graphics&, int, int, juce::TextEditor&) override {}

    const lp::ui::Theme* th = &lp::ui::theme (0);
    std::map<std::pair<const lp::ui::Theme*, int>, juce::Image> balls; // grass knob images
    float displayScale = 1.0f;
};

// Two-way switch, like "live | manual"
class Segmented : public juce::Component
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
    std::unique_ptr<ChoiceChips> style;
    juce::ComboBox keyBox, modeBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> keyAtt, modeAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> useSamplesAtt, playWithAtt, samplesOnlyAtt, dawTempoAtt;
    juce::TextButton write { "write a song" };
    bool full = true;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<float> scanLink, folderLink, sourcesLink;
    juce::Rectangle<float> licenseLink;
    void showActivate (const juce::String& message);
public:
    std::function<void()> onShowSources;
private:
    juce::Rectangle<int> hero, settings;
};

// Song mode, on the screen: the song's sections as a timeline, and progress
class SongView : public juce::Component
{
public:
    explicit SongView (LivePlanetProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;

private:
    LivePlanetProcessor& proc;
};

// The studio: every track of the song, where its sound came from, and its mixer and remix tools
class StudioView : public juce::Component
{
public:
    explicit StudioView (LivePlanetProcessor& p) : proc (p) {}
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
    enum Col { None, Mute, Solo, Volume, Pan, Tone, Swap, Reverse, PitchDown, PitchUp };
    juce::Rectangle<float> rowBounds (int row) const;
    juce::Rectangle<float> colBounds (int row, Col c) const;
    juce::Rectangle<float> addBounds (int numTracks) const;
    void showAddMenu();
    bool hoverAdd = false;
    int scroll = 0;                 // first track row shown
    int visibleRows() const;
    bool rowShown (int row) const { return row >= scroll && row < scroll + visibleRows(); }
    Col colAt (int row, juce::Point<float> p) const;
    void setFromX (int row, Col c, float x);
    LivePlanetProcessor& proc;
    int hoverRow = -1, dragRow = -1;
    Col dragCol = None;
};

// EDIT: one track at a time, like a sampler: its sample (choose which bars play), its own effects and remix tools
class FilePill : public juce::Component
{
public:
    FilePill (juce::String text, std::function<juce::StringArray()> files) : label (std::move (text)), getFiles (std::move (files)) {}
    // optional: the files actually handed over when dragged (e.g. recorded with effects), if not getFiles
    std::function<juce::StringArray()> filesToDrag;
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
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    juce::Rectangle<float> gridArea() const;
    int numSteps() const;
    bool cellAt (juce::Point<float> p, int& row, int& step) const;
    std::vector<size_t> hitsIn (int row, int step) const;
    void setCell (int row, int step, bool on);
    void copyAcross (bool everySectionOfKind);
    void pushUndo();
    void changed();

    LivePlanetProcessor& proc;
    const lp::ui::Theme*& th;
    int track = -1, totalBars = 1, startBar = 0, bars = 1, stepsPerBeat = 4, paintRow = -1;
    bool painting = true, dirty = false, waitingForRender = false;
    juce::uint32 lastChange = 0;
    juce::String songTitle;
    std::vector<lp::song::Section> sections;
    std::vector<lp::song::Note> notes;
    std::vector<std::vector<lp::song::Note>> undo;
    std::vector<int> rows; // drum notes, top to bottom
    juce::TextButton prevBar { "<" }, nextBar { ">" }, lenBtn { "1 bar" }, resBtn { "1/16" },
                     fillSection { "fill section" }, fillAll { "every chorus" }, clearBtn { "clear" }, undoBtn { "undo" };
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
    juce::TextButton printFx { "Print FX" };
    FilePill dragTrack;
    juce::TextButton love { "Love it" }, notThis { "Not this" }; // your taste: remembered for every later song
    DrumGrid drumGrid;
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
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> studioKeyAtt, studioModeAtt;
    GrainView grainView;
    SourcesPanel sourcesPanel;
    FilePill pillMidi, pillStems, pillMix, pillHelper, pillFolder;
    std::unique_ptr<Stepper> keyStepper, scaleStepper;
    ThemeDots themeDots;
    juce::TextButton loadButton { "Load sample" };
    juce::ToggleButton dayToggle { "daylight" }, thunderToggle { "thunder" };

    std::vector<std::unique_ptr<Knob>> knobs;
    std::map<juce::String, Knob*> knobById;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> dayAtt, thunderAtt;
    std::unique_ptr<juce::FileChooser> chooser;

    // cards on each page
    juce::Rectangle<int> cardTimeline, cardInstrument, cardSky, cardWeather, plateMix, plateTex, plateFx;
    int page = 0;
    bool dragging = false;

    juce::String cityText, headerText, nowText, statusText;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LivePlanetEditor)
};
