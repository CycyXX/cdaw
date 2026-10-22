#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "../DSP/MultibandEngine.h"
#include <functional>

namespace cdaw::MultibandParameters
{
    /** C-Multicomp's parameters, defined once for both builds (CDAW's built-in plugin and the
        VST3): the same IDs, names, ranges, defaults and display text everywhere. The IDs are
        stored in projects and presets: never change one.

        A parameter is addressed by an index: crossovers first (crossover (0..2)), then seven per
        band (bandParam (band, what)), then the output gain. */
    constexpr int numBands = MultibandEngine::numBands;

    enum BandParam { bandOn, bandSolo, bandThreshold, bandRatio, bandAttack, bandRelease, bandGain, bandParamCount };

    constexpr int crossover (int i)                         { return i; }
    constexpr int bandParam (int band, BandParam what)      { return numBands - 1 + band * bandParamCount + what; }
    constexpr int output = numBands - 1 + numBands * bandParamCount;
    constexpr int count = output + 1;

    struct Spec
    {
        juce::String id;
        juce::String name;
        float minimum, maximum, defaultValue;
        float skewCentre = 0.0f;        // 0 = linear
        float interval = 0.0f;          // 1 for on / off switches
        std::function<juce::String (float)> toText;
        std::function<float (const juce::String&)> fromText;

        juce::NormalisableRange<float> getRange() const;
    };

    const Spec& get (int index);

    juce::String bandName (int band);

    /** The engine settings for a set of parameter values (in their own units). */
    MultibandEngine::Settings toSettings (const std::function<float (int)>& valueOf);
}
