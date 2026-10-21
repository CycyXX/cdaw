#pragma once

#include "../../PCH.h"
#include "../../Model/EditViewState.h"
#include "../../Model/DrumMapLibrary.h"

namespace cdaw
{
    /** Piano roll / drum editor for one MIDI clip. Has its own zoom; snaps to the
        arrangement's grid setting. Drum mode is used when the track has a drum map. */
    class PianoRoll : public juce::Component,
                      private juce::ValueTree::Listener,
                      private juce::ScrollBar::Listener,
                      private juce::Timer
    {
    public:
        PianoRoll (EditViewState&, te::MidiClip::Ptr);
        ~PianoRoll() override;

        te::MidiClip* getClip() const           { return clip.get(); }
        void setDrumMap (std::optional<DrumMap>);

        void paint (juce::Graphics&) override;
        void resized() override;
        bool keyPressed (const juce::KeyPress&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

        // Editing (also used by tests)
        void quantizeNotes (bool selectedOnly);
        void deleteSelected();
        void selectAll();
        void transposeSelected (int semitones);
        void duplicateSelected();

        struct Layout
        {
            double pixelsPerBeat = 60.0;
            double scrollBeat = 0.0;        // edit beat at the grid's left edge
            float rowHeight = 12.0f;
            float scrollY = 0.0f;
        };

    private:
        friend struct NoteGrid;
        friend struct KeysPanel;
        friend struct VelocityLane;
        friend struct BeatRuler;

        EditViewState& viewState;
        te::Edit& edit;
        te::MidiClip::Ptr clip;
        std::optional<DrumMap> drumMap;
        std::vector<int> rowNotes;          // row index -> note number (top to bottom)
        Layout layout;
        juce::Array<te::MidiNote*> selected;
        int lastVelocity = 100;

        std::unique_ptr<juce::Component> keys, grid, velocity, ruler;
        juce::Label titleLabel;
        juce::TextButton quantizeButton { "Quantize" }, drumModeButton;
        juce::ScrollBar vScroll { true };

        void rebuildRows();
        int noteToRow (int note) const;
        int rowToNote (int row) const;
        int getNumRows() const                  { return (int) rowNotes.size(); }
        bool isDrumMode() const                 { return drumMap.has_value(); }

        // Coordinates (x relative to the grid)
        double xToEditBeat (double x) const     { return layout.scrollBeat + x / layout.pixelsPerBeat; }
        double editBeatToX (double beat) const  { return (beat - layout.scrollBeat) * layout.pixelsPerBeat; }
        double contentBeatToEditBeat (te::BeatPosition) const;
        te::BeatPosition editBeatToContentBeat (double editBeat) const;
        double snapEditBeat (double editBeat) const;
        double getGridBeats() const;
        float rowToY (int row) const            { return row * layout.rowHeight - layout.scrollY; }
        int yToRow (float y) const              { return (int) std::floor ((y + layout.scrollY) / layout.rowHeight); }

        te::MidiNote* noteAt (juce::Point<float> gridPos) const;
        juce::Rectangle<float> getNoteBounds (const te::MidiNote&) const;
        void audition (int note, int velocity = 100);
        void validateSelection();
        void updateScrollBar();
        void centreOnNotes();

        void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override        { changed(); }
        void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { changed(); }
        void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { changed(); }
        void changed();
        void timerCallback() override;
        void scrollBarMoved (juce::ScrollBar*, double start) override   { layout.scrollY = (float) start; repaint(); }

        double lastPlayheadBeat = -1.0;
    };
}
