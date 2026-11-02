#include "MainWindow.h"
#include "MainComponent.h"
#include "AppContext.h"
#include "../UI/LookAndFeel/Palette.h"

#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace cdaw
{
    namespace
    {
        /** Hides the window from the screen without hiding it from Windows (DWM "cloak"): it keeps
            its size, layout and painting, and appears when uncloaked. Restoring a maximised window
            goes through a normal-sized frame first (Windows shows it at its old size, then maximises);
            cloaked, none of that is seen. Does nothing where the API isn't available. */
        void setCloaked ([[maybe_unused]] juce::Component& window, [[maybe_unused]] bool cloaked)
        {
           #if JUCE_WINDOWS
            using SetAttribute = HRESULT (WINAPI*) (HWND, DWORD, LPCVOID, DWORD);
            static const auto setAttribute = [] () -> SetAttribute
            {
                if (auto dll = ::LoadLibraryW (L"dwmapi.dll"))
                    return reinterpret_cast<SetAttribute> (::GetProcAddress (dll, "DwmSetWindowAttribute"));

                return nullptr;
            }();

            constexpr DWORD dwmaCloak = 13;
            auto* peer = window.getPeer();

            if (setAttribute != nullptr && peer != nullptr)
            {
                const BOOL value = cloaked ? TRUE : FALSE;
                setAttribute ((HWND) peer->getNativeHandle(), dwmaCloak, &value, sizeof (value));
            }
           #endif
        }
    }

    MainWindow::MainWindow()
        : DocumentWindow ("CDAW", Palette::windowBackground, DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        setResizable (true, false);
        setResizeLimits (900, 560, 10000, 10000);
        setContentOwned (new MainComponent(), false);

        addKeyListener (AppContext::get().commandManager.getKeyMappings());
        // Shown by the application (showWindow) once the startup project has loaded.
    }

    void MainWindow::showWindow()
    {
        auto stored = AppContext::get().settings.getString (SettingKeys::mainWindowState);

        // Everything below (restoring the size, maximising) happens out of sight.
        setCloaked (*this, true);

        if (stored.isEmpty() || ! restoreWindowStateFromString (stored))
        {
            centreWithSize (1500, 900);
            setFullScreen (true);
        }

        shown = true;
        setVisible (true);
        repaint();

        // Give it time to settle at its final size and paint once, then reveal it.
        juce::Timer::callAfterDelay (150, [safe = juce::Component::SafePointer<juce::Component> (this)]
        {
            if (safe != nullptr)
                setCloaked (*safe, false);
        });
    }

    MainWindow::~MainWindow()
    {
        // A window that was never shown would save its unrestored default state over the real one.
        if (shown && AppContext::exists())
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
