#include "Common/SharedEffectProcessor.h"
#include "Shared/Synth/SynthView.h"

namespace cdaw
{
    /** C-Synth as a VST3 instrument: the engine, parameters, presets and view of CDAW's built-in
        C-Synth, playing the host's MIDI in step with its tempo and position. */
    class CSynthProcessor : public SharedEffectProcessor
    {
    public:
        CSynthProcessor() : SharedEffectProcessor ("C-Synth", "CSynth", SynthParameters::all(), true, true) {}

        double getTailLengthSeconds() const override
        {
            const auto s = SynthParameters::toSettings (valueOf());
            return juce::jmin (30.0, s.ampEnv.release + (s.reverbOn ? s.reverbDecay * 1.1 : 0.0)
                                     + (s.delayOn ? DelayEngine::getTailSeconds (SynthEngine::delaySettings (s), getLastBpm()) : 0.0));
        }

        void prepareToPlay (double sampleRate, int maximumBlockSize) override   { engine.prepare (sampleRate, maximumBlockSize); }
        void reset() override                                   { engine.reset(); }

        void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
        {
            juce::ScopedNoDenormals noDenormals;
            const auto song = readPlayHead();
            engine.process (buffer, 0, buffer.getNumSamples(), midi, SynthParameters::toSettings (valueOf()), getLastBpm(), song);
        }

        juce::AudioProcessorEditor* createEditor() override     { return new HostEffectEditor<SynthView, SynthEngine> (*this, engine); }

        SynthEngine engine;
    };
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new cdaw::CSynthProcessor();
}
