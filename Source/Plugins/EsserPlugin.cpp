#include "EsserPlugin.h"
#include "Shared/Esser/EsserView.h"
#include "../UI/Plugins/SharedEffectEditor.h"

namespace cdaw
{
    const char* EsserPlugin::xmlTypeName = "cdawEsser";

    EsserPlugin::EsserPlugin (te::PluginCreationInfo info) : SharedEffectPlugin (info, EsserParameters::all(), true) {}

    EsserPlugin::~EsserPlugin()
    {
        notifyListenersOfDeletion();
    }

    void EsserPlugin::initialise (const te::PluginInitialisationInfo& info)
    {
        engine.prepare (info.sampleRate, info.blockSizeSamples);
    }

    void EsserPlugin::applyToBuffer (const te::PluginRenderContext& fc)
    {
        if (fc.destBuffer != nullptr)
            engine.process (*fc.destBuffer, fc.bufferStartSample, fc.bufferNumSamples, getSettings());
    }

    std::unique_ptr<te::Plugin::EditorComponent> EsserPlugin::createEditor()
    {
        return std::make_unique<SharedEffectEditor<EsserView, EsserEngine>> (*this, engine);
    }
}
