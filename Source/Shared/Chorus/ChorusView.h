#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "ChorusParameters.h"
#include "../UI/EffectWidgets.h"

namespace cdaw
{
    /** C-Chorus's editor, shared by both builds:
        - voices: each voice's delay sweeping over the last few cycles (left side solid, right
          dashed), the range the sweep covers shaded behind
        - space: the voices seen from above, as sources moving towards and away from the
          listener (a delay is a distance: 1 ms is about 34 cm). Blue while one approaches (its
          pitch rises), red while it moves away: the Doppler shift that makes a chorus.
        - response: the comb filter the voices make with the dry sound, live; feedback turns its
          peaks into a flanger's
        - Chorus / Ensemble / Flanger / Vibrato starting points, and knobs grouped by Voices,
          Motion, Colour and Output. */
    class ChorusView : public juce::Component,
                       private juce::Timer
    {
    public:
        ChorusView (EffectModel&, ChorusEngine&);
        ~ChorusView() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        /** One frame of meter values (gain): input and the voices' output. */
        void tick (float input, float wet);

        static constexpr int defaultWidth = 1020, defaultHeight = 620, minimumWidth = 880, minimumHeight = 540;

        class VoiceTraces;
        class SpaceView;
        class ResponseView;

    private:
        void timerCallback() override;
        void applyCharacter (int index);
        void updateCharacterButtons();

        EffectModel& model;
        ChorusEngine& engine;
        std::unique_ptr<VoiceTraces> traces;
        std::unique_ptr<SpaceView> space;
        std::unique_ptr<ResponseView> response;
        juce::OwnedArray<ParamKnob> knobs;
        juce::OwnedArray<juce::TextButton> characterButtons;
        std::vector<EffectUI::KnobGroup> groups;
        float wetLevel = 0.0f;
        int emptyTicks = 0;
    };
}
