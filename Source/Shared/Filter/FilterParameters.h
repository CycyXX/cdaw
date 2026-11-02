#pragma once

#include "../Common/ParameterSpec.h"
#include "../DSP/FilterEngine.h"

namespace cdaw::FilterParameters
{
    /** C-Filter's parameters (shared by CDAW's built-in plugin and the VST3). Stored by ID in
        projects and presets: never change one, only append. The cutoff's ID is "frequency", as
        in Tracktion's low / high-pass filter, so automation of that filter carries over. */
    enum ID { type, slope, cutoff, resonance, drive, lfoRate, lfoSync, lfoDivision, lfoAmount, lfoShape, lfoStereo,
              envAmount, envRelease, mix, output, count };

    const ParameterSpec& get (ID);
    const ParameterSpecs& all();

    FilterEngine::Settings toSettings (const std::function<float (int)>& valueOf);

    juce::StringArray getTypeNames();
    juce::StringArray getShapeNames();
}
