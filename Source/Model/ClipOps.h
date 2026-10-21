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

        /** MP3/OGG imports run in the background (importAudioFiles returns an empty array and a
            progress window shows). This variant does everything synchronously (tests). */
        juce::Array<te::Clip*> importAudioFilesNow (ProjectSession&, const juce::Array<juce::File>&,
                                                    te::AudioTrack* track, te::TimePosition start, bool copyIntoProject);

        /** Stops a background import (call before closing the project). */
        void cancelRunningImport();

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

        /** Crossfades two audio clips that touch (or overlap) on the same track: each is
            extended into the other by half of `length` (as far as its audio allows) and
            given an equal-power fade over the overlap. Returns false if they don't touch. */
        bool createCrossfade (te::AudioClipBase& left, te::AudioClipBase& right, te::TimeDuration length);

        /** Crossfades every touching pair among `clips` (or, for a single clip, with its
            touching neighbours). Returns the number of crossfades made. */
        int crossfadeClips (const juce::Array<te::Clip*>&, te::TimeDuration length);

        static constexpr double defaultCrossfadeSeconds = 0.02;

        /** Loop a MIDI clip: its current content repeats when the end is dragged out. */
        bool canToggleLoop (te::Clip&);
        void setLooping (te::Clip&, bool shouldLoop);

        juce::String describeClip (te::Clip&);
    }
}
