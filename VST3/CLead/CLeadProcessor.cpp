#include "Common/SharedEffectProcessor.h"
#include "Shared/Lead/LeadView.h"

namespace cdaw
{
    /** C-Lead as a VST3 instrument: the engine, parameters, presets and view of CDAW's built-in
        C-Lead, playing the host's MIDI in step with its tempo and position. */
    class CLeadProcessor : public SharedEffectProcessor
    {
    public:
        CLeadProcessor() : SharedEffectProcessor ("C-Lead", "CLead", LeadParameters::all(), true, true) {}

        double getTailLengthSeconds() const override
        {
            const auto s = LeadParameters::toSettings (valueOf());
            return juce::jmin (30.0, s.ampEnv.release + (s.reverbOn ? s.reverbDecay * 1.1 : 0.0)
                                     + (s.delayOn ? DelayEngine::getTailSeconds (LeadEngine::delaySettings (s), getLastBpm()) : 0.0));
        }

        void prepareToPlay (double sampleRate, int maximumBlockSize) override   { engine.prepare (sampleRate, maximumBlockSize); }
        void reset() override                                   { engine.reset(); }

        void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
        {
            juce::ScopedNoDenormals noDenormals;
            const auto song = readPlayHead();
            engine.process (buffer, 0, buffer.getNumSamples(), midi, LeadParameters::toSettings (valueOf()), getLastBpm(), song);
        }

        juce::AudioProcessorEditor* createEditor() override     { return new HostEffectEditor<LeadView, LeadEngine> (*this, engine); }

        LeadEngine engine;
    };
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new cdaw::CLeadProcessor();
}
