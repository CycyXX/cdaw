#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "EsserParameters.h"
#include "../UI/EffectWidgets.h"

namespace cdaw
{
    /** C-Esser's editor, shared by both builds:
        - band: the sibilant band shaded, with handles for its low and high edge (drag the middle
          to move both); what the de-esser does to each frequency right now, and at full range
        - activity: the band's level scrolling past against the threshold (which moves with auto),
          the reduction hanging from the top, and a reduction meter
        - knobs grouped by Detection, Reduction and Output; mode, listen and auto in the header. */
    class EsserView : public juce::Component,
                      private juce::Timer
    {
    public:
        EsserView (EffectModel&, EsserEngine&);
        ~EsserView() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        /** One frame of meter values: band level (gain), threshold (dB) and reduction (dB). Scrolls the activity. */
        void tick (float bandLevel, float thresholdDb, float reductionDb);

        static constexpr int defaultWidth = 940, defaultHeight = 620, minimumWidth = 780, minimumHeight = 540;

        class BandView;
        class ActivityView;

    private:
        void timerCallback() override;

        EffectModel& model;
        EsserEngine& engine;
        std::unique_ptr<BandView> band;
        std::unique_ptr<ActivityView> activity;
        juce::OwnedArray<ParamKnob> knobs;
        std::vector<EffectUI::KnobGroup> groups;
        ChoiceButtons modeButtons, listenButtons;
        ParamToggle autoButton;
    };
}
