#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "LeadPanels.h"
#include "LeadPresets.h"

namespace cdaw
{
    /** C-Lead's editor, shared by both builds:
        - header: the preset browser (step, pick by category, save your own), the four macros
          under their preset's names, volume, the output level and the pages
        - PLAY: the pitch playing now scrolling past a keyboard (glides, vibrato, bends), voice
          mode, glide and vibrato; both oscillators as they play (B with sync, FM and ring); a
          phase-locked scope; drive with its curve; the filter with every voice's curve; both
          envelopes with a dot per voice
        - MOD: the modulation matrix with what each slot adds right now, the LFOs, the slots'
          movement, the macros and how the keys play (priority, voices, bends)
        - FX: chorus, delay and reverb, and the signal's path through them
        - footer: a keyboard with an orb at each voice's pitch, the stereo image and the spectrum.
        Knobs that something modulates show how far it can move them and where they are now. */
    class LeadView : public juce::Component,
                     private juce::Timer
    {
    public:
        LeadView (EffectModel&, LeadEngine&);
        ~LeadView() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        enum Page { playPage = 0, modPage, fxPage };
        void showPage (int);

        /** One display frame (the timer calls it; tests and snapshots too). */
        void tick();

        /** For tests: the stereo display, to feed. */
        LeadUI::StereoField& getStereoField()           { return *stereo; }

        void applyPreset (const LeadPresets::Preset&);
        juce::String getPresetName() const;

        static constexpr int defaultWidth = 1360, defaultHeight = 860, minimumWidth = 1260, minimumHeight = 800;

    private:
        void timerCallback() override                   { tick(); }
        void updateModulationRings();
        void updatePresetButton();
        void stepPreset (int delta);
        void showPresetMenu();
        void savePreset();
        ParamKnob* knobFor (int id) const;

        EffectModel& model;
        LeadEngine& engine;

        juce::TextButton previousButton { "<" }, nextButton { ">" }, presetButton, saveButton { "Save" };
        juce::OwnedArray<ParamKnob> macroKnobs;
        std::unique_ptr<ParamKnob> volumeKnob;
        juce::OwnedArray<juce::TextButton> tabs;

        juce::Component pages[3];
        juce::OwnedArray<LeadUI::Panel> panels[3];
        std::unique_ptr<LeadUI::VoiceField> voiceField;
        std::unique_ptr<LeadUI::StereoField> stereo;
        std::unique_ptr<LeadUI::OutputSpectrum> spectrum;

        std::vector<LeadPresets::Preset> presets;
        int page = playPage;
        unsigned frame = 0;
        float outputLevel[2] {};
        juce::Rectangle<int> headerArea, meterArea, flowArea;
    };
}
