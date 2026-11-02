#pragma once

#include "../PCH.h"

namespace cdaw
{
    /** Takes and comping, for audio and MIDI clips, without Tracktion projects.

        A clip can hold several takes: one is playing, the others wait in its state, so they move,
        copy and undo with the clip. Loop recording leaves a clip like that, and so does recording
        over something already on the track (see absorbRecording): the new recording plays, and
        whatever it covers stays available as takes.

        A comp is the clip split into adjacent pieces, each playing one take, and each still knowing
        all of them. Swiping a range on a take lane splits at the range edges and switches the pieces
        inside to that take. Everything stays editable: any piece can be switched again later.

        Takes are addressed by row, the lane they show in, which is the same in every piece of a comp.
        Passes don't have to line up: each take knows where it starts against the others, and how
        much of the time line it covers. */
    namespace CompOps
    {
        struct TakeInfo
        {
            int row = 0;
            te::TimeRange range;            // the edit time this take covers, as this piece is placed now
            bool active = false;            // what this piece is playing
            juce::File file;                // audio
            te::MidiList* midi = nullptr;   // MIDI
            te::BeatPosition midiStart;     // MIDI: the edit beat its beat 0 falls on
        };

        /** The clip's takes in lane order. A clip that has never had a second take may return one or none. */
        std::vector<TakeInfo> getTakes (te::Clip&);

        bool hasTakes (te::Clip&);
        int getNumTakes (te::Clip&);

        /** The row of the take currently playing, or -1 if the clip's source isn't one of its takes. */
        int getActiveTake (te::Clip&);

        /** Plays that take in the whole clip, whatever part of it the take covers. Mostly useful when
            all takes line up: useTake only switches the part the take has material for. */
        void setActiveTake (te::Clip&, int row);

        /** Lanes needed to show every clip on the track (for sizing the take lanes). */
        int getMaxTakesOnTrack (te::Track&);

        /** Whether any clip on the track holds more than one take. Cheap enough for a UI timer. */
        bool trackHasTakes (te::Track&);

        /** Comps `range` on `track` to that row: splits clips at the range edges (as far as the take
            has material) and switches the pieces inside. Adds short fades at the new joins, and rejoins
            pieces that end up playing the same take without a break. */
        void compRange (te::Track&, te::TimeRange, int row, double joinFadeMs = 5.0);

        /** Switches this clip to that row, as far as the take covers it. */
        void useTake (te::Clip&, int row);

        /** Switches to the next (or previous) take that has material under this clip, wrapping round. */
        void stepTake (te::Clip&, int direction);

        /** Called when a recording lands: whatever it overlaps on its track becomes takes in the
            recording's lanes, and the recording plays over the range it covers. */
        void absorbRecording (te::Clip& recorded);

        /** The clip and the pieces around it that form one comp: touching it and sharing takes with it. */
        juce::ReferenceCountedArray<te::Clip> getComp (te::Clip&);

        /** Creates one new track per take under the clip's track, each with the whole take. */
        juce::Array<te::Clip*> unpackTakesToTracks (te::Clip&);

        /** Forgets the take list (what's playing stays). Audio files are kept on disk. */
        void removeTakes (te::Clip&);

        /** Forgets the takes no piece of this comp is playing, and closes up the lanes. */
        void deleteUnusedTakes (te::Clip&);
    }
}
