#include "PluginEditor.h"

namespace
{
    const juce::Colour kInk       { 0xff0d0f24 };  // panel background
    const juce::Colour kInkLight  { 0xff191c3d };
    const juce::Colour kText      { 0xffeef0ff };
    const juce::Colour kTextDim   { 0xff9aa0c8 };

    constexpr float kHueSky = 0.53f, kHueMusic = 0.88f, kHueSound = 0.11f;

    juce::Font font (float size, bool bold = false)
    {
        return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
    }

    juce::String describeLine (const lp::Weather& w)
    {
        const juce::String degree (juce::CharPointer_UTF8 ("\xc2\xb0"));
        juce::String s = w.describe() + "  |  " + juce::String (juce::roundToInt (w.temperatureC)) + degree + "C"
                       + "  |  wind " + juce::String (juce::roundToInt (w.windKmh)) + " km/h";
        if (w.snowCm > 0.05f) s << "  |  snow " << juce::String (w.snowCm, 1) << " cm";
        else s << "  |  rain " << juce::String (w.rainMm, 1) << " mm";
        return s;
    }

    juce::Colour mix (juce::Colour a, juce::Colour b, float t) { return a.interpolatedWith (b, juce::jlimit (0.0f, 1.0f, t)); }

}

//==============================================================================
// SkyView

SkyView::SkyView()
{
    setInterceptsMouseClicks (false, true);
    for (int i = 0; i < 90; ++i)
        stars.push_back ({ rng.nextFloat(), rng.nextFloat() * 0.6f, rng.nextFloat() * 6.28f });
    for (int i = 0; i < 12; ++i)
        clouds.push_back ({ rng.nextFloat() * 1.3f - 0.15f, 0.08f + rng.nextFloat() * 0.35f,
                            0.18f + rng.nextFloat() * 0.22f, 0.06f + rng.nextFloat() * 0.06f,
                            0.6f + rng.nextFloat() * 0.8f });
}

void SkyView::setText (const juce::String& t, const juce::String& a, const juce::String& b, const juce::String& m)
{
    title = t; line1 = a; line2 = b; music = m;
}

void SkyView::addNote (const lp::VisualEvent& e)
{
    const float w = (float) getWidth(), h = (float) getHeight();
    if (w <= 0) return;
    if (e.layer == 3)
    {
        flash = 1.0f;
        bolt.clear();
        float x = w * (0.15f + rng.nextFloat() * 0.7f), y = 0;
        bolt.startNewSubPath (x, y);
        while (y < h * 0.72f)
        {
            x += (rng.nextFloat() - 0.5f) * 40.0f;
            y += 12.0f + rng.nextFloat() * 22.0f;
            bolt.lineTo (x, y);
        }
        return;
    }
    if (orbs.size() > 160) orbs.erase (orbs.begin());
    const float x = juce::jmap ((float) e.note, 28.0f, 100.0f, w * 0.06f, w * 0.94f) + (rng.nextFloat() - 0.5f) * 30.0f;
    const float hue = (float) (((e.note % 12) * 7) % 12) / 12.0f;
    Orb o {};
    o.x = x;
    o.hue = hue;
    o.layer = e.layer;
    if (e.layer == 1)      { o.y = -10.0f; o.vy = 160.0f + rng.nextFloat() * 80.0f; o.r = 4.0f + e.velocity * 5.0f; o.maxLife = 2.6f; }
    else if (e.layer == 0) { o.y = h * 0.78f; o.vy = -(12.0f + rng.nextFloat() * 10.0f); o.r = 24.0f + e.velocity * 20.0f; o.maxLife = 6.5f; }
    else                   { o.y = h * 0.92f; o.vy = -4.0f; o.r = 70.0f; o.maxLife = 4.0f; }
    o.life = o.maxLife;
    orbs.push_back (o);
}

