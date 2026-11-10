#include "WeatherService.h"

namespace
{
    constexpr int kRefreshMs = 15 * 60 * 1000;

    juce::var getJson (const juce::URL& url, juce::String& error)
    {
        auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                           .withConnectionTimeoutMs (8000)
                           .withExtraHeaders ("User-Agent: LivePlanet/0.1");
        // connections can drop for a moment, so try a few times before giving up
        for (int attempt = 0; attempt < 4; ++attempt)
        {
            if (attempt > 0)
                juce::Thread::sleep (600 * attempt);
            std::unique_ptr<juce::InputStream> in (url.createInputStream (options));
            if (in == nullptr)
                continue;
            auto parsed = juce::JSON::parse (in->readEntireStreamAsString());
            if (parsed.isObject())
            {
                error = {};
                return parsed;
            }
        }
        error = "Couldn't reach the weather service. Check your internet connection.";
        return {};
    }

    float num (const juce::var& obj, const char* key, float fallback)
    {
        auto v = obj.getProperty (key, {});
        return v.isVoid() ? fallback : (float) (double) v;
    }
}

WeatherService::WeatherService() : juce::Thread ("Live Planet weather")
{
    startThread (juce::Thread::Priority::low);
}

WeatherService::~WeatherService()
{
    signalThreadShouldExit();
    notify();
    stopThread (10000);
}

void WeatherService::requestCity (const juce::String& name)
{
    {
        const juce::ScopedLock sl (lock);
        pendingCity = name.trim();
        hasPendingName = pendingCity.isNotEmpty();
    }
    notify();
}

void WeatherService::requestCoordinates (const juce::String& city, double lat, double lon)
{
    {
        const juce::ScopedLock sl (lock);
        pendingCity = city;
        pendingLat = lat;
        pendingLon = lon;
        hasPendingCoords = true;
    }
    notify();
}

bool WeatherService::takeResult (Result& out)
{
    const juce::ScopedLock sl (lock);
    if (! hasNew) return false;
    out = latest;
    hasNew = false;
    return true;
}

WeatherService::Result WeatherService::fetchWeather (const juce::String& city, const juce::String& country,
                                                     double lat, double lon)
{
    Result r;
    r.city = city;
    r.country = country;
    r.lat = lat;
    r.lon = lon;

    auto url = juce::URL ("https://api.open-meteo.com/v1/forecast")
                   .withParameter ("latitude", juce::String (lat, 4))
                   .withParameter ("longitude", juce::String (lon, 4))
                   .withParameter ("current", "temperature_2m,relative_humidity_2m,is_day,precipitation,rain,"
                                              "showers,snowfall,weather_code,cloud_cover,pressure_msl,"
                                              "wind_speed_10m,wind_direction_10m")
                   .withParameter ("timezone", "auto");

    auto json = getJson (url, r.error);
    auto cur = json.getProperty ("current", {});
    if (! cur.isObject())
    {
        if (r.error.isEmpty()) r.error = "Weather not available right now";
        return r;
    }

    lp::Weather w;
    w.temperatureC = num (cur, "temperature_2m", 15.0f);
    w.humidityPct  = num (cur, "relative_humidity_2m", 60.0f);
    w.isDay        = num (cur, "is_day", 1.0f) > 0.5f;
    w.rainMm       = num (cur, "rain", 0.0f) + num (cur, "showers", 0.0f);
    if (w.rainMm <= 0.0f) w.rainMm = num (cur, "precipitation", 0.0f) * 0.5f;
    w.snowCm       = num (cur, "snowfall", 0.0f);
    w.weatherCode  = (int) num (cur, "weather_code", 0.0f);
    w.cloudPct     = num (cur, "cloud_cover", 30.0f);
    w.pressureHpa  = num (cur, "pressure_msl", 1013.0f);
    w.windKmh      = num (cur, "wind_speed_10m", 8.0f);
    w.windDirDeg   = num (cur, "wind_direction_10m", 270.0f);
    w.applyCodeFlags();

    r.weather = w;
    r.ok = true;
    r.fetchedAt = juce::Time::getCurrentTime();
    return r;
}

void WeatherService::run()
{
    while (! threadShouldExit())
    {
        juce::String name, city, country;
        double lat = 0, lon = 0;
        bool lookUpName = false, fetch = false;
        {
            const juce::ScopedLock sl (lock);
            if (hasPendingName)          { name = pendingCity; lookUpName = true; hasPendingName = false; }
            else if (hasPendingCoords)   { city = pendingCity; lat = pendingLat; lon = pendingLon; fetch = true; hasPendingCoords = false; }
            else if (hasLocation)        { city = currentCity; country = currentCountry; lat = currentLat; lon = currentLon; fetch = true; }
        }

        if (lookUpName || fetch)
        {
            busy = true;
            Result r;
            if (lookUpName)
            {
                auto url = juce::URL ("https://geocoding-api.open-meteo.com/v1/search")
                               .withParameter ("name", name)
                               .withParameter ("count", "1")
                               .withParameter ("language", "en");
                auto json = getJson (url, r.error);
                auto results = json.getProperty ("results", {});
                if (results.isArray() && results.size() > 0)
                {
                    auto first = results[0];
                    city = first.getProperty ("name", name).toString();
                    country = first.getProperty ("country", "").toString();
                    lat = (double) first.getProperty ("latitude", 0.0);
                    lon = (double) first.getProperty ("longitude", 0.0);
                    fetch = true;
                }
                else
                {
                    r.city = name;
                    if (r.error.isEmpty()) r.error = "Couldn't find \"" + name + "\". Try another spelling.";
                }
            }

            if (fetch)
            {
                r = fetchWeather (city, country, lat, lon);
                if (r.ok)
                {
                    const juce::ScopedLock sl (lock);
                    currentCity = city; currentCountry = country;
                    currentLat = lat; currentLon = lon;
                    hasLocation = true;
                }
            }

            {
                const juce::ScopedLock sl (lock);
                latest = r;
                hasNew = true;
            }
            busy = false;
        }

        bool pendingWork;
        {
            const juce::ScopedLock sl (lock);
            pendingWork = hasPendingName || hasPendingCoords;
        }
        if (! pendingWork)
            wait (kRefreshMs);
    }
}
