#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "FilterParameters.h"
#include "../UI/EffectWidgets.h"

namespace cdaw
{
    /** C-Filter's editor, shared by both builds:
        - response: the filter's curve where the cutoff is right now (with a fading trail as it
          moves; the right side dashed when the LFO is offset), the set curve faintly, the span
          the LFO and envelope can move it over shaded, and the output's spectrum behind. Drag
          the node sideways for the cutoff, up and down (or the mouse wheel) for the resonance.
        - motion: the cutoff over the last seconds on the same frequency scale, scrolling down,
          so the LFO draws its shape and the envelope its swells
        - type and slope, the LFO's shape and Sync, and knobs grouped by Filter, LFO, Envelope
          and Output. */
    class FilterView : public juce::Component,
                       private juce::Timer
    {
    public:
        FilterView (EffectModel&, FilterEngine&);
        ~FilterView() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        void visibilityChanged() override;

        /** One frame: input and output peaks (gain), where the cutoff is on each side (Hz) and
            the envelope (0..1). */
        void tick (float input, float output, float cutoffLeft, float cutoffRight, float envelope);

        /** Reads new audio into the spectrum (the timer does this while showing). */
        void updateSpectrum();

        static constexpr int defaultWidth = 1040, defaultHeight = 680, minimumWidth = 900, minimumHeight = 600;

        class ResponseView;
        class MotionView;

    private:
        void timerCallback() override;
        void updateRateKnob();

        EffectModel& model;
        FilterEngine& engine;
        std::unique_ptr<ResponseView> response;
        std::unique_ptr<MotionView> motion;
        juce::OwnedArray<ParamKnob> knobs;
        std::vector<EffectUI::KnobGroup> groups;
        ChoiceButtons typeButtons, slopeButtons, shapeButtons;
        ParamToggle syncButton;
        bool showingNote = false, rateKnobReady = false;
        unsigned frame = 0;
    };
}
