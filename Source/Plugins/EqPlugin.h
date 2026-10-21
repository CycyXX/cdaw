#pragma once

#include "../PCH.h"
#include "../Shared/DSP/EqMath.h"

namespace cdaw
{
    struct AnalyserFifo;

    /** CDAW's 8-band parametric EQ (in the spirit of Cubase's Frequency). Each band has an
        on switch, a shape (low/high cut at 6-48 dB/oct, low/high shelf, peak, notch), frequency,
        gain and Q; plus an output gain. Feeds pre- and post-EQ audio to its editor's spectrum. */
    class EqPlugin : public te::Plugin
    {
    public:
        static constexpr int numBands = 8;

        explicit EqPlugin (te::PluginCreationInfo);
        ~EqPlugin() override;

        static const char* getPluginName()      { return NEEDS_TRANS("EQ"); }
        static const char* xmlTypeName;

        juce::String getName() const override                   { return TRANS("EQ"); }
        juce::String getPluginType() override                   { return xmlTypeName; }
        juce::String getShortName (int) override                { return "EQ"; }
        juce::String getSelectableDescription() override        { return TRANS("8-band EQ"); }
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
            te::ParameterWithStateValue on, freq, gain, q;
            juce::CachedValue<int> type, slope;
        };

        Band& getBand (int index)                           { return bands[(size_t) index]; }
        EqMath::BandType getType (int band) const;
        int getSlope (int band) const;                      // index into EqMath::slopes
        bool isOn (int band) const;

        void setType (int band, EqMath::BandType);
        void setSlope (int band, int slopeIndex);
        void setOn (int band, bool);

        /** Default settings for a band (its frequency spread across the spectrum, off). */
        static EqMath::BandType getDefaultType (int band);
        static float getDefaultFreq (int band);
        static float getDefaultQ (EqMath::BandType);

        /** The band's filter with its current settings. */
        EqMath::BandFilter designBand (int band, double sampleRate) const;

        /** Total response in dB at `hz` (bands that are on, plus output gain). */
        double getResponseDb (double hz, double sampleRate) const;

        double getSampleRate() const                        { return sampleRate > 0.0 ? sampleRate : 48000.0; }

        te::ParameterWithStateValue output;

        std::shared_ptr<AnalyserFifo> preFifo, postFifo;

        static constexpr float minFreq = 20.0f, maxFreq = 20000.0f, maxGain = 24.0f, minQ = 0.1f, maxQ = 18.0f;

    private:
        void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
        void updateTypeCache();

        std::array<Band, numBands> bands;
        std::array<std::atomic<int>, numBands> typeCache, slopeCache;     // read by the audio thread

        // Audio thread
        struct Section { double z1 = 0, z2 = 0; };
        struct Settings { int on = 0, type = -1, slope = -1; float freq = 0, gain = 0, q = 0; bool operator!= (const Settings&) const; };
        std::array<EqMath::BandFilter, numBands> filters;
        std::array<Settings, numBands> designed;
        std::array<std::array<std::array<Section, 2>, 4>, numBands> filterState {};
        float lastOutputGain = 1.0f;
        double designedRate = 0.0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EqPlugin)
    };
}
