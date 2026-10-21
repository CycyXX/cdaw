#pragma once

#include "../PCH.h"
#include "../Shared/DSP/PeakMeter.h"

namespace cdaw
{
    /** CDAW's compressor. Keeps Tracktion's compressor parameters (and its sidechain input,
        so sidechain routing and automation work unchanged) but computes gain in decibels, so the
        ratio is a true ratio, adds a soft knee, and meters input, output and gain reduction for
        its editor.

        Parameters: threshold (gain, -40..0 dB), ratio (stored as 1/ratio, 0 = limiter), attack,
        release, output (make-up) and sidechain gain from Tracktion; knee (dB) added here. */
    class CompressorPlugin : public te::CompressorPlugin
    {
    public:
        explicit CompressorPlugin (te::PluginCreationInfo);
        ~CompressorPlugin() override;

        static const char* getPluginName()      { return NEEDS_TRANS("Compressor"); }
        static const char* xmlTypeName;

        juce::String getName() const override                   { return TRANS("Compressor"); }
        juce::String getPluginType() override                   { return xmlTypeName; }
        juce::String getSelectableDescription() override        { return TRANS("Compressor"); }

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

        // Meters for the editor (linear gain peaks; gain reduction in positive dB).
        PeakMeter inputPeak[2], outputPeak[2], detectorPeak, gainReduction;
        std::atomic<bool> sidechainActive { false };

    private:
        double envelopeDb = 0.0;        // smoothed gain change (<= 0)

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CompressorPlugin)
    };
}
