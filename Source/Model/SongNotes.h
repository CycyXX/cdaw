#pragma once

#include "../PCH.h"

namespace cdaw
{
    /** The songwriting notebook kept in a project: free notes for the whole song (ideas, to-dos,
        lyrics drafts) and the lyrics of each song section.

        Section lyrics live on the section's ArrangerClip, so copying, duplicating or moving a
        section takes them along, and editing them is undoable. The project notes are a scratch
        pad, kept beside the view state without undo. */
    namespace SongNotes
    {
        inline const juce::Identifier lyricsID ("cdawLyrics");
        inline const juce::Identifier notesID ("cdawNotes");

        juce::String getProjectNotes (te::Edit&);
        /** Not undoable; marks the project as changed. */
        void setProjectNotes (te::Edit&, const juce::String&);

        juce::String getLyrics (const te::ArrangerClip&);
        void setLyrics (te::ArrangerClip&, const juce::String&, juce::UndoManager*);
    }
}
