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

        /** The same, for the bars between two x positions (bar containing fromX first). */
        void forEachBar (const EditViewState&, double fromX, double toX, const std::function<void (int, te::TimePosition)>& fn);

        /** Width of one bar at the left edge of the view, in pixels. */
        double pixelsPerBar (const EditViewState&);

        /** Smallest power-of-two bar count (1, 2, 4 ...) that spans at least minPixels. */
        int barStep (double pxPerBar, double minPixels);
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
        static bool affectsRuler (const juce::ValueTree&, const juce::Identifier&);
        void valueTreePropertyChanged (juce::ValueTree& v, const juce::Identifier& id) override  { if (affectsRuler (v, id)) itemsChanged(); }
        void valueTreeChildAdded (juce::ValueTree& p, juce::ValueTree& c) override                 { if (affectsRuler (c, {}) || affectsRuler (p, {})) itemsChanged(); }
        void valueTreeChildRemoved (juce::ValueTree& p, juce::ValueTree& c, int) override          { if (affectsRuler (c, {}) || affectsRuler (p, {})) itemsChanged(); }
        void itemsChanged()                                                                         { itemsDirty = true; repaint(); }

        struct Item
        {
            enum class Kind { marker, tempo, timeSig } kind;
            juce::Rectangle<int> bounds;
            te::MarkerClip::Ptr marker;
            int index = -1;
        };

        std::vector<Item> layoutItems() const;

        /** layoutItems(), cached: measuring the labels on every playhead repaint adds up. */
        const std::vector<Item>& getItems() const;
        mutable std::vector<Item> itemCache;
        mutable bool itemsDirty = true;
        mutable double cachedViewStart = -1.0, cachedZoom = -1.0;
        mutable int cachedWidth = -1;
        const Item* itemAt (juce::Point<int>) const;
        void showItemMenu (const Item&);
        void showStripMenu (te::TimePosition);

        EditViewState& viewState;
        te::Edit& edit;
        bool draggingLoop = false, draggingPlayhead = false;
        te::TimePosition loopDragStart;
    };
}
