#include "EditViewState.h"
#include "CompOps.h"

namespace cdaw
{
    EditViewState::EditViewState (te::Edit& e, te::SelectionManager& sm)
        : edit (e), selectionManager (sm)
    {
        state = edit.state.getOrCreateChildWithName (ViewIDs::CDAWVIEW, nullptr);

        viewStart.referTo (state, ViewIDs::viewStart, nullptr, 0.0);
        pixelsPerSecond.referTo (state, ViewIDs::pixelsPerSecond, nullptr, 40.0);
        cursorPos.referTo (state, ViewIDs::cursorPos, nullptr, 0.0);
        rangeStart.referTo (state, ViewIDs::rangeStart, nullptr, 0.0);
        rangeEnd.referTo (state, ViewIDs::rangeEnd, nullptr, 0.0);
        scrollY.referTo (state, ViewIDs::scrollY, nullptr, 0);
        gridIndex.referTo (state, ViewIDs::gridIndex, nullptr, getAdaptiveGridIndex());
        tool.referTo (state, ViewIDs::tool, nullptr, (int) Tool::select);
        snapEnabled.referTo (state, ViewIDs::snapEnabled, nullptr, true);
        followPlayhead.referTo (state, ViewIDs::followPlayhead, nullptr, true);
    }

    const juce::Array<GridDivision>& EditViewState::getGridDivisions()
    {
        static const juce::Array<GridDivision> grids {
            GridDivision { "Bar",    0.0,             false },
            GridDivision { "1/2",    0.5,             false },
            GridDivision { "1/4",    0.25,            false },
            GridDivision { "1/8",    0.125,           false },
            GridDivision { "1/16",   0.0625,          false },
            GridDivision { "1/32",   0.03125,         false },
            GridDivision { "1/4T",   0.25 * 2 / 3,    true  },
            GridDivision { "1/8T",   0.125 * 2 / 3,   true  },
            GridDivision { "1/16T",  0.0625 * 2 / 3,  true  },
            GridDivision { "Adaptive", 0.0,           false, 1, true },
        };
        return grids;
    }

    int EditViewState::getAdaptiveGridIndex()   { return getGridDivisions().size() - 1; }

    int EditViewState::findGridIndex (const juce::String& name)
    {
        auto& grids = getGridDivisions();
        for (int i = 0; i < grids.size(); ++i)
            if (grids.getReference (i).name == name)
                return i;
        return 0;
    }

    bool EditViewState::isAdaptiveGrid() const
    {
        return gridIndex.get() == getAdaptiveGridIndex();
    }

    GridDivision EditViewState::resolveGrid (double pixelsPerBeat) const
    {
        auto& grids = getGridDivisions();
        auto chosen = grids[juce::jlimit (0, grids.size() - 1, gridIndex.get())];

        if (! chosen.adaptive)
            return chosen;

        // Finest division that is still at least ~16 px wide.
        constexpr double minPixels = 16.0;
        auto& sig = edit.tempoSequence.getTimeSigAt (xToTime (0.0));
        const int den = juce::jmax (1, sig.denominator.get());
        const int num = juce::jmax (1, sig.numerator.get());

        for (double whole : { 0.03125, 0.0625, 0.125, 0.25, 0.5 })
        {
            auto beats = whole * den;
            if (beats < num && beats * pixelsPerBeat >= minPixels)
            {
                auto name = "1/" + juce::String (juce::roundToInt (1.0 / whole));
                return GridDivision { name, whole, false, 1, false };
            }
        }

        for (int bars : { 1, 2, 4, 8, 16, 32, 64 })
            if (bars * num * pixelsPerBeat >= minPixels)
                return GridDivision { bars == 1 ? juce::String ("Bar") : juce::String (bars) + " bars", 0.0, false, bars, false };

        return GridDivision { "64 bars", 0.0, false, 64, false };
    }

    GridDivision EditViewState::getGrid() const
    {
        auto secondsPerBeat = 1.0 / juce::jmax (0.0001, edit.tempoSequence.getBeatsPerSecondAt (xToTime (0.0), true));
        return resolveGrid (pixelsPerSecond.get() * secondsPerBeat);
    }

