#include "ProgressWindow.h"

namespace cdaw
{
    struct ProgressWindow::Window : public juce::DocumentWindow
    {
        Window (const juce::String& title, juce::Component* centreOver)
            : juce::DocumentWindow (title, juce::Colour (0xff23262b), 0, true)
        {
            setUsingNativeTitleBar (true);
            content.setSize (460, 80);
            status.setBounds (12, 10, 436, 24);
            bar.setBounds (12, 42, 436, 22);
            status.setMinimumHorizontalScale (0.7f);
            content.addAndMakeVisible (status);
            content.addAndMakeVisible (bar);
            setContentNonOwned (&content, true);
            setAlwaysOnTop (true);

            if (centreOver != nullptr && centreOver->isShowing())
                setCentrePosition (centreOver->getScreenBounds().getCentre());
            else
                centreWithSize (getWidth(), getHeight());

            setVisible (true);
            enterModalState (true);
        }

        void closeButtonPressed() override {}     // runs to the end

        double value = 0.0;
        juce::Component content;
        juce::Label status;
        juce::ProgressBar bar { value };
    };

    ProgressWindow::ProgressWindow (const juce::String& title, juce::Component* centreOver)
        : window (std::make_unique<Window> (title, centreOver))
    {
        update (0.0, "Starting...");
    }

    ProgressWindow::~ProgressWindow()
    {
        if (window != nullptr)
            window->exitModalState (0);
    }

    void ProgressWindow::update (double progress, const juce::String& message)
    {
        window->value = juce::jlimit (0.0, 1.0, progress);
        window->status.setText (message, juce::dontSendNotification);

        // Let the window (and its bar) repaint, without spending much time doing so.
        const auto now = juce::Time::getMillisecondCounterHiRes();
        if (now - lastPump < 40.0)
            return;

        lastPump = now;
        window->repaint();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
    }
}
