#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>
#include <typeinfo>
#include "DemoAudioTap.h"

// For recording tutorial videos (off unless LP_DEMO_SCRIPT is set): plays a script of real mouse and key
// events into the window (the plugin reacts exactly as to a person: hovers, clicks, its own pop-ups) and
// saves what the window shows, about 30 times a second, with any window that pops up over it.
//
//   LP_DEMO_SCRIPT=<file>   one step a line, coordinates in the editor's own 1120 x 760 space:
//       wait <ms> | move <x> <y> [<ms>] | click | fakeclick | drag <x> <y> [<ms>] | paste <text> | type <text> | key return|tab|escape | mark <name> | end
//   LP_CAPTURE_DIR=<folder> where the frames go (<n>.jpg) and capture.json (frame times, cursor, clicks, marks)
//   LP_CAPTURE_WIDTH=<px>   the frames' width (default 2240)
namespace lp::demo
{
class Director : public juce::Timer
{
public:
    explicit Director (juce::Component& editorView) : view (editorView)
    {
        const juce::File script (juce::String (std::getenv ("LP_DEMO_SCRIPT")));
        lines.addLines (script.loadFileAsString());
        if (auto* d = std::getenv ("LP_CAPTURE_DIR")) outDir = juce::File (juce::String (d));
        if (auto* w = std::getenv ("LP_CAPTURE_WIDTH")) outW = juce::jmax (640, juce::String (w).getIntValue());
        outDir.deleteRecursively();
        outDir.createDirectory();
        cursor = { view.getWidth() * 0.5f, view.getHeight() * 0.55f };
        start = juce::Time::getMillisecondCounterHiRes();
        // its sound too: what the plugin plays, from now (audio.wav, starting at this moment of the recording)
        {
            juce::WavAudioFormat wav;
            const auto f = outDir.getChildFile ("audio.wav");
            if (auto* os = f.createOutputStream().release())
                audioOut.reset (wav.createWriterFor (os, tap().sampleRate.load(), 2, 24, {}, 0));
            audioStart = now();
            tap().fifo.reset();
            tap().on = audioOut != nullptr;
        }
        startTimerHz (60);
    }
    ~Director() override { stopTimer(); pool.removeAllJobs (false, 10000); }

private:
    juce::Component& view;
    juce::StringArray lines;
    int line = 0;
    juce::File outDir;
    int outW = 2240;
    double start = 0.0, stepUntil = 0.0, lastCapture = -1000.0;
    juce::Point<float> cursor, moveFrom, moveTo;
    double moveStart = 0.0, moveMs = 0.0;
    bool moving = false, pressed = false, cutting = false;
    juce::Array<juce::var> frames, log;
    int frameNo = 0;
    juce::ThreadPool pool { 3 };

    double now() const { return juce::Time::getMillisecondCounterHiRes() - start; }
    std::unique_ptr<juce::AudioFormatWriter> audioOut;
    double audioStart = 0.0;

    void note (const juce::String& what = {})
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("t", (int) now());
        o->setProperty ("x", cursor.x);
        o->setProperty ("y", cursor.y);
        if (what.isNotEmpty()) o->setProperty (what.upToFirstOccurrenceOf (" ", false, false),
                                               what.containsChar (' ') ? juce::var (what.fromFirstOccurrenceOf (" ", false, false)) : juce::var (true));
        log.add (juce::var (o));
    }

    // the component under the cursor: in a pop-up over the editor (an alert), or in the editor itself
    juce::Component* targetAt (juce::Point<float>& local)
    {
        auto* top = view.getTopLevelComponent();
        auto& desk = juce::Desktop::getInstance();
        for (int i = desk.getNumComponents(); --i >= 0;)
        {
            auto* c = desk.getComponent (i);
            if (c == nullptr || c == top || ! c->isVisible()) continue;
            const auto r = view.getLocalArea (nullptr, c->getScreenBounds()).toFloat();
            if (! r.contains (cursor)) continue;
            const auto inC = juce::Point<float> ((cursor.x - r.getX()) * c->getWidth() / r.getWidth(), (cursor.y - r.getY()) * c->getHeight() / r.getHeight());
            auto* deep = c->getComponentAt (inC.roundToInt());
            if (deep == nullptr) deep = c;
            local = deep->getLocalPoint (c, inC);
            return deep;
        }
        auto* deep = view.getComponentAt (cursor.roundToInt());
        if (deep == nullptr) deep = &view;
        local = deep->getLocalPoint (&view, cursor);
        return deep;
    }

