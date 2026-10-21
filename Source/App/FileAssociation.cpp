#include "FileAssociation.h"
#include "../Model/ProjectSession.h"

#if JUCE_WINDOWS
 #include <ShlObj.h>
#endif

namespace cdaw::FileAssociation
{
    void registerIfNeeded()
    {
       #if JUCE_WINDOWS
        const juce::String classes = "HKEY_CURRENT_USER\\Software\\Classes\\";
        const juce::String progId = "CDAW.Project";
        const auto commandKey = classes + progId + "\\shell\\open\\command\\";

        auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
        auto command = "\"" + exe.getFullPathName() + "\" \"%1\"";
        auto existing = juce::WindowsRegistry::getValue (commandKey);

        if (existing == command
             && juce::WindowsRegistry::getValue (classes + ProjectSession::projectFileSuffix + "\\") == progId)
            return;

        const bool isDevBuild = exe.getFullPathName().containsIgnoreCase ("\\build\\");
        const auto registeredExe = juce::File (existing.fromFirstOccurrenceOf ("\"", false, false)
                                                       .upToFirstOccurrenceOf ("\"", false, false));

        if (isDevBuild && existing.isNotEmpty() && registeredExe.existsAsFile())
            return;

        bool ok = juce::WindowsRegistry::setValue (classes + ProjectSession::projectFileSuffix + "\\", progId);
        ok = ok && juce::WindowsRegistry::setValue (classes + progId + "\\", juce::String ("CDAW Project"));
        ok = ok && juce::WindowsRegistry::setValue (classes + progId + "\\DefaultIcon\\", "\"" + exe.getFullPathName() + "\",0");
        ok = ok && juce::WindowsRegistry::setValue (commandKey, command);

        if (ok)
        {
            SHChangeNotify (SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
            juce::Logger::writeToLog ("Registered .cdaw files with " + exe.getFullPathName());
        }
        else
        {
            juce::Logger::writeToLog ("Could not register the .cdaw file type");
        }
       #endif
    }
}