void SkyView::advance (float dt)
{
    time += dt;
    const float w = (float) getWidth(), h = (float) getHeight();
    dayAmount += ((sky.isDay ? 1.0f : 0.0f) - dayAmount) * std::min (1.0f, dt * 0.8f);
    flash = std::max (0.0f, flash - dt * 2.2f);
    const float wind = sky.windKmh / 50.0f;
    const float dir = std::sin (juce::degreesToRadians (sky.windDirDeg)) >= 0 ? -1.0f : 1.0f; // wind blows away from its source

    for (auto& c : clouds)
    {
        c.x += dir * dt * (0.004f + wind * 0.03f) * c.speed;
        if (c.x > 1.3f) c.x = -0.35f;
        if (c.x < -0.35f) c.x = 1.3f;
    }

    // rain
    const auto targetDrops = (size_t) (sky.rainNorm() * 260.0f);
    while (drops.size() < targetDrops)
        drops.push_back ({ rng.nextFloat() * w, rng.nextFloat() * h, 500.0f + rng.nextFloat() * 300.0f, 8.0f + rng.nextFloat() * 10.0f });
    if (drops.size() > targetDrops) drops.resize (targetDrops);
    for (auto& d : drops)
    {
        d.y += d.speed * dt;
        d.x += dir * wind * 220.0f * dt;
        if (d.y > h) { d.y = -d.len; d.x = rng.nextFloat() * w; }
        if (d.x > w) d.x -= w;
        if (d.x < 0) d.x += w;
    }

    // snow
    const auto targetFlakes = (size_t) (sky.snowNorm() * 220.0f);
    while (flakes.size() < targetFlakes)
        flakes.push_back ({ rng.nextFloat() * w, rng.nextFloat() * h, 25.0f + rng.nextFloat() * 35.0f, rng.nextFloat() * 6.28f, 1.2f + rng.nextFloat() * 2.2f });
    if (flakes.size() > targetFlakes) flakes.resize (targetFlakes);
    for (auto& f : flakes)
    {
        f.y += f.speed * dt;
        f.x += (std::sin (time + f.drift) * 12.0f + dir * wind * 90.0f) * dt;
        if (f.y > h) { f.y = -4; f.x = rng.nextFloat() * w; }
        if (f.x > w) f.x -= w;
        if (f.x < 0) f.x += w;
    }

    for (auto& o : orbs)
    {
        o.life -= dt;
        if (o.layer == 1 && ! o.landed)
        {
            o.y += o.vy * dt;
            if (o.y >= h * 0.76f) { o.landed = true; o.life = std::min (o.life, 1.4f); o.maxLife = 1.4f; }
        }
        else if (o.layer != 1)
            o.y += o.vy * dt;
        o.x += dir * wind * 10.0f * dt;
    }
    orbs.erase (std::remove_if (orbs.begin(), orbs.end(), [] (const Orb& o) { return o.life <= 0; }), orbs.end());
}

void SkyView::drawHills (juce::Graphics& g, juce::Rectangle<float> b, juce::Colour c, float height, float seed)
{
    juce::Path p;
    p.startNewSubPath (b.getX(), b.getBottom());
    for (float x = 0; x <= b.getWidth(); x += 6.0f)
    {
        const float t = x / b.getWidth();
        const float y = b.getBottom() - height * (0.55f + 0.25f * std::sin (t * 5.0f + seed) + 0.2f * std::sin (t * 13.0f + seed * 2.0f));
        p.lineTo (b.getX() + x, y);
    }
    p.lineTo (b.getRight(), b.getBottom());
    p.closeSubPath();
    g.setColour (c);
    g.fillPath (p);
}

