#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Shared/DSP/CompressorEngine.h"
#include "Shared/Compressor/CompressorParameters.h"

namespace cdaw
{
    /** C-Comp as a VST3 (for Cubase and other hosts). The sound, the parameters and the editor
        are the ones CDAW's built-in C-Comp uses (Source/Shared): only this host glue is separate,
        so a change there changes both. The host's sidechain input keys the detector when the
        "Sidechain" switch (a VST3-only parameter, off by default) is on: hosts may enable the
        input without routing anything to it, and a silent key would stop all compression. */
    class CCompProcessor : public juce::AudioProcessor
    {
    public:
        CCompProcessor();

        const juce::String getName() const override             { return "C-Comp"; }
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
        CompressorEngine::Settings getSettings() const;
        juce::RangedAudioParameter& getHostParameter (CompressorParameters::ID id)     { return *parameters[(size_t) id]; }

        static constexpr const char* sidechainSwitchID = "sidechain";
        juce::RangedAudioParameter& getSidechainSwitch()        { return *sidechainSwitch; }
        bool isSidechainOn() const                              { return sidechainSwitchValue->load() >= 0.5f; }
        bool isSidechainConnected() const;                      // the host has enabled the sidechain input

        CompressorEngine engine;
        juce::AudioProcessorValueTreeState state;

    private:
        static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

        std::array<juce::RangedAudioParameter*, CompressorParameters::count> parameters {};
        std::array<std::atomic<float>*, CompressorParameters::count> values {};
        juce::RangedAudioParameter* sidechainSwitch = nullptr;
        std::atomic<float>* sidechainSwitchValue = nullptr;
        std::vector<float> key;         // the sidechain input as one channel

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CCompProcessor)
    };
}
