#pragma once

#include <juce_events/juce_events.h>

namespace cdaw
{
    /** AsyncUpdater that coalesces several kinds of pending updates via flags. */
    class FlaggedAsyncUpdater : public juce::AsyncUpdater
    {
    public:
        void markAndUpdate (bool& flag)             { flag = true; triggerAsyncUpdate(); }

        static bool compareAndReset (bool& flag) noexcept
        {
            if (! flag)
                return false;

            flag = false;
            return true;
        }
    };
}
