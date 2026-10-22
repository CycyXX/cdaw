#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Shared/DSP/MultibandEngine.h"
#include "Shared/Multiband/MultibandParameters.h"

namespace cdaw
{
    /** C-Multicomp as a VST3 (for Cubase and other hosts). The sound, the parameters and the
        editor are the ones CDAW's built-in C-Multicomp uses (Source/Shared): only this host glue
        is separate, so a change there changes both. */
    class CMulticompProcessor : public juce::AudioProcessor
    {
    public:
        CMulticompProcessor();

        const juce::String getName() const override             { return "C-Multicomp"; }
        bool acceptsMidi() const override                       { return false; }
        bool producesMidi() const override                      { return false; }
        double getTailLengthSeconds() const override            { return 0.0; }

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
        MultibandEngine::Settings getSettings() const;
        juce::RangedAudioParameter& getHostParameter (int index)    { return *parameters[(size_t) index]; }

        MultibandEngine engine;
        juce::AudioProcessorValueTreeState state;

    private:
        static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

        std::array<juce::RangedAudioParameter*, MultibandParameters::count> parameters {};
        std::array<std::atomic<float>*, MultibandParameters::count> values {};

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CMulticompProcessor)
    };
}
