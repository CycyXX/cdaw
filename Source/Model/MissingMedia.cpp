#include "MissingMedia.h"

namespace cdaw::MissingMedia
{
    juce::Array<MissingFile> findMissingAudio (te::Edit& edit)
    {
        juce::Array<MissingFile> result;

        auto entryFor = [&result, &edit] (const juce::File& expected) -> MissingFile&
        {
            for (auto& m : result)
                if (m.expected == expected)
                    return m;

            result.add ({});
            auto& entry = result.getReference (result.size() - 1);
            entry.expected = expected;
            entry.edit = &edit;
            return entry;
        };

        for (auto* track : te::getAudioTracks (edit))
        {
            for (auto* clip : track->getClips())
            {
                auto* audio = dynamic_cast<te::AudioClipBase*> (clip);

                if (audio == nullptr || audio->getSourceFileReference().source.get().isEmpty())
                    continue;

                auto expected = audio->getSourceFileReference().getFile();

                if (! expected.existsAsFile())
                {
                    auto& entry = entryFor (expected);
                    entry.clips.add (clip);
                    entry.clipNames.addIfNotAlreadyThere (clip->getName());
                }

                // Every pass on the take lanes counts, not only what's playing.
                for (auto take : clip->state.getChildWithName (te::IDs::TAKES))
                {
                    te::SourceFileReference reference (edit, take, te::IDs::source);
                    auto takeFile = reference.getFile();

                    if (reference.source.get().isNotEmpty() && ! takeFile.existsAsFile())
                    {
                        auto& entry = entryFor (takeFile);
                        entry.takes.add (take);
                        entry.clipNames.addIfNotAlreadyThere (clip->getName());
                    }
                }
            }
        }

        return result;
    }

    juce::File findByName (const juce::File& folder, const juce::String& fileName)
    {
        if (fileName.isEmpty() || ! folder.isDirectory())
            return {};

        // The name is the pattern (no wildcards in file names); Windows matches it case-insensitively.
        auto found = folder.findChildFiles (juce::File::findFiles, true, fileName);

        for (auto& f : found)
            if (f.getFileName().equalsIgnoreCase (fileName))
                return f;

        return {};
    }

    int relink (const MissingFile& missing, const juce::File& file)
    {
        int count = 0;

        if (! file.existsAsFile())
            return 0;

        for (auto* clip : missing.clips)
        {
            clip->getSourceFileReference().setToFile (file, te::SourceFileReference::PathStyle::chooseBest, false);
            ++count;
        }

        for (auto take : missing.takes)
        {
            te::SourceFileReference (*missing.edit, take, te::IDs::source)
                .setToFile (file, te::SourceFileReference::PathStyle::chooseBest, false);
            ++count;
        }

        juce::Logger::writeToLog ("Reconnected " + missing.expected.getFileName() + " -> " + file.getFullPathName());
        return count;
    }

    int relinkFromFolders (te::Edit& edit, const juce::Array<juce::File>& folders)
    {
        int found = 0;

        for (auto& missing : findMissingAudio (edit))
        {
            for (auto& folder : folders)
            {
                auto file = findByName (folder, missing.expected.getFileName());

                if (file.existsAsFile())
                {
                    relink (missing, file);
                    ++found;
                    break;
                }
            }
        }

        return found;
    }

    juce::StringArray findMissingPlugins (te::Edit& edit)
    {
        juce::StringArray names;

        for (auto* plugin : te::getAllPlugins (edit, false))
        {
            if (auto* external = dynamic_cast<te::ExternalPlugin*> (plugin); external != nullptr && external->isMissing())
            {
                auto where = plugin->getOwnerTrack() != nullptr ? plugin->getOwnerTrack()->getName() : juce::String ("Master");
                names.add (plugin->getName() + " (on " + where + ")");
            }
        }

        return names;
    }
}
