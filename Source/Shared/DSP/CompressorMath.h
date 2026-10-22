#pragma once

#include <cmath>

namespace cdaw::CompressorMath
{
    /** The static curve of CDAW's compressors: output level (dB, before make-up) for an input
        level (dB), with a quadratic soft knee centred on the threshold. */
    inline float transfer (float inDb, float thresholdDb, float ratio, float kneeDb)
    {
        const float over = inDb - thresholdDb;
        const float slope = 1.0f / ratio - 1.0f;     // gain change per dB above threshold (<= 0)

        if (kneeDb > 0.0f && std::abs (over) <= kneeDb * 0.5f)
        {
            const float x = over + kneeDb * 0.5f;
            return inDb + slope * x * x / (2.0f * kneeDb);
        }

        return over > 0.0f ? inDb + slope * over : inDb;
    }
}
