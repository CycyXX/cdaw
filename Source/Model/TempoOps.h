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
}
