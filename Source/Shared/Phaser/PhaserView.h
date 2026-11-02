#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PhaserParameters.h"
#include "../UI/EffectWidgets.h"

namespace cdaw
{
    /** C-Phaser's editor, shared by both builds:
        - response: what the phaser does to each frequency right now (left side solid, right
          dashed), the notches marked; the range the sweep covers shaded, with handles for its
          centre and its depth; a dial with the LFO's position on each side
        - sweep: the response scrolling down over the last seconds as a heat map, so the notches
          draw their own paths (bright while sound is coming through)
        - knobs grouped by Sweep, Character and Output, and Sync. */
    class PhaserView : public juce::Component,
                       private juce::Timer
    {
    public:
        PhaserView (EffectModel&, PhaserEngine&);
        ~PhaserView() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        /** One frame of meter values (gain): input and output. Also scrolls the sweep. */
        void tick (float input, float output);

        static constexpr int defaultWidth = 1000, defaultHeight = 660, minimumWidth = 860, minimumHeight = 580;

        class ResponseView;
        class SweepView;

    private:
        void timerCallback() override;
        void updateRateKnob();

        EffectModel& model;
        PhaserEngine& engine;
        std::unique_ptr<ResponseView> response;
        std::unique_ptr<SweepView> sweep;
        juce::OwnedArray<ParamKnob> knobs;
        std::vector<EffectUI::KnobGroup> groups;
        ParamToggle syncButton;
        bool showingNote = false, rateKnobReady = false;
        float activity = 0.0f;
    };
}
