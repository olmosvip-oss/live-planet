#pragma once
#include <juce_core/juce_core.h>
#include "../engine/Weather.h"

// Looks up a city and fetches its current weather from Open-Meteo on a
// background thread, then refreshes it every 15 minutes.
class WeatherService : private juce::Thread
{
public:
    struct Result
    {
        bool ok = false;
        juce::String city, country, error;
        double lat = 0, lon = 0;
        lp::Weather weather;
        juce::Time fetchedAt;
    };

    WeatherService();
    ~WeatherService() override;

    void requestCity (const juce::String& name);
    void requestCoordinates (const juce::String& city, double lat, double lon);
    bool takeResult (Result& out); // true when a new result arrived
    bool isBusy() const { return busy.load(); }

private:
    void run() override;
    Result fetchWeather (const juce::String& city, const juce::String& country, double lat, double lon);

    juce::CriticalSection lock;
    juce::String pendingCity, currentCity, currentCountry;
    double pendingLat = 0, pendingLon = 0, currentLat = 0, currentLon = 0;
    bool hasPendingName = false, hasPendingCoords = false, hasLocation = false;
    Result latest;
    bool hasNew = false;
    std::atomic<bool> busy { false };
};
