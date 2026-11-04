#pragma once

#include "SharedEffectPlugin.h"
#include "Shared/DSP/LeadEngine.h"
#include "Shared/Lead/LeadParameters.h"

namespace cdaw
{
    /** C-Lead: CDAW's virtual analog synth for leads (see LeadEngine). Plays the track's MIDI.
        Its DSP, parameters (LeadParameters), presets (LeadPresets) and editor view (LeadView)
        are shared with the VST3 build in VST3/CLead. */
    class LeadPlugin : public SharedEffectPlugin
    {
    public:
        explicit LeadPlugin (te::PluginCreationInfo);
        ~LeadPlugin() override;

        static const char* getPluginName()      { return NEEDS_TRANS("C-Lead"); }
        static const char* xmlTypeName;

        juce::String getName() const override                   { return TRANS("C-Lead"); }
        juce::String getPluginType() override                   { return xmlTypeName; }
        juce::String getShortName (int) override                { return "C-Lead"; }
        juce::String getSelectableDescription() override        { return TRANS("C-Lead"); }

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

        LeadEngine::Settings getSettings() const                { return LeadParameters::toSettings (valueOf()); }

        LeadEngine engine;

    private:
        juce::MidiBuffer midi;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LeadPlugin)
    };
}
