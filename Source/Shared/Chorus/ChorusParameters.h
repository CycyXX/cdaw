#pragma once

#include "../Common/ParameterSpec.h"
#include "../DSP/ChorusEngine.h"

namespace cdaw::ChorusParameters
{
    /** C-Chorus's parameters (shared by CDAW's built-in plugin and the VST3). Stored by ID in
        projects and presets: never change one, only append. */
    enum ID { voices, rate, depth, delay, feedback, spread, tone, width, mix, count };

    const ParameterSpec& get (ID);
    const ParameterSpecs& all();

    ChorusEngine::Settings toSettings (const std::function<float (int)>& valueOf);

    /** Starting points: settings that make it a chorus, an ensemble, a flanger or a vibrato. */
    struct Character { const char* name; const char* tooltip; std::vector<std::pair<ID, float>> values; };
    const std::vector<Character>& getCharacters();
}
