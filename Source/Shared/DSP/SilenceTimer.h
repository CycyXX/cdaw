#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace cdaw
{
    /** For engines that can rest on silence: says when the input has been exactly zero for longer
        than the engine's tail. By then the engine's output and state have died away to nothing
        (the null tests check that it picks up as if it had kept running). */
    struct SilenceTimer
    {
        bool isPast (const juce::AudioBuffer<float>& buffer, int start, int n, int numChannels, double rate, double tailSeconds) noexcept
        {
            for (int ch = 0; ch < numChannels; ++ch)
                if (buffer.getMagnitude (ch, start, n) > 0.0f)
                {
                    silentSeconds = 0.0;
                    return false;
                }

            silentSeconds += n / rate;
            return silentSeconds > tailSeconds;
        }

        void reset() noexcept       { silentSeconds = 0.0; }

    private:
        double silentSeconds = 0.0;
    };
}
