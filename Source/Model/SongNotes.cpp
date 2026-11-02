#include "SongNotes.h"
#include "EditViewState.h"

namespace cdaw::SongNotes
{
    static juce::ValueTree getViewTree (te::Edit& edit)
    {
        return edit.state.getOrCreateChildWithName (ViewIDs::CDAWVIEW, nullptr);
    }

    juce::String getProjectNotes (te::Edit& edit)
    {
        return getViewTree (edit)[notesID].toString();
    }

    void setProjectNotes (te::Edit& edit, const juce::String& text)
    {
        auto view = getViewTree (edit);

        if (view[notesID].toString() == text)
            return;

        if (text.isEmpty())
            view.removeProperty (notesID, nullptr);
        else
            view.setProperty (notesID, text, nullptr);

        edit.markAsChanged();   // nothing went through the undo manager, which is how Tracktion notices
    }

    juce::String getLyrics (const te::ArrangerClip& section)
    {
        return section.state[lyricsID].toString();
    }

    void setLyrics (te::ArrangerClip& section, const juce::String& text, juce::UndoManager* um)
    {
        auto trimmed = text.trimEnd();

        if (getLyrics (section) == trimmed)
            return;

        if (trimmed.isEmpty())
            section.state.removeProperty (lyricsID, um);
        else
            section.state.setProperty (lyricsID, trimmed, um);
    }
}
