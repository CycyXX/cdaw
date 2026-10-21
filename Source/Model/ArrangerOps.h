#pragma once

#include "../PCH.h"

namespace cdaw
{
    /** Song sections (Intro, Verse, Chorus ...). They are ArrangerClips on the Edit's
        ArrangerTrack; moving, duplicating or deleting a section does the same to
        everything in its time range on every track: clips, automation and markers. */
    namespace ArrangerOps
    {
        te::ArrangerTrack* getTrack (te::Edit&);

        /** All sections, sorted by start time. */
        juce::Array<te::ArrangerClip*> getSections (te::Edit&);
        te::ArrangerClip* findSectionAt (te::Edit&, te::TimePosition);

        /** Common section names, in song order. */
        const juce::StringArray& getPresetNames();

        /** A fixed colour per preset name (so every "Chorus" looks the same), else one from the name. */
        juce::Colour getColourForName (const juce::String&);

        /** Adds a section over `range`. Sections never overlap: existing ones are trimmed. */
        te::ArrangerClip* addSection (te::Edit&, te::TimeRange, const juce::String& name);

        /** Renames a section and gives it the colour for that name. */
        void renameSection (te::ArrangerClip&, const juce::String& name);

        /** Changes where a section starts / ends (the material on the tracks stays put).
            Clamped so it doesn't overlap its neighbours. */
        void resizeSection (te::ArrangerClip&, te::TimeRange newRange);

        /** Copies everything in `source` (clips, automation, markers and sections) to
            `destination`, first opening a gap there so the material after it moves later. */
        void copyRange (te::Edit&, te::TimeRange source, te::TimePosition destination);

        /** Inserts a copy of the section right after it. */
        void duplicateSection (te::Edit&, te::ArrangerClip&);

        /** Moves the section and its material to `destination` (closing the gap it leaves). */
        void moveSection (te::Edit&, te::ArrangerClip&, te::TimePosition destination);

        /** Deletes the section and its material and closes the gap. */
        void deleteSection (te::Edit&, te::ArrangerClip&, te::SelectionManager*);
    }
}
