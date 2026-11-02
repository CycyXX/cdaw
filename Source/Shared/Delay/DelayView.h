#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "DelayParameters.h"
#include "../UI/EffectWidgets.h"

namespace cdaw
{
    /** C-Delay's editor, shared by both builds:
        - the echo map: time runs to the right, the left side's echoes rise above the centre line
          and the right side's hang below, as tall as they are loud. What comes in scrolls along
          underneath (age to the right), so each echo lights up as the sound reaches it. Drag the
          first echo sideways for the time (snapping to notes when synced), the second up or
          down for the feedback.
        - repeats: the loop's cuts after one, two, three... passes (each repeat is darker and
          thinner than the last), with handles for the cuts, and the drive's curve.
        - knobs grouped by Time, Feedback, Motion and Output; Sync and the stereo / ping-pong mode. */
    class DelayView : public juce::Component,
                      private juce::Timer
    {
    public:
        DelayView (EffectModel&, DelayEngine&);
        ~DelayView() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        /** One frame of meter values (gain): input per side, echoes per side, ducking in dB. */
        void tick (float inL, float inR, float wetL, float wetR, float duckDb);

        static constexpr int defaultWidth = 1060, defaultHeight = 620, minimumWidth = 900, minimumHeight = 540;

        class EchoMap;
        class RepeatsView;

    private:
        void timerCallback() override;
        void updateTimeKnob();

        EffectModel& model;
        DelayEngine& engine;
        std::unique_ptr<EchoMap> echoMap;
        std::unique_ptr<RepeatsView> repeats;
        juce::OwnedArray<ParamKnob> knobs;
        std::vector<EffectUI::KnobGroup> groups;
        ParamToggle syncButton;
        ChoiceButtons modeButtons;
        bool showingNote = false, timeKnobReady = false;
        int emptyTicks = 0;
    };
}
