#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "../DSP/ReverbEngine.h"
#include <functional>

namespace cdaw::CVerbParameters
{
    /** C-Verb's parameters, defined once for both builds (CDAW's built-in plugin and the VST3):
        the same IDs, names, ranges, defaults and display text everywhere. The IDs are stored in
        projects and presets: never change one. */
    enum ID { mix, preDelay, size, decay, diffusion, modulation, lowDecay, highDecay, dampFreq, early, width,
              preLowCut, preHighCut, postLowCut, postHighCut, freeze, count };

    struct Spec
    {
        const char* id;
        const char* name;
        float minimum, maximum, defaultValue;
        float skewCentre = 0.0f;        // 0 = linear
        float interval = 0.0f;          // 1 for on / off switches
        std::function<juce::String (float)> toText;
        std::function<float (const juce::String&)> fromText;

        juce::NormalisableRange<float> getRange() const;
    };

    const Spec& get (ID);

    /** The engine settings for a set of parameter values (in their own units). */
    ReverbEngine::Settings toSettings (const std::function<float (ID)>& valueOf);
}
