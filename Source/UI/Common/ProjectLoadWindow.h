#pragma once

#include "../../PCH.h"

namespace cdaw
{
    /** Small window shown while another project opens: what's happening and a progress bar.

        Opening runs on the message thread and blocks it, so setStep() paints the window
        immediately. It doesn't run the message loop as ProgressWindow does: nothing should get to
        run while one project is half closed and the next half built. Modal, because plugins
        can run the message loop themselves while they load. Closes when destroyed. */
    class ProjectLoadWindow : public juce::Component
    {
    public:
        ProjectLoadWindow (const juce::String& projectName, juce::Component& centreOver);

        /** Shows `text` as the current step and moves the bar to `progress` (0..1). */
        void setStep (const juce::String& text, double progress);

        void paint (juce::Graphics&) override;

    private:
        juce::String title, step;
        double progress = 0.0;
    };
}
