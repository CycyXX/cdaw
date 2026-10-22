#pragma once

#include "../PCH.h"
#include "../Shared/DSP/MultibandEngine.h"
#include "../Shared/Multiband/MultibandParameters.h"

namespace cdaw
{
    /** C-Multicomp: CDAW's 4-band compressor (see MultibandEngine for what it does).
        The type name stays "cdawMultiband" so projects made before the rename still open.
        Its DSP (MultibandEngine), parameters (MultibandParameters) and editor view
        (MultibandView) are shared with the VST3 build in VST3/CMulticomp. */
    class MultibandPlugin : public te::Plugin
    {
    public:
        static constexpr int numBands = MultibandEngine::numBands;

        explicit MultibandPlugin (te::PluginCreationInfo);
        ~MultibandPlugin() override;

        static const char* getPluginName()      { return NEEDS_TRANS("C-Multicomp"); }
        static const char* xmlTypeName;

        juce::String getName() const override                   { return TRANS("C-Multicomp"); }
        juce::String getPluginType() override                   { return xmlTypeName; }
        juce::String getShortName (int) override                { return "C-Multicomp"; }
        juce::String getSelectableDescription() override        { return TRANS("C-Multicomp"); }
        int getNumOutputChannelsGivenInputs (int numInputs) override    { return juce::jmin (numInputs, 2); }
        BusLayout getBusses() const override                    { return BusLayout::singleStereoInOut(); }

        void initialise (const te::PluginInitialisationInfo&) override;
        void deinitialise() override;
        void reset() override;
        void applyToBuffer (const te::PluginRenderContext&) override;
        void restorePluginStateFromValueTree (const juce::ValueTree&) override;
        std::unique_ptr<EditorComponent> createEditor() override;

        //==============================================================================
        struct Band
        {
            te::ParameterWithStateValue on, solo, threshold, ratio, attack, release, gain;
        };

        Band& getBand (int index)                           { return bands[(size_t) index]; }
        bool isOn (int band) const                          { return bands[(size_t) band].on.getCurrentValue() >= 0.5f; }
        bool isSoloed (int band) const                      { return bands[(size_t) band].solo.getCurrentValue() >= 0.5f; }

        te::ParameterWithStateValue& getCrossover (int index)   { return crossovers[(size_t) index]; }

        /** The parameters by their shared index (see MultibandParameters). */
        te::ParameterWithStateValue& parameter (int index);
        MultibandEngine::Settings getSettings() const;

        te::ParameterWithStateValue output;
        std::array<te::ParameterWithStateValue, numBands - 1> crossovers;
        MultibandEngine engine;

    private:
        std::array<Band, numBands> bands;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MultibandPlugin)
    };
}
