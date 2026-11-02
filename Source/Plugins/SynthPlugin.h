#pragma once

#include "SharedEffectPlugin.h"
#include "../Shared/DSP/SynthEngine.h"
#include "../Shared/Synth/SynthParameters.h"

namespace cdaw
{
    /** C-Synth: CDAW's polyphonic wavetable synth for pads (see SynthEngine). Plays the track's
        MIDI. Its DSP, parameters (SynthParameters), presets (SynthPresets) and editor view
        (SynthView) are shared with the VST3 build in VST3/CSynth. */
    class SynthPlugin : public SharedEffectPlugin
    {
    public:
        explicit SynthPlugin (te::PluginCreationInfo);
        ~SynthPlugin() override;

        static const char* getPluginName()      { return NEEDS_TRANS("C-Synth"); }
        static const char* xmlTypeName;

        juce::String getName() const override                   { return TRANS("C-Synth"); }
        juce::String getPluginType() override                   { return xmlTypeName; }
        juce::String getShortName (int) override                { return "C-Synth"; }
        juce::String getSelectableDescription() override        { return TRANS("C-Synth"); }

        bool takesMidiInput() override                          { return true; }
        bool isSynth() override                                 { return true; }
        bool producesAudioWhenNoAudioInput() override           { return true; }
        bool noTail() override                                  { return false; }
        double getTailLength() const override;

        void initialise (const te::PluginInitialisationInfo&) override;
        void deinitialise() override                            {}
        void reset() override                                   { engine.reset(); }
        void midiPanic() override                               { engine.panic(); }
        void applyToBuffer (const te::PluginRenderContext&) override;
        std::unique_ptr<EditorComponent> createEditor() override;

        SynthEngine::Settings getSettings() const               { return SynthParameters::toSettings (valueOf()); }

        SynthEngine engine;

    private:
        juce::MidiBuffer midi;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SynthPlugin)
    };
}
