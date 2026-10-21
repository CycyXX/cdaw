#pragma once

#include "../../PCH.h"

namespace cdaw
{
    /** Bottom strip: transient messages on the left, device / CPU / save state on the right. */
    class StatusBar : public juce::Component,
                      private juce::Timer
    {
    public:
        explicit StatusBar (te::Engine&);
        ~StatusBar() override;

        void showMessage (const juce::String&);
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;

        std::function<void()> onDeviceClicked;

    private:
        void timerCallback() override;

        te::Engine& engine;
        juce::String message, device;
        juce::uint32 messageTime = 0;
        float cpu = 0.0f;
        bool clipped = false;
        juce::Rectangle<int> deviceArea;
    };
}
