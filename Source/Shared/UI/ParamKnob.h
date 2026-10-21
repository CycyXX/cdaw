#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "KnobParameter.h"

namespace cdaw
{
    /** Rotary knob bound to a plugin parameter: name above, value below, coloured arc from the
        default (or from the start for one-sided ranges), drag gestures recorded for undo and
        automation. Double-click returns to the default. Call refresh() from the editor's timer.
        `inverted` turns the dial the other way (e.g. a ratio stored as 1/ratio); `skewMidPoint` is
        the parameter value shown at the middle of the dial. */
    class ParamKnob : public juce::Component
    {
    public:
        ParamKnob (std::shared_ptr<KnobParameter>, const juce::String& label, juce::Colour colour,
                   float defaultValue, float skewMidPoint = 0.0f, bool inverted = false);
        ~ParamKnob() override;

        /** Picks up changes made elsewhere (automation, other editors, undo). */
        void refresh();

        /** Shows a different parameter (e.g. when the selected band changes). */
        void setParameter (std::shared_ptr<KnobParameter>, float defaultValue, float skewMidPoint = 0.0f, bool inverted = false);

        void setColour (juce::Colour);
        void setLabel (const juce::String&);

        void resized() override;
        void paint (juce::Graphics&) override;

    private:
        struct Dial;
        double toDial (double paramValue) const;
        double toParam (double dialValue) const;

        std::shared_ptr<KnobParameter> param;
        juce::String label;
        juce::Colour colour;
        std::unique_ptr<Dial> dial;
        juce::String shownValue;
        bool inverted = false;
    };
}
