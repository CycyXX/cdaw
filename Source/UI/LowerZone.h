#pragma once

#include "../PCH.h"
#include "../Model/EditViewState.h"

namespace cdaw
{
    class PianoRoll;
    class MixerView;
    class NotesPage;

    /** Bottom panel with three pages: the MIDI editor, the mixer and the song's notes and lyrics.
        Resizable from its top edge. */
    class LowerZone : public juce::Component
    {
    public:
        enum class Page { editor, mixer, notes };   // stored in the settings by number: add at the end

        explicit LowerZone (EditViewState&);
        ~LowerZone() override;

        void showPage (Page);
        Page getPage() const                    { return page; }
        void editClip (te::MidiClip&);
        void drumMapChanged (te::Track&);
        PianoRoll* getPianoRoll() const         { return pianoRoll.get(); }

        /** Shows the Notes page with the cursor in a section's lyrics. */
        void editLyrics (te::ArrangerClip&);
        NotesPage* getNotesPage() const         { return notes.get(); }

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;

        std::function<void (int newHeight)> onResizeRequest;
        std::function<void()> onClose;
        std::function<void (te::Track&)> onTrackMenu;

        static constexpr int tabBarHeight = 26;

    private:
        EditViewState& viewState;
        Page page = Page::editor;
        std::unique_ptr<PianoRoll> pianoRoll;
        std::unique_ptr<MixerView> mixer;
        std::unique_ptr<NotesPage> notes;
        juce::TextButton editorTab { "Editor" }, mixerTab { "Mixer" }, notesTab { "Notes" }, closeButton { "x" };
        int dragStartHeight = 0;

        void updateTabs();
    };
}
