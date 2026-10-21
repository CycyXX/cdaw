#pragma once

#include "../../PCH.h"
#include "../../Model/EditViewState.h"

namespace cdaw
{
    /** The song-section strip under the ruler (Intro, Verse, Chorus ...).

        - drag on empty space to create a section, double-click to add one up to the next section
        - click a section to jump there, Shift-click to loop it
        - drag a section to move it (with everything on every track), Alt-drag to copy it
        - drag a section's edge to resize it, double-click to rename, right-click for more

        It spans the header column too; the time area starts at `headerWidth`. */
    class ArrangerLane : public juce::Component,
                         public juce::SettableTooltipClient,
                         private juce::ValueTree::Listener
    {
    public:
        ArrangerLane (EditViewState&, int headerWidth);
        ~ArrangerLane() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;

        /** Adds a section at the loop range (if set) or from the playhead to the next section. */
        void addSectionAtPlayhead();

        /** Called whenever sections change (so the lanes can redraw section lines). */
        std::function<void()> onSectionsChanged;

        static constexpr int height = 26;

    private:
        enum class Drag { none, create, move, resizeStart, resizeEnd };

        double timeToX (te::TimePosition t) const   { return headerWidth + viewState.timeToX (t); }
        te::TimePosition xToTime (double x) const   { return viewState.xToTime (x - headerWidth); }
        juce::Rectangle<int> getSectionBounds (te::ArrangerClip&) const;
        te::ArrangerClip* sectionAt (int x) const;
        te::TimePosition snapToBar (te::TimePosition) const;
        te::TimePosition findDropTime (int x) const;
        te::TimeRange rangeForNewSection (te::TimePosition at) const;
        juce::String suggestName (te::TimePosition at) const;

        void showSectionMenu (te::ArrangerClip&);
        void showEmptyMenu (te::TimePosition);
        void startRename (te::ArrangerClip&);
        void createSection (te::TimeRange, bool rename);

        void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier& id) override
        {
            // The track lanes only draw section *starts* (in the section colour); a section's
            // end moving (e.g. while resizing) changes nothing there.
            changed (id == te::IDs::start || id == te::IDs::colour);
        }
        void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override             { changed (true); }
        void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override      { changed (true); }
        void changed (bool affectsLanes);

        EditViewState& viewState;
        te::Edit& edit;
        juce::ValueTree trackState;
        const int headerWidth;

        juce::TextButton addButton { "+" };
        std::unique_ptr<juce::TextEditor> nameEditor;

        Drag drag = Drag::none;
        te::ArrangerClip::Ptr dragSection, selectedSection;
        te::TimePosition dragStartTime;
        te::TimeRange createRange;
        bool dragCopy = false;
        int dragX = -1, dropX = -1;
    };
}
