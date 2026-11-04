#include "DistPlugin.h"
#include "Shared/Dist/DistView.h"
#include "../UI/Plugins/SharedEffectEditor.h"

namespace cdaw
{
    const char* DistPlugin::xmlTypeName = "cdawDist";

    DistPlugin::DistPlugin (te::PluginCreationInfo info) : SharedEffectPlugin (info, DistParameters::all(), false) {}

    DistPlugin::~DistPlugin()
    {
        notifyListenersOfDeletion();
    }

    void DistPlugin::initialise (const te::PluginInitialisationInfo& info)
    {
        engine.prepare (info.sampleRate, info.blockSizeSamples);
    }

    void DistPlugin::applyToBuffer (const te::PluginRenderContext& fc)
    {
        if (fc.destBuffer != nullptr)
            engine.process (*fc.destBuffer, fc.bufferStartSample, fc.bufferNumSamples, getSettings());
    }

    std::unique_ptr<te::Plugin::EditorComponent> DistPlugin::createEditor()
    {
        return std::make_unique<SharedEffectEditor<DistView, DistEngine>> (*this, engine);
    }
}
