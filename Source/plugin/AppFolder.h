#pragma once
#include <juce_core/juce_core.h>

// Where Loopwright keeps its things: ~/Music/Loopwright (songs, the library cache, your sample folders).
// The first time, an older "Live Planet" folder is renamed to it, so nothing is lost.
namespace lp
{
inline juce::File appFolder()
{
    const auto music = juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    auto folder = music.getChildFile ("Loopwright");
    if (! folder.exists())
    {
        const auto old = music.getChildFile ("Live Planet");
        if (old.isDirectory()) old.moveFileTo (folder);
    }
    return folder;
}

// small settings kept for this computer (not per project), e.g. that you've done the tour
inline juce::var setting (const juce::String& key, const juce::var& otherwise = {})
{
    const auto v = juce::JSON::parse (appFolder().getChildFile ("settings.json"));
    return v.isObject() && v.hasProperty (key) ? v[key.toRawUTF8()] : otherwise;
}
inline void setSetting (const juce::String& key, const juce::var& value)
{
    const auto f = appFolder().getChildFile ("settings.json");
    auto v = juce::JSON::parse (f);
    if (! v.isObject()) v = juce::var (new juce::DynamicObject());
    v.getDynamicObject()->setProperty (key, value);
    f.getParentDirectory().createDirectory();
    f.replaceWithText (juce::JSON::toString (v));
}
}
