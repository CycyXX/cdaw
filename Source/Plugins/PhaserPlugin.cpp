#include "PhaserPlugin.h"
#include "../Shared/Phaser/PhaserView.h"
#include "../UI/Plugins/SharedEffectEditor.h"

namespace cdaw
{
    const char* PhaserPlugin::xmlTypeName = "cdawPhaser";

    PhaserPlugin::PhaserPlugin (te::PluginCreationInfo info) : SharedEffectPlugin (info, PhaserParameters::all(), true) {}

    PhaserPlugin::~PhaserPlugin()
    {
        notifyListenersOfDeletion();
    }

    void PhaserPlugin::initialise (const te::PluginInitialisationInfo& info)
    {
        engine.prepare (info.sampleRate, info.blockSizeSamples);
    }

    void PhaserPlugin::applyToBuffer (const te::PluginRenderContext& fc)
    {
        if (fc.destBuffer != nullptr)
            engine.process (*fc.destBuffer, fc.bufferStartSample, fc.bufferNumSamples, getSettings(), getBpm (fc), getSongPosition (fc));
    }

    std::unique_ptr<te::Plugin::EditorComponent> PhaserPlugin::createEditor()
    {
        return std::make_unique<SharedEffectEditor<PhaserView, PhaserEngine>> (*this, engine);
    }
}
