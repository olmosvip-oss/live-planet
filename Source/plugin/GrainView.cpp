#include "PluginEditor.h"

using namespace juce;

void GrainView::paint (Graphics& g)
{
    const auto& th = lp::ui::theme (proc.themeIndex.load());
    auto r = getLocalBounds().toFloat();
    g.setColour (Colour (0xff0a0a0b));
    g.fillRoundedRectangle (r, 12.0f);
    auto w = r.reduced (16.0f, 14.0f);

    // the sound's shape (worked out once per sound)
    const int id = proc.engine.uiSound.load();
    if (id != peaksFor)
    {
        peaksFor = id;
        peaks.assign (240, 0.0f);
        const auto& src = proc.engine.soundForDisplay ((lp::SoundId) id);
        const size_t n = src.data.size();
        if (n > 0)
            for (size_t c = 0; c < peaks.size(); ++c)
                for (size_t i = n * c / peaks.size(); i < n * (c + 1) / peaks.size(); i += 4)
                    peaks[c] = std::max (peaks[c], std::abs (src.data[i]));
        float top = 0.0f;
        for (float v : peaks) top = std::max (top, v);
        if (top > 0.0f) for (auto& v : peaks) v /= top;
    }
    auto prm = [this] (const char* pid) { return proc.params.getRawParameterValue (pid)->load(); };
    const float pos = prm ("position"), scatter = prm ("texture"), sparkle = prm ("sparkle"), ms = prm ("grain");
    const float centre = w.getX() + w.getWidth() * (0.05f + 0.9f * pos);
    const float spread = w.getWidth() * (0.04f + 0.4f * scatter);
    const float gw = jlimit (3.0f, w.getWidth() * 0.3f, w.getWidth() * ms / 2000.0f);

    // where grains come from
    g.setColour (th.grass.withAlpha (0.12f));
    g.fillRoundedRectangle (Rectangle<float> (centre - spread, w.getY(), spread * 2.0f, w.getHeight()), 6.0f);
    const float mid = w.getCentreY();
    for (size_t c = 0; c < peaks.size(); ++c)
    {
        const float x = w.getX() + w.getWidth() * (float) c / (float) peaks.size();
        const float h = std::max (1.0f, peaks[c] * w.getHeight() * 0.42f);
        g.setColour (Colours::white.withAlpha (std::abs (x - centre) < spread ? 0.55f : 0.18f));
        g.fillRect (x, mid - h, std::max (1.0f, w.getWidth() / (float) peaks.size() - 1.0f), h * 2.0f);
    }

    // the grains: a few new ones every frame while it plays (more with scatter), fading out
    const double now = Time::getMillisecondCounterHiRes() * 0.001;
    const bool playing = proc.engine.uiRunning.load();
    if (playing && now - last > 0.03)
    {
        last = now;
        const int count = 1 + (int) (scatter * 3.0f);
        for (int i = 0; i < count; ++i)
            grains.push_back ({ centre + (rng.nextFloat() * 2.0f - 1.0f) * spread, gw, (float) now, 0.25f + ms / 900.0f,
                                rng.nextFloat() < sparkle * 0.5f ? 1.0f : 0.0f });
    }
    grains.erase (std::remove_if (grains.begin(), grains.end(), [now] (const G& q) { return now - q.born > q.life; }), grains.end());
    while (grains.size() > 80) grains.erase (grains.begin());
    for (auto& q : grains)
    {
        const float age = (float) (now - q.born) / q.life;
        const float a = (1.0f - age) * (q.high > 0.5f ? 0.95f : 0.6f);
        const float y = q.high > 0.5f ? w.getY() + w.getHeight() * 0.18f : mid;
        const float h = q.high > 0.5f ? 4.0f : w.getHeight() * (0.55f - 0.25f * age);
        g.setColour ((q.high > 0.5f ? th.grassLight : th.grass).withAlpha (a));
        g.fillRoundedRectangle (q.x - q.w * 0.5f, y - h * 0.5f, q.w, h, std::min (3.0f, q.w * 0.5f));
    }
    g.setFont (lp::ui::mono (9.5f, false, 0.12f));
    g.setColour (Colours::white.withAlpha (0.45f));
    g.drawText (playing ? "GRAINS" : "PRESS PLAY OR A KEY TO SEE THE GRAINS", r.reduced (14.0f, 8.0f), Justification::topLeft, false);
}
