#pragma once

#include "../PCH.h"

namespace cdaw
{
    class MainComponent;

    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow();
        ~MainWindow() override;

        MainComponent& getMainComponent();
        void closeButtonPressed() override;

        /** Restores the saved size / position / maximised state and shows the window. Call once the
            startup project has loaded: restoring a maximised window shows it at once, which would
            put an empty project on screen while the project loads. */
        void showWindow();

    private:
        bool shown = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };
}
