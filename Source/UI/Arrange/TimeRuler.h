#pragma once

#include "../../PCH.h"
#include "../../Model/EditViewState.h"

namespace cdaw
{
    namespace GridPainter
    {
        /** Draws bar / beat / grid lines for the visible time range into `area`
            (x = 0 at area's left edge = viewStart). */
        void drawGridLines (juce::Graphics&, const EditViewState&, juce::Rectangle<int> area);

        /** Calls fn (barNumber (1-based), time) for each bar start in the visible range. */
        void forEachBar (const EditViewState&, double widthPx, const std::function<void (int, te::TimePosition)>& fn);
    }

    /** Ruler with three zones:
        top    - markers and tempo / time-signature changes (click a marker to jump, right-click to edit)
        middle - drag to set the loop range, double-click toggles looping
        bottom - bars and beats; click / drag to move the playhead */
    class TimeRuler : public juce::Component,
                      private juce::ValueTree::Listener
    {
    public:
        explicit TimeRuler (EditViewState&);
        ~TimeRuler() override;

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;

        static constexpr int markerZoneHeight = 15;
        static constexpr int loopZoneHeight = 13;

    private:
        void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override  { repaint(); }
        void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override             { repaint(); }
        void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override      { repaint(); }

        struct Item
        {
            enum class Kind { marker, tempo, timeSig } kind;
            juce::Rectangle<int> bounds;
            te::MarkerClip::Ptr marker;
            int index = -1;
        };

        std::vector<Item> layoutItems() const;
        const Item* itemAt (juce::Point<int>) const;
        void showItemMenu (const Item&);
        void showStripMenu (te::TimePosition);

        EditViewState& viewState;
        te::Edit& edit;
        bool draggingLoop = false, draggingPlayhead = false;
        te::TimePosition loopDragStart;
    };
}
