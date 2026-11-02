#include "Common/SharedEffectProcessor.h"
#include "Shared/Esser/EsserView.h"

namespace cdaw
{
    /** C-Esser as a VST3: the engine, parameters and view of CDAW's built-in C-Esser. */
    class CEsserProcessor : public SharedEffectProcessor
    {
    public:
        CEsserProcessor() : SharedEffectProcessor ("C-Esser", "CEsser", EsserParameters::all(), true) {}

        void prepareToPlay (double sampleRate, int maximumBlockSize) override   { engine.prepare (sampleRate, maximumBlockSize); }
        void reset() override                                   { engine.reset(); }

        void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
        {
            juce::ScopedNoDenormals noDenormals;
            copyMonoInput (buffer);
            engine.process (buffer, 0, buffer.getNumSamples(), EsserParameters::toSettings (valueOf()));
        }

        juce::AudioProcessorEditor* createEditor() override     { return new HostEffectEditor<EsserView, EsserEngine> (*this, engine); }

        EsserEngine engine;
    };
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new cdaw::CEsserProcessor();
}
