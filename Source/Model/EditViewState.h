#pragma once

#include "../PCH.h"

namespace cdaw
{
    namespace ViewIDs
    {
        #define CDAW_DECLARE_ID(name) inline const juce::Identifier name (#name);
        CDAW_DECLARE_ID (CDAWVIEW)
        CDAW_DECLARE_ID (viewStart)
        CDAW_DECLARE_ID (pixelsPerSecond)
        CDAW_DECLARE_ID (scrollY)
        CDAW_DECLARE_ID (snapEnabled)
        CDAW_DECLARE_ID (gridIndex)
        CDAW_DECLARE_ID (tool)
        CDAW_DECLARE_ID (followPlayhead)
        CDAW_DECLARE_ID (cursorPos)
        CDAW_DECLARE_ID (rangeStart)
        CDAW_DECLARE_ID (rangeEnd)
        CDAW_DECLARE_ID (cdawHeight)
        CDAW_DECLARE_ID (cdawShowTakes)
        CDAW_DECLARE_ID (cdawAutoLanes)
        CDAW_DECLARE_ID (cdawType)
        CDAW_DECLARE_ID (cdawDrumMap)
        CDAW_DECLARE_ID (cdawCollapsed)
        CDAW_DECLARE_ID (showSpectrum)
        CDAW_DECLARE_ID (showArranger)
        #undef CDAW_DECLARE_ID
    }

    enum class Tool { select = 0, range, split, draw, erase, mute, glue };

    class MasterAnalyser;

    /** A musical grid value. */
    struct GridDivision
    {
        juce::String name;
        double wholeNotes = 0.25;   // length as fraction of a whole note; <= 0 means whole bars
        bool triplet = false;
        int bars = 1;               // for bar grids: snap to every N bars
        bool adaptive = false;      // follows the zoom level
    };

    /** View state for one Edit (zoom, scroll, grid, tool, cursor), stored in the Edit's
        CDAWVIEW child with no UndoManager so view changes are never undoable. */
    class EditViewState
    {
    public:
        EditViewState (te::Edit&, te::SelectionManager&);

        te::Edit& edit;
        te::SelectionManager& selectionManager;
        juce::ValueTree state;

        /** The project's master output analyser (owned by ProjectSession; null in tests that don't make one). */
        MasterAnalyser* analyser = nullptr;

        juce::CachedValue<double> viewStart, pixelsPerSecond, cursorPos, rangeStart, rangeEnd;
        juce::CachedValue<int> scrollY, gridIndex, tool;
        juce::CachedValue<bool> snapEnabled, followPlayhead;

        // Time <-> pixel (x relative to the lane area's left edge)
        double timeToX (te::TimePosition t) const       { return (t.inSeconds() - viewStart.get()) * pixelsPerSecond.get(); }
        double timeToX (double seconds) const           { return (seconds - viewStart.get()) * pixelsPerSecond.get(); }
        te::TimePosition xToTime (double x) const       { return te::TimePosition::fromSeconds (juce::jmax (0.0, viewStart.get() + x / pixelsPerSecond.get())); }
        double durationToPixels (te::TimeDuration d) const { return d.inSeconds() * pixelsPerSecond.get(); }

        // Grid / snapping
        static const juce::Array<GridDivision>& getGridDivisions();
        GridDivision getGrid() const;                                   // resolves "Adaptive" for the arrangement zoom
        GridDivision resolveGrid (double pixelsPerBeat) const;          // resolves "Adaptive" for any zoom (piano roll)
        bool isAdaptiveGrid() const;
        static int getAdaptiveGridIndex();
        static int findGridIndex (const juce::String& name);
        te::TimePosition snap (te::TimePosition) const;                 // honours snapEnabled
        te::TimePosition snapAlways (te::TimePosition) const;           // ignores snapEnabled
        te::TimeDuration getGridDuration (te::TimePosition at) const;
        Tool getTool() const                            { return (Tool) tool.get(); }

        // Range selection (for range tool, cut/copy of time ranges)
        bool hasRange() const                           { return rangeEnd.get() > rangeStart.get(); }
        te::TimeRange getRange() const;
        void setRange (te::TimeRange);
        void clearRange()                               { setRange ({}); }

        // Zoom
        void zoomHorizontally (double factor, double anchorX);
        void setVisibleRange (double startSec, double endSec, int laneWidth);

        // Per-track UI state (stored on the track, without undo)
        static int getTrackHeight (const te::Track&);
        /** Track height plus its take lanes when they're shown. */
        static int getDisplayHeight (te::Track&);
        static void setTrackHeight (te::Track&, int);
        static bool isShowingTakes (const te::Track&);
        static void setShowingTakes (te::Track&, bool);
        static constexpr int defaultTrackHeight = 72;
        static constexpr int minTrackHeight = 34;
        static constexpr int maxTrackHeight = 400;
    };
}
