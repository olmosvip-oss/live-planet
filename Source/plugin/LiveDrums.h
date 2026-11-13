#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <map>
#include <memory>
#include <vector>

// Plays a drum track's hits straight from the pattern you're drawing: each click is heard at once
// (audition), and while the loop plays, every hit you add or erase is heard on the next pass,
// without waiting for the track to be re-recorded.
class LiveDrums
{
public:
    struct Hit { double beat; int note; float vel; double len = 0.0; }; // len: beats it's held (0: the whole sound, like a drum)
    using Kit = std::map<int, std::shared_ptr<juce::AudioBuffer<float>>>;
    static constexpr double kitRate = 48000.0; // the hits are made at the song's rate

    // ---- message thread (or any thread that isn't the audio one)
    void setKit (Kit k)
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        std::swap (kit, k);
        for (auto& v : voices) v.active = false; // nothing keeps pointing at the old sounds
        // `k` (the old kit) is freed here, after the lock
    }
    bool hasKit() const { const juce::SpinLock::ScopedLockType sl (lock); return ! kit.empty(); }

    void setPattern (std::vector<Hit> h, double loopStartBeat, double loopBeats)
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        std::swap (hits, h);
        loopStart = loopStartBeat;
        loopLen = std::max (0.25, loopBeats);
    }

    void audition (int note, float vel, double seconds = 0.0)
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        if (pending.size() < 16) pending.push_back ({ 0.0, note, vel, seconds });
    }

    // ---- audio thread. Adds into L/R. When `sequence` is on, plays the pattern's hits that fall in
    // [b0, b1) beats (wrapping at the loop's end). `gain` is the track's volume.
    void process (float* L, float* R, int n, double hostRate, bool sequence, double b0, double b1, float gain)
    {
        const juce::SpinLock::ScopedTryLockType tl (lock);
        if (! tl.isLocked()) return;
        for (auto& p : pending) start (p.note, p.vel, 0, 1.0f, p.len > 0.0 ? (int) (p.len * hostRate) : -1);
        pending.clear();
        if (sequence && b1 > b0)
        {
            const double perSample = (b1 - b0) / n, loopEnd = loopStart + loopLen;
            auto held = [perSample] (const Hit& h) { return h.len > 0.0 ? (int) (h.len / perSample) : -1; };
            for (auto& h : hits)
            {
                if (h.beat >= b0 && h.beat < b1) start (h.note, h.vel, (int) ((h.beat - b0) / perSample), gain, held (h));
                if (b1 > loopEnd && h.beat >= loopStart && h.beat < loopStart + (b1 - loopEnd))
                    start (h.note, h.vel, (int) ((loopEnd - b0 + h.beat - loopStart) / perSample), gain, held (h));
            }
        }
        const float fadeStep = 1.0f / (0.012f * (float) hostRate); // a held note lets go over 12 ms
        const double step = kitRate / hostRate;
        for (auto& v : voices)
        {
            if (! v.active) continue;
            const int len = v.buf->getNumSamples();
            const float* a = v.buf->getReadPointer (0);
            const float* b = v.buf->getReadPointer (std::min (1, v.buf->getNumChannels() - 1));
            for (int i = std::max (0, v.delay); i < n; ++i)
            {
                const int i0 = (int) v.pos;
                if (i0 + 1 >= len) { v.active = false; break; }
                if (v.stopAt >= 0 && v.age >= v.stopAt) { v.env -= fadeStep; if (v.env <= 0.0f) { v.active = false; break; } }
                ++v.age;
                const float f = (float) (v.pos - i0);
                L[i] += (a[i0] + (a[i0 + 1] - a[i0]) * f) * v.gain * v.env;
                R[i] += (b[i0] + (b[i0 + 1] - b[i0]) * f) * v.gain * v.env;
                v.pos += step;
            }
            v.delay = 0;
        }
    }

private:
    struct Voice { const juce::AudioBuffer<float>* buf = nullptr; double pos = 0; float gain = 1; int delay = 0; int note = 0; bool active = false;
                   int stopAt = -1, age = 0; float env = 1.0f; };

    void start (int note, float vel, int offset, float gain, int heldSamples = -1)
    {
        auto it = kit.find (note);
        if (it == kit.end() || it->second == nullptr) return;
        if (note == 42) for (auto& v : voices) if (v.active && v.note == 46) v.active = false; // a closed hat chokes the open one
        Voice* slot = nullptr;
        for (auto& v : voices) if (! v.active) { slot = &v; break; }
        if (slot == nullptr) slot = &voices[(size_t) (next++ % voices.size())];
        *slot = { it->second.get(), 0.0, juce::jlimit (0.0f, 1.0f, vel) * gain, offset, note, true, heldSamples < 0 ? -1 : heldSamples, 0, 1.0f };
        if (heldSamples >= 0) // a bass line: one note at a time, the old one lets go as the new one starts
            for (auto& v : voices) if (&v != slot && v.active && v.stopAt >= 0) v.stopAt = std::min (v.stopAt, v.age + std::max (0, offset));
    }

    mutable juce::SpinLock lock;
    Kit kit;
    std::vector<Hit> hits, pending;
    double loopStart = 0, loopLen = 4;
    std::array<Voice, 32> voices {};
    size_t next = 0;
};
