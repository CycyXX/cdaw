#pragma once

#include "../PCH.h"

namespace cdaw
{
    /** Takes and comping without Tracktion projects.

        Loop recording leaves a WaveAudioClip with a list of take files (one per pass,
        all aligned to the clip start). A comp is simply the clip split into adjacent
        pieces, each piece playing one take. Swiping a range on a take lane splits at
        the range edges and switches the pieces inside to that take. Everything stays
        editable: any piece can be switched to another take later. */
    namespace CompOps
    {
        /** The clip's take files, de-duplicated, in recording order. */
        juce::Array<juce::File> getTakeFiles (te::WaveAudioClip&);
        bool hasTakes (te::Clip&);
        int getNumTakes (te::Clip&);

        /** Index of the take currently playing, or -1 if the source isn't one of the takes. */
        int getActiveTake (te::WaveAudioClip&);
        void setActiveTake (te::WaveAudioClip&, int takeIndex);

        /** Most takes of any clip on the track (for sizing the take lanes). */
        int getMaxTakesOnTrack (te::Track&);

        /** Comps `range` on `track` to `takeIndex`: splits clips with takes at the range
            edges and switches the pieces inside. Adds short fades at the new joins. */
        void compRange (te::Track&, te::TimeRange, int takeIndex, double joinFadeMs = 5.0);

        /** Creates one new audio track per take under the clip's track. */
        juce::Array<te::Clip*> unpackTakesToTracks (te::WaveAudioClip&);

        /** Forgets the take list (the current take stays). Files are kept on disk. */
        void removeTakes (te::WaveAudioClip&);
    }
}
