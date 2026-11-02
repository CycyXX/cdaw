#pragma once

#include "../PCH.h"

namespace cdaw
{
    /** Group editing: the tracks in a folder are edited as one, e.g. a drum kit split over 16 tracks.

        It is switched on per folder. Inside such a folder, clips with the same start and end across
        its tracks are linked: selecting one selects them all, so moving, deleting, muting, nudging and
        the rest of the selection's commands apply to all of them, and trimming, fades, gain and
        slipping one follow on the others. Splits, and comping on a take lane, go through the whole
        group at that time, so the tracks stay in phase. */
    namespace GroupEdit
    {
        bool isEnabled (const te::Track& folder);
        void setEnabled (te::Track& folder, bool);

        /** The folder that makes this track part of a group (the outermost one with group editing on), or null. */
        te::FolderTrack* findGroupFolder (const te::Track&);

        /** The track's own folder, or the track itself if it is one: what a toggle command acts on. */
        te::FolderTrack* findNearestFolder (te::Track&);

        /** The clip tracks in the track's group, or just the track when it isn't in one. */
        juce::Array<te::ClipTrack*> getGroupTracks (te::Track&);

        /** The clip and those lined up with it on the other tracks of its group. */
        juce::ReferenceCountedArray<te::Clip> getLinkedClips (te::Clip&);

        /** Adds what's linked to each clip. */
        juce::ReferenceCountedArray<te::Clip> withLinkedClips (const juce::Array<te::Clip*>&);

        /** Selects the clip and those linked to it, on their own or added to the selection. */
        void selectClip (te::SelectionManager&, te::Clip&, bool addToSelection);
        void deselectClip (te::SelectionManager&, te::Clip&);

        /** Adds the other tracks of the groups the selected tracks are in (a range covers the whole group). */
        void addGroupTracksToSelection (te::SelectionManager&);

        /** Splits the clips at `time` on every track of the group. */
        void splitAt (te::Track&, te::TimePosition);

        /** Comps a range to a take lane on every track of the group. */
        void compRange (te::Track&, te::TimeRange, int row);

        /** Plays that take in the clips at `time` on every track of the group. */
        void useTakeAt (te::Track&, te::TimePosition, int row);

        /** Puts the tracks in a new folder with group editing on. */
        te::FolderTrack::Ptr groupTracks (te::Edit&, te::SelectionManager*, juce::Array<te::Track*>);
    }
}