    te::TimePosition EditViewState::snap (te::TimePosition t) const
    {
        return snapEnabled.get() ? snapAlways (t) : t;
    }

    te::TimePosition EditViewState::snapAlways (te::TimePosition t) const
    {
        auto& ts = edit.tempoSequence;
        auto grid = getGrid();

        if (grid.wholeNotes <= 0.0)
        {
            const int n = juce::jmax (1, grid.bars);
            auto bb = ts.toBarsAndBeats (t);
            auto first = (bb.bars / n) * n;
            auto barStart = ts.toTime (te::tempo::BarsAndBeats { first, {} });
            auto nextBar = ts.toTime (te::tempo::BarsAndBeats { first + n, {} });
            return (t - barStart) < (nextBar - t) ? barStart : nextBar;
        }

        // Grid measured in the time signature's beat unit (a beat is 1/denominator of a whole note).
        auto denominator = juce::jmax (1, ts.getTimeSigAt (t).denominator.get());
        auto gridBeats = grid.wholeNotes * denominator;
        auto beats = ts.toBeats (t).inBeats();
        auto rounded = std::round (beats / gridBeats) * gridBeats;
        return ts.toTime (te::BeatPosition::fromBeats (juce::jmax (0.0, rounded)));
    }

    te::TimeDuration EditViewState::getGridDuration (te::TimePosition at) const
    {
        auto& ts = edit.tempoSequence;
        auto grid = getGrid();
        auto denominator = juce::jmax (1, ts.getTimeSigAt (at).denominator.get());
        auto numerator = juce::jmax (1, ts.getTimeSigAt (at).numerator.get());
        auto gridBeats = grid.wholeNotes <= 0.0 ? (double) (numerator * juce::jmax (1, grid.bars)) : grid.wholeNotes * denominator;
        auto start = ts.toBeats (at);
        return ts.toTime (start + te::BeatDuration::fromBeats (gridBeats)) - ts.toTime (start);
    }

    te::TimeRange EditViewState::getRange() const
    {
        if (! hasRange())
            return {};

        return { te::TimePosition::fromSeconds (rangeStart.get()), te::TimePosition::fromSeconds (rangeEnd.get()) };
    }

    void EditViewState::setRange (te::TimeRange r)
    {
        rangeStart = r.getStart().inSeconds();
        rangeEnd = r.getEnd().inSeconds();
    }

    void EditViewState::zoomHorizontally (double factor, double anchorX)
    {
        auto anchorTime = viewStart.get() + anchorX / pixelsPerSecond.get();
        auto newPps = juce::jlimit (0.5, 4000.0, pixelsPerSecond.get() * factor);
        pixelsPerSecond = newPps;
        viewStart = juce::jmax (0.0, anchorTime - anchorX / newPps);
    }

    void EditViewState::setVisibleRange (double startSec, double endSec, int laneWidth)
    {
        if (endSec <= startSec || laneWidth <= 0)
            return;

        pixelsPerSecond = juce::jlimit (0.5, 4000.0, laneWidth / (endSec - startSec));
        viewStart = juce::jmax (0.0, startSec);
    }

    int EditViewState::getTrackHeight (const te::Track& t)
    {
        return juce::jlimit (minTrackHeight, maxTrackHeight, (int) t.state.getProperty (ViewIDs::cdawHeight, defaultTrackHeight));
    }

    int EditViewState::getDisplayHeight (te::Track& t)
    {
        auto h = getTrackHeight (t);

        if (isShowingTakes (t))
        {
            auto takes = CompOps::getMaxTakesOnTrack (t);
            if (takes > 1)
                h += takes * 28;   // TakeLanes::rowHeight
        }

        return h;
    }

    void EditViewState::setTrackHeight (te::Track& t, int h)
    {
        t.state.setProperty (ViewIDs::cdawHeight, juce::jlimit (minTrackHeight, maxTrackHeight, h), nullptr);
    }

    bool EditViewState::isShowingTakes (const te::Track& t)
    {
        return t.state.getProperty (ViewIDs::cdawShowTakes, false);
    }

    void EditViewState::setShowingTakes (te::Track& t, bool b)
    {
        t.state.setProperty (ViewIDs::cdawShowTakes, b, nullptr);
    }
}
