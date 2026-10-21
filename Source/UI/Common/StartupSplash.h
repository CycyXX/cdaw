#pragma once

#include "../../PCH.h"

namespace cdaw
{
    /** Window shown while CDAW starts: logo, the current startup step and a progress bar.

        Startup runs on the message thread and blocks it, so setStep() paints the window
        immediately instead of waiting for the message loop. */
    class StartupSplash : public juce::Component
    {
    public:
        explicit StartupSplash (const juce::String& version);
        ~StartupSplash() override;

        /** Shows `text` as the current step and moves the bar to `progress` (0..1). */
        void setStep (const juce::String& text, double progress);

        void paint (juce::Graphics&) override;

    private:
        juce::Image logo;
        juce::String version, step;
        double progress = 0.0;
    };
}
