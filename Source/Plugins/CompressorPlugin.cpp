#include "CompressorPlugin.h"
#include "../UI/Plugins/CompressorEditor.h"
#include "../Shared/DSP/CompressorMath.h"

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
        return CompressorMath::transfer (inDb, thresholdDb, ratioValue, kneeDb);
    }

    void CompressorPlugin::initialise (const te::PluginInitialisationInfo& info)
    {
        te::CompressorPlugin::initialise (info);
        engine.prepare (info.sampleRate);
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

    CompressorEngine::Settings CompressorPlugin::getSettings() const
    {
        CompressorEngine::Settings s;
        s.thresholdDb = getThresholdDb();
        s.ratio = getRatio();
        s.kneeDb = knee.getCurrentValue();
        s.attackMs = attackMs.getCurrentValue();
        s.releaseMs = releaseMs.getCurrentValue();
        s.makeUpDb = outputDb.getCurrentValue();
        s.sidechainDb = sidechainDb.getCurrentValue();
        return s;
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

        // Channel 3 carries the sidechain when one is routed here (Tracktion's convention).
        const bool useSidechain = useSidechainTrigger.get() && numChannels > 2;
        const float* key = useSidechain ? buffer.getReadPointer (2, start) : nullptr;

        engine.process (buffer, start, n, getSettings(), key);

        // The sidechain channel must not reach the output. (Not te::clearChannels: it passes its
        // end sample to JUCE as a sample count, which overruns the block when start > 0.)
        for (int ch = 2; ch < numChannels; ++ch)
            buffer.clear (ch, start, n);
    }
}
