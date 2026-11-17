#include "PluginEditor.h"
#include "../song/Tune.h"

using namespace juce;
using lp::ui::font;

// The Auto-tune window for your tuned vocal: Natural / Extreme, key and scale, retune speed, humanize and
// amount, a meter of how far off the voice is right now, its pitch line (as sung, and tuned) over the notes,
// and a keyboard where you take notes out. Changes are tuned again from your untouched recording.

namespace
{
    const lp::ui::Theme& themeOf (Component& c)
    {
        const auto* lnf = dynamic_cast<LivePlanetLookAndFeel*> (&c.getLookAndFeel());
        return lnf != nullptr ? *lnf->th : lp::ui::theme (0);
    }
    const char* names12[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    constexpr double hopSec = 0.005;
}

TunePanel::TunePanel (LivePlanetProcessor& p, int trackIndex, std::function<void()> closed)
    : proc (p), track (trackIndex), onClosed (std::move (closed))
{
    setWantsKeyboardFocus (true);
    const auto v = proc.songJob.tuneView (track);
    tune = v.tune;
    name = v.name;
    seenVersion = v.version;

    naturalBtn.onClick = [this] { tune.speed = 0.35f; tune.humanize = 0.45f; tune.amount = 1.0f; syncControls(); changed(); };
    extremeBtn.onClick = [this] { tune.speed = 0.0f; tune.humanize = 0.0f; tune.amount = 1.0f; syncControls(); changed(); };
    playBtn.onClick = [this] { if (proc.studio.length() > 0) proc.previewPlaying = ! proc.previewPlaying.load(); };
    for (auto* b : { &naturalBtn, &extremeBtn, &playBtn }) { b->setWantsKeyboardFocus (false); addAndMakeVisible (*b); }
    for (int k = 0; k < 12; ++k) keyBox.addItem (names12[k], k + 1);
    for (int sc = 0; sc < 6; ++sc) scaleBox.addItem (lp::song::tune::scaleName (sc), sc + 1);
    keyBox.onChange = [this] { tune.key = keyBox.getSelectedId() - 1; changed(); };
    scaleBox.onChange = [this] { tune.scale = scaleBox.getSelectedId() - 1; changed(); };
    for (auto* b : { &keyBox, &scaleBox }) { b->setWantsKeyboardFocus (false); addAndMakeVisible (*b); }

    auto knob = [this] (Slider& s, const char* label, std::function<String (double)> text)
    {
        s.setName (label);
        s.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
        s.setRotaryParameters (MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
        s.setRange (0.0, 1.0, 0.0);
        s.getProperties().set ("size", 96);
        s.setMouseDragSensitivity (240);
        s.textFromValueFunction = std::move (text);
        s.setWantsKeyboardFocus (false);
        addAndMakeVisible (s);
    };
    knob (speedKnob, "retune speed", [] (double v) { return v > 0.97 ? String ("instant") : v > 0.7 ? "fast " + String (roundToInt (v * 100)) : v > 0.35 ? "medium " + String (roundToInt (v * 100)) : "slow " + String (roundToInt (v * 100)); });
    knob (humanKnob, "humanize", [] (double v) { return v < 0.02 ? String ("off") : String (roundToInt (v * 100)) + " %"; });
    knob (amountKnob, "amount", [] (double v) { return String (roundToInt (v * 100)) + " %"; });
    amountKnob.getProperties().set ("size", 64);
    speedKnob.onValueChange = [this] { tune.speed = 1.0f - (float) speedKnob.getValue(); changed(); };
    humanKnob.onValueChange = [this] { tune.humanize = (float) humanKnob.getValue(); changed(); };
    amountKnob.onValueChange = [this] { tune.amount = (float) amountKnob.getValue(); changed(); };
    syncControls();

    // your recording's pitch line, heard in the background (a few seconds for a whole song)
    auto alive = aliveFlag;
    const String src = tune.source;
    SafePointer<TunePanel> self (this);
    ++proc.printsRunning;
    Thread::launch ([src, self, alive, &pr = proc]
    {
        std::vector<double> pitch;
        AudioFormatManager fm;
        fm.registerBasicFormats();
        if (std::unique_ptr<AudioFormatReader> rd (fm.createReaderFor (File (src))); rd != nullptr && rd->sampleRate > 0)
        {
            const int len = (int) std::min<int64> (rd->lengthInSamples, (int64) (rd->sampleRate * 600.0));
            AudioBuffer<float> b (2, len);
            b.clear();
            rd->read (&b, 0, len, 0, true, rd->numChannels > 1);
            std::vector<float> mono ((size_t) len);
            for (int i = 0; i < len; ++i) mono[(size_t) i] = rd->numChannels > 1 ? 0.5f * (b.getSample (0, i) + b.getSample (1, i)) : b.getSample (0, i);
            const int hop = std::max (64, (int) (hopSec * rd->sampleRate));
            pitch = lp::song::tune::track (mono, rd->sampleRate, hop);
        }
        --pr.printsRunning;
        if (! alive->load()) return;
        MessageManager::callAsync ([self, pitch = std::move (pitch)]() mutable
        {
            if (self == nullptr) return;
            self->pitch = std::move (pitch);
            self->recompute();
            self->repaint();
        });
    });
    startTimerHz (30);
}

TunePanel::~TunePanel()
{
    *aliveFlag = false;
    if (dirty) proc.songJob.setTune (track, tune);
}

void TunePanel::syncControls()
{
    keyBox.setSelectedId (tune.key + 1, dontSendNotification);
    scaleBox.setSelectedId (tune.scale + 1, dontSendNotification);
    speedKnob.setValue (1.0 - tune.speed, dontSendNotification);
    humanKnob.setValue (tune.humanize, dontSendNotification);
    amountKnob.setValue (tune.amount, dontSendNotification);
}

lp::song::tune::Settings TunePanel::settings() const
{
    lp::song::tune::Settings s;
    s.key = tune.key; s.scale = tune.scale; s.speed = tune.speed; s.humanize = tune.humanize; s.amount = tune.amount; s.removed = tune.removed;
    return s;
}

void TunePanel::recompute()
{
    if (pitch.empty()) return;
    const auto s = settings();
    corr = lp::song::tune::correction (pitch, s, 1, 1.0 / hopSec);
    target = lp::song::tune::targets (pitch, s);
    // the notes the voice uses (the pitch line's range)
    std::vector<double> voiced;
    for (double p : pitch) if (p > 0.0) voiced.push_back (p);
    if (! voiced.empty())
    {
        std::sort (voiced.begin(), voiced.end());
        lowNote = (int) std::floor (voiced[voiced.size() / 20]) - 2;
        highNote = (int) std::ceil (voiced[voiced.size() - 1 - voiced.size() / 20]) + 2;
        if (highNote - lowNote < 12) { const int mid = (lowNote + highNote) / 2; lowNote = mid - 6; highNote = mid + 6; }
    }
}

void TunePanel::changed()
{
    dirty = true;
    changedAt = Time::getMillisecondCounter();
    recompute();
    repaint();
}

void TunePanel::timerCallback()
{
    const auto now = Time::getMillisecondCounter();
    const bool mouseDown = speedKnob.isMouseButtonDown() || humanKnob.isMouseButtonDown() || amountKnob.isMouseButtonDown();
    if (dirty && ! mouseDown && now - changedAt > 600) { dirty = false; proc.songJob.setTune (track, tune); }
    const auto st = proc.songJob.getStatus();
    busy = st.editing;
    if (st.version != seenVersion) { seenVersion = st.version; if (! proc.songJob.tuneView (track).valid) { if (onClosed) onClosed(); return; } }
    playBtn.setButtonText (proc.previewPlaying.load() ? "Stop" : "Play");
    repaint();
}

Rectangle<int> TunePanel::card() const
{
    return Rectangle<int> (jmin (1040, getWidth() - 40), jmin (720, getHeight() - 40)).withCentre (getLocalBounds().getCentre());
}
Rectangle<float> TunePanel::meterArea() const
{
    const auto c = card().toFloat();
    return Rectangle<float> (230.0f, 230.0f).withCentre ({ c.getCentreX(), c.getY() + 270.0f });
}
Rectangle<float> TunePanel::graphArea() const
{
    const auto c = card().toFloat();
    return { c.getX() + 28.0f, c.getY() + 410.0f, c.getWidth() - 56.0f, c.getHeight() - 410.0f - 128.0f };
}
Rectangle<float> TunePanel::keysArea() const
{
    const auto c = card().toFloat();
    const auto g = graphArea();
    return { c.getX() + 28.0f, g.getBottom() + 16.0f, c.getWidth() - 56.0f, c.getBottom() - g.getBottom() - 16.0f - 26.0f };
}

void TunePanel::resized()
{
    const auto c = card();
    closeX = Rectangle<int> (c.getRight() - 50, c.getY() + 24, 30, 30);
    playBtn.setBounds (c.getRight() - 140, c.getY() + 22, 76, 36);
    naturalBtn.setBounds (c.getX() + 28, c.getY() + 96, 110, 36);
    extremeBtn.setBounds (c.getX() + 146, c.getY() + 96, 110, 36);
    scaleBox.setBounds (c.getRight() - 28 - 190, c.getY() + 96, 190, 36);
    keyBox.setBounds (c.getRight() - 28 - 190 - 10 - 80, c.getY() + 96, 80, 36);
    const auto m = meterArea();
    speedKnob.setBounds (Rectangle<int> (170, 170).withCentre ({ (int) (c.getX() + (m.getX() - c.getX()) / 2), (int) m.getCentreY() }));
    humanKnob.setBounds (Rectangle<int> (170, 170).withCentre ({ (int) (m.getRight() + (c.getRight() - m.getRight()) / 2) - 50, (int) m.getCentreY() }));
    amountKnob.setBounds (Rectangle<int> (110, 130).withCentre ({ c.getRight() - 90, (int) m.getCentreY() + 20 }));
}

double TunePanel::nowSec() const { return proc.playheadPos.load() / StudioPlayer::fileRate; }

void TunePanel::paint (Graphics& g)
{
    const auto& t = themeOf (*this);
    g.fillAll (Colours::black.withAlpha (0.72f));
    const auto cf = card().toFloat();
    const Colour accent (0xff7aa7ff);
    {
        Graphics::ScopedSaveState s (g);
        Path clip; clip.addRoundedRectangle (cf, 22.0f);
        g.reduceClipRegion (clip);
        g.setColour (t.bodyDark.interpolatedWith (Colour (0xff09090c), 0.7f));
        g.fillRect (cf);
        g.setGradientFill (ColourGradient (accent.withAlpha (0.16f), cf.getCentreX(), cf.getY() + 200.0f, accent.withAlpha (0.0f), cf.getCentreX(), cf.getY() + 560.0f, true));
        g.fillRect (cf);
    }
    g.setColour (Colours::white.withAlpha (0.1f));
    g.drawRoundedRectangle (cf.reduced (0.5f), 22.0f, 1.0f);
    auto r = cf.reduced (28.0f, 24.0f);
    g.setFont (lp::ui::mono (10.0f, true, 0.18f));
    g.setColour (t.grassLight);
    g.drawText ("AUTO-TUNE", r.removeFromTop (14.0f), Justification::centredLeft, false);
    g.setFont (font (26.0f, lp::ui::Medium, -0.02f));
    g.setColour (t.ink);
    g.drawText (File (tune.source).getFileNameWithoutExtension(), r.removeFromTop (40.0f).withTrimmedRight (200.0f), Justification::centredLeft, true);
    g.setColour (t.inkDim);
    const auto cx = closeX.toFloat().reduced (9.0f);
    g.drawLine (cx.getX(), cx.getY(), cx.getRight(), cx.getBottom(), 1.6f);
    g.drawLine (cx.getRight(), cx.getY(), cx.getX(), cx.getBottom(), 1.6f);
    if (busy || dirty)
    {
        g.setFont (lp::ui::mono (10.0f, true, 0.12f));
        g.setColour (accent);
        g.drawText (dirty ? "CHANGED: TUNING IN A MOMENT" : "TUNING YOUR VOCAL...", Rectangle<float> (cf.getRight() - 460.0f, cf.getY() + 32.0f, 300.0f, 16.0f), Justification::centredRight, false);
    }

    // ---- the meter: how far the voice is from its note right now (as sung), and where tuning puts it
    const auto m = meterArea();
    const double sec = nowSec();
    const long f = (long) (sec / hopSec);
    const bool live = proc.songIsPlaying() && f >= 0 && f < (long) pitch.size() && pitch[(size_t) f] > 0.0 && target[(size_t) f] > 0.0;
    const float cents = live ? (float) ((pitch[(size_t) f] - target[(size_t) f]) * 100.0) : 0.0f;
    const float tunedCents = live ? cents + (float) corr[(size_t) f] * 100.0f : 0.0f;
    {
        g.setColour (Colours::black.withAlpha (0.45f));
        g.fillEllipse (m);
        g.setColour (Colours::white.withAlpha (0.08f));
        g.drawEllipse (m.reduced (1.0f), 1.0f);
        const auto ctr = m.getCentre();
        const float rad = m.getWidth() * 0.5f;
        auto angleOf = [] (float c) { return jlimit (-100.0f, 100.0f, c) / 100.0f * MathConstants<float>::pi * 0.75f; };
        for (int c = -100; c <= 100; c += 5)
        {
            const float a = angleOf ((float) c);
            const float len = c % 25 == 0 ? 16.0f : 9.0f;
            const bool lit = live && ((cents >= 0 && c >= 0 && c <= cents) || (cents < 0 && c <= 0 && c >= cents));
            g.setColour (lit ? accent.withAlpha (0.9f) : Colours::white.withAlpha (0.16f));
            const Point<float> p0 (ctr.x + std::sin (a) * (rad - 10.0f), ctr.y - std::cos (a) * (rad - 10.0f));
            const Point<float> p1 (ctr.x + std::sin (a) * (rad - 10.0f - len), ctr.y - std::cos (a) * (rad - 10.0f - len));
            g.drawLine ({ p0, p1 }, c % 25 == 0 ? 2.0f : 1.2f);
        }
        g.setFont (lp::ui::mono (9.0f, false, 0.06f));
        g.setColour (t.inkDim);
        for (int c : { -100, -50, 0, 50, 100 })
        {
            const float a = angleOf ((float) c);
            g.drawText ((c > 0 ? "+" : "") + String (c), Rectangle<float> (40.0f, 14.0f).withCentre ({ ctr.x + std::sin (a) * (rad + 18.0f), ctr.y - std::cos (a) * (rad + 18.0f) }), Justification::centred, false);
        }
        // the tuned voice's needle
        if (live)
        {
            const float a = angleOf (tunedCents);
            g.setColour (Colours::white);
            g.drawLine (ctr.x, ctr.y, ctr.x + std::sin (a) * (rad - 34.0f), ctr.y - std::cos (a) * (rad - 34.0f), 2.0f);
        }
        g.setColour (t.ink);
        g.setFont (font (34.0f, lp::ui::Medium, -0.02f));
        const String noteText = live ? String (names12[((int) target[(size_t) f] % 12 + 12) % 12]) : String::fromUTF8 ("\xe2\x80\x94");
        g.drawText (noteText, Rectangle<float> (m.getWidth(), 40.0f).withCentre ({ ctr.x, ctr.y + 10.0f }), Justification::centred, false);
        g.setFont (lp::ui::mono (10.0f, false, 0.08f));
        g.setColour (t.inkDim);
        g.drawText (live ? String ("sung ") + (cents >= 0 ? "+" : "") + String (roundToInt (cents)) + " cents" : (proc.songIsPlaying() ? String ("listening") : String ("press play")),
                    Rectangle<float> (m.getWidth(), 16.0f).withCentre ({ ctr.x, ctr.y + 42.0f }), Justification::centred, false);
    }

    // ---- the pitch line: as sung (grey) and tuned (blue), over the notes
    const auto ga = graphArea();
    g.setColour (Colours::black.withAlpha (0.38f));
    g.fillRoundedRectangle (ga, 12.0f);
    const auto s = settings();
    const float laneH = ga.getHeight() / (float) std::max (1, highNote - lowNote + 1);
    for (int n = lowNote; n <= highNote; ++n)
    {
        const float y = ga.getBottom() - (float) (n - lowNote + 1) * laneH;
        const bool ok = lp::song::tune::allowed (n, s);
        if (ok) { g.setColour (Colours::white.withAlpha (0.045f)); g.fillRect (ga.getX() + 34.0f, y, ga.getWidth() - 34.0f, laneH); }
        g.setFont (lp::ui::mono (8.5f, ok, 0.04f));
        g.setColour (ok ? t.ink.withAlpha (0.7f) : t.inkDim.withAlpha (0.4f));
        if (laneH >= 8.0f) g.drawText (String (names12[(n % 12 + 12) % 12]) + String (n / 12 - 1), Rectangle<float> (ga.getX() + 4.0f, y, 30.0f, laneH), Justification::centredLeft, false);
    }
    // 8 seconds of it around where the song is
    const double span = 8.0, t0 = std::max (0.0, sec - span * 0.35);
    auto yOf = [&] (double midi) { return ga.getBottom() - (float) ((midi - lowNote + 0.5) * laneH); };
    auto xOfT = [&] (double ts) { return ga.getX() + 34.0f + (float) ((ts - t0) / span) * (ga.getWidth() - 34.0f); };
    if (! pitch.empty())
    {
        Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (ga.toNearestInt());
        const long f0 = (long) (t0 / hopSec), f1 = std::min ((long) pitch.size(), (long) ((t0 + span) / hopSec));
        for (int pass = 0; pass < 2; ++pass)
        {
            Path p;
            bool open = false;
            for (long k = std::max (0L, f0); k < f1; ++k)
            {
                const double pm = pitch[(size_t) k];
                if (pm <= 0.0) { open = false; continue; }
                const double v = pass == 0 ? pm : pm + corr[(size_t) k];
                const Point<float> pt (xOfT (k * hopSec), yOf (v));
                if (! open) { p.startNewSubPath (pt); open = true; } else p.lineTo (pt);
            }
            g.setColour (pass == 0 ? Colours::white.withAlpha (0.35f) : accent);
            g.strokePath (p, PathStrokeType (pass == 0 ? 1.4f : 2.2f, PathStrokeType::curved, PathStrokeType::rounded));
        }
        if (proc.songIsPlaying())
        {
            g.setColour (Colours::white.withAlpha (0.5f));
            g.drawVerticalLine ((int) xOfT (sec), ga.getY(), ga.getBottom());
        }
    }
    else
    {
        g.setFont (font (14.0f, lp::ui::Medium));
        g.setColour (t.inkDim);
        g.drawText ("listening to your vocal...", ga, Justification::centred, false);
    }
    g.setFont (lp::ui::mono (9.0f, false, 0.1f));
    g.setColour (t.inkDim);
    g.drawText ("GREY: AS YOU SANG IT   |   BLUE: TUNED", Rectangle<float> (ga.getRight() - 300.0f, ga.getY() + 6.0f, 290.0f, 12.0f), Justification::centredRight, false);

    // ---- the keyboard: the key's notes lit; click one to take it out (or put it back)
    const auto ka = keysArea();
    const float kw = ka.getWidth() / 12.0f;
    for (int pc = 0; pc < 12; ++pc)
    {
        const auto kr = Rectangle<float> (ka.getX() + pc * kw, ka.getY(), kw - 4.0f, ka.getHeight());
        const bool inScale = lp::song::tune::allowed (pc + 60, { tune.key, tune.scale, 0, 0, 1, 0 });
        const bool removed = (tune.removed >> pc) & 1;
        const bool black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
        g.setColour (black ? Colour (0xff1c1c22) : Colour (0xffe9e9ee));
        g.fillRoundedRectangle (kr, 6.0f);
        if (inScale)
        {
            g.setColour (removed ? Colour (0xffff5a6e).withAlpha (0.5f) : accent.withAlpha (black ? 0.8f : 0.55f));
            g.fillRoundedRectangle (kr.withTrimmedTop (kr.getHeight() * 0.55f).reduced (4.0f), 4.0f);
        }
        g.setFont (font (14.0f, lp::ui::Medium));
        g.setColour (black ? Colours::white.withAlpha (inScale ? 0.95f : 0.4f) : Colours::black.withAlpha (inScale ? 0.9f : 0.35f));
        g.drawText (names12[pc], kr.withTrimmedBottom (kr.getHeight() * 0.45f), Justification::centred, false);
        if (inScale)
        {
            g.setFont (lp::ui::mono (8.5f, true, 0.06f));
            g.setColour (Colours::white);
            g.drawText (removed ? "OFF" : "ON", kr.withTrimmedTop (kr.getHeight() * 0.55f), Justification::centred, false);
        }
    }
    g.setFont (lp::ui::mono (9.0f, false, 0.1f));
    g.setColour (t.inkDim);
    g.drawText ("CLICK A NOTE OF THE KEY TO TAKE IT OUT (NOTHING IS TUNED TO IT), AGAIN TO PUT IT BACK", Rectangle<float> (ka.getX(), ka.getBottom() + 6.0f, ka.getWidth(), 12.0f), Justification::centredLeft, false);
}

void TunePanel::mouseDown (const MouseEvent& e)
{
    if (closeX.contains (e.getPosition()) || ! card().contains (e.getPosition())) { if (onClosed) onClosed(); return; }
    const auto ka = keysArea();
    if (ka.contains (e.position))
    {
        const int pc = jlimit (0, 11, (int) ((e.position.x - ka.getX()) / (ka.getWidth() / 12.0f)));
        if (! lp::song::tune::allowed (pc + 60, { tune.key, tune.scale, 0, 0, 1, 0 })) return;
        tune.removed ^= (1 << pc);
        // at least two notes stay in
        int left = 0;
        for (int k = 0; k < 12; ++k) if (lp::song::tune::allowed (k + 60, settings())) ++left;
        if (left < 2) { tune.removed ^= (1 << pc); return; }
        changed();
    }
}

bool TunePanel::keyPressed (const KeyPress& k)
{
    if (k == KeyPress::escapeKey) { if (onClosed) onClosed(); return true; }
    if (k == KeyPress::spaceKey) { if (proc.studio.length() > 0) proc.previewPlaying = ! proc.previewPlaying.load(); return true; }
    return false;
}
