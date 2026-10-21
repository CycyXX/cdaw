#include "AppSettings.h"

namespace cdaw
{
    AppSettings::AppSettings()
    {
        juce::PropertiesFile::Options options;
        options.applicationName     = "CDAW";
        options.filenameSuffix      = ".settings";
        options.folderName          = "CDAW";
        options.osxLibrarySubFolder = "Application Support";
        options.storageFormat       = juce::PropertiesFile::storeAsXML;
        options.millisecondsBeforeSaving = 2000;
        appProperties.setStorageParameters (options);
    }

    juce::PropertiesFile& AppSettings::props()
    {
        return *appProperties.getUserSettings();
    }

    void AppSettings::saveIfNeeded()
    {
        appProperties.saveIfNeeded();
    }

    juce::File AppSettings::getSettingsFolder()
    {
        auto f = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("CDAW");
        f.createDirectory();
        return f;
    }

    juce::File AppSettings::getLogsFolder()
    {
        auto f = getSettingsFolder().getChildFile ("Logs");
        f.createDirectory();
        return f;
    }

    juce::File AppSettings::getCrashFolder()
    {
        auto f = getSettingsFolder().getChildFile ("Crashes");
        f.createDirectory();
        return f;
    }

    juce::File AppSettings::getUserDrumMapsFolder()
    {
        auto f = getSettingsFolder().getChildFile ("DrumMaps");
        f.createDirectory();
        return f;
    }

    juce::File AppSettings::getProjectsFolder()
    {
        auto stored = props().getValue (SettingKeys::projectsFolder);

        if (stored.isNotEmpty() && juce::File::isAbsolutePath (stored))
            return juce::File (stored);

        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("CDAW Projects");
    }

    void AppSettings::setProjectsFolder (const juce::File& f)
    {
        props().setValue (SettingKeys::projectsFolder, f.getFullPathName());
        broadcaster.sendChangeMessage();
    }

    juce::StringArray AppSettings::getRecentProjects()
    {
        juce::StringArray list;
        list.addLines (props().getValue (SettingKeys::recentProjects));
        list.removeEmptyStrings();
        return list;
    }

    void AppSettings::addRecentProject (const juce::File& f)
    {
        auto list = getRecentProjects();
        list.removeString (f.getFullPathName(), true);
        list.insert (0, f.getFullPathName());

        while (list.size() > 12)
            list.remove (list.size() - 1);

        props().setValue (SettingKeys::recentProjects, list.joinIntoString ("\n"));
        props().setValue (SettingKeys::lastProject, f.getFullPathName());
        broadcaster.sendChangeMessage();
    }

    void AppSettings::removeRecentProject (const juce::File& f)
    {
        auto list = getRecentProjects();
        list.removeString (f.getFullPathName(), true);
        props().setValue (SettingKeys::recentProjects, list.joinIntoString ("\n"));
        broadcaster.sendChangeMessage();
    }

    bool AppSettings::getBool (juce::StringRef key, bool defaultValue)        { return props().getBoolValue (key, defaultValue); }
    void AppSettings::setBool (juce::StringRef key, bool value)               { props().setValue (key, value); broadcaster.sendChangeMessage(); }
    int AppSettings::getInt (juce::StringRef key, int defaultValue)           { return props().getIntValue (key, defaultValue); }
    void AppSettings::setInt (juce::StringRef key, int value)                 { props().setValue (key, value); broadcaster.sendChangeMessage(); }
    double AppSettings::getDouble (juce::StringRef key, double defaultValue)  { return props().getDoubleValue (key, defaultValue); }
    void AppSettings::setDouble (juce::StringRef key, double value)           { props().setValue (key, value); broadcaster.sendChangeMessage(); }

    juce::String AppSettings::getString (juce::StringRef key, const juce::String& defaultValue)
    {
        return props().getValue (key, defaultValue);
    }

    void AppSettings::setString (juce::StringRef key, const juce::String& value)
    {
        props().setValue (key, value);
        broadcaster.sendChangeMessage();
    }

    std::unique_ptr<juce::XmlElement> AppSettings::getXml (juce::StringRef key)
    {
        return props().getXmlValue (key);
    }

    void AppSettings::setXml (juce::StringRef key, const juce::XmlElement* xml)
    {
        props().setValue (key, xml);
        broadcaster.sendChangeMessage();
    }
}
