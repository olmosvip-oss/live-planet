#pragma once
#include <optional>
#include <set>
// GRAINS: the granular instrument's page. Drop a sound on it (or take one from your song or your library), hold a
// note, and shape it: the screen shows the sound, its chops and the grains as they play; the cells sequence the
// chops; the knobs, the grain shape and the filter shape the sound. "Add to song" records it as a new track.
// (Declared here, included by PluginEditor.h.)

class GrainsPage : public juce::Component,
                   public juce::FileDragAndDropTarget,
                   public juce::DragAndDropTarget,
                   private juce::Timer
{
public:
    GrainsPage (LivePlanetProcessor& p, const lp::ui::Theme*& theme);
    ~GrainsPage() override;
    void shown (bool on);                       // the page opened / closed (your keys play it while it's open)
    void loadFile (const juce::File& f);        // a sound to play
    void loadTrack (int track);                 // a track of your song
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dropping = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dropping = false; repaint(); }
    bool isInterestedInDragSource (const SourceDetails&) override { return false; }
    void itemDropped (const SourceDetails&) override {}
    std::function<void (const juce::String&)> onMessage; // a short note for the status line

private:
    void timerCallback() override;
    void changed (bool pushUndo = true);           // settings to the engine (and remembered)
    void beginChange();                            // (before a change: for undo)
    void syncControls();                           // the controls show the settings
    void pickPreset (int i);
    void showPresetMenu();
    void showSongMenu();
    void showLibrary();
    void showCellMenu (int cell);
    void addToSong();
    void hold (bool on);
    juce::Rectangle<float> screenArea() const, cellsArea() const, envArea() const, filterArea() const, waveArea() const;
    juce::Rectangle<float> cellRect (int i) const;
    int cellAt (juce::Point<float>) const;
    float xOfFrac (float frac) const;               // (a place in the sound: 0..1) -> x on the screen
    float fracOfX (float x) const;
    int chopsNow() const { return set().chops == 16 ? 16 : 8; }
    lp::grains::Settings& set() const { return proc.grainSettings; }
    juce::String knobText (int k, double v) const;

    LivePlanetProcessor& proc;
    const lp::ui::Theme*& th;
    struct Knob { juce::Slider s; int id = -1; juce::Rectangle<float> linkBox; };
    std::array<Knob, lp::grains::numKnobs> knobs;
    juce::Slider output, hpF, lpF, res, drive, fmix;
    juce::TextButton presetBtn, prevPreset { "<" }, nextPreset { ">" }, savePreset { "Save" },
                     loadBtn { "Load sound" }, songBtn { "From your song" }, libBtn { "Library" },
                     addBtn { "Add to song" }, holdBtn { "Hold note" },
                     modeHold { "hold" }, modeStep { "step" }, modeKeys { "keys" }, chops8 { "8" }, chops16 { "16" },
                     clockDown { "<" }, clockUp { ">" }, resetBtn { "reset" }, undoBtn { "undo" }, redoBtn { "redo" },
                     syncBtn { "sync" }, envPrev { "<" }, envNext { ">" }, revBtn { "reverse" }, chokeBtn { "choke" },
                     hpOn { "low cut" }, lpOn { "high cut" };
    HelpDot help { "Grains", "A granular instrument. Drop any sound here (or take one from your song or your library) and hold a note: "
                             "it's played as a cloud of tiny overlapping slices. hold: position picks where in the sound they come from "
                             "(drag on the screen). step: the cells play the chops in time with your song; click one to pick it, drag "
                             "it sideways to give it another chop, right-click to copy, paste or switch it off. keys: C3 and up play one "
                             "chop each. Unlink a knob (the dot under it) to give each chop its own value. Your computer keys play it too: "
                             "A S D F G H J K are C3 to C4." };
    int presetIndex = 0;
    bool presetEdited = false;
    int selChop = 0;                    // the cell (chop) picked: unlinked knobs edit it
    int dragCell = -1, dragCellFrom = -1;
    float dragStartX = 0.0f;
    enum class Drag { None, Position, TrimStart, TrimEnd, Cell } drag = Drag::None;
    bool dropping = false, holding = false, isShown = false;
    std::vector<lp::grains::Settings> undo, redo;
    std::optional<std::array<float, lp::grains::numKnobs>> copied;
    std::array<bool, lp::grains::numKnobs> copiedWhich {};
    std::set<int> keysDown;              // (computer keys held: their notes)
    std::array<float, 96> trailX {}, trailA {};
    juce::String note;             // a short message (shown for a few seconds)
    juce::uint32 noteAt = 0;
    std::unique_ptr<juce::FileChooser> chooser;
    std::atomic<bool> recordingNow { false };
};
