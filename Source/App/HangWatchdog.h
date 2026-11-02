#pragma once

#include "../PCH.h"

namespace cdaw
{
    /** Notices when the message thread stops responding (a plugin or driver call that doesn't
        return, a startup step that takes very long) and writes to the log what CDAW was doing
        and where the thread is stuck: its call stack names the plugin or driver involved. The
        log then explains a hang that otherwise just ends mid-way. */
    class HangWatchdog : private juce::Thread,
                         private juce::Timer
    {
    public:
        /** Create on the message thread. */
        HangWatchdog();
        ~HangWatchdog() override;

        /** What the app is doing now, e.g. "Opening the audio device" (logged if it hangs).
            Also counts as a sign of life. Any thread. */
        static void setActivity (const juce::String&);

    private:
        void run() override;
        void timerCallback() override;

        void* messageThread = nullptr;
    };
}
