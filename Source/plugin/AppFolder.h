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
}
