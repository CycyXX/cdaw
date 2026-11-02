#include "GroupEdit.h"
#include "CompOps.h"
#include "TrackOps.h"

namespace cdaw::GroupEdit
{
    namespace
    {
        const juce::Identifier enabledID ("cdawGroupEdit");

        // Multi-mic recordings of one pass can end a few samples apart.
        constexpr double lineUpSeconds = 0.01;
    }

    bool isEnabled (const te::Track& folder)
    {
        return folder.state.getProperty (enabledID, false);
    }

    void setEnabled (te::Track& folder, bool enabled)
    {
        folder.state.setProperty (enabledID, enabled, nullptr);
        folder.edit.markAsChanged();
    }

    te::FolderTrack* findGroupFolder (const te::Track& track)
    {
        te::FolderTrack* found = nullptr;
        auto* t = dynamic_cast<te::FolderTrack*> (const_cast<te::Track*> (&track));

        for (t = t != nullptr ? t : track.getParentFolderTrack(); t != nullptr; t = t->getParentFolderTrack())
            if (isEnabled (*t))
                found = t;

        return found;
    }

    te::FolderTrack* findNearestFolder (te::Track& track)
    {
        if (auto* ft = dynamic_cast<te::FolderTrack*> (&track))
            return ft;

        return track.getParentFolderTrack();
    }

    juce::Array<te::ClipTrack*> getGroupTracks (te::Track& track)
    {
        juce::Array<te::ClipTrack*> tracks;

        if (auto* folder = findGroupFolder (track))
        {
            for (auto* t : folder->getAllSubTracks (true))
                if (auto* ct = dynamic_cast<te::ClipTrack*> (t))
                    tracks.add (ct);
        }
        else if (auto* ct = dynamic_cast<te::ClipTrack*> (&track))
        {
            tracks.add (ct);
        }

        return tracks;
    }

    juce::ReferenceCountedArray<te::Clip> getLinkedClips (te::Clip& clip)
    {
        juce::ReferenceCountedArray<te::Clip> linked;
        linked.add (&clip);

        auto* track = clip.getTrack();

        if (track == nullptr || findGroupFolder (*track) == nullptr)
            return linked;

        auto range = clip.getEditTimeRange();

        for (auto* ct : getGroupTracks (*track))
        {
            if (ct == clip.getClipTrack())
                continue;

            for (auto c : ct->getClips())
                if (std::abs ((c->getPosition().getStart() - range.getStart()).inSeconds()) < lineUpSeconds
                     && std::abs ((c->getPosition().getEnd() - range.getEnd()).inSeconds()) < lineUpSeconds)
                    linked.add (c);
        }

        return linked;
    }

    juce::ReferenceCountedArray<te::Clip> withLinkedClips (const juce::Array<te::Clip*>& clips)
    {
        juce::ReferenceCountedArray<te::Clip> all;

        for (auto c : clips)
            for (auto l : getLinkedClips (*c))
                all.addIfNotAlreadyThere (l);

        return all;
    }

    void selectClip (te::SelectionManager& sm, te::Clip& clip, bool addToSelection)
    {
        if (! addToSelection)
            sm.selectOnly (&clip);

        for (auto c : getLinkedClips (clip))
            sm.addToSelection (c);
    }

    void deselectClip (te::SelectionManager& sm, te::Clip& clip)
    {
        for (auto c : getLinkedClips (clip))
            sm.deselect (c);
    }

    void addGroupTracksToSelection (te::SelectionManager& sm)
    {
        for (auto* t : sm.getItemsOfType<te::Track>())
            if (findGroupFolder (*t) != nullptr)
                for (auto* ct : getGroupTracks (*t))
                    sm.addToSelection (ct);
    }

    void splitAt (te::Track& track, te::TimePosition time)
    {
        for (auto* ct : getGroupTracks (track))
        {
            juce::ReferenceCountedArray<te::Clip> clips;

            for (auto c : ct->getClips())
                if (c->getEditTimeRange().contains (time))
                    clips.add (c);

            for (auto c : clips)
                ct->splitClip (*c, time);
        }
    }

    void compRange (te::Track& track, te::TimeRange range, int row)
    {
        for (auto* ct : getGroupTracks (track))
            CompOps::compRange (*ct, range, row);
    }

    void useTakeAt (te::Track& track, te::TimePosition time, int row)
    {
        for (auto* ct : getGroupTracks (track))
        {
            juce::ReferenceCountedArray<te::Clip> clips;

            for (auto c : ct->getClips())
                if (c->getEditTimeRange().contains (time))
                    clips.add (c);

            for (auto c : clips)
                CompOps::useTake (*c, row);
        }
    }

    te::FolderTrack::Ptr groupTracks (te::Edit& edit, te::SelectionManager* sm, juce::Array<te::Track*> tracks)
    {
        // In arrangement order, without tracks whose folder is moving anyway.
        auto order = TrackOps::getArrangeTracks (edit);
        std::stable_sort (tracks.begin(), tracks.end(), [&order] (te::Track* a, te::Track* b) { return order.indexOf (a) < order.indexOf (b); });

        const auto all = tracks;
        tracks.removeIf ([&all] (te::Track* t)
        {
            return std::any_of (all.begin(), all.end(), [t] (te::Track* other) { return other != t && t->isAChildOf (*other); });
        });

        if (tracks.isEmpty())
            return {};

        auto folder = TrackOps::addFolderTrack (edit, sm, false, tracks.getFirst());

        if (folder == nullptr)
            return {};

        te::Track* previous = nullptr;

        for (auto* t : tracks)
        {
            edit.moveTrack (t, te::TrackInsertPoint (folder.get(), previous));
            previous = t;
        }

        setEnabled (*folder, true);
        return folder;
    }
}
