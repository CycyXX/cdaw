#include "DelayPlugin.h"
#include "Shared/Delay/DelayView.h"
#include "../UI/Plugins/SharedEffectEditor.h"

namespace cdaw
{
    const char* DelayPlugin::xmlTypeName = "cdawDelay";

    DelayPlugin::DelayPlugin (te::PluginCreationInfo info) : SharedEffectPlugin (info, DelayParameters::all(), true) {}

    DelayPlugin::~DelayPlugin()
    {
        notifyListenersOfDeletion();
    }

    double DelayPlugin::getTailLength() const
    {
        return DelayEngine::getTailSeconds (getSettings(), getBpmNow());
    }

    void DelayPlugin::initialise (const te::PluginInitialisationInfo& info)
    {
        engine.prepare (info.sampleRate, info.blockSizeSamples);
    }

    void DelayPlugin::applyToBuffer (const te::PluginRenderContext& fc)
    {
        if (fc.destBuffer != nullptr)
            engine.process (*fc.destBuffer, fc.bufferStartSample, fc.bufferNumSamples, getSettings(), getBpm (fc));
    }

    std::unique_ptr<te::Plugin::EditorComponent> DelayPlugin::createEditor()
    {
        return std::make_unique<SharedEffectEditor<DelayView, DelayEngine>> (*this, engine);
    }
}