void SkyView::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const float w = b.getWidth(), h = b.getHeight();
    const float warm = sky.warmthNorm(), cloud = sky.cloudNorm(), rain = sky.rainNorm();
    const float grey = juce::jlimit (0.0f, 0.85f, cloud * 0.4f + rain * 0.3f + (sky.thunder ? 0.2f : 0.0f) + (sky.fog ? 0.3f : 0.0f));

    // sky colours: warm days glow peach and violet, cold days are icy, nights are deep purple
    const auto dayTop = mix (juce::Colour (0xff5d9bff), juce::Colour (0xff3a5bff), warm);
    const auto dayMid = mix (juce::Colour (0xffa9c9ff), juce::Colour (0xffb07cff), warm);
    const auto dayBot = mix (juce::Colour (0xffe8f1ff), juce::Colour (0xffffb38a), warm);
    const juce::Colour nightTop (0xff060a24), nightMid (0xff23175a), nightBot (0xff6b2f86);
    const juce::Colour greyTop (0xff3d4457), greyMid (0xff6a7186), greyBot (0xff9aa0b2);
    auto top = mix (mix (nightTop, dayTop, dayAmount), greyTop.withMultipliedBrightness (0.4f + 0.6f * dayAmount), grey);
    auto mid = mix (mix (nightMid, dayMid, dayAmount), greyMid.withMultipliedBrightness (0.4f + 0.6f * dayAmount), grey);
    auto bot = mix (mix (nightBot, dayBot, dayAmount), greyBot.withMultipliedBrightness (0.4f + 0.6f * dayAmount), grey);

    juce::ColourGradient grad (top, 0, 0, bot, 0, h, false);
    grad.addColour (0.55, mid);
    g.setGradientFill (grad);
    g.fillRect (b);

    // stars
    const float starAlpha = (1.0f - dayAmount) * (1.0f - cloud * 0.8f);
    if (starAlpha > 0.01f)
        for (auto& s : stars)
        {
            const float tw = 0.5f + 0.5f * std::sin (time * 1.7f + s.phase);
            g.setColour (juce::Colours::white.withAlpha (starAlpha * (0.3f + 0.7f * tw)));
            g.fillEllipse (s.x * w, s.y * h, 2.0f, 2.0f);
        }

    // sun or moon
    {
        const float alpha = 1.0f - cloud * 0.75f;
        const float cx = w * 0.8f, cy = h * 0.3f;
        if (dayAmount > 0.02f)
        {
            juce::ColourGradient sun (juce::Colour (0xfffff1b8).withAlpha (alpha * dayAmount), cx, cy,
                                      juce::Colour (0xffffb36b).withAlpha (0.0f), cx + 120.0f, cy, true);
            g.setGradientFill (sun);
            g.fillEllipse (cx - 120.0f, cy - 120.0f, 240.0f, 240.0f);
            g.setColour (juce::Colour (0xfffff6d8).withAlpha (alpha * dayAmount));
            g.fillEllipse (cx - 30.0f, cy - 30.0f, 60.0f, 60.0f);
        }
        if (dayAmount < 0.98f)
        {
            const float a = alpha * (1.0f - dayAmount);
            juce::ColourGradient glow (juce::Colour (0xffd9d4ff).withAlpha (a * 0.45f), cx, cy,
                                       juce::Colour (0xffd9d4ff).withAlpha (0.0f), cx + 80.0f, cy, true);
            g.setGradientFill (glow);
            g.fillEllipse (cx - 80.0f, cy - 80.0f, 160.0f, 160.0f);
            g.setColour (juce::Colour (0xfff4f1ff).withAlpha (a));
            g.fillEllipse (cx - 20.0f, cy - 20.0f, 40.0f, 40.0f);
            g.setColour (mid.withAlpha (a));
            g.fillEllipse (cx - 11.0f, cy - 24.0f, 40.0f, 40.0f);
        }
    }

    // glowing notes (behind the clouds, so chords bloom through them)
    for (auto& o : orbs)
    {
        if (o.layer == 1) continue;
        const float lifeT = o.life / o.maxLife;
        const float fadeIn = std::min (1.0f, (o.maxLife - o.life) * 1.5f);
        const float a = lifeT * fadeIn * (o.layer == 2 ? 0.35f : 0.75f);
        const auto c = juce::Colour::fromHSV (o.hue, 0.7f, 1.0f, a);
        const float r = o.r * (1.0f + (1.0f - lifeT) * 0.8f);
        juce::ColourGradient rg (c, o.x, o.y, c.withAlpha (0.0f), o.x + r, o.y, true);
        g.setGradientFill (rg);
        g.fillEllipse (o.x - r, o.y - r, r * 2, r * 2);
    }

    // clouds
    const auto visibleClouds = (size_t) std::round (cloud * (float) clouds.size());
    const auto cloudCol = mix (juce::Colours::white, juce::Colour (0xff59607a), grey * 0.8f + (1.0f - dayAmount) * 0.4f);
    for (size_t i = 0; i < visibleClouds; ++i)
    {
        const auto& c = clouds[i];
        const float cx = c.x * w, cy = c.y * h, cw = c.w * w, ch = c.h * h * 1.6f;
        for (int k = 0; k < 5; ++k)
        {
            const float ox = (k - 2) * cw * 0.18f, oy = std::abs (k - 2) * ch * 0.12f;
            const float rr = cw * (0.32f - std::abs (k - 2) * 0.05f);
            juce::ColourGradient cg (cloudCol.withAlpha (0.42f), cx + ox, cy + oy, cloudCol.withAlpha (0.0f), cx + ox + rr, cy + oy, true);
            g.setGradientFill (cg);
            g.fillEllipse (cx + ox - rr, cy + oy - rr * 0.6f, rr * 2, rr * 1.2f);
        }
    }

    // hills (the landscape the music lands on)
    drawHills (g, b, mix (bot, juce::Colour (0xff1b1440), 0.55f).withAlpha (0.85f), h * 0.3f, 1.3f);
    drawHills (g, b, mix (bot, juce::Colour (0xff0d0a26), 0.8f), h * 0.2f, 4.1f);

    // rain notes: falling sparks that splash into rings
    for (auto& o : orbs)
    {
        if (o.layer != 1) continue;
        const auto c = juce::Colour::fromHSV (o.hue, 0.65f, 1.0f, 1.0f);
        if (! o.landed)
        {
            juce::ColourGradient rg (c.withAlpha (0.95f), o.x, o.y, c.withAlpha (0.0f), o.x + o.r * 3, o.y, true);
            g.setGradientFill (rg);
            g.fillEllipse (o.x - o.r * 3, o.y - o.r * 3, o.r * 6, o.r * 6);
            g.setColour (c.withAlpha (0.35f));
            g.drawLine (o.x, o.y - 26.0f, o.x, o.y, 2.0f);
        }
        else
        {
            const float t = 1.0f - o.life / o.maxLife;
            const float rr = 6.0f + t * 46.0f;
            g.setColour (c.withAlpha ((1.0f - t) * 0.9f));
            g.drawEllipse (o.x - rr, o.y - rr * 0.3f, rr * 2, rr * 0.6f, 2.0f);
        }
    }

    // weather particles
    const float dirX = std::sin (juce::degreesToRadians (sky.windDirDeg)) >= 0 ? -1.0f : 1.0f;
    g.setColour (juce::Colours::white.withAlpha (0.35f));
    for (auto& d : drops)
        g.drawLine (d.x, d.y, d.x + dirX * sky.windNorm() * 8.0f, d.y + d.len, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (0.8f));
    for (auto& f : flakes)
        g.fillEllipse (f.x, f.y, f.r * 2, f.r * 2);

    if (sky.fog)
    {
        juce::ColourGradient fg (juce::Colours::white.withAlpha (0.0f), 0, h * 0.35f, juce::Colours::white.withAlpha (0.35f), 0, h, false);
        g.setGradientFill (fg);
        g.fillRect (b);
    }

    if (flash > 0.0f)
    {
        g.setColour (juce::Colours::white.withAlpha (flash * 0.35f));
        g.fillRect (b);
        g.setColour (juce::Colour (0xffe8f0ff).withAlpha (flash));
        g.strokePath (bolt, juce::PathStrokeType (2.5f));
    }

    // text
    auto text = b.reduced (24.0f, 18.0f);
    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.setFont (font (34.0f, true));
    g.drawText (title, text.translated (1, 2).removeFromTop (40), juce::Justification::topLeft);
    g.setColour (kText);
    g.drawText (title, text.removeFromTop (40), juce::Justification::topLeft);
    g.setFont (font (16.0f));
    g.setColour (kText.withAlpha (0.95f));
    g.drawText (line1, text.removeFromTop (24), juce::Justification::topLeft);
    g.setFont (font (13.0f));
    g.setColour (kText.withAlpha (0.75f));
    g.drawText (line2, text.removeFromTop (20), juce::Justification::topLeft);

    auto bottom = b.reduced (24.0f, 14.0f).removeFromBottom (26.0f);
    g.setFont (font (15.0f, true));
    g.setColour (kText);
    g.drawText (music, bottom, juce::Justification::bottomLeft);
}

