#pragma once

#include "../../PCH.h"
#include "../../Shared/UI/KnobParameter.h"

namespace cdaw
{
    /** A Tracktion plugin parameter for the shared knobs and views. */
    struct TeKnobParameter : public KnobParameter
    {
        explicit TeKnobParameter (te::AutomatableParameter& p) : param (&p) {}

        float getValue() const override                     { return param->getCurrentValue(); }
        void setValue (float v) override                    { param->setParameter (v, juce::sendNotification); }
        void beginGesture() override                        { param->parameterChangeGestureBegin(); }
        void endGesture() override                          { param->parameterChangeGestureEnd(); }
        juce::Range<float> getRange() const override        { return param->getValueRange(); }
        juce::String getText (float v) const override       { return param->valueToString (v); }

        te::AutomatableParameter::Ptr param;
    };

    inline std::shared_ptr<KnobParameter> knobParameter (te::AutomatableParameter& p)
    {
        return std::make_shared<TeKnobParameter> (p);
    }
}
