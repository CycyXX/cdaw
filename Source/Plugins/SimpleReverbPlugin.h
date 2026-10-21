#pragma once

#include "../PCH.h"
#include "../Shared/DSP/ReverbEngine.h"

namespace cdaw
{
    /** "Reverb": the quick one. The same engine as C-Verb behind five controls: size, decay,
        pre-delay, tone (dark to bright: how fast the highs die and where the reverb's high cut
        sits) and mix. Everything else is set for a clean, smooth sound (a 120 Hz low cut on the
        reverb, diffusion, gentle modulation, early reflections that follow the size). */
    class SimpleReverbPlugin : public te::Plugin
    {
    public:
        explicit SimpleReverbPlugin (te::PluginCreationInfo);
        ~SimpleReverbPlugin() override;

        static const char* getPluginName()      { return NEEDS_TRANS("Reverb"); }
        static const char* xmlTypeName;

        juce::String getName() const override                   { return TRANS("Reverb"); }
        juce::String getPluginType() override                   { return xmlTypeName; }
        juce::String getShortName (int) override                { return "Reverb"; }
        juce::String getSelectableDescription() override        { return TRANS("Reverb"); }
        int getNumOutputChannelsGivenInputs (int) override      { return 2; }
        BusLayout getBusses() const override                    { return BusLayout::singleStereoInOut(); }
        bool noTail() override                                  { return false; }
        double getTailLength() const override;

        void initialise (const te::PluginInitialisationInfo&) override;
        void deinitialise() override                            {}
        void reset() override                                   { engine.reset(); }
        void applyToBuffer (const te::PluginRenderContext&) override;
        void restorePluginStateFromValueTree (const juce::ValueTree&) override;
        std::unique_ptr<EditorComponent> createEditor() override;

        //==============================================================================
        te::ParameterWithStateValue size, decay, preDelay, tone, mix;

        /** The engine settings these five controls stand for. */
        ReverbEngine::Settings getSettings() const;

        ReverbEngine engine;

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SimpleReverbPlugin)
    };
}
