#pragma once

#include "../PCH.h"

namespace cdaw
{
    class ProjectSession;

    namespace ClipOps
    {
        /** Audio file extensions CDAW can import. */
        bool isImportableAudioFile (const juce::File&);
        bool isMidiFile (const juce::File&);

        /** Imports audio files at `start`. If `track` is null, each file gets a new track;
            otherwise files go onto consecutive tracks starting at `track`.
            Files are copied into the project's Audio/Imported folder when copyIntoProject is set. */
        juce::Array<te::Clip*> importAudioFiles (ProjectSession&, const juce::Array<juce::File>&,
                                                 te::AudioTrack* track, te::TimePosition start, bool copyIntoProject);

        /** Selected clips (from the selection manager). */
        juce::Array<te::Clip*> getSelectedClips (te::SelectionManager&);

        /** Removes whatever parts of other clips on `track` overlap `range`, so the clip
            occupying `range` plays alone (Tracktion sums overlapping clips). */
        void clearRangeForClip (te::ClipTrack&, te::TimeRange range, const juce::Array<te::Clip*>& exclude);

        /** Moves (or copies) clips by a time delta and a number of tracks in the arrangement order. */
        juce::Array<te::Clip*> moveClips (te::Edit&, const juce::Array<te::Clip*>&, te::TimeDuration delta,
                                          int trackDelta, bool copy);

        /** Splits every clip that spans `time` (selected clips only if any are selected). */
        void splitAt (te::Edit&, te::SelectionManager&, te::TimePosition time);
        void deleteClips (const juce::Array<te::Clip*>&);
        juce::Array<te::Clip*> duplicateClips (te::Edit&, const juce::Array<te::Clip*>&);
        void setMuted (const juce::Array<te::Clip*>&, bool);

        /** Trim an edge to `time` (clamped so the clip keeps a minimum length). */
        void trimStart (te::Clip&, te::TimePosition);
        void trimEnd (te::Clip&, te::TimePosition);

        juce::String describeClip (te::Clip&);
    }
}
