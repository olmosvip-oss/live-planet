#pragma once
#include <juce_graphics/juce_graphics.h>
#include <vector>

// Paints soft, grass-covered topiary shapes: thousands of tiny blades over a
// shaded base, with fuzzy edges where the blades stick out past the outline.
namespace lp::art
{

struct Shape { float x, y, w, h, r; };

inline float sdShape (float px, float py, const Shape& s)
{
    const float cx = s.x + s.w * 0.5f, cy = s.y + s.h * 0.5f;
    const float qx = std::abs (px - cx) - s.w * 0.5f + s.r;
    const float qy = std::abs (py - cy) - s.h * 0.5f + s.r;
    const float ox = std::max (qx, 0.0f), oy = std::max (qy, 0.0f);
    return std::sqrt (ox * ox + oy * oy) + std::min (std::max (qx, qy), 0.0f) - s.r;
}

struct Grass
{
    juce::Colour light, base, dark;
};

inline juce::Image makeTopiary (int w, int h, const std::vector<Shape>& shapes, const Grass& gr,
                                juce::uint32 seed, float scale, float density = 0.16f)
{
    const int W = juce::roundToInt ((float) w * scale), H = juce::roundToInt ((float) h * scale);
    juce::Image img (juce::Image::ARGB, W, H, true);
    juce::Image::BitmapData bd (img, juce::Image::BitmapData::readWrite);

    auto nearest = [&shapes] (float px, float py, int& idx)
    {
        float best = 1e9f;
        for (int i = 0; i < (int) shapes.size(); ++i)
        {
            const float d = sdShape (px, py, shapes[(size_t) i]);
            if (d < best) { best = d; idx = i; }
        }
        return best;
    };

    // light from the top: lighter on top of each block, darker underneath and at the lower edges
    auto shade = [&] (float px, float py, float sd, int idx)
    {
        const auto& s = shapes[(size_t) idx];
        const float t = juce::jlimit (0.0f, 1.0f, (py - s.y) / s.h);
        auto c = t < 0.5f ? gr.light.interpolatedWith (gr.base, t * 2.0f)
                          : gr.base.interpolatedWith (gr.dark, (t - 0.5f) * 2.0f);
        const float edge = juce::jlimit (0.0f, 1.0f, -sd / 18.0f);
        if (t > 0.5f) c = c.interpolatedWith (gr.dark.darker (0.4f), (1.0f - edge) * 0.5f);
        else          c = c.interpolatedWith (gr.light.brighter (0.15f), (1.0f - edge) * 0.25f);
        return c;
    };

    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
        {
            const float px = (float) x / scale, py = (float) y / scale;
            int idx = 0;
            const float sd = nearest (px, py, idx);
            if (sd > 1.0f) continue;
            const float a = juce::jlimit (0.0f, 1.0f, 0.5f - sd);
            bd.setPixelColour (x, y, shade (px, py, sd, idx).withAlpha (a));
        }

    juce::Random rng ((juce::int64) seed);
    const int blades = (int) ((float) W * (float) H * density);
    for (int i = 0; i < blades; ++i)
    {
        const float x0 = rng.nextFloat() * (float) W, y0 = rng.nextFloat() * (float) H;
        const float px = x0 / scale, py = y0 / scale;
        int idx = 0;
        const float sd = nearest (px, py, idx);
        if (sd > 0.0f) continue;
        const bool rim = sd > -7.0f;

        float ang = rng.nextFloat() * juce::MathConstants<float>::twoPi;
        if (rim)
        {
            // blades on the rim point outwards: that's what makes the edge fuzzy
            const float gx = nearest (px + 1.0f, py, idx) - sd;
            const float gy = nearest (px, py + 1.0f, idx) - sd;
            nearest (px, py, idx);
            ang = std::atan2 (gy, gx) + (rng.nextFloat() - 0.5f) * 1.4f;
        }
        const float len = (2.0f + rng.nextFloat() * (rim ? 6.0f : 3.5f)) * scale;
        auto c = shade (px, py, sd, idx);
        const float b = 0.65f + rng.nextFloat() * 0.75f;
        c = b > 1.0f ? c.brighter ((b - 1.0f) * 1.2f) : c.darker ((1.0f - b) * 1.4f);
        c = c.withRotatedHue ((rng.nextFloat() - 0.5f) * 0.03f);

        const float dx = std::cos (ang), dy = std::sin (ang);
        const int steps = juce::jmax (2, (int) len);
        for (int k = 0; k < steps; ++k)
        {
            const int x = (int) (x0 + dx * (float) k), y = (int) (y0 + dy * (float) k);
            if (x < 0 || y < 0 || x >= W || y >= H) break;
            const float f = (float) k / (float) steps;
            const auto tip = c.brighter (f * 0.35f);
            const auto old = bd.getPixelColour (x, y);
            const float a = 0.85f * (1.0f - f * 0.35f);
            const float oa = old.getFloatAlpha();
            const float na = a + oa * (1.0f - a);
            if (na <= 0.0f) continue;
            auto mixed = old.withAlpha (1.0f).interpolatedWith (tip, oa > 0.0f ? a / na : 1.0f);
            bd.setPixelColour (x, y, mixed.withAlpha (na));
        }
    }
    return img;
}

} // namespace lp::art
