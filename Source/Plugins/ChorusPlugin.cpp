#include "ChorusPlugin.h"
#include "Shared/Chorus/ChorusView.h"
#include "../UI/Plugins/SharedEffectEditor.h"

namespace cdaw
{
    const char* ChorusPlugin::xmlTypeName = "cdawChorus";

    ChorusPlugin::ChorusPlugin (te::PluginCreationInfo info) : SharedEffectPlugin (info, ChorusParameters::all(), true) {}

    ChorusPlugin::~ChorusPlugin()
    {
        notifyListenersOfDeletion();
    }

    void ChorusPlugin::initialise (const te::PluginInitialisationInfo& info)
    {
        engine.prepare (info.sampleRate, info.blockSizeSamples);
    }

    void ChorusPlugin::applyToBuffer (const te::PluginRenderContext& fc)
    {
        if (fc.destBuffer != nullptr)
            engine.process (*fc.destBuffer, fc.bufferStartSample, fc.bufferNumSamples, getSettings(), getSongPosition (fc));
    }

    std::unique_ptr<te::Plugin::EditorComponent> ChorusPlugin::createEditor()
    {
        return std::make_unique<SharedEffectEditor<ChorusView, ChorusEngine>> (*this, engine);
    }
}
