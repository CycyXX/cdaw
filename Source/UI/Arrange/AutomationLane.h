#pragma once

#include "ArrangeHost.h"

namespace cdaw
{
    /** Left-hand header for an automation lane: parameter picker, mode, value, close. */
    class AutomationLaneHeader : public juce::Component,
                                 private juce::Timer
    {
    public:
        AutomationLaneHeader (ArrangeHost&, te::Track::Ptr, te::AutomatableParameter::Ptr);
        void paint (juce::Graphics&) override;
        void resized() override;
        te::AutomatableParameter& getParameter()     { return *param; }

    private:
        void timerCallback() override;

        ArrangeHost& host;
        te::Track::Ptr track;
        te::AutomatableParameter::Ptr param;
        juce::TextButton paramButton, modeButton, closeButton { "x" };
        juce::Label valueLabel;
    };

    /** The curve editor for one parameter, drawn across the timeline. */
    class AutomationLane : public juce::Component,
                           private te::AutomatableParameter::Listener,
                           private juce::AsyncUpdater,
                           private juce::ValueTree::Listener
    {
    public:
        AutomationLane (ArrangeHost&, te::Track::Ptr, te::AutomatableParameter::Ptr);
        ~AutomationLane() override;

        te::AutomatableParameter& getParameter()     { return *param; }
        te::Track& getTrack()                        { return *track; }

        void paint (juce::Graphics&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;

        static constexpr int defaultHeight = 64;

    private:
        void curveHasChanged (te::AutomatableParameter&) override       { triggerAsyncUpdate(); }
        void currentValueChanged (te::AutomatableParameter&) override   { triggerAsyncUpdate(); }
        void handleAsyncUpdate() override                                { repaint(); }
        void valueTreePropertyChanged (juce::ValueTree& v, const juce::Identifier& id) override;

        float valueToY (float value) const;
        float yToValue (float y) const;
        int pointAt (juce::Point<float>) const;
        te::TimePosition xToTimeSnapped (float x, const juce::ModifierKeys&) const;

        ArrangeHost& host;
        EditViewState& viewState;
        te::Track::Ptr track;
        te::AutomatableParameter::Ptr param;

        int dragIndex = -1, hoverIndex = -1;
        bool bending = false, drawing = false;
        float bendStartCurve = 0.0f;
        te::TimePosition lastDrawTime;
    };
}
