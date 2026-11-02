#include "SharedEffectPlugin.h"

namespace cdaw
{
    SharedEffectPlugin::SharedEffectPlugin (te::PluginCreationInfo info, const ParameterSpecs& specs, bool stereo)
        : te::Plugin (info), stereoOutput (stereo)
    {
        parameters.resize (specs.size());

        for (size_t i = 0; i < specs.size(); ++i)
        {
            auto& spec = specs[i];
            parameters[i] = { *this, spec.id, juce::Identifier (spec.id), TRANS(spec.name), spec.defaultValue,
                              spec.getRange(), spec.toText, spec.fromText };
        }
    }

    SharedEffectPlugin::~SharedEffectPlugin()
    {
        for (auto& p : parameters)
            p.reset();
    }

    void SharedEffectPlugin::restorePluginStateFromValueTree (const juce::ValueTree& v)
    {
        for (auto& p : parameters)
            p.setFromValueTree (v);
    }

    double SharedEffectPlugin::getBpm (const te::PluginRenderContext& fc) const
    {
        // As Tracktion's own external plugins do for their play head.
        return edit.tempoSequence.getBpmAt (fc.editTime.getStart());
    }

    TempoSync::SongPosition SharedEffectPlugin::getSongPosition (const te::PluginRenderContext& fc) const
    {
        const auto time = fc.editTime.getStart();
        return { fc.isPlaying || fc.isRendering, time.inSeconds(), edit.tempoSequence.toBeats (time).inBeats() };
    }

    double SharedEffectPlugin::getBpmNow() const
    {
        return edit.tempoSequence.getBpmAt (edit.getTransport().getPosition());
    }
}
