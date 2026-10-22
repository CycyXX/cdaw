#include "MultibandPlugin.h"
#include "../UI/Plugins/MultibandEditor.h"

namespace cdaw
{
    const char* MultibandPlugin::xmlTypeName = "cdawMultiband";

    MultibandPlugin::MultibandPlugin (te::PluginCreationInfo info) : te::Plugin (info)
    {
        // The parameters come from the list shared with the VST3 build (same IDs, ranges, text).
        for (int i = 0; i < MultibandParameters::count; ++i)
        {
            auto& spec = MultibandParameters::get (i);
            parameter (i) = { *this, spec.id, juce::Identifier (spec.id), TRANS(spec.name), spec.defaultValue,
                              spec.getRange(), spec.toText, spec.fromText };
        }
    }

    MultibandPlugin::~MultibandPlugin()
    {
        notifyListenersOfDeletion();

        for (int i = 0; i < MultibandParameters::count; ++i)
            parameter (i).reset();
    }

    te::ParameterWithStateValue& MultibandPlugin::parameter (int index)
    {
        using namespace MultibandParameters;

        if (index == MultibandParameters::output)
            return output;

        if (index < numBands - 1)
            return crossovers[(size_t) index];

        const int band = (index - (numBands - 1)) / bandParamCount;
        auto& b = bands[(size_t) band];

        switch ((BandParam) ((index - (numBands - 1)) % bandParamCount))
        {
            case bandOn:        return b.on;
            case bandSolo:      return b.solo;
            case bandThreshold: return b.threshold;
            case bandRatio:     return b.ratio;
            case bandAttack:    return b.attack;
            case bandRelease:   return b.release;
            default:            return b.gain;
        }
    }

    MultibandEngine::Settings MultibandPlugin::getSettings() const
    {
        return MultibandParameters::toSettings ([this] (int i) { return const_cast<MultibandPlugin*> (this)->parameter (i).getCurrentValue(); });
    }

    std::unique_ptr<te::Plugin::EditorComponent> MultibandPlugin::createEditor()
    {
        return std::make_unique<MultibandEditor> (*this);
    }

    void MultibandPlugin::restorePluginStateFromValueTree (const juce::ValueTree& v)
    {
        for (int i = 0; i < MultibandParameters::count; ++i)
            parameter (i).setFromValueTree (v);
    }

    //==============================================================================
    void MultibandPlugin::initialise (const te::PluginInitialisationInfo& info)
    {
        engine.prepare (info.sampleRate, info.blockSizeSamples);
    }

    void MultibandPlugin::deinitialise() {}
    void MultibandPlugin::reset()           { engine.reset(); }

    void MultibandPlugin::applyToBuffer (const te::PluginRenderContext& fc)
    {
        if (fc.destBuffer != nullptr)
            engine.process (*fc.destBuffer, fc.bufferStartSample, fc.bufferNumSamples, getSettings());
    }
}
