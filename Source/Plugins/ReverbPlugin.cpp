#include "ReverbPlugin.h"
#include "../UI/Plugins/ReverbEditor.h"

namespace cdaw
{
    const char* ReverbPlugin::xmlTypeName = "cdawReverb";

    ReverbPlugin::ReverbPlugin (te::PluginCreationInfo info) : te::Plugin (info)
    {
        // The parameters come from the list shared with the VST3 build (same IDs, ranges, text).
        for (int i = 0; i < CVerbParameters::count; ++i)
        {
            auto& spec = CVerbParameters::get ((CVerbParameters::ID) i);
            parameter ((CVerbParameters::ID) i) = { *this, spec.id, juce::Identifier (spec.id), TRANS(spec.name), spec.defaultValue,
                                                    spec.getRange(), spec.toText, spec.fromText };
        }
    }

    ReverbPlugin::~ReverbPlugin()
    {
        notifyListenersOfDeletion();

        for (int i = 0; i < CVerbParameters::count; ++i)
            parameter ((CVerbParameters::ID) i).reset();
    }

    te::ParameterWithStateValue& ReverbPlugin::parameter (CVerbParameters::ID id)
    {
        te::ParameterWithStateValue* all[CVerbParameters::count] { &mix, &preDelay, &size, &decay, &diffusion, &modulation, &lowDecay, &highDecay,
                                                                   &dampFreq, &early, &width, &preLowCut, &preHighCut, &postLowCut, &postHighCut, &freeze };
        return *all[id];
    }

    const te::ParameterWithStateValue& ReverbPlugin::parameter (CVerbParameters::ID id) const
    {
        return const_cast<ReverbPlugin*> (this)->parameter (id);
    }

    std::unique_ptr<te::Plugin::EditorComponent> ReverbPlugin::createEditor()
    {
        return std::make_unique<ReverbEditor> (*this);
    }

    void ReverbPlugin::restorePluginStateFromValueTree (const juce::ValueTree& v)
    {
        for (int i = 0; i < CVerbParameters::count; ++i)
            parameter ((CVerbParameters::ID) i).setFromValueTree (v);
    }

    ReverbEngine::Settings ReverbPlugin::getSettings() const
    {
        return CVerbParameters::toSettings ([this] (CVerbParameters::ID id) { return parameter (id).getCurrentValue(); });
    }

    double ReverbPlugin::getTailLength() const
    {
        return preDelay.getCurrentValue() * 0.001 + decay.getCurrentValue() * juce::jmax (1.0f, lowDecay.getCurrentValue()) * 1.1;
    }

    //==============================================================================
    void ReverbPlugin::initialise (const te::PluginInitialisationInfo& info)
    {
        engine.prepare (info.sampleRate, info.blockSizeSamples, getSettings());
    }

    void ReverbPlugin::deinitialise() {}
    void ReverbPlugin::reset()          { engine.reset(); }

    void ReverbPlugin::applyToBuffer (const te::PluginRenderContext& fc)
    {
        if (fc.destBuffer != nullptr)
            engine.process (*fc.destBuffer, fc.bufferStartSample, fc.bufferNumSamples, getSettings());
    }
}
