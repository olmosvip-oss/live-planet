#pragma once
#include <juce_core/juce_core.h>
#include <cmath>

namespace lp
{

// A snapshot of the sky. Units match what Open-Meteo reports.
struct Weather
{
    float temperatureC = 16.0f;  // °C
    float rainMm       = 0.6f;   // mm of rain in the last hour
    float snowCm       = 0.0f;   // cm of snow in the last hour
    float windKmh      = 12.0f;  // km/h
    float windDirDeg   = 250.0f; // where the wind comes from, degrees
    float cloudPct     = 55.0f;  // 0..100
    float pressureHpa  = 1012.0f;
    float humidityPct  = 70.0f;
    bool  isDay        = true;
    bool  thunder      = false;
    bool  fog          = false;
    int   weatherCode  = 61;     // WMO weather code

    // 0..1 helpers the music brain uses
    float rainNorm() const     { return juce::jlimit (0.0f, 1.0f, std::log1p (rainMm * 3.0f) / std::log1p (24.0f)); }
    float snowNorm() const     { return juce::jlimit (0.0f, 1.0f, std::log1p (snowCm * 4.0f) / std::log1p (8.0f)); }
    float windNorm() const     { return juce::jlimit (0.0f, 1.0f, windKmh / 50.0f); }
    float cloudNorm() const    { return juce::jlimit (0.0f, 1.0f, cloudPct / 100.0f); }
    float humidityNorm() const { return juce::jlimit (0.0f, 1.0f, humidityPct / 100.0f); }
    float warmthNorm() const   { return juce::jlimit (0.0f, 1.0f, (temperatureC + 5.0f) / 35.0f); }

    // How "tense" the sky is: low pressure and storms push it up.
    float tension() const
    {
        float t = juce::jlimit (0.0f, 1.0f, (1022.0f - pressureHpa) / 26.0f);
        t += rainNorm() * 0.2f + (thunder ? 0.35f : 0.0f);
        return juce::jlimit (0.0f, 1.0f, t);
    }

    // How bright and happy the sky feels: warm, sunny and dry is high.
    float mood() const
    {
        float m = warmthNorm() * 0.75f + (isDay ? 0.15f : -0.05f) - cloudNorm() * 0.15f
                - rainNorm() * 0.1f - snowNorm() * 0.1f + 0.1f;
        return juce::jlimit (0.0f, 1.0f, m);
    }

    static juce::String describeCode (int code)
    {
        if (code == 0) return "Clear sky";
        if (code == 1) return "Mostly clear";
        if (code == 2) return "Partly cloudy";
        if (code == 3) return "Overcast";
        if (code == 45 || code == 48) return "Fog";
        if (code >= 51 && code <= 57) return "Drizzle";
        if (code >= 61 && code <= 67) return code >= 65 ? "Heavy rain" : "Rain";
        if (code >= 71 && code <= 77) return "Snow";
        if (code >= 80 && code <= 82) return "Rain showers";
        if (code >= 85 && code <= 86) return "Snow showers";
        if (code >= 95) return "Thunderstorm";
        return "Weather";
    }

    // A plain-language description that also works for hand-made weather.
    juce::String describe() const
    {
        juce::String s;
        if (thunder)                 s = "Thunderstorm";
        else if (snowCm > 0.05f)     s = snowCm > 1.0f ? "Heavy snow" : "Snow";
        else if (rainMm > 4.0f)      s = "Heavy rain";
        else if (rainMm > 0.6f)      s = "Rain";
        else if (rainMm > 0.05f)     s = "Drizzle";
        else if (fog)                s = "Fog";
        else if (cloudPct > 85.0f)   s = "Overcast";
        else if (cloudPct > 40.0f)   s = "Partly cloudy";
        else                         s = isDay ? "Clear sky" : "Clear night";
        if (windKmh > 35.0f)         s << ", windy";
        return s;
    }

    void applyCodeFlags()
    {
        thunder = weatherCode >= 95;
        fog = weatherCode == 45 || weatherCode == 48;
    }

    // Glide toward another sky so weather changes sound smooth.
    void glideTo (const Weather& t, float a)
    {
        auto g = [a] (float& v, float target) { v += (target - v) * a; };
        g (temperatureC, t.temperatureC);
        g (rainMm, t.rainMm);
        g (snowCm, t.snowCm);
        g (windKmh, t.windKmh);
        g (cloudPct, t.cloudPct);
        g (pressureHpa, t.pressureHpa);
        g (humidityPct, t.humidityPct);
        // wind direction: glide the short way round
        float d = std::remainder (t.windDirDeg - windDirDeg, 360.0f);
        windDirDeg = std::fmod (windDirDeg + d * a + 360.0f, 360.0f);
        isDay = t.isDay;
        thunder = t.thunder;
        fog = t.fog;
        weatherCode = t.weatherCode;
    }
};

} // namespace lp
