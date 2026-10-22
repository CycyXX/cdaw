#include "MissingMedia.h"

namespace cdaw::MissingMedia
{
    juce::Array<MissingFile> findMissingAudio (te::Edit& edit)
    {
        juce::Array<MissingFile> result;

        for (auto* track : te::getAudioTracks (edit))
        {
            for (auto* clip : track->getClips())
            {
                auto* audio = dynamic_cast<te::AudioClipBase*> (clip);

                if (audio == nullptr || audio->getSourceFileReference().source.get().isEmpty())
                    continue;

                auto expected = audio->getSourceFileReference().getFile();

                if (expected.existsAsFile())
                    continue;

                MissingFile* entry = nullptr;

                for (auto& m : result)
                    if (m.expected == expected)
                        entry = &m;

                if (entry == nullptr)
                {
                    result.add ({});
                    entry = &result.getReference (result.size() - 1);
                    entry->expected = expected;
                }

                entry->clips.add (clip);
                entry->clipNames.addIfNotAlreadyThere (clip->getName());
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
