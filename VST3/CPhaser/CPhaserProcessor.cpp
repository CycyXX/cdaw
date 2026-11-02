#include "Common/SharedEffectProcessor.h"
#include "Shared/Phaser/PhaserView.h"

namespace cdaw
{
    /** C-Phaser as a VST3: the engine, parameters and view of CDAW's built-in C-Phaser, synced to the host's tempo. */
    class CPhaserProcessor : public SharedEffectProcessor
    {
    public:
        CPhaserProcessor() : SharedEffectProcessor ("C-Phaser", "CPhaser", PhaserParameters::all(), true) {}

        void prepareToPlay (double sampleRate, int maximumBlockSize) override   { engine.prepare (sampleRate, maximumBlockSize); }
        void reset() override                                   { engine.reset(); }

        void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
        {
            juce::ScopedNoDenormals noDenormals;
            copyMonoInput (buffer);
            const auto song = readPlayHead();
            engine.process (buffer, 0, buffer.getNumSamples(), PhaserParameters::toSettings (valueOf()), getLastBpm(), song);
        }

        juce::AudioProcessorEditor* createEditor() override     { return new HostEffectEditor<PhaserView, PhaserEngine> (*this, engine); }

        PhaserEngine engine;
    };
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new cdaw::CPhaserProcessor();
}
