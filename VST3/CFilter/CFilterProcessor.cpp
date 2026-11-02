#include "Common/SharedEffectProcessor.h"
#include "Shared/Filter/FilterView.h"

namespace cdaw
{
    /** C-Filter as a VST3: the engine, parameters and view of CDAW's built-in C-Filter, its LFO synced to the host's tempo. */
    class CFilterProcessor : public SharedEffectProcessor
    {
    public:
        CFilterProcessor() : SharedEffectProcessor ("C-Filter", "CFilter", FilterParameters::all(), false) {}

        void prepareToPlay (double sampleRate, int maximumBlockSize) override   { engine.prepare (sampleRate, maximumBlockSize); }
        void reset() override                                   { engine.reset(); }

        void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
        {
            juce::ScopedNoDenormals noDenormals;
            const auto song = readPlayHead();
            engine.process (buffer, 0, buffer.getNumSamples(), FilterParameters::toSettings (valueOf()), getLastBpm(), song);
        }

        juce::AudioProcessorEditor* createEditor() override     { return new HostEffectEditor<FilterView, FilterEngine> (*this, engine); }

        FilterEngine engine;
    };
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new cdaw::CFilterProcessor();
}
