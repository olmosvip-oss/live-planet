#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"

// Draws the living sky: weather, clouds, rain, and every note as a coloured light.
class SkyView : public juce::Component
{
public:
    SkyView();
    void setSky (const lp::Weather& w) { sky = w; }
    void setText (const juce::String& title, const juce::String& line1, const juce::String& line2, const juce::String& music);
    void addNote (const lp::VisualEvent& e);
    void advance (float dt);
    void paint (juce::Graphics&) override;

private:
    struct Orb { float x, y, vy, r, hue, life, maxLife; int layer; bool landed; };
    struct Drop { float x, y, speed, len; };
    struct Flake { float x, y, speed, drift, r; };
    struct Cloud { float x, y, w, h, speed; };
    struct Star { float x, y, phase; };

    void drawHills (juce::Graphics&, juce::Rectangle<float>, juce::Colour, float height, float seed);

    lp::Weather sky;
    float dayAmount = 1.0f, flash = 0.0f, time = 0.0f;
    std::vector<Orb> orbs;
    std::vector<Drop> drops;
    std::vector<Flake> flakes;
    std::vector<Cloud> clouds;
    std::vector<Star> stars;
    juce::Path bolt;
    juce::String title, line1, line2, music;
    juce::Random rng;
};

class LivePlanetLookAndFeel : public juce::LookAndFeel_V4
{
public:
    LivePlanetLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end,
                           juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int) override;
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
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int, int) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragging = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragging = false; repaint(); }

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void timerCallback() override;
    Knob& addKnob (const char* id, const juce::String& name, float hue);
    void layoutKnobs (std::initializer_list<Knob*> knobs, juce::Rectangle<int> area, int cols);
    void runSearch();

    LivePlanetProcessor& proc;
    LivePlanetLookAndFeel lnf;
    SkyView skyView;

    juce::TextEditor cityBox;
    juce::TextButton goButton { "Go" }, liveButton { "LIVE" }, loadButton { "Load sample" };
    juce::ToggleButton dayToggle { "Daylight" }, thunderToggle { "Thunder" };
    juce::ComboBox keyBox, scaleBox, soundBox;
    juce::Label keyLabel, scaleLabel, soundLabel, sampleLabel;

    std::vector<std::unique_ptr<Knob>> knobs;
    std::map<juce::String, Knob*> knobById;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> liveAtt, dayAtt, thunderAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> keyAtt, scaleAtt, soundAtt;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::Rectangle<int> skyPanel, musicPanel, soundPanel;
    bool dragging = false;
    int wasManual = -1;
    double lastTick = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LivePlanetEditor)
};
