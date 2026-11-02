#pragma once

#include "../PCH.h"
#include "../Shared/Common/ParameterSpec.h"
#include "../Shared/DSP/TempoSync.h"

namespace cdaw
{
    /** Base of CDAW's effects whose parameters come from a list shared with their VST3 build
        (C-Delay, C-Chorus, C-Phaser, C-Filter): one automatable parameter per spec, with the
        same ID, range and text as in the VST3, restored from the plugin's state.
        Subclasses call notifyListenersOfDeletion() first thing in their destructor. */
    class SharedEffectPlugin : public te::Plugin
    {
    public:
        SharedEffectPlugin (te::PluginCreationInfo, const ParameterSpecs&, bool stereoOutput);
        ~SharedEffectPlugin() override;

        int getNumOutputChannelsGivenInputs (int numInputs) override    { return stereoOutput ? 2 : juce::jmin (numInputs, 2); }
        BusLayout getBusses() const override                            { return BusLayout::singleStereoInOut(); }
        void restorePluginStateFromValueTree (const juce::ValueTree&) override;

        te::ParameterWithStateValue& parameter (int id)                 { return parameters[(size_t) id]; }
        float value (int id) const                                      { return parameters[(size_t) id].getCurrentValue(); }
        std::function<float (int)> valueOf() const                      { return [this] (int id) { return value (id); }; }
        int getNumSharedParameters() const                              { return (int) parameters.size(); }

        /** The tempo at the start of a block (for synced times and rates). */
        double getBpm (const te::PluginRenderContext&) const;

        /** Where the song is at the start of a block (for LFOs that keep in step with it). */
        TempoSync::SongPosition getSongPosition (const te::PluginRenderContext&) const;

        /** The tempo where the transport is now (for editors). */
        double getBpmNow() const;

    private:
        std::vector<te::ParameterWithStateValue> parameters;
        const bool stereoOutput;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SharedEffectPlugin)
    };
}
