#pragma once

#include "../PCH.h"
#include "Shared/DSP/CompressorEngine.h"

namespace cdaw
{
    /** C-Comp: CDAW's compressor (see CompressorEngine for what it does). Keeps Tracktion's compressor parameters (and its sidechain input,
        so sidechain routing and automation work unchanged) but computes gain in decibels, so the
        ratio is a true ratio, adds a soft knee, and meters input, output and gain reduction for
        its editor.

        Parameters: threshold (gain, -40..0 dB), ratio (stored as 1/ratio, 0 = limiter), attack,
        release, output (make-up) and sidechain gain from Tracktion; knee (dB) added here.
        The type name stays "cdawCompressor" so projects made before the rename still open.
        Its DSP (CompressorEngine), parameter list (CompressorParameters) and editor view
        (CompressorView) are shared with the VST3 build in VST3/CComp. */
    class CompressorPlugin : public te::CompressorPlugin
    {
    public:
        explicit CompressorPlugin (te::PluginCreationInfo);
        ~CompressorPlugin() override;

        static const char* getPluginName()      { return NEEDS_TRANS("C-Comp"); }
        static const char* xmlTypeName;

        juce::String getName() const override                   { return TRANS("C-Comp"); }
        juce::String getPluginType() override                   { return xmlTypeName; }
        juce::String getShortName (int) override                { return "C-Comp"; }
        juce::String getSelectableDescription() override        { return TRANS("C-Comp"); }

        void initialise (const te::PluginInitialisationInfo&) override;
        void applyToBuffer (const te::PluginRenderContext&) override;
        void restorePluginStateFromValueTree (const juce::ValueTree&) override;
        std::unique_ptr<EditorComponent> createEditor() override;

        //==============================================================================
        float getThresholdDb() const;
        float getRatio() const;                     // e.g. 4 for 4:1; very large = limiter
        float getKneeDb() const                     { return knee.getCurrentValue(); }

        /** The static curve: output level (dB, before make-up) for an input level (dB). */
        static float transfer (float inDb, float thresholdDb, float ratio, float kneeDb);
        float transfer (float inDb) const           { return transfer (inDb, getThresholdDb(), getRatio(), getKneeDb()); }

        te::ParameterWithStateValue knee;

        CompressorEngine::Settings getSettings() const;

        /** Holds the meters for the editor (linear gain peaks; gain reduction in positive dB). */
        CompressorEngine engine;

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CompressorPlugin)
    };
}
