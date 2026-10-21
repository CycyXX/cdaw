#include "../PCH.h"
#include "AppContext.h"
#include "Logging.h"
#include "CrashHandler.h"
#include "MainWindow.h"
#include "MainComponent.h"
#include "../Engine/AudioDeviceSetup.h"
#include "../Model/ProjectSession.h"

namespace cdaw
{
    class CDAWApplication : public juce::JUCEApplication
    {
    public:
        const juce::String getApplicationName() override       { return "CDAW"; }
        const juce::String getApplicationVersion() override    { return JUCE_APPLICATION_VERSION_STRING; }

        // Must stay true: the out-of-process plugin scanner is this same exe.
        bool moreThanOneInstanceAllowed() override             { return true; }

        void initialise (const juce::String& commandLine) override
        {
            // Plugin scanner child process: scan and exit. No engine, no ASIO, no window.
            if (te::PluginManager::startChildProcessPluginScan (commandLine))
            {
                isScannerChild = true;
                return;
            }

            Logging::init();
            CrashHandler::install();
            const bool previousSessionCrashed = CrashHandler::beginSession();
            juce::Logger::writeToLog ("Starting CDAW " + getApplicationVersion() + "  args: " + commandLine);

            context = std::make_unique<AppContext>();
            auto status = AudioDeviceSetup::ensureAsioDefault (context->engine(), context->settings);

            context->mainWindow = std::make_unique<MainWindow>();
            auto& main = context->mainWindow->getMainComponent();

            // Open the file given on the command line, else the last project, else a fresh one.
            auto argFile = juce::File::getCurrentWorkingDirectory().getChildFile (commandLine.trim().unquoted());
            auto last = juce::File (context->settings.getString (SettingKeys::lastProject));

            if (commandLine.isNotEmpty() && argFile.hasFileExtension (te::editFileSuffix) && argFile.existsAsFile())
                main.openProjectChecked (argFile);
            else if (last.existsAsFile() && (context->settings.getBool (SettingKeys::reopenLastProject, true) || previousSessionCrashed))
                main.openProjectChecked (last);
            else
            {
                auto folder = context->settings.getProjectsFolder();
                folder.createDirectory();
                main.openProject (ProjectSession::createProjectFolder (folder, "Untitled"));

                if (context->session != nullptr)
                    context->session->save();
            }

            if (context->postStatus)
                context->postStatus (status);

            if (previousSessionCrashed)
            {
                juce::Logger::writeToLog ("Previous session did not exit cleanly");
                CrashHandler::offerToDisableCrashedPlugins (context->engine());
            }
        }

        void shutdown() override
        {
            if (isScannerChild)
                return;

            if (context != nullptr)
            {
                if (context->session != nullptr)
                    context->session->getTransport().stop (false, true);

                context->mainWindow.reset();
                context->session.reset();
                context.reset();
            }

            CrashHandler::endSession();
            juce::Logger::writeToLog ("CDAW shut down cleanly");
            Logging::shutdown();
        }

        void systemRequestedQuit() override
        {
            if (isScannerChild || context == nullptr || context->mainWindow == nullptr)
            {
                quit();
                return;
            }

            context->mainWindow->getMainComponent().confirmDiscardOrSave ([] (bool ok)
            {
                if (ok)
                    juce::JUCEApplication::quit();
            });
        }

        void anotherInstanceStarted (const juce::String&) override {}

    private:
        std::unique_ptr<AppContext> context;
        bool isScannerChild = false;
    };
}

START_JUCE_APPLICATION (cdaw::CDAWApplication)
