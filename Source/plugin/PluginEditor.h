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
    Segmented (juce::String a, juce::String b, std::function<int()> get, std::function<void (int)> set,
               const lp::ui::Theme*& theme);
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::String labels[2];
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
    Segmented liveSwitch, pageSwitch;
    SoundKeys keys;
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
