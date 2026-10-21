#include "CompressorPlugin.h"
#include "../UI/Plugins/CompressorEditor.h"

namespace cdaw
{
    const char* CompressorPlugin::xmlTypeName = "cdawCompressor";

    CompressorPlugin::CompressorPlugin (te::PluginCreationInfo info) : te::CompressorPlugin (info)
    {
        knee = { *this, "knee", "knee", TRANS("Knee"), 6.0f, { 0.0f, 24.0f },
                 [] (float v)                 { return juce::String (v, 1) + " dB"; },
                 [] (const juce::String& s)   { return s.getFloatValue(); } };

        // Tracktion's ratio text reads "4.00 : 1"; keep it short for the knob.
        ratio.parameter->valueToStringFunction = [] (float v)
        {
            return v > 0.001f ? juce::String (1.0f / v, 1) + ":1" : juce::String ("Limit");
        };
    }

    CompressorPlugin::~CompressorPlugin()
    {
        notifyListenersOfDeletion();
        knee.reset();
    }

    float CompressorPlugin::getThresholdDb() const
    {
        return juce::Decibels::gainToDecibels (thresholdGain.getCurrentValue(), -100.0f);
    }

    float CompressorPlugin::getRatio() const
    {
        auto r = ratio.getCurrentValue();
        return r > 0.001f ? 1.0f / r : 1000.0f;
    }

    float CompressorPlugin::transfer (float inDb, float thresholdDb, float ratioValue, float kneeDb)
    {
        const float over = inDb - thresholdDb;
        const float slope = 1.0f / ratioValue - 1.0f;     // gain change per dB above threshold (<= 0)

        if (kneeDb > 0.0f && std::abs (over) <= kneeDb * 0.5f)
        {
            const float x = over + kneeDb * 0.5f;
            return inDb + slope * x * x / (2.0f * kneeDb);
        }

        return over > 0.0f ? inDb + slope * over : inDb;
    }

    void CompressorPlugin::initialise (const te::PluginInitialisationInfo& info)
    {
        te::CompressorPlugin::initialise (info);
        envelopeDb = 0.0;
    }

    void CompressorPlugin::restorePluginStateFromValueTree (const juce::ValueTree& v)
    {
        te::CompressorPlugin::restorePluginStateFromValueTree (v);
        knee.setFromValueTree (v);
    }

    std::unique_ptr<te::Plugin::EditorComponent> CompressorPlugin::createEditor()
    {
        return std::make_unique<CompressorEditor> (*this);
    }

    void CompressorPlugin::applyToBuffer (const te::PluginRenderContext& fc)
    {
        if (fc.destBuffer == nullptr || fc.bufferNumSamples <= 0)
            return;

        auto& buffer = *fc.destBuffer;
        const int numChannels = buffer.getNumChannels();
        const int n = fc.bufferNumSamples, start = fc.bufferStartSample;

        if (numChannels == 0)
            return;

        const float thresholdDb = getThresholdDb();
        const float ratioValue = getRatio();
        const float kneeDb = knee.getCurrentValue();
        const double makeUpDb = outputDb.getCurrentValue();
        const float sidechainGain = juce::Decibels::decibelsToGain (sidechainDb.getCurrentValue());
        const double sr = sampleRate > 0.0 ? sampleRate : 48000.0;
        const double attack = std::exp (-1.0 / (juce::jmax (0.05, (double) attackMs.getCurrentValue()) * 0.001 * sr));
        const double release = std::exp (-1.0 / (juce::jmax (1.0, (double) releaseMs.getCurrentValue()) * 0.001 * sr));

        // Channel 3 carries the sidechain when one is routed here (Tracktion's convention).
        const bool useSidechain = useSidechainTrigger.get() && numChannels > 2;
        sidechainActive.store (useSidechain, std::memory_order_relaxed);

        float* left = buffer.getWritePointer (0, start);
        float* right = numChannels > 1 ? buffer.getWritePointer (1, start) : nullptr;
        const float* key = useSidechain ? buffer.getReadPointer (2, start) : nullptr;

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
            const double target = transfer (inDb, thresholdDb, ratioValue, kneeDb) - inDb;     // <= 0

            // Attack while the reduction deepens, release while it recovers.
            envelopeDb = target < envelopeDb ? attack * envelopeDb + (1.0 - attack) * target
                                             : release * envelopeDb + (1.0 - release) * target;

            if (envelopeDb > -1.0e-6)
                envelopeDb = 0.0;

            mostReduction = juce::jmin (mostReduction, envelopeDb);
            const float gain = (float) std::pow (10.0, (envelopeDb + makeUpDb) / 20.0);

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

        // The sidechain channel must not reach the output.
        te::clearChannels (buffer, 2, -1, start, start + n);

        for (int ch = 0; ch < 2; ++ch)
        {
            inputPeak[ch].push (inPeak[ch]);
            outputPeak[ch].push (outPeak[ch]);
        }

        detectorPeak.push (keyPeak);
        gainReduction.push ((float) -mostReduction);
    }
}
