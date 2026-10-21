#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Shared/CVerb/CVerbParameters.h"
#include "Shared/DSP/ReverbEngine.h"

namespace cdaw
{
    /** C-Verb as a VST3 (for Cubase and other hosts). The sound, the parameters and the editor
        are the ones CDAW's built-in C-Verb uses (Source/Shared): only this host glue is separate,
        so a change there changes both. */
    class CVerbProcessor : public juce::AudioProcessor
    {
    public:
        CVerbProcessor();

        const juce::String getName() const override             { return "C-Verb"; }
        bool acceptsMidi() const override                       { return false; }
        bool producesMidi() const override                      { return false; }
        double getTailLengthSeconds() const override;

        bool isBusesLayoutSupported (const BusesLayout&) const override;
        void prepareToPlay (double sampleRate, int maximumBlockSize) override;
        void releaseResources() override                        {}
        void reset() override                                   { engine.reset(); }
        void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

        bool hasEditor() const override                         { return true; }
        juce::AudioProcessorEditor* createEditor() override;

        int getNumPrograms() override                           { return 1; }
        int getCurrentProgram() override                        { return 0; }
        void setCurrentProgram (int) override                   {}
        const juce::String getProgramName (int) override        { return "Default"; }
        void changeProgramName (int, const juce::String&) override {}

        void getStateInformation (juce::MemoryBlock&) override;
        void setStateInformation (const void*, int) override;

        //==============================================================================
        ReverbEngine::Settings getSettings() const;
        juce::RangedAudioParameter& getParameter (CVerbParameters::ID id)  { return *parameters[(size_t) id]; }

        ReverbEngine engine;
        juce::AudioProcessorValueTreeState state;

    private:
        static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

        std::array<juce::RangedAudioParameter*, CVerbParameters::count> parameters {};
        std::array<std::atomic<float>*, CVerbParameters::count> values {};
        int maxBlockSize = 512;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CVerbProcessor)
    };
}
