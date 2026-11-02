#pragma once

#include "../Common/ParameterSpec.h"
#include "../DSP/PhaserEngine.h"

namespace cdaw::PhaserParameters
{
    /** C-Phaser's parameters (shared by CDAW's built-in plugin and the VST3). Stored by ID in
        projects and presets: never change one, only append. */
    enum ID { stages, rate, sync, division, depth, centre, feedback, spread, mix, count };

    const ParameterSpec& get (ID);
    const ParameterSpecs& all();

    PhaserEngine::Settings toSettings (const std::function<float (int)>& valueOf);
}
