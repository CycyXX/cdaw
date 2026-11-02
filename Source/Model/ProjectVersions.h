#pragma once

#include "../PCH.h"

namespace cdaw
{
    /** Numbered versions of a song, side by side in its project folder and sharing its Audio
        folder: Song.cdaw, Song_02.cdaw, Song_03.cdaw ... (a file without a number is version 1,
        so Song_01.cdaw is version 1 too). Each version can carry a one-line note saying what
        changed, kept on the edit's root element so the menu can read it without loading the
        whole project. */
    namespace ProjectVersions
    {
        inline const juce::Identifier noteID ("cdawVersionNote");

        struct ParsedName
        {
            juce::String base;
            int number = 1;
        };

        /** "Song_03" -> { "Song", 3 }; "Song" -> { "Song", 1 }. */
        ParsedName parseName (const juce::String& nameWithoutExtension);

        /** <base>_NN with at least two digits. */
        juce::String makeName (const juce::String& base, int number);

        struct Version
        {
            juce::File file;
            int number = 1;
            juce::String note;
            juce::Time modified;
        };

        /** The versions of the song `editFile` belongs to, in the same folder, oldest number first. */
        juce::Array<Version> list (const juce::File& editFile);

        /** The file the next version goes to: one past the highest number in the folder. */
        juce::File getNextVersionFile (const juce::File& editFile);

        /** The note stored in a project file (reads only its root element). */
        juce::String readNote (const juce::File& editFile);

        juce::String getNote (const te::Edit&);
        void setNote (te::Edit&, const juce::String&);
    }
}
