#pragma once
#include <atomic>

namespace lp::ui
{
// the audio thread writes the loudest sample it played; the screen takes it (and resets it) each frame
inline void notePeak (std::atomic<float>& slot, float v)
{
    float cur = slot.load (std::memory_order_relaxed);
    while (v > cur && ! slot.compare_exchange_weak (cur, v, std::memory_order_relaxed)) {}
}
}
