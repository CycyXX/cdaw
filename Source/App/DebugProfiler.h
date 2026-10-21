#pragma once

#include "../PCH.h"

namespace cdaw::DebugProfiler
{
    /** Developer aid: samples a thread's call stack about 1000 times a second for `seconds`,
        then writes the most expensive functions (self and inclusive) to the log. The thread is
        the caller (the message thread), or with hottestThread the busiest thread in the process.
        Windows only. Started by CDAW_DEBUG_PROFILE=<seconds> (CDAW_DEBUG_PROFILE_HOT=1 for the busiest thread). */
    void start (double seconds, bool hottestThread = false);
}
