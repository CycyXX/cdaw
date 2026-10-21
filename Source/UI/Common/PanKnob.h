#pragma once

#include "../../PCH.h"

namespace cdaw
{
    /** Pan knob (-1 = hard left, +1 = hard right) with L / R markings beside the dial,
        a live value bubble while dragging and a tooltip showing the current position. */
    class PanKnob : public juce::Slider
    {
    public:
        PanKnob();

        /** "C", "L 35", "R 100" */
        static juce::String describe (double pan);

        juce::String getTextFromValue (double) override;
        double getValueFromText (const juce::String&) override;
        juce::String getTooltip() override;
        void paint (juce::Graphics&) override;

        /** Width needed for a knob of the given height including the L / R letters. */
        static int widthForHeight (int h)       { return h + juce::jmax (12, h / 2); }
    };
}
