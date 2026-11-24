#pragma once
#include <juce_core/juce_core.h>
#include <vector>

// What the Voice page knows about the local voice helper (a separate app on this computer that runs the
// singing models; nothing of it is inside the plugin). Filled in by the background client; read by the page.
namespace lp::voice
{
struct Result { int n = 0; juce::String vocal, mix, heard; int words = 0, key = 0; };
struct Job
{
    juce::String id, state, stage, note, title, error;
    double created = 0.0;
    bool clean = true;
    std::vector<Result> results;
    bool live() const { return state == "queued" || state == "running"; }
};
struct Snapshot
{
    bool connected = false;          // the helper answered just now
    juce::String problem;            // why not, in plain words
    juce::StringArray voices, tracks;
    std::vector<std::pair<juce::String, juce::String>> stages; // (key, what it says)
    std::vector<Job> jobs;           // newest first
    int serial = 0;                  // goes up with every change
};
} // namespace lp::voice
