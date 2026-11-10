#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_opengl/juce_opengl.h>
#include "PluginProcessor.h"

namespace lp::ui
{
    enum Weight { Light, Medium, Bold };
    juce::Font font (float size, Weight w = Light, float kerning = 0.0f);

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
    juce::Rectangle<int> dropZone() const { return getLocalBounds().removeFromTop (50); }
    void refresh(); // update the button from the message thread (never while drawing)

private:
    LivePlanetProcessor& proc;
    const lp::ui::Theme*& th;
    juce::ToggleButton newKey { "new key" }, useSamples { "my samples" }, playWith { "play it with my song" }, samplesOnly { "samples only" }, dawTempo { "daw tempo" };
    Segmented length;
    std::unique_ptr<Stepper> style;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> useSamplesAtt, playWithAtt, samplesOnlyAtt, dawTempoAtt;
    juce::TextButton write { "write a song" };
    bool full = true;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<float> scanLink, folderLink;
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
    void mouseExit (const juce::MouseEvent&) override { hoverRow = -1; repaint(); }
    std::function<void()> onMixChanged;
    bool mixDirty = false;

private:
    enum Col { None, Mute, Solo, Volume, Pan, Tone, Swap, Reverse, PitchDown, PitchUp };
    juce::Rectangle<float> rowBounds (int row) const;
    juce::Rectangle<float> colBounds (int row, Col c) const;
    Col colAt (int row, juce::Point<float> p) const;
    void setFromX (int row, Col c, float x);
    LivePlanetProcessor& proc;
    int hoverRow = -1, dragRow = -1;
    Col dragCol = None;
};

// A pill you drag into your DAW (or click to show in Finder)
class FilePill : public juce::Component
{
public:
    FilePill (juce::String text, std::function<juce::StringArray()> files) : label (std::move (text)), getFiles (std::move (files)) {}
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
                         private juce::OpenGLRenderer,
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
    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;

    void timerCallback() override;
    void runSearch();
    void setPage (int p);
    void applyTheme();
    void paintScreen (juce::Graphics&);

    struct Knob
    {
        juce::Slider slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    Knob& addKnob (const char* id, const juce::String& name, float hue, int size);

    struct SceneState
    {
        float w1[4] {}, w2[4] {}, w3[4] {};
        float cityLon = 0;
        float palA[3] {}, palB[3] {}, palC[3] {};
        float notes[24 * 4] {};
        int noteCount = 0;
        float planet[4] {};
        float screen[4] {}; // x, y (from bottom), w, h in points
        float size[2] { 1120, 720 };
    };

    struct NoteFx { float hue; int kind; double born; float seed; double life; };
    struct LaneDot { int step; int note; int kind; double born; float hue; };

    LivePlanetProcessor& proc;
    LivePlanetLookAndFeel lnf;
    juce::OpenGLContext gl;
    std::unique_ptr<juce::OpenGLShaderProgram> shader;
    GLuint vbo = 0;
    std::atomic<bool> shaderFailed { false };

    juce::SpinLock sceneLock;
    SceneState scene;
    std::vector<NoteFx> fx;
    std::vector<LaneDot> lane;
    double startMs = 0, stepClock = 0;
    int lastUiStep = -1;
    float dayAmt = 1.0f, flash = 0.0f, smoothLat = 0.6f, smoothLon = 2.4f;
    bool haveCity = false;

    juce::TextEditor cityBox;
    Segmented liveSwitch, pageSwitch, keySwitch;
    SoundKeys keys;
    PlayButton playButton;
    PresetBrowser presetBrowser;
    SampleView sampleView;
    DragNotes dragNotes;
    SongPanel songPanel;
    SongView songView;
    StudioView studioView;
    FilePill pillMidi, pillStems, pillMix, pillHelper, pillFolder;
    std::unique_ptr<Stepper> keyStepper, scaleStepper;
    ThemeDots themeDots;
    juce::TextButton loadButton { "load sample" };
    juce::ToggleButton dayToggle { "daylight" }, thunderToggle { "thunder" };

    std::vector<std::unique_ptr<Knob>> knobs;
    std::map<juce::String, Knob*> knobById;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> dayAtt, thunderAtt;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::Rectangle<int> bezel, screen, infoCard, pageCard, rightPlate, plateMix, plateTex, plateFx;
    juce::Image topiary;
    const lp::ui::Theme* topiaryTheme = nullptr;
    int page = 0;
    bool dragging = false;

    juce::String cityText, headerText, nowText, statusText;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LivePlanetEditor)
};
