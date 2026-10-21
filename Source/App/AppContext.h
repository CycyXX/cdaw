#pragma once

#include "../PCH.h"
#include "AppSettings.h"
#include "../Engine/CDAWEngine.h"
#include "../UI/LookAndFeel/CDAWLookAndFeel.h"

namespace cdaw
{
    class MainWindow;
    class ProjectSession;

    /** Application-wide services. Created once in CDAWApplication::initialise. */
    class AppContext
    {
    public:
        AppContext();
        ~AppContext();

        static AppContext& get();
        static bool exists();

        te::Engine& engine()                    { return engineHost.get(); }

        AppSettings settings;
        CDAWLookAndFeel lookAndFeel;
        juce::ApplicationCommandManager commandManager;
        CDAWEngine engineHost { settings };
        std::unique_ptr<ProjectSession> session;   // the open project, may be null
        std::unique_ptr<MainWindow> mainWindow;

        /** Status bar message hook (set by the main component). */
        std::function<void (const juce::String&)> postStatus;

    private:
        static AppContext* instance;
    };
}
