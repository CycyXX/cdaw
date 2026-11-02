#pragma once

#include "../../PCH.h"

namespace cdaw
{
    /** A modal progress window for long jobs that have to run on the message thread (such as
        importing a project, where plugins load). Each update() redraws it and lets the message
        loop run briefly, so the window stays responsive; the modal state keeps the rest of the
        app from taking input meanwhile. Closes when destroyed. */
    class ProgressWindow
    {
    public:
        ProgressWindow (const juce::String& title, juce::Component* centreOver);
        ~ProgressWindow();

        /** progress 0..1, and what's happening now. */
        void update (double progress, const juce::String& message);

    private:
        struct Window;
        std::unique_ptr<Window> window;
        double lastPump = 0.0;
    };
}
