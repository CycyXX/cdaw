#pragma once

#include "../Common/ParameterSpec.h"
#include "../DSP/DelayEngine.h"

namespace cdaw::DelayParameters
{
    /** C-Delay's parameters (shared by CDAW's built-in plugin and the VST3). Stored by ID in
        projects and presets: never change one, only append. */
    enum ID { time, sync, division, mode, spread, feedback, lowCut, highCut, drive, wobble, duck, width, mix, count };

    const ParameterSpec& get (ID);
    const ParameterSpecs& all();

    /** The engine settings for a set of parameter values (in their own units). */
    DelayEngine::Settings toSettings (const std::function<float (int)>& valueOf);
}
