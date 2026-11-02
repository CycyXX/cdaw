#include "ProjectVersions.h"
#include "ProjectSession.h"

namespace cdaw::ProjectVersions
{
    ParsedName parseName (const juce::String& name)
    {
        auto underscore = name.lastIndexOfChar ('_');

        if (underscore > 0)
        {
            auto digits = name.substring (underscore + 1);

            if (digits.isNotEmpty() && digits.containsOnly ("0123456789") && digits.length() <= 6)
                return { name.substring (0, underscore), digits.getIntValue() };
        }

        return { name, 1 };
    }

    juce::String makeName (const juce::String& base, int number)
    {
        return base + "_" + juce::String (number).paddedLeft ('0', 2);
    }

    juce::Array<Version> list (const juce::File& editFile)
    {
        juce::Array<Version> versions;
        auto base = parseName (editFile.getFileNameWithoutExtension()).base;

        for (auto& f : editFile.getParentDirectory().findChildFiles (juce::File::findFiles, false,
                                                                     "*" + juce::String (ProjectSession::projectFileSuffix)))
        {
            auto parsed = parseName (f.getFileNameWithoutExtension());

            if (parsed.base.equalsIgnoreCase (base))
                versions.add ({ f, parsed.number, readNote (f), f.getLastModificationTime() });
        }

        std::sort (versions.begin(), versions.end(), [] (const Version& a, const Version& b)
        {
            return a.number != b.number ? a.number < b.number
                                        : a.file.getFileName().compareNatural (b.file.getFileName()) < 0;
        });

        return versions;
    }

    juce::File getNextVersionFile (const juce::File& editFile)
    {
        auto base = parseName (editFile.getFileNameWithoutExtension()).base;
        int highest = 1;

        for (auto& v : list (editFile))
            highest = juce::jmax (highest, v.number);

        for (int n = highest + 1;; ++n)
        {
            auto f = editFile.getSiblingFile (makeName (base, n) + ProjectSession::projectFileSuffix);

            if (! f.exists())
                return f;
        }
    }

    juce::String readNote (const juce::File& editFile)
    {
        juce::XmlDocument doc (editFile);

        if (auto root = doc.getDocumentElement (true))
            return root->getStringAttribute (noteID.toString());

        return {};
    }

    juce::String getNote (const te::Edit& edit)
    {
        return edit.state[noteID].toString();
    }

    void setNote (te::Edit& edit, const juce::String& note)
    {
        auto text = note.trim();

        if (text.isEmpty())
            edit.state.removeProperty (noteID, nullptr);
        else
            edit.state.setProperty (noteID, text, nullptr);
    }
}
