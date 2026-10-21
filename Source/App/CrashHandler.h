#pragma once

#include "../PCH.h"

namespace cdaw::CrashHandler
{
    /** Installs a handler that writes a crash report (stack, Tracktion crash tracer,
        plugins that were running) to %APPDATA%\CDAW\Crashes. */
    void install();

    /** Creates the session lock; returns true if the previous session didn't exit cleanly. */
    bool beginSession();
    void endSession();

    /** Plugin names recorded by the last crash (empty if none). Clears the record. */
    juce::StringArray takeCrashedPluginNames();

    /** Asks whether to disable plugins that crashed last time, then marks them disabled. */
    void offerToDisableCrashedPlugins (te::Engine&);
}
