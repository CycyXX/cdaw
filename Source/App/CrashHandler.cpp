#include "CrashHandler.h"
#include "AppSettings.h"

namespace cdaw::CrashHandler
{
    static juce::File getLockFile()           { return AppSettings::getSettingsFolder().getChildFile ("session.lock"); }
    static juce::File getCrashedPluginsFile() { return AppSettings::getCrashFolder().getChildFile ("crashed_plugins.txt"); }

    static void handleCrash (void*)
    {
        // Keep this simple: we're in an unknown state.
        auto report = AppSettings::getCrashFolder()
                          .getChildFile ("crash_" + juce::Time::getCurrentTime().formatted ("%Y%m%d_%H%M%S") + ".txt");

        juce::FileOutputStream out (report);

        if (out.openedOk())
        {
            out << "CDAW crash report\n" << juce::Time::getCurrentTime().toString (true, true) << "\n\n";
            out << "Stack:\n" << juce::SystemStats::getStackBacktrace() << "\n\nTracktion crash tracer:\n";
            te::CrashStackTracer::dump (out);

            auto plugins = te::CrashStackTracer::getCrashedPlugins();
            out << "\nPlugins on the crashing thread(s):\n" << plugins.joinIntoString ("\n") << "\n";
            out.flush();

            if (! plugins.isEmpty())
                getCrashedPluginsFile().replaceWithText (plugins.joinIntoString ("\n"));
        }

        juce::Logger::writeToLog ("CRASH - report written to " + report.getFullPathName());
    }

    void install()
    {
        juce::SystemStats::setApplicationCrashHandler (handleCrash);
    }

    bool beginSession()
    {
        auto lock = getLockFile();
        const bool crashed = lock.existsAsFile();
        lock.replaceWithText (juce::Time::getCurrentTime().toISO8601 (true));
        return crashed;
    }

    void endSession()
    {
        getLockFile().deleteFile();
    }

    juce::StringArray takeCrashedPluginNames()
    {
        auto f = getCrashedPluginsFile();
        juce::StringArray names;

        if (f.existsAsFile())
        {
            names.addLines (f.loadFileAsString());
            names.removeEmptyStrings();
            f.deleteFile();
        }

        return names;
    }

    void offerToDisableCrashedPlugins (te::Engine& engine)
    {
        auto names = takeCrashedPluginNames();
        if (names.isEmpty())
            return;

        // The tracer records debug names; match them to known plugins.
        juce::StringArray ids, prettyNames;

        for (auto& d : engine.getPluginManager().knownPluginList.getTypes())
            for (auto& n : names)
                if (n.containsIgnoreCase (d.name))
                {
                    ids.addIfNotAlreadyThere (d.createIdentifierString());
                    prettyNames.addIfNotAlreadyThere (d.name + " (" + d.manufacturerName + ")");
                }

        if (ids.isEmpty())
            return;

        auto options = juce::MessageBoxOptions::makeOptionsYesNo (juce::MessageBoxIconType::WarningIcon,
                                                                  "A plugin crashed CDAW",
                                                                  "CDAW closed unexpectedly while this plugin was running:\n\n"
                                                                    + prettyNames.joinIntoString ("\n")
                                                                    + "\n\nDisable it for now? You can re-enable it in Preferences > Plugins.",
                                                                  "Disable", "Keep enabled");

        juce::AlertWindow::showAsync (options, [&engine, ids] (int result)
        {
            if (result == 1)
                for (auto& id : ids)
                    engine.getEngineBehaviour().setPluginDisabled (id, true);
        });
    }
}
