#include "CompressorEngine.h"

namespace cdaw
{
    void CompressorEngine::prepare (double rate)
    {
        sampleRate = rate > 0.0 ? rate : 48000.0;
        reset();
    }

    void CompressorEngine::reset()
    {
        envelopeDb = 0.0;
    }

    void CompressorEngine::process (juce::AudioBuffer<float>& buffer, int start, int n, const Settings& s, const float* key)
    {
        const int numChannels = buffer.getNumChannels();

        if (numChannels == 0 || n <= 0)
            return;

        const double makeUpDb = s.makeUpDb;
        const float sidechainGain = juce::Decibels::decibelsToGain (s.sidechainDb);
        const double attack = std::exp (-1.0 / (juce::jmax (0.05, (double) s.attackMs) * 0.001 * sampleRate));
        const double release = std::exp (-1.0 / (juce::jmax (1.0, (double) s.releaseMs) * 0.001 * sampleRate));
        const float gainWithoutReduction = (float) std::pow (10.0, makeUpDb / 20.0);

        sidechainActive.store (key != nullptr, std::memory_order_relaxed);

        float* left = buffer.getWritePointer (0, start);
        float* right = numChannels > 1 ? buffer.getWritePointer (1, start) : nullptr;

        float inPeak[2] {}, outPeak[2] {}, keyPeak = 0.0f;
        double mostReduction = 0.0;

        for (int i = 0; i < n; ++i)
        {
            const float l = left[i];
            const float r = right != nullptr ? right[i] : l;
            inPeak[0] = juce::jmax (inPeak[0], std::abs (l));
            inPeak[1] = juce::jmax (inPeak[1], std::abs (r));

            const float detector = key != nullptr ? std::abs (key[i]) * sidechainGain : juce::jmax (std::abs (l), std::abs (r));
            keyPeak = juce::jmax (keyPeak, detector);

            const float inDb = detector > 1.0e-6f ? 20.0f * std::log10 (detector) : -120.0f;
            const double target = transfer (inDb, s) - inDb;     // <= 0

            // Attack while the reduction deepens, release while it recovers.
            envelopeDb = target < envelopeDb ? attack * envelopeDb + (1.0 - attack) * target
                                             : release * envelopeDb + (1.0 - release) * target;

            if (envelopeDb > -1.0e-6)
                envelopeDb = 0.0;

            mostReduction = juce::jmin (mostReduction, envelopeDb);
            const float gain = juce::exactlyEqual (envelopeDb, 0.0) ? gainWithoutReduction
                                                                    : (float) std::pow (10.0, (envelopeDb + makeUpDb) / 20.0);

            left[i] = l * gain;
            outPeak[0] = juce::jmax (outPeak[0], std::abs (left[i]));

            if (right != nullptr)
            {
                right[i] = r * gain;
                outPeak[1] = juce::jmax (outPeak[1], std::abs (right[i]));
            }
        }

        if (right == nullptr)
            outPeak[1] = outPeak[0];

        for (int ch = 0; ch < 2; ++ch)
        {
            inputPeak[ch].push (inPeak[ch]);
            outputPeak[ch].push (outPeak[ch]);
        }

        detectorPeak.push (keyPeak);
        gainReduction.push ((float) -mostReduction);
    }
}
