#pragma once

#include "../../PCH.h"

namespace cdaw
{
    /** Preferences window: Audio, MIDI, Recording, Plugins, General, Shortcuts. */
    class PreferencesDialog : public juce::Component
    {
    public:
        PreferencesDialog();
        ~PreferencesDialog() override;

        static void show (int initialTab = 0);
        void resized() override;

    private:
        juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    };
}
