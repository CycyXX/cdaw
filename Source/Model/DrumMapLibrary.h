#pragma once

#include "../PCH.h"

namespace cdaw
{
    /** A named list of drum pieces (MIDI note -> name) shown as rows in the drum editor. */
    struct DrumMap
    {
        juce::String name;
        juce::File file;
        std::vector<std::pair<int, juce::String>> pieces;   // in file order

        juce::String getNameForNote (int note) const;
        static DrumMap parse (const juce::String& name, const juce::String& text);
    };

    /** Maps from <exe>/DrumMaps (bundled) and %APPDATA%/CDAW/DrumMaps (user, wins on name clash). */
    namespace DrumMapLibrary
    {
        juce::Array<DrumMap> getAll();
        std::optional<DrumMap> find (const juce::String& name);

        /** The map assigned to a track (stored on the track state), if any. */
        std::optional<DrumMap> getForTrack (const te::Track&);
        void setForTrack (te::Track&, const juce::String& mapName);

        /** Guesses a map from the instrument plugin name (SSD5, GGD, ...). */
        juce::String guessForTrack (te::Track&);
    }
}
