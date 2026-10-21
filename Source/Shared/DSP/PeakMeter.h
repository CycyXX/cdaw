#pragma once

#include <atomic>

namespace cdaw
{
    /** Peak value since the editor last looked: written by the audio thread, taken by the UI. */
    struct PeakMeter
    {
        void push (float v) noexcept        { if (v > value.load (std::memory_order_relaxed)) value.store (v, std::memory_order_relaxed); }
        float take() noexcept               { return value.exchange (0.0f, std::memory_order_relaxed); }

    private:
        std::atomic<float> value { 0.0f };
    };
}
