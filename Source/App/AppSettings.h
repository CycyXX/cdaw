#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace cdaw
{
    /** CDAW's own preferences, stored at %APPDATA%\CDAW\CDAW.settings.
        Engine-level settings (audio device, plugin list) live in Tracktion's Settings.xml. */
    class AppSettings
    {
    public:
        AppSettings();

        juce::PropertiesFile& props();
        void saveIfNeeded();

        static juce::File getSettingsFolder();   // %APPDATA%\CDAW
        static juce::File getLogsFolder();
        static juce::File getCrashFolder();
        static juce::File getUserDrumMapsFolder();

        // Paths
        juce::File getProjectsFolder();
        void setProjectsFolder (const juce::File&);

        // Recent projects (most recent first)
        juce::StringArray getRecentProjects();
        void addRecentProject (const juce::File&);
        void removeRecentProject (const juce::File&);

        // Generic typed accessors
        bool getBool (juce::StringRef key, bool defaultValue);
        void setBool (juce::StringRef key, bool value);
        int getInt (juce::StringRef key, int defaultValue);
        void setInt (juce::StringRef key, int value);
        double getDouble (juce::StringRef key, double defaultValue);
        void setDouble (juce::StringRef key, double value);
        juce::String getString (juce::StringRef key, const juce::String& defaultValue = {});
        void setString (juce::StringRef key, const juce::String& value);
        std::unique_ptr<juce::XmlElement> getXml (juce::StringRef key);
        void setXml (juce::StringRef key, const juce::XmlElement*);

        juce::ChangeBroadcaster& getBroadcaster()     { return broadcaster; }

    private:
        juce::ApplicationProperties appProperties;
        juce::ChangeBroadcaster broadcaster;
    };

    /** Keys used in CDAW.settings. */
    namespace SettingKeys
    {
        inline constexpr const char* projectsFolder        = "projectsFolder";
        inline constexpr const char* recentProjects        = "recentProjects";
        inline constexpr const char* keyMappings           = "keyMappings";
        inline constexpr const char* mainWindowState       = "mainWindowState";
        inline constexpr const char* lowerZoneHeight       = "lowerZoneHeight";
        inline constexpr const char* browserWidth          = "browserWidth";
        inline constexpr const char* inspectorWidth        = "inspectorWidth";
        inline constexpr const char* showBrowser           = "showBrowser";
        inline constexpr const char* showInspector         = "showInspector";
        inline constexpr const char* pluginDpiUnaware      = "pluginDpiUnaware";
        inline constexpr const char* disabledPlugins       = "disabledPlugins";
        inline constexpr const char* pluginWindowsOnTop    = "pluginWindowsOnTop";
        inline constexpr const char* autosaveSeconds       = "autosaveSeconds";
        inline constexpr const char* backupsToKeep         = "backupsToKeep";
        inline constexpr const char* recordBitDepth        = "recordBitDepth";
        inline constexpr const char* recordOffsetMs        = "recordOffsetMs";
        inline constexpr const char* midiRecordMode        = "midiRecordMode";     // 0 overdub, 1 replace, 2 takes
        inline constexpr const char* countInBars           = "countInBars";        // 0, 1, 2
        inline constexpr const char* clickVolumeDb         = "clickVolumeDb";
        inline constexpr const char* middleCOctave         = "middleCOctave";
        inline constexpr const char* autoCrossfade         = "autoCrossfade";
        inline constexpr const char* defaultFadeMs         = "defaultFadeMs";
        inline constexpr const char* copyImportedFiles     = "copyImportedFiles";
        inline constexpr const char* uiScale               = "uiScale";
        inline constexpr const char* firstRunDone          = "firstRunDone";
        inline constexpr const char* sessionLock           = "sessionLock";
        inline constexpr const char* lastProject           = "lastProject";
        inline constexpr const char* reopenLastProject     = "reopenLastProject";
        inline constexpr const char* constrainPdcOnMonitor = "constrainPdcOnMonitor";
        inline constexpr const char* followPlayhead        = "followPlayhead";
        inline constexpr const char* returnOnStop          = "returnOnStop";
        inline constexpr const char* lowerZoneVisible      = "lowerZoneVisible";
        inline constexpr const char* lowerZonePage         = "lowerZonePage";
    }
}