    // a click where the cursor is, given to that component the way a real click reaches it
    juce::Component::SafePointer<juce::TextEditor> typingInto;
    void clickHere()
    {
        juce::Point<float> local;
        auto* c = targetAt (local);
        if (std::getenv ("LP_DEMO_DEBUG") != nullptr)
            std::fprintf (stderr, "demo: click %.0f,%.0f on %s '%s'\n", cursor.x, cursor.y, typeid (*c).name(), c->getName().toRawUTF8());
        if (auto* b = dynamic_cast<juce::Button*> (c)) { b->triggerClick(); return; }
        if (auto* te = dynamic_cast<juce::TextEditor*> (c)) { typingInto = te; te->grabKeyboardFocus(); return; }
        auto src = juce::Desktop::getInstance().getMainMouseSource();
        const auto t = juce::Time::getCurrentTime();
        juce::Component::SafePointer<juce::Component> sp (c);
        const juce::MouseEvent down (src, local, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier | extraMods), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, c, c, t, local, t, 1, false);
        c->mouseDown (down);
        if (sp == nullptr) return;
        const juce::MouseEvent up (src, local, juce::ModifierKeys (extraMods), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, c, c, t, local, t, 1, false);
        c->mouseUp (up);
    }
    void hoverHere()
    {
        juce::Point<float> local;
        auto* c = targetAt (local);
        auto src = juce::Desktop::getInstance().getMainMouseSource();
        const auto t = juce::Time::getCurrentTime();
        c->mouseMove (juce::MouseEvent (src, local, {}, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, c, c, t, local, t, 0, false));
    }
    // dragging (knobs, sliders, clips): pressed on the component under the cursor, moved, let go
    juce::Component::SafePointer<juce::Component> dragging;
    juce::Point<float> dragDownLocal;
    juce::Time dragDownTime;
    juce::Point<float> localIn (juce::Component& c)
    {
        juce::Point<float> local;
        targetAt (local);
        return c.getLocalPoint (&view, cursor);
    }
    void dragStart()
    {
        juce::Point<float> local;
        auto* c = targetAt (local);
        if (std::getenv ("LP_DEMO_DEBUG") != nullptr)
            std::fprintf (stderr, "demo: drag from %.0f,%.0f on %s\n", cursor.x, cursor.y, typeid (*c).name());
        dragging = c;
        dragDownLocal = local;
        dragDownTime = juce::Time::getCurrentTime();
        c->mouseDown (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), local, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier | extraMods),
                                        1.0f, 0.0f, 0.0f, 0.0f, 0.0f, c, c, dragDownTime, local, dragDownTime, 1, false));
    }
    void dragMove()
    {
        if (dragging == nullptr) return;
        auto* c = dragging.getComponent();
        const auto local = c->getLocalPoint (&view, cursor);
        c->mouseDrag (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), local, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier | extraMods),
                                        1.0f, 0.0f, 0.0f, 0.0f, 0.0f, c, c, juce::Time::getCurrentTime(), dragDownLocal, dragDownTime, 1, true));
    }
    void dragEnd()
    {
        if (dragging == nullptr) return;
        auto* c = dragging.getComponent();
        const auto local = c->getLocalPoint (&view, cursor);
        c->mouseUp (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), local, juce::ModifierKeys (extraMods), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, c, c,
                                      juce::Time::getCurrentTime(), dragDownLocal, dragDownTime, 1, true));
        dragging = nullptr;
    }

    int extraMods = 0; // (mods shift / alt / cmd / none: held for the clicks and drags after it)
    // drop <file>: a file let go over the component under the cursor (the first one up from it that takes files)
    void dropHere (const juce::String& path)
    {
        juce::Point<float> local;
        for (auto* c = targetAt (local); c != nullptr; c = c->getParentComponent())
            if (auto* t = dynamic_cast<juce::FileDragAndDropTarget*> (c))
            {
                const auto at = c->getLocalPoint (&view, cursor).roundToInt();
                if (t->isInterestedInFileDrag ({ path })) { t->filesDropped ({ path }, at.x, at.y); return; }
            }
    }
    // press <key>: a key, to the component under the cursor (or the first one up from it that wants it)
    void pressHere (const juce::String& k)
    {
        const auto kp = k == "space" ? juce::KeyPress (juce::KeyPress::spaceKey) : k == "delete" ? juce::KeyPress (juce::KeyPress::backspaceKey)
                      : juce::KeyPress ((int) k[0], juce::ModifierKeys (extraMods), k[0]);
        juce::Point<float> local;
        for (auto* c = targetAt (local); c != nullptr; c = c->getParentComponent())
            if (c->keyPressed (kp)) return;
    }

    void typeChar (juce::juce_wchar ch)
    {
        if (typingInto != nullptr) typingInto->insertTextAtCaret (juce::String::charToString (ch));
    }

    void timerCallback() override
    {
        if (audioOut != nullptr) tap().drainInto (*audioOut);
        const double t = now();
        if (moving)
        {
            const double k = juce::jlimit (0.0, 1.0, (t - moveStart) / juce::jmax (1.0, moveMs));
            const double e = k < 0.5 ? 4 * k * k * k : 1 - std::pow (-2 * k + 2, 3) / 2;
            cursor = moveFrom + (moveTo - moveFrom) * (float) e;
            if (dragging != nullptr) dragMove(); else hoverHere();
            note();
            if (k >= 1.0) moving = false;
        }
        if (t - lastCapture >= (cutting ? 1000.0 : 33.0)) { lastCapture = t; capture(); } // (a wait that's cut out: a frame a second)
        while (! moving && t >= stepUntil && line < lines.size())
            if (! step (lines[line++].trim())) break;
    }

    // one step; false when it takes time (the next waits)
    bool step (const juce::String& s)
    {
        auto w = juce::StringArray::fromTokens (s, " ", "");
        if (s.isEmpty() || s.startsWith ("#")) return true;
        const auto cmd = w[0];
        if (cmd == "wait") { stepUntil = now() + w[1].getDoubleValue(); return false; }
        if (cmd == "move")
        {
            moveFrom = cursor;
            moveTo = { w[1].getFloatValue(), w[2].getFloatValue() };
            moveMs = w.size() > 3 ? w[3].getDoubleValue() : 800.0;
            moveStart = now();
            moving = true;
            return false;
        }
        if (cmd == "click")
        {
            note ("click");
            clickHere();
            stepUntil = now() + 120;
            return false;
        }
        if (cmd == "type")
        {
            // a key at a time, like typing
            const auto text = s.fromFirstOccurrenceOf ("type ", false, false);
            for (int i = text.length() - 1; i >= 0; --i)
            {
                lines.insert (line, "wait 35");
                lines.insert (line, "char " + (text[i] == ' ' ? juce::String ("<space>") : juce::String::charToString (text[i])));
            }
            return true;
        }
        if (cmd == "paste") { if (typingInto != nullptr) typingInto->insertTextAtCaret (s.fromFirstOccurrenceOf ("paste ", false, false)); return true; }
        if (cmd == "char") { const auto c = s.fromFirstOccurrenceOf ("char ", false, false); typeChar (c == "<space>" ? (juce::juce_wchar) ' ' : c[0]); return true; }
        if (cmd == "key")
        {
            const auto k = w[1];
            if (typingInto != nullptr)
                typingInto->keyPressed (k == "return" ? juce::KeyPress (juce::KeyPress::returnKey) : k == "tab" ? juce::KeyPress (juce::KeyPress::tabKey)
                                                                                                   : juce::KeyPress (juce::KeyPress::escapeKey));
            return true;
        }
        if (cmd == "mark") { note ("mark " + w[1]); if (w[1] == "cutstart") cutting = true; if (w[1] == "cutend") cutting = false; return true; }
        // the click is shown, not done (a button that would leave the app, like Buy: the video cuts to the website)
        // drag <x> <y> [<ms>]: press here, move there, let go (a knob turns, a slider slides, a clip moves)
        if (cmd == "drag")
        {
            note ("click");
            dragStart();
            moveFrom = cursor;
            moveTo = { w[1].getFloatValue(), w[2].getFloatValue() };
            moveMs = w.size() > 3 ? w[3].getDoubleValue() : 900.0;
            moveStart = now();
            moving = true;
            lines.insert (line, "release");
            return false;
        }
        if (cmd == "release") { dragEnd(); note(); return true; }
        if (cmd == "fakeclick") { note ("click"); stepUntil = now() + 120; return false; }
        if (cmd == "drop") { note ("click"); dropHere (s.fromFirstOccurrenceOf ("drop ", false, false).trim()); stepUntil = now() + 120; return false; }
        if (cmd == "press") { pressHere (w[1]); return true; }
        if (cmd == "dclick")
        {
            note ("click");
            juce::Point<float> local;
            auto* c = targetAt (local);
            const auto t = juce::Time::getCurrentTime();
            c->mouseDoubleClick (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), local, juce::ModifierKeys::leftButtonModifier,
                                                   1.0f, 0.0f, 0.0f, 0.0f, 0.0f, c, c, t, local, t, 2, false));
            stepUntil = now() + 120;
            return false;
        }
        if (cmd == "mods")
        {
            extraMods = 0;
            for (int i = 1; i < w.size(); ++i)
                extraMods |= w[i] == "shift" ? juce::ModifierKeys::shiftModifier : w[i] == "alt" ? juce::ModifierKeys::altModifier
                           : w[i] == "cmd" ? juce::ModifierKeys::commandModifier : 0;
            return true;
        }
        if (cmd == "end") { finish(); return false; }
        return true;
    }

    // the window as it is now (plus any pop-up over it), at outW pixels wide
    void capture()
    {
        const float sc = (float) outW / (float) view.getWidth();
        juce::Image img (juce::Image::RGB, outW, juce::roundToInt (view.getHeight() * sc), true);
        {
            juce::Graphics g (img);
            g.addTransform (juce::AffineTransform::scale (sc));
            view.paintEntireComponent (g, true);
            auto& desk = juce::Desktop::getInstance();
            auto* mine = view.getTopLevelComponent();
            for (int i = 0; i < desk.getNumComponents(); ++i)
            {
                auto* c = desk.getComponent (i);
                if (c == nullptr || c == mine || ! c->isShowing()) continue;
                const auto r = view.getLocalArea (nullptr, c->getScreenBounds()).toFloat();
                if (r.getWidth() < 1.0f || ! r.intersects (view.getLocalBounds().toFloat())) continue;
                juce::Graphics::ScopedSaveState ss (g);
                g.addTransform (juce::AffineTransform::scale (r.getWidth() / (float) c->getWidth()).translated (r.getX(), r.getY()));
                c->paintEntireComponent (g, true);
            }
        }
        const auto file = outDir.getChildFile (juce::String (frameNo++) + ".jpg");
        frames.add ((int) now());
        pool.addJob ([img, file]
        {
            juce::JPEGImageFormat jpg;
            jpg.setQuality (0.9f);
            juce::FileOutputStream out (file);
            if (out.openedOk()) jpg.writeImageToStream (img, out);
        });
    }

    void finish()
    {
        stopTimer();
        capture();
        pool.removeAllJobs (false, 20000);
        auto* o = new juce::DynamicObject();
        o->setProperty ("w", view.getWidth());
        o->setProperty ("h", view.getHeight());
        o->setProperty ("duration", (int) now());
        o->setProperty ("frames", frames);
        o->setProperty ("log", log);
        o->setProperty ("drawCursor", true);
        if (audioOut != nullptr)
        {
            tap().on = false;
            tap().drainInto (*audioOut);
            audioOut.reset(); // (the file finished)
            o->setProperty ("audio", "audio.wav");
            o->setProperty ("audioStart", audioStart);
            o->setProperty ("audioRate", tap().sampleRate.load());
        }
        outDir.getChildFile ("capture.json").replaceWithText (juce::JSON::toString (juce::var (o), true));
        juce::JUCEApplicationBase::quit();
    }
};
} // namespace lp::demo
