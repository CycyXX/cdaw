#pragma once

#include "../PCH.h"
#include "CompressorPlugin.h"
#include "../Shared/DSP/EqMath.h"

namespace cdaw
{
    struct AnalyserFifo;

    /** CDAW's 4-band compressor. Three Linkwitz-Riley crossovers (24 dB/oct, phase-aligned, so
        the bands add back up flat when nothing is compressing) split the signal; each band has
        its own compressor (threshold, ratio, attack, release, make-up; soft knee as in CDAW's
        compressor) and can be switched to pass through uncompressed or soloed. Meters every
        band and feeds pre- and post-processing audio to its editor's spectrum. */
    class MultibandPlugin : public te::Plugin
    {
    public:
        static constexpr int numBands = 4;

        explicit MultibandPlugin (te::PluginCreationInfo);
        ~MultibandPlugin() override;

        static const char* getPluginName()      { return NEEDS_TRANS("Multiband Compressor"); }
        static const char* xmlTypeName;

        juce::String getName() const override                   { return TRANS("Multiband Compressor"); }
        juce::String getPluginType() override                   { return xmlTypeName; }
        juce::String getShortName (int) override                { return "MB Comp"; }
        juce::String getSelectableDescription() override        { return TRANS("Multiband Compressor"); }
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

            // Meters for the editor: band level before compression, gain reduction (dB), level after.
            PeakMeter inputPeak, reduction, outputPeak;
        };

        Band& getBand (int index)                           { return bands[(size_t) index]; }
        bool isOn (int band) const                          { return bands[(size_t) band].on.getCurrentValue() >= 0.5f; }
        bool isSoloed (int band) const                      { return bands[(size_t) band].solo.getCurrentValue() >= 0.5f; }
        bool anySoloed() const;

        /** Crossover frequencies, kept in order with at least a third of an octave between them. */
        std::array<float, numBands - 1> getCrossovers() const;
        te::ParameterWithStateValue& getCrossover (int index) { return crossovers[(size_t) index]; }

        /** A band's range in Hz (20 Hz / 20 kHz at the ends). */
        juce::Range<float> getBandRange (int band) const;

        static juce::String getBandName (int band);
        static float transfer (float inDb, float thresholdDb, float ratio)  { return CompressorPlugin::transfer (inDb, thresholdDb, ratio, kneeDb); }

        double getSampleRate() const                        { return sampleRate > 0.0 ? sampleRate : 48000.0; }

        te::ParameterWithStateValue output;
        std::array<te::ParameterWithStateValue, numBands - 1> crossovers;
        PeakMeter inputPeak[2], outputPeak[2];
        std::shared_ptr<AnalyserFifo> preFifo, postFifo;

        static constexpr float kneeDb = 6.0f;
        static constexpr float minCrossover = 30.0f, maxCrossover = 16000.0f;

    private:
        std::array<Band, numBands> bands;

        // Audio thread: filter states per crossover stage and channel, band buffers, envelopes.
        struct State { double z1 = 0, z2 = 0; };
        struct Stage { EqMath::Biquad coeffs; std::array<State, 2> state; };

        static double run (Stage&, int channel, double x) noexcept;

        // Crossover i: lowPass x2, highPass x2, and the all-pass used on bands below it.
        struct CrossoverStages { Stage lp1, lp2, hp1, hp2; std::array<Stage, 2> ap; };
        std::array<CrossoverStages, numBands - 1> stages;
        std::array<float, numBands - 1> designedFrequencies {};
        double designedRate = 0.0;

        std::array<double, numBands> envelopeDb {};
        std::array<juce::AudioBuffer<float>, numBands> bandBuffers;
        float lastOutputGain = 1.0f;

        void designCrossovers (double rate);

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MultibandPlugin)
    };
}