//==============================================================================
// Look and feel

LivePlanetLookAndFeel::LivePlanetLookAndFeel()
{
    setColour (juce::Label::textColourId, kTextDim);
    setColour (juce::TextEditor::backgroundColourId, juce::Colours::black.withAlpha (0.3f));
    setColour (juce::TextEditor::textColourId, kText);
    setColour (juce::TextEditor::outlineColourId, juce::Colours::white.withAlpha (0.25f));
    setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::white.withAlpha (0.7f));
    setColour (juce::TextEditor::highlightColourId, juce::Colours::white.withAlpha (0.25f));
    setColour (juce::CaretComponent::caretColourId, kText);
    setColour (juce::ComboBox::textColourId, kText);
    setColour (juce::ComboBox::arrowColourId, kTextDim);
    setColour (juce::PopupMenu::backgroundColourId, kInkLight);
    setColour (juce::PopupMenu::textColourId, kText);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff3b3f86));
    setColour (juce::TextButton::textColourOffId, kText);
    setColour (juce::TextButton::textColourOnId, juce::Colour (0xff0d0f24));
    setColour (juce::ToggleButton::textColourId, kTextDim);
    setColour (juce::ToggleButton::tickColourId, juce::Colour::fromHSV (kHueSky, 0.6f, 1.0f, 1.0f));
    setColour (juce::ToggleButton::tickDisabledColourId, kTextDim);
    setColour (juce::BubbleComponent::backgroundColourId, kInkLight);
    setColour (juce::BubbleComponent::outlineColourId, juce::Colours::white.withAlpha (0.2f));
    setColour (juce::TooltipWindow::textColourId, kText);
}

void LivePlanetLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                              float start, float end, juce::Slider& s)
{
    const float hue = (float) (double) s.getProperties().getWithDefault ("hue", 0.5);
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
    const float r = std::min (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto c = bounds.getCentre();
    const float alpha = s.isEnabled() ? 1.0f : 0.35f;
    const float angle = start + pos * (end - start);

    juce::Path track;
    track.addCentredArc (c.x, c.y, r - 3, r - 3, 0, start, end, true);
    g.setColour (juce::Colours::white.withAlpha (0.08f * alpha));
    g.strokePath (track, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path value;
    value.addCentredArc (c.x, c.y, r - 3, r - 3, 0, start, angle, true);
    juce::ColourGradient vg (juce::Colour::fromHSV (hue, 0.75f, 1.0f, alpha), c.x - r, c.y + r,
                             juce::Colour::fromHSV (std::fmod (hue + 0.12f, 1.0f), 0.6f, 1.0f, alpha), c.x + r, c.y - r, false);
    g.setGradientFill (vg);
    g.strokePath (value, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float kr = r - 10.0f;
    juce::ColourGradient body (juce::Colour (0xff2a2e5c).withAlpha (alpha), c.x, c.y - kr,
                               juce::Colour (0xff12142e).withAlpha (alpha), c.x, c.y + kr, false);
    g.setGradientFill (body);
    g.fillEllipse (c.x - kr, c.y - kr, kr * 2, kr * 2);
    g.setColour (juce::Colours::white.withAlpha (0.08f * alpha));
    g.drawEllipse (c.x - kr, c.y - kr, kr * 2, kr * 2, 1.0f);

    const juce::Point<float> tip (c.x + (kr - 6) * std::sin (angle), c.y - (kr - 6) * std::cos (angle));
    g.setColour (juce::Colour::fromHSV (hue, 0.35f, 1.0f, alpha));
    g.fillEllipse (tip.x - 3, tip.y - 3, 6, 6);
}

void LivePlanetLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = b.getToggleState();
    if (on)
    {
        juce::ColourGradient gr (juce::Colour (0xff7cf5d4), r.getX(), r.getY(), juce::Colour (0xff8ab4ff), r.getRight(), r.getBottom(), false);
        g.setGradientFill (gr);
    }
    else
        g.setColour (juce::Colours::black.withAlpha (down ? 0.45f : (over ? 0.38f : 0.3f)));
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    if (! on)
    {
        g.setColour (juce::Colours::white.withAlpha (over ? 0.5f : 0.25f));
        g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
    }
}

void LivePlanetLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (juce::Colours::white.withAlpha (box.isMouseOver (true) ? 0.1f : 0.06f));
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (juce::Colours::white.withAlpha (0.15f));
    g.drawRoundedRectangle (r, 8.0f, 1.0f);
    juce::Path arrow;
    const float ax = (float) w - 16.0f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 4, ay - 2, ax + 4, ay - 2, ax, ay + 3);
    g.setColour (kTextDim);
    g.fillPath (arrow);
}

juce::Font LivePlanetLookAndFeel::getComboBoxFont (juce::ComboBox&) { return font (14.0f); }
juce::Font LivePlanetLookAndFeel::getTextButtonFont (juce::TextButton&, int) { return font (13.0f, true); }

//==============================================================================
// Editor

