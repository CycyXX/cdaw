#pragma once

#include "../PCH.h"

namespace cdaw::TempoOps
{
    /** Inserts (or edits) a tempo change at the beat nearest `t`. */
    void setTempoAt (te::Edit&, te::TimePosition t, double bpm);
    /** Inserts (or edits) a time signature at the bar containing `t` (e.g. "7/8"). */
    void setTimeSigAt (te::Edit&, te::TimePosition t, const juce::String& sig);

    /** Adds a marker at `t`, numbered after the existing ones. */
    te::MarkerClip* addMarker (te::Edit&, te::TimePosition t, const juce::String& name = {});
    juce::ReferenceCountedArray<te::MarkerClip> getMarkers (te::Edit&);
    te::TimePosition getNextMarkerTime (te::Edit&, te::TimePosition from, bool forwards);

    /** A position as bar.beat.tick, counted from 1 with 960 ticks a beat ("5.1.000"). */
    juce::String formatBarsBeats (te::Edit&, te::TimePosition);

    /** Reads a position typed as bars ("5"), bars and beats ("5.3"), with ticks ("5.3.480"), or a
        time ("1:23.5", "83.5 s"). Nothing if it can't be read. */
    std::optional<te::TimePosition> parsePosition (te::Edit&, const juce::String&);

    /** A time as m:ss.mmm. */
    juce::String formatTime (te::TimePosition);
}
