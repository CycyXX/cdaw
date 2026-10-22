#pragma once

#include "../PCH.h"

namespace cdaw
{
    /** Audio files a project refers to that aren't where it expects them (moved or renamed project
        folders, a Save As, files deleted), and ways to reconnect them. */
    namespace MissingMedia
    {
        /** One missing file and the clips that use it. */
        struct MissingFile
        {
            juce::File expected;                    // where the project looks for it
            juce::StringArray clipNames;
            juce::ReferenceCountedArray<te::Clip> clips;
        };

        /** Audio clips whose source file doesn't exist, grouped by file. */
        juce::Array<MissingFile> findMissingAudio (te::Edit&);

        /** A file called `fileName` in `folder` or below (case-insensitive); {} if there is none. */
        juce::File findByName (const juce::File& folder, const juce::String& fileName);

        /** Points every clip of `missing` at `file`. The path is stored relative to the project
            where possible. Returns the number of clips reconnected. */
        int relink (const MissingFile& missing, const juce::File& file);

        /** Looks for each missing file by name in the folders, in the order given (the first folder
            that has it wins), and reconnects what it finds. Returns the number of files found. */
        int relinkFromFolders (te::Edit&, const juce::Array<juce::File>& folders);

        /** Plugins the project uses that couldn't be loaded, as "Name (on Track)". */
        juce::StringArray findMissingPlugins (te::Edit&);
    }
}
