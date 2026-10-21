#include "Logging.h"
#include "AppSettings.h"

namespace cdaw::Logging
{
    static std::unique_ptr<juce::FileLogger> logger;

    void init()
    {
        auto folder = AppSettings::getLogsFolder();

        // Rotate: keep the newest 10 logs.
        auto logs = folder.findChildFiles (juce::File::findFiles, false, "CDAW_*.log");
        std::sort (logs.begin(), logs.end(), [] (const juce::File& a, const juce::File& b)
                   { return a.getLastModificationTime() > b.getLastModificationTime(); });

        for (int i = 9; i < logs.size(); ++i)
            logs.getReference (i).deleteFile();

        auto name = "CDAW_" + juce::Time::getCurrentTime().formatted ("%Y%m%d_%H%M%S") + ".log";
        logger = std::make_unique<juce::FileLogger> (folder.getChildFile (name),
                                                     "CDAW " + juce::String (JUCE_APPLICATION_VERSION_STRING)
                                                       + " - " + juce::SystemStats::getOperatingSystemName(),
                                                     0);
        juce::Logger::setCurrentLogger (logger.get());
    }

    void shutdown()
    {
        juce::Logger::setCurrentLogger (nullptr);
        logger.reset();
    }

    juce::File getCurrentLogFile()
    {
        return logger != nullptr ? logger->getLogFile() : juce::File();
    }
}
