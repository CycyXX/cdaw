#pragma once

#include "../PCH.h"
#include "../Shared/DSP/ReverbEngine.h"
#include "../Shared/CVerb/CVerbParameters.h"

namespace cdaw
{
    /** C-Verb: CDAW's full reverb, every control of the ReverbEngine exposed (pre-delay, size,
        decay, diffusion, modulation, low / high decay, damping, early reflections, width, pre and
        post cuts, freeze, mix). The simpler "Reverb" (SimpleReverbPlugin) uses the same engine.
        The type name stays "cdawReverb" so projects made before the rename still open.
        Its DSP (ReverbEngine), parameters (CVerbParameters) and editor view (CVerbView) are
        shared with the VST3 build in VST3/CVerb. */
    class ReverbPlugin : public te::Plugin
    {
    public:
        explicit ReverbPlugin (te::PluginCreationInfo);
        ~ReverbPlugin() override;

        static const char* getPluginName()      { return NEEDS_TRANS("C-Verb"); }
        static const char* xmlTypeName;

        juce::String getName() const override                   { return TRANS("C-Verb"); }
        juce::String getPluginType() override                   { return xmlTypeName; }
        juce::String getShortName (int) override                { return "C-Verb"; }
        juce::String getSelectableDescription() override        { return TRANS("C-Verb"); }
        int getNumOutputChannelsGivenInputs (int) override      { return 2; }
        BusLayout getBusses() const override                    { return BusLayout::singleStereoInOut(); }
        bool noTail() override                                  { return false; }
        double getTailLength() const override;                  // so renders keep the tail

        void initialise (const te::PluginInitialisationInfo&) override;
        void deinitialise() override;
        void reset() override;
        void applyToBuffer (const te::PluginRenderContext&) override;
        void restorePluginStateFromValueTree (const juce::ValueTree&) override;
        std::unique_ptr<EditorComponent> createEditor() override;

        //==============================================================================
        te::ParameterWithStateValue mix, preDelay, size, decay, diffusion, modulation,
                                    lowDecay, highDecay, dampFreq, early, width,
                                    preLowCut, preHighCut, postLowCut, postHighCut, freeze;

        bool isFrozen() const                               { return freeze.getCurrentValue() >= 0.5f; }
        ReverbEngine::Settings getSettings() const;

        /** The parameters by their shared ID (see CVerbParameters). */
        te::ParameterWithStateValue& parameter (CVerbParameters::ID);
        const te::ParameterWithStateValue& parameter (CVerbParameters::ID) const;

        /** For the editor (see ReverbEngine). */
        double getDecayAt (double hz) const                 { return ReverbEngine::getDecayAt (getSettings(), hz); }
        double getCutsDb (double hz, bool post) const       { return ReverbEngine::getCutsDb (getSettings(), hz, post, engine.getSampleRate()); }
        std::vector<ReverbEngine::Tap> getEarlyTaps() const { return ReverbEngine::getEarlyTaps (getSettings()); }

        static constexpr float lowCutOff = ReverbEngine::lowCutOff, highCutOff = ReverbEngine::highCutOff;

        ReverbEngine engine;
        PeakMeter& dryPeak = engine.dryPeak;
        PeakMeter& wetPeak = engine.wetPeak;

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReverbPlugin)
    };
}
