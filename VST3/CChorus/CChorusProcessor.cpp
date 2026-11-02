#include "Common/SharedEffectProcessor.h"
#include "Shared/Chorus/ChorusView.h"

namespace cdaw
{
    /** C-Chorus as a VST3: the engine, parameters and view of CDAW's built-in C-Chorus. */
    class CChorusProcessor : public SharedEffectProcessor
    {
    public:
        CChorusProcessor() : SharedEffectProcessor ("C-Chorus", "CChorus", ChorusParameters::all(), true) {}

        void prepareToPlay (double sampleRate, int maximumBlockSize) override   { engine.prepare (sampleRate, maximumBlockSize); }
        void reset() override                                   { engine.reset(); }

        void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
        {
            juce::ScopedNoDenormals noDenormals;
            copyMonoInput (buffer);
            engine.process (buffer, 0, buffer.getNumSamples(), ChorusParameters::toSettings (valueOf()), readPlayHead());
        }

        juce::AudioProcessorEditor* createEditor() override     { return new HostEffectEditor<ChorusView, ChorusEngine> (*this, engine); }

        ChorusEngine engine;
    };
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new cdaw::CChorusProcessor();
}
