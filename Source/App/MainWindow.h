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

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };
}
