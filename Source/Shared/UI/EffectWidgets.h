#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Common/EffectModel.h"
#include "ParamKnob.h"

namespace cdaw
{
    /** Drawing and controls shared by the views of C-Delay, C-Chorus, C-Phaser and C-Filter, so
        they look like one family (and like C-Verb, C-Comp and C-Multicomp). */
    namespace EffectUI
    {
        constexpr int headerHeight = 34, knobRowHeight = 128;
        inline const juce::Colour plotBackground { 0xff15171b };

        /** A dark rounded panel with an outline, optionally with a small title at its top left. */
        void drawPanel (juce::Graphics&, juce::Rectangle<float>, const juce::String& title = {});

        /** The plugin's name in bold and a one-line summary after it. */
        void drawHeader (juce::Graphics&, juce::Rectangle<int> header, const juce::String& title, const juce::String& summary);

        /** A row of knobs in titled groups (as in C-Verb). */
        struct KnobGroup { juce::String title; int first, count; juce::Rectangle<int> area; };
        void layoutKnobGroups (std::vector<KnobGroup>&, const juce::OwnedArray<ParamKnob>&, juce::Rectangle<int> area);
        void paintKnobGroups (juce::Graphics&, const std::vector<KnobGroup>&);

        /** A log frequency scale across a rectangle. */
        struct FrequencyAxis
        {
            double lowHz = 20.0, highHz = 20000.0;

            float toX (double hz, juce::Rectangle<float> plot) const
            {
                return plot.getX() + (float) (std::log (juce::jmax (1.0, hz) / lowHz) / std::log (highHz / lowHz)) * plot.getWidth();
            }

            double toHz (float x, juce::Rectangle<float> plot) const
            {
                return lowHz * std::pow (highHz / lowHz, juce::jlimit (0.0, 1.0, (double) (x - plot.getX()) / plot.getWidth()));
            }
        };

        /** Vertical lines at 1-2-5 steps (labelled below the plot when `labels`), minor lines between. */
        void drawFrequencyGrid (juce::Graphics&, juce::Rectangle<float> plot, const FrequencyAxis&, bool labels);

        juce::String formatHz (double hz);

        /** A soft glow: concentric translucent circles. */
        void drawGlow (juce::Graphics&, juce::Point<float> centre, float radius, juce::Colour, float strength);

        /** A draggable handle (as in C-Comp's curve). */
        void drawHandle (juce::Graphics&, juce::Point<float>, juce::Colour, bool active, const juce::String& letter = {});
    }

    //==============================================================================
    /** A segmented switch for a choice parameter: one segment per value, the current one lit.
        Segments show text, or an icon when drawIcon is set. */
    class ChoiceButtons : public juce::Component,
                          public juce::SettableTooltipClient
    {
    public:
        ChoiceButtons (EffectModel&, int parameterId, juce::StringArray labels, juce::Colour, const juce::String& undoName);

        /** Draws segment `index`'s icon in `area` (instead of its label). */
        std::function<void (juce::Graphics&, juce::Rectangle<float> area, int index, juce::Colour)> drawIcon;

        /** The value of each segment, when it isn't simply its index. */
        std::vector<float> values;

        void refresh();
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;

    private:
        int segmentAt (juce::Point<float>) const;
        float valueOf (int segment) const;
        int current() const;

        EffectModel& model;
        const int id;
        juce::StringArray labels;
        juce::Colour colour;
        juce::String undoName;
        int shown = -1, hover = -1;
    };

    /** A drop-down list for a choice parameter (the value is the item's index). */
    class ParamCombo : public juce::ComboBox
    {
    public:
        ParamCombo (EffectModel&, int parameterId, const juce::StringArray& names, const juce::String& undoName);
        void refresh();

    private:
        EffectModel& model;
        const int id;
        juce::String undoName;
        bool updating = false;
    };

    /** An on / off button for a switch parameter. */
    class ParamToggle : public juce::TextButton
    {
    public:
        ParamToggle (EffectModel&, int parameterId, const juce::String& text, juce::Colour onColour, const juce::String& tooltip);
        void refresh();

    private:
        EffectModel& model;
        const int id;
    };
}
