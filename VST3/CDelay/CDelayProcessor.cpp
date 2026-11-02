#include "Common/SharedEffectProcessor.h"
#include "Shared/Delay/DelayView.h"

namespace cdaw
{
    /** C-Delay as a VST3: the engine, parameters and view of CDAW's built-in C-Delay, synced to the host's tempo. */
    class CDelayProcessor : public SharedEffectProcessor
    {
    public:
        CDelayProcessor() : SharedEffectProcessor ("C-Delay", "CDelay", DelayParameters::all(), true) {}

        double getTailLengthSeconds() const override            { return DelayEngine::getTailSeconds (getSettings(), getLastBpm()); }
        void prepareToPlay (double sampleRate, int maximumBlockSize) override   { engine.prepare (sampleRate, maximumBlockSize); }
        void reset() override                                   { engine.reset(); }

        void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
        {
            juce::ScopedNoDenormals noDenormals;
            copyMonoInput (buffer);
            readPlayHead();
            engine.process (buffer, 0, buffer.getNumSamples(), getSettings(), getLastBpm());
        }

        juce::AudioProcessorEditor* createEditor() override     { return new HostEffectEditor<DelayView, DelayEngine> (*this, engine); }

        DelayEngine::Settings getSettings() const               { return DelayParameters::toSettings (valueOf()); }

        DelayEngine engine;
    };
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new cdaw::CDelayProcessor();
}
