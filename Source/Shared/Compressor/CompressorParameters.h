#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "../DSP/CompressorEngine.h"
#include <functional>

namespace cdaw::CompressorParameters
{
    /** C-Comp's parameters, defined once for both builds (CDAW's built-in plugin and the VST3):
        the same IDs, names, ranges, defaults and display text everywhere, in the units shown to
        the user (dB, N:1, ms). The IDs are stored in presets: never change one. (CDAW's built-in
        plugin keeps Tracktion's own parameters, with threshold as a gain and ratio as 1 / ratio,
        so projects stay compatible; its editor converts.) */
    enum ID { threshold, ratio, knee, attack, release, makeUp, sidechainGain, count };

    /** The top of the ratio range is the limiter. */
    constexpr float limitRatio = 30.0f;

    struct Spec
    {
        const char* id;
        const char* name;
        float minimum, maximum, defaultValue;
        float skewCentre = 0.0f;        // 0 = linear
        std::function<juce::String (float)> toText;
        std::function<float (const juce::String&)> fromText;

        juce::NormalisableRange<float> getRange() const;
    };

    const Spec& get (ID);

    /** The ratio the engine should use for a ratio parameter value. */
    inline float engineRatio (float value)      { return value >= limitRatio ? 1000.0f : value; }

    /** The engine settings for a set of parameter values (in their own units). */
    CompressorEngine::Settings toSettings (const std::function<float (ID)>& valueOf);
}
