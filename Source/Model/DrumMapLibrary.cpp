#include "DrumMapLibrary.h"
#include "EditViewState.h"
#include "../App/AppSettings.h"

namespace cdaw
{
    juce::String DrumMap::getNameForNote (int note) const
    {
        for (auto& [n, name] : pieces)
            if (n == note)
                return name;

        return {};
    }

    DrumMap DrumMap::parse (const juce::String& name, const juce::String& text)
    {
        DrumMap map;
        map.name = name;

        for (auto line : juce::StringArray::fromLines (text))
        {
            line = line.trim();

            if (line.isEmpty() || line.startsWithChar ('#'))
                continue;

            auto note = line.upToFirstOccurrenceOf (" ", false, false).getIntValue();
            auto pieceName = line.fromFirstOccurrenceOf (" ", false, false).trim();

            if (juce::isPositiveAndBelow (note, 128) && pieceName.isNotEmpty())
                map.pieces.emplace_back (note, pieceName);
        }

        return map;
    }

    namespace DrumMapLibrary
    {
        static juce::Array<juce::File> getFolders()
        {
            auto exeDir = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
            return { exeDir.getChildFile ("DrumMaps"), AppSettings::getUserDrumMapsFolder() };
        }

        juce::Array<DrumMap> getAll()
        {
            juce::Array<DrumMap> maps;

            for (auto& folder : getFolders())
            {
                for (auto& f : folder.findChildFiles (juce::File::findFiles, false, "*.txt"))
                {
                    auto map = DrumMap::parse (f.getFileNameWithoutExtension(), f.loadFileAsString());
                    map.file = f;

                    if (map.pieces.empty())
                        continue;

                    // User maps replace bundled ones with the same name.
                    for (int i = maps.size(); --i >= 0;)
                        if (maps.getReference (i).name.equalsIgnoreCase (map.name))
                            maps.remove (i);

                    maps.add (map);
                }
            }

            return maps;
        }

        std::optional<DrumMap> find (const juce::String& name)
        {
            for (auto& m : getAll())
                if (m.name.equalsIgnoreCase (name))
                    return m;

            return {};
        }

        std::optional<DrumMap> getForTrack (const te::Track& t)
        {
            auto name = t.state[ViewIDs::cdawDrumMap].toString();
            return name.isNotEmpty() ? find (name) : std::nullopt;
        }

        void setForTrack (te::Track& t, const juce::String& mapName)
        {
            t.state.setProperty (ViewIDs::cdawDrumMap, mapName, &t.edit.getUndoManager());
        }

        juce::String guessForTrack (te::Track& t)
        {
            for (auto p : t.pluginList.getPlugins())
            {
                auto n = p->getName();

                if (n.containsIgnoreCase ("SSD") || n.containsIgnoreCase ("Slate"))  return "SSD5";
                if (n.containsIgnoreCase ("GGD") || n.containsIgnoreCase ("Modern and Massive")) return "GGD_MM2";
                if (n.containsIgnoreCase ("drum"))                                  return "GM";
            }

            return {};
        }
    }
}
