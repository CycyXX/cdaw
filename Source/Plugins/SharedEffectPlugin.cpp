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
        // Runs on audio threads, so through a Position, as Tracktion's external plugins do for their
        // play head. TempoSequence::getBpmAt and toBeats would apply a pending tempo change right
        // here: that allocates and takes the audio callback lock, which the callback holds while it
        // waits for this thread.
        auto position = te::createPosition (edit.tempoSequence);
        position.set (fc.editTime.getStart());
        return position.getTempo();
    }

    TempoSync::SongPosition SharedEffectPlugin::getSongPosition (const te::PluginRenderContext& fc) const
    {
        const auto time = fc.editTime.getStart();
        auto position = te::createPosition (edit.tempoSequence);
        position.set (time);
        return { fc.isPlaying || fc.isRendering, time.inSeconds(), position.getBeats().inBeats() };
    }

    double SharedEffectPlugin::getBpmNow() const
    {
        return edit.tempoSequence.getBpmAt (edit.getTransport().getPosition());
    }
}