LivePlanetEditor::LivePlanetEditor (LivePlanetProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (skyView);

    cityBox.setTextToShowWhenEmpty ("Search any city...", kText.withAlpha (0.6f));
    cityBox.setFont (font (15.0f));
    cityBox.setIndents (12, 7);
    cityBox.onReturnKey = [this] { runSearch(); };
    addAndMakeVisible (cityBox);
    goButton.onClick = [this] { runSearch(); };
    addAndMakeVisible (goButton);

    liveButton.setClickingTogglesState (true);
    liveButton.setTooltip ("On: the real weather plays. Off: make your own weather with the Sky knobs.");
    addAndMakeVisible (liveButton);
    liveAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.params, "live", liveButton);

    // the sky
    addKnob ("temp", "Temp", kHueSky);
    addKnob ("rain", "Rain", kHueSky);
    addKnob ("snow", "Snow", kHueSky);
    addKnob ("wind", "Wind", kHueSky);
    addKnob ("clouds", "Clouds", kHueSky);
    addKnob ("pressure", "Pressure", kHueSky);
    addKnob ("humidity", "Humidity", kHueSky);
    addAndMakeVisible (dayToggle);
    addAndMakeVisible (thunderToggle);
    dayAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.params, "daylight", dayToggle);
    thunderAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.params, "thunder", thunderToggle);

    // the music
    auto setupCombo = [this] (juce::ComboBox& box, juce::Label& label, const juce::String& name,
                              const juce::StringArray& items)
    {
        box.addItemList (items, 1);
        addAndMakeVisible (box);
        label.setText (name, juce::dontSendNotification);
        label.setFont (font (12.0f, true));
        addAndMakeVisible (label);
    };
    juce::StringArray keys { "Auto (from the city)" };
    for (int i = 0; i < 12; ++i) keys.add (lp::noteName (i));
    setupCombo (soundBox, soundLabel, "SOUND", LivePlanetProcessor::soundNames());
    setupCombo (keyBox, keyLabel, "KEY", keys);
    setupCombo (scaleBox, scaleLabel, "SCALE", { "Auto (from the weather)", "Major", "Lydian", "Mixolydian", "Dorian", "Minor" });
    soundAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.params, "sound", soundBox);
    keyAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.params, "key", keyBox);
    scaleAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.params, "scale", scaleBox);

    loadButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Choose a sound for Live Planet to play", juce::File(),
                                                       "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              {
                                  if (fc.getResult().existsAsFile()) proc.loadSample (fc.getResult());
                              });
    };
    addAndMakeVisible (loadButton);
    sampleLabel.setFont (font (12.0f));
    sampleLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (sampleLabel);

    addKnob ("pad", "Chords", kHueMusic);
    addKnob ("drops", "Melody", kHueMusic);
    addKnob ("bass", "Bass", kHueMusic);

    // the sound
    addKnob ("grain", "Grain", kHueSound);
    addKnob ("texture", "Scatter", kHueSound);
    addKnob ("sparkle", "Sparkle", kHueSound);
    addKnob ("position", "Position", kHueSound);
    addKnob ("warmth", "Warmth", kHueSound);
    addKnob ("echo", "Echo", kHueSound);
    addKnob ("space", "Space", kHueSound);
    addKnob ("shimmer", "Shimmer", kHueSound);
    addKnob ("atmos", "Atmos", kHueSound);
    addKnob ("gain", "Volume", kHueSound);

    setSize (980, 650);
    lastTick = juce::Time::getMillisecondCounterHiRes();
    startTimerHz (40);
    timerCallback();
}

LivePlanetEditor::~LivePlanetEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

LivePlanetEditor::Knob& LivePlanetEditor::addKnob (const char* id, const juce::String& name, float hue)
{
    auto k = std::make_unique<Knob>();
    k->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k->slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    k->slider.setPopupDisplayEnabled (true, true, this);
    k->slider.getProperties().set ("hue", hue);
    k->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.params, id, k->slider);
    k->label.setText (name, juce::dontSendNotification);
    k->label.setJustificationType (juce::Justification::centred);
    k->label.setFont (font (12.0f));
    addAndMakeVisible (k->slider);
    addAndMakeVisible (k->label);
    auto* raw = k.get();
    knobById[id] = raw;
    knobs.push_back (std::move (k));
    return *raw;
}

void LivePlanetEditor::runSearch()
{
    const auto text = cityBox.getText().trim();
    if (text.isEmpty()) return;
    proc.searchCity (text);
    if (auto* live = proc.params.getParameter ("live"))
        live->setValueNotifyingHost (1.0f);
    cityBox.setText ({}, false);
    cityBox.giveAwayKeyboardFocus();
}

