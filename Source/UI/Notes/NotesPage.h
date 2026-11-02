#pragma once

#include "../../PCH.h"
#include "../../Model/EditViewState.h"

namespace cdaw
{
    /** The lower zone's Notes page: the song's notebook.

        - left: free notes for the whole song (ideas, to-dos, chords, a lyrics draft)
        - right: the lyric sheet, one box per song section in song order, following the playhead.
          Click a section's name to jump there. The lyrics belong to the section, so moving or
          copying it in the arranger takes them along. */
    class NotesPage : public juce::Component,
                      private juce::ValueTree::Listener,
                      private juce::Timer
    {
    public:
        explicit NotesPage (EditViewState&);
        ~NotesPage() override;

        /** Scrolls to a section's lyrics and puts the cursor in them. */
        void focusSection (te::ArrangerClip&);

        /** Writes what's being typed into the project now (also happens on its own after a pause). */
        void commitPendingEdits();

        /** All sections' lyrics as plain text, with the section names as headings. */
        juce::String getLyricSheet() const;

        void paint (juce::Graphics&) override;
        void resized() override;
        void visibilityChanged() override;

    private:
        struct SectionRow;
        struct RowList;

        void timerCallback() override;
        void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override   { sectionsChanged(); }
        void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override               { sectionsChanged(); }
        void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override        { sectionsChanged(); }
        void sectionsChanged();
        void rebuildRows();
        void layoutRows();
        void updateCurrentSection();

        EditViewState& viewState;
        te::Edit& edit;
        juce::ValueTree arrangerState;

        juce::TextEditor notesEditor;
        juce::TextButton copyButton { "Copy Lyric Sheet" };
        juce::Viewport viewport;
        std::unique_ptr<RowList> rowList;
        juce::OwnedArray<SectionRow> rows;
        juce::Rectangle<int> notesHeader, lyricsHeader;

        bool notesDirty = false, needsRebuild = false;
        juce::uint32 lastEditTime = 0;
    };
}
