#pragma once

#include <juce_core/juce_core.h>
#include <memory>

namespace cdaw
{
    /** What a knob or a drag in a plugin view needs from a parameter, whoever owns it: CDAW's
        Tracktion plugins (TeKnobParameter) or the VST3 builds (JUCE's AudioProcessor parameters).
        Values are in the parameter's own units (dB, Hz, ms...). */
    struct KnobParameter
    {
        virtual ~KnobParameter() = default;

        virtual float getValue() const = 0;
        virtual void setValue (float) = 0;
        virtual void beginGesture() = 0;
        virtual void endGesture() = 0;
        virtual juce::Range<float> getRange() const = 0;
        virtual juce::String getText (float value) const = 0;

        juce::String getCurrentText() const     { return getText (getValue()); }
    };
}
