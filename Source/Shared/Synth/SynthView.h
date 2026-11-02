#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "SynthPanels.h"
#include "SynthPresets.h"

namespace cdaw
{
    /** C-Synth's editor, shared by both builds:
        - header: the preset browser (step, pick by category, save your own), the four macros
          under their preset's names, volume, the output level and the pages
        - SYNTH: the oscillators as wavetable stacks with the frame playing now, the layers
          (sub, noise, drift), the filter with every voice's curve, both envelopes with a dot per
          voice, the LFOs
        - MOD: the modulation matrix with what each slot adds right now, its movement over time,
          the macros and the voice settings
        - FX: chorus, delay and reverb, and the signal's path through them
        - footer: a keyboard with an orb over each sounding key, the stereo image and the
          output spectrum.
        Knobs that something modulates show how far it can move them and where they are now. */
    class SynthView : public juce::Component,
                      private juce::Timer
    {
    public:
        SynthView (EffectModel&, SynthEngine&);
        ~SynthView() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        enum Page { synthPage = 0, modPage, fxPage };
        void showPage (int);

        /** One display frame (the timer calls it; tests and snapshots too). */
        void tick();

        /** For tests: the stereo display, to feed. */
        StereoField& getStereoField()                   { return *stereo; }

        void applyPreset (const SynthPresets::Preset&);
        juce::String getPresetName() const;

        static constexpr int defaultWidth = 1280, defaultHeight = 850, minimumWidth = 1180, minimumHeight = 780;

    private:
        void timerCallback() override                   { tick(); }
        void updateModulationRings();
        void updatePresetButton();
        void stepPreset (int delta);
        void showPresetMenu();
        void savePreset();
        ParamKnob* knobFor (int id) const;

        EffectModel& model;
        SynthEngine& engine;

        juce::TextButton previousButton { "<" }, nextButton { ">" }, presetButton, saveButton { "Save" };
        juce::OwnedArray<ParamKnob> macroKnobs;
        std::unique_ptr<ParamKnob> volumeKnob;
        juce::OwnedArray<juce::TextButton> tabs;

        juce::Component pages[3];
        juce::OwnedArray<SynthPanel> panels[3];
        std::unique_ptr<VoiceField> voiceField;
        std::unique_ptr<StereoField> stereo;
        std::unique_ptr<OutputSpectrum> spectrum;

        std::vector<SynthPresets::Preset> presets;
        int page = synthPage;
        unsigned frame = 0;
        float outputLevel[2] {};
        juce::Rectangle<int> headerArea, meterArea, flowArea;
    };
}
