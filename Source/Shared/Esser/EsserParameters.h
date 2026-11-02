#pragma once

#include "../Common/ParameterSpec.h"
#include "../DSP/EsserEngine.h"

namespace cdaw::EsserParameters
{
    /** C-Esser's parameters (shared by CDAW's built-in plugin and the VST3). Stored by ID in
        projects and presets: never change one, only append. */
    enum ID { threshold, autoThreshold, range, release, lowfreq, highfreq, mode, listen, output, count };

    const ParameterSpec& get (ID);
    const ParameterSpecs& all();

    EsserEngine::Settings toSettings (const std::function<float (int)>& valueOf);
}
