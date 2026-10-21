#include "ClipOps.h"
#include "ProjectSession.h"
#include "TrackOps.h"

namespace cdaw::ClipOps
{
    static constexpr double minClipLength = 0.005;

    bool isImportableAudioFile (const juce::File& f)
    {
        return f.hasFileExtension ("wav;wave;aif;aiff;flac;ogg;mp3;bwf");
    }

    bool isMidiFile (const juce::File& f)
    {
        return f.hasFileExtension ("mid;midi");
    }

    static juce::File copyIntoProjectIfNeeded (ProjectSession& session, const juce::File& file, bool copy)
    {
        if (! copy || file.isAChildOf (session.getProjectFolder()))
            return file;

        auto dest = session.getAudioFolder().getChildFile ("Imported");
        dest.createDirectory();
        auto target = dest.getChildFile (file.getFileName());

        // Re-use an identical copy rather than duplicating it.
        if (target.existsAsFile() && target.getSize() == file.getSize())
            return target;

        target = dest.getNonexistentChildFile (file.getFileNameWithoutExtension(), file.getFileExtension(), false);
        return file.copyFileTo (target) ? target : file;
    }

    juce::Array<te::Clip*> importAudioFiles (ProjectSession& session, const juce::Array<juce::File>& files,
                                             te::AudioTrack* track, te::TimePosition start, bool copyIntoProject)
    {
        auto& edit = session.getEdit();
        auto& sm = session.getSelectionManager();
        juce::Array<te::Clip*> result;
        te::Track* lastTrack = track;

        edit.getUndoManager().beginNewTransaction ("Import audio");

        for (auto& source : files)
        {
            if (! isImportableAudioFile (source))
                continue;

            auto file = copyIntoProjectIfNeeded (session, source, copyIntoProject);
            te::AudioFile af (edit.engine, file);

            if (! af.isValid())
            {
                juce::Logger::writeToLog ("Import skipped (unreadable): " + source.getFullPathName());
                continue;
            }

            te::AudioTrack* target = nullptr;

            if (track != nullptr && result.isEmpty())
            {
                target = track;
            }
            else if (track != nullptr)
            {
                // Next audio track below the previous one, creating it if needed.
                auto all = te::getAudioTracks (edit);
                auto idx = all.indexOf (dynamic_cast<te::AudioTrack*> (lastTrack));
                target = idx >= 0 && idx + 1 < all.size() ? all[idx + 1] : nullptr;
            }

            if (target == nullptr)
                target = TrackOps::addAudioTrack (edit, nullptr, lastTrack, source.getFileNameWithoutExtension()).get();

            if (target == nullptr)
                continue;

            auto length = te::TimeDuration::fromSeconds (af.getLength());
            auto range = te::TimeRange (start, length);
            clearRangeForClip (*target, range, {});

            if (auto clip = target->insertWaveClip (source.getFileNameWithoutExtension(), file, { range, {} }, false))
                result.add (clip.get());

            lastTrack = target;
        }

        if (! result.isEmpty())
        {
            sm.deselectAll();
            for (auto c : result)
                sm.addToSelection (c);
        }

        return result;
    }

    juce::Array<te::Clip*> getSelectedClips (te::SelectionManager& sm)
    {
        juce::Array<te::Clip*> clips;

        for (auto s : sm.getSelectedObjects())
            if (auto c = dynamic_cast<te::Clip*> (s))
                clips.add (c);

        return clips;
    }

    void clearRangeForClip (te::ClipTrack& track, te::TimeRange range, const juce::Array<te::Clip*>& exclude)
    {
        juce::ReferenceCountedArray<te::Clip> overlapping;

        for (auto c : track.getClips())
            if (! exclude.contains (c) && c->getEditTimeRange().overlaps (range))
                overlapping.add (c);

        for (auto c : overlapping)
            te::deleteRegionOfClip (*c, range);
    }

