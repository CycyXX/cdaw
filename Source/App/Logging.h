#pragma once

#include <juce_core/juce_core.h>

namespace cdaw::Logging
{
    /** Starts a file logger in %APPDATA%\CDAW\Logs and installs it as juce::Logger.
        Keeps the last 10 log files. */
    void init();
    void shutdown();
    juce::File getCurrentLogFile();
}
