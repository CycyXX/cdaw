#include "MainWindow.h"
#include "MainComponent.h"
#include "AppContext.h"
#include "../UI/LookAndFeel/Palette.h"

namespace cdaw
{
    MainWindow::MainWindow()
        : DocumentWindow ("CDAW", Palette::windowBackground, DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        setResizable (true, false);
        setResizeLimits (900, 560, 10000, 10000);
        setContentOwned (new MainComponent(), false);

        auto& settings = AppContext::get().settings;
        auto stored = settings.getString (SettingKeys::mainWindowState);

        if (stored.isEmpty() || ! restoreWindowStateFromString (stored))
        {
            centreWithSize (1500, 900);
            setFullScreen (true);
        }

        addKeyListener (AppContext::get().commandManager.getKeyMappings());
        // Shown by the application once the startup project has loaded.
    }

    MainWindow::~MainWindow()
    {
        if (AppContext::exists())
            AppContext::get().settings.setString (SettingKeys::mainWindowState, getWindowStateAsString());

        clearContentComponent();
    }

    MainComponent& MainWindow::getMainComponent()
    {
        return *static_cast<MainComponent*> (getContentComponent());
    }

    void MainWindow::closeButtonPressed()
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }
}