void LivePlanetEditor::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const float dt = (float) juce::jlimit (0.0, 0.1, (now - lastTick) * 0.001);
    lastTick = now;

    lp::VisualEvent e;
    while (proc.engine.popVisual (e))
        skyView.addNote (e);

    const auto status = proc.getSkyStatus();
    skyView.setSky (proc.engine.getCurrentWeather()); // the picture morphs smoothly...
    const lp::Weather w = proc.engine.getTargetWeather(); // ...the numbers show the real sky

    juce::String title, line1, line2;
    const bool usingLive = status.live && status.haveLive;
    if (usingLive)
    {
        title = status.city;
        line1 = describeLine (w);
        const auto mins = (int) ((juce::Time::getCurrentTime() - status.fetchedAt).inMinutes());
        line2 = (status.country.isNotEmpty() ? status.country + "  -  " : juce::String())
              + "Live weather, updated " + (mins < 1 ? juce::String ("just now") : juce::String (mins) + " min ago");
    }
    else
    {
        title = status.live ? status.city : juce::String ("Your own sky");
        line1 = describeLine (w);
        line2 = status.live ? juce::String ("Connecting to the live weather... (using the Sky knobs until then)")
                            : juce::String ("Live is off: the Sky knobs are the weather. Turn LIVE on to follow a real city.");
    }
    if (status.busy) line2 = "Checking the weather...";
    if (status.message.isNotEmpty()) line2 = status.message;

    const int chordRoot = proc.engine.uiChordRoot.load();
    const int quality = proc.engine.uiChordMinor.load();
    juce::String chord = quality < 0 ? juce::String ("your chord")
                                     : juce::String (lp::noteName (chordRoot)) + (quality == 1 ? "m" : (quality == 2 ? "sus2" : ""));
    const juce::String musicLine = juce::String (lp::profileFor ((lp::SoundId) proc.engine.uiSound.load()).name) + "   -   "
                                 + juce::String (lp::noteName (proc.engine.uiKey.load())) + " "
                                 + lp::getScale ((lp::ScaleId) proc.engine.uiScale.load()).name
                                 + "   -   now playing " + chord
                                 + "   -   " + juce::String (juce::roundToInt (proc.engine.uiBpm.load())) + " BPM";
    skyView.setText (title, line1, line2, musicLine);
    skyView.advance (dt);
    skyView.repaint();

    // the Sky knobs only matter when LIVE is off
    const bool manual = ! status.live;
    for (auto id : { "temp", "rain", "snow", "wind", "clouds", "pressure", "humidity" })
        knobById[id]->slider.setEnabled (manual);
    dayToggle.setEnabled (manual);
    thunderToggle.setEnabled (manual);

    const auto sampleName = proc.getSampleName();
    sampleLabel.setText (sampleName.isEmpty() ? "or drag in a file" : "Sample: " + sampleName, juce::dontSendNotification);

    if (wasManual != (int) manual) { wasManual = (int) manual; repaint (skyPanel); }
}

void LivePlanetEditor::paint (juce::Graphics& g)
{
    juce::ColourGradient bg (kInk, 0, (float) skyPanel.getY(), juce::Colour (0xff070816), 0, (float) getHeight(), false);
    g.setGradientFill (bg);
    g.fillAll();

    auto drawPanel = [&g] (juce::Rectangle<int> r, const juce::String& title, float hue)
    {
        auto rf = r.toFloat();
        g.setColour (juce::Colours::white.withAlpha (0.035f));
        g.fillRoundedRectangle (rf, 14.0f);
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.drawRoundedRectangle (rf.reduced (0.5f), 14.0f, 1.0f);
        g.setColour (juce::Colour::fromHSV (hue, 0.55f, 1.0f, 1.0f));
        g.fillEllipse (rf.getX() + 16, rf.getY() + 18, 7, 7);
        g.setColour (kText);
        g.setFont (font (13.0f, true));
        g.drawText (title, (int) rf.getX() + 30, (int) rf.getY() + 11, 200, 20, juce::Justification::centredLeft);
    };
    drawPanel (skyPanel, "THE SKY", kHueSky);
    drawPanel (musicPanel, "THE MUSIC", kHueMusic);
    drawPanel (soundPanel, "THE SOUND", kHueSound);
}