    juce::Array<te::Clip*> moveClips (te::Edit& edit, const juce::Array<te::Clip*>& clips, te::TimeDuration delta,
                                      int trackDelta, bool copy)
    {
        juce::Array<te::Clip*> moved;

        if (clips.isEmpty())
            return moved;

        // Don't move anything before zero.
        for (auto c : clips)
            if (c->getPosition().getStart() + delta < te::TimePosition())
                delta = te::TimePosition() - c->getPosition().getStart();

        auto tracks = TrackOps::getArrangeTracks (edit);

        for (auto c : clips)
        {
            te::Clip::Ptr clip = c;

            if (copy)
                clip = te::duplicateClip (*c);

            if (clip == nullptr)
                continue;

            if (trackDelta != 0)
            {
                auto idx = tracks.indexOf (c->getTrack());
                auto newIdx = idx + trackDelta;

                // Find the nearest clip track at or beyond the target index.
                while (juce::isPositiveAndBelow (newIdx, tracks.size()) && dynamic_cast<te::ClipTrack*> (tracks[newIdx]) == nullptr)
                    newIdx += trackDelta > 0 ? 1 : -1;

                if (auto destTrack = dynamic_cast<te::ClipTrack*> (tracks[newIdx]))
                    if (destTrack != clip->getClipTrack())
                        clip->moveTo (*destTrack);
            }

            auto pos = clip->getPosition();
            pos.time = pos.time + delta;
            clip->setPosition (pos);
            moved.add (clip.get());
        }

        // Moved clips replace whatever they now cover (Cubase-style "clip on top wins").
        for (auto c : moved)
            if (auto ct = c->getClipTrack())
                clearRangeForClip (*ct, c->getEditTimeRange(), moved);

        return moved;
    }

    void splitAt (te::Edit& edit, te::SelectionManager& sm, te::TimePosition time)
    {
        te::SelectableList toSplit;

        for (auto c : getSelectedClips (sm))
            toSplit.add (c);

        if (toSplit.isEmpty())
            for (auto ct : te::getClipTracks (edit))
                for (auto c : ct->getClips())
                    toSplit.add (c);

        edit.getUndoManager().beginNewTransaction ("Split");
        te::splitClips (toSplit, time);
    }

    void deleteClips (const juce::Array<te::Clip*>& clips)
    {
        juce::ReferenceCountedArray<te::Clip> keep;
        for (auto c : clips)
            keep.add (c);

        for (auto c : keep)
            c->removeFromParent();
    }

    juce::Array<te::Clip*> duplicateClips (te::Edit& edit, const juce::Array<te::Clip*>& clips)
    {
        if (clips.isEmpty())
            return {};

        auto start = clips.getFirst()->getPosition().getStart();
        auto end = clips.getFirst()->getPosition().getEnd();

        for (auto c : clips)
        {
            start = juce::jmin (start, c->getPosition().getStart());
            end = juce::jmax (end, c->getPosition().getEnd());
        }

        return moveClips (edit, clips, end - start, 0, true);
    }

    void setMuted (const juce::Array<te::Clip*>& clips, bool muted)
    {
        for (auto c : clips)
        {
            if (auto ac = dynamic_cast<te::AudioClipBase*> (c))
                ac->setMuted (muted);
            else if (auto mc = dynamic_cast<te::MidiClip*> (c))
                mc->setMuted (muted);
        }
    }

    void trimStart (te::Clip& clip, te::TimePosition t)
    {
        auto end = clip.getPosition().getEnd();
        t = juce::jmin (t, end - te::TimeDuration::fromSeconds (minClipLength));
        t = juce::jmax (t, te::TimePosition());

        // Can't extend before the start of the source material.
        auto pos = clip.getPosition();
        auto earliest = pos.getStart() - pos.getOffset();

        if (clip.usesSourceFile())
            t = juce::jmax (t, earliest);

        clip.setStart (t, true, false);
    }

    void trimEnd (te::Clip& clip, te::TimePosition t)
    {
        auto start = clip.getPosition().getStart();
        t = juce::jmax (t, start + te::TimeDuration::fromSeconds (minClipLength));
        clip.setEnd (t, true);
    }

    juce::String describeClip (te::Clip& c)
    {
        auto pos = c.getPosition();
        return c.getName() + "  (" + juce::String (pos.getStart().inSeconds(), 3) + " s, "
               + juce::String (pos.getLength().inSeconds(), 3) + " s)";
    }
}
