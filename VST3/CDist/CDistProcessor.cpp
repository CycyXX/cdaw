#include "Common/SharedEffectProcessor.h"
#include "Shared/Dist/DistView.h"

namespace cdaw
{
    /** C-Dist as a VST3: the engine, parameters, presets and view of CDAW's built-in C-Dist,
        reporting its oversampling's latency to the host. */
    class CDistProcessor : public SharedEffectProcessor
    {
    public:
        CDistProcessor() : SharedEffectProcessor ("C-Dist", "CDist", DistParameters::all(), false) {}

        void prepareToPlay (double sampleRate, int maximumBlockSize) override
        {
            engine.prepare (sampleRate, maximumBlockSize);
            setLatencySamples (engine.getLatencySamples());
        }

        void reset() override                                   { engine.reset(); }

        void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
        {
            juce::ScopedNoDenormals noDenormals;
            engine.process (buffer, 0, buffer.getNumSamples(), DistParameters::toSettings (valueOf()));
        }

        juce::AudioProcessorEditor* createEditor() override     { return new HostEffectEditor<DistView, DistEngine> (*this, engine); }

        DistEngine engine;
    };
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new cdaw::CDistProcessor();
}