void LivePlanetEditor::paintOverChildren (juce::Graphics& g)
{
    if (proc.params.getRawParameterValue ("live")->load() > 0.5f)
    {
        auto r = skyPanel.reduced (12).withTrimmedTop (32).toFloat();
        g.setColour (kInk.withAlpha (0.82f));
        g.fillRoundedRectangle (r, 10.0f);
        g.setColour (kText.withAlpha (0.85f));
        g.setFont (font (13.0f));
        g.drawFittedText ("Following the real sky.\nTurn LIVE off to make your own weather.",
                          r.toNearestInt().reduced (16), juce::Justification::centred, 3);
    }
    if (dragging)
    {
        g.setColour (juce::Colour (0xff7cf5d4).withAlpha (0.25f));
        g.fillRect (getLocalBounds());
        g.setColour (kText);
        g.setFont (font (22.0f, true));
        g.drawText ("Drop to play this sound with the weather", getLocalBounds(), juce::Justification::centred);
    }
}

void LivePlanetEditor::layoutKnobs (std::initializer_list<Knob*> list, juce::Rectangle<int> area, int cols)
{
    const int rows = ((int) list.size() + cols - 1) / cols;
    const int cw = area.getWidth() / cols, ch = area.getHeight() / rows;
    int i = 0;
    for (auto* k : list)
    {
        auto cell = juce::Rectangle<int> (area.getX() + (i % cols) * cw, area.getY() + (i / cols) * ch, cw, ch);
        k->label.setBounds (cell.removeFromBottom (18));
        const int size = std::min (cell.getWidth(), cell.getHeight()) - 2;
        k->slider.setBounds (cell.withSizeKeepingCentre (size, size));
        ++i;
    }
}

void LivePlanetEditor::resized()
{
    auto b = getLocalBounds();
    auto sky = b.removeFromTop (300);
    skyView.setBounds (sky);

    auto bar = sky.reduced (20, 18).removeFromTop (34);
    liveButton.setBounds (bar.removeFromRight (70));
    bar.removeFromRight (10);
    goButton.setBounds (bar.removeFromRight (48));
    bar.removeFromRight (6);
    cityBox.setBounds (bar.removeFromRight (220));

    b.reduce (14, 12);
    skyPanel = b.removeFromLeft (318);
    b.removeFromLeft (12);
    musicPanel = b.removeFromLeft (264);
    b.removeFromLeft (12);
    soundPanel = b;

    auto k = [this] (const char* id) { return knobById[id]; };

    {
        auto r = skyPanel.reduced (12);
        auto head = r.removeFromTop (24);
        thunderToggle.setBounds (head.removeFromRight (84));
        dayToggle.setBounds (head.removeFromRight (86));
        r.removeFromTop (8);
        layoutKnobs ({ k ("temp"), k ("rain"), k ("snow"), k ("wind"), k ("clouds"), k ("pressure"), k ("humidity") }, r, 4);
    }
    {
        auto r = musicPanel.reduced (14);
        r.removeFromTop (30);
        auto row = [&r] (int h) { auto x = r.removeFromTop (h); r.removeFromTop (6); return x; };
        auto soundRow = row (26);
        soundLabel.setBounds (soundRow.removeFromLeft (52));
        soundBox.setBounds (soundRow);
        auto sampleRow = row (24).withTrimmedLeft (52);
        loadButton.setBounds (sampleRow.removeFromLeft (100));
        sampleRow.removeFromLeft (8);
        sampleLabel.setBounds (sampleRow);
        auto keyRow = row (26);
        keyLabel.setBounds (keyRow.removeFromLeft (52));
        keyBox.setBounds (keyRow);
        auto scaleRow = row (26);
        scaleLabel.setBounds (scaleRow.removeFromLeft (52));
        scaleBox.setBounds (scaleRow);
        layoutKnobs ({ k ("pad"), k ("drops"), k ("bass") }, r, 3);
    }
    {
        auto r = soundPanel.reduced (12);
        r.removeFromTop (32);
        layoutKnobs ({ k ("grain"), k ("texture"), k ("sparkle"), k ("position"), k ("warmth"),
                       k ("echo"), k ("space"), k ("shimmer"), k ("atmos"), k ("gain") }, r, 5);
    }
}

bool LivePlanetEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;mp3;ogg"))
            return true;
    return false;
}

void LivePlanetEditor::filesDropped (const juce::StringArray& files, int, int)
{
    dragging = false;
    for (auto& f : files)
        if (proc.loadSample (juce::File (f)))
            break;
    repaint();
}

juce::AudioProcessorEditor* LivePlanetProcessor::createEditor()
{
    return new LivePlanetEditor (*this);
}
