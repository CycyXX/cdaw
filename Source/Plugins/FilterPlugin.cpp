#include "FilterPlugin.h"
#include "Shared/Filter/FilterView.h"
#include "../UI/Plugins/SharedEffectEditor.h"

namespace cdaw
{
    const char* FilterPlugin::xmlTypeName = "cdawFilter";

    FilterPlugin::FilterPlugin (te::PluginCreationInfo info) : SharedEffectPlugin (info, FilterParameters::all(), false) {}

    FilterPlugin::~FilterPlugin()
    {
        notifyListenersOfDeletion();
    }

    void FilterPlugin::initialise (const te::PluginInitialisationInfo& info)
    {
        engine.prepare (info.sampleRate, info.blockSizeSamples);
    }

    void FilterPlugin::applyToBuffer (const te::PluginRenderContext& fc)
    {
        if (fc.destBuffer != nullptr)
            engine.process (*fc.destBuffer, fc.bufferStartSample, fc.bufferNumSamples, getSettings(), getBpm (fc), getSongPosition (fc));
    }

    std::unique_ptr<te::Plugin::EditorComponent> FilterPlugin::createEditor()
    {
        return std::make_unique<SharedEffectEditor<FilterView, FilterEngine>> (*this, engine);
    }
}
