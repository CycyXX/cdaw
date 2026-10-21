#include "CompOps.h"
#include "TrackOps.h"

namespace cdaw::CompOps
{
    juce::Array<juce::File> getTakeFiles (te::WaveAudioClip& clip)
    {
        juce::Array<juce::File> files;
        auto takes = clip.state.getChildWithName (te::IDs::TAKES);

        for (auto take : takes)
        {
            auto source = take.getProperty (te::IDs::source).toString();
            auto file = te::SourceFileReference::findFileFromString (clip.edit, source);

            if (file != juce::File() && ! files.contains (file))
                files.add (file);
        }

        // Tracktion adds the first pass both first and last; keeping first occurrences
        // gives recording order: pass 1, pass 2, ...
        return files;
    }

    bool hasTakes (te::Clip& c)
    {
        return getNumTakes (c) > 1;
    }

    int getNumTakes (te::Clip& c)
    {
        if (auto wc = dynamic_cast<te::WaveAudioClip*> (&c))
            return getTakeFiles (*wc).size();

        return 0;
    }

    int getActiveTake (te::WaveAudioClip& clip)
    {
        return getTakeFiles (clip).indexOf (clip.getSourceFileReference().getFile());
    }

    void setActiveTake (te::WaveAudioClip& clip, int takeIndex)
    {
        auto files = getTakeFiles (clip);

        if (! juce::isPositiveAndBelow (takeIndex, files.size()))
            return;

        if (files[takeIndex] == clip.getSourceFileReference().getFile())
            return;

        // Re-point the source; position and offset stay, and all takes share the same timing.
        clip.getSourceFileReference().setToFile (files[takeIndex], te::SourceFileReference::PathStyle::chooseBest, false);
    }

    int getMaxTakesOnTrack (te::Track& t)
    {
        int maxTakes = 0;

        if (auto ct = dynamic_cast<te::ClipTrack*> (&t))
            for (auto c : ct->getClips())
                maxTakes = juce::jmax (maxTakes, getNumTakes (*c));

        return maxTakes;
    }

    void compRange (te::Track& track, te::TimeRange range, int takeIndex, double joinFadeMs)
    {
        auto ct = dynamic_cast<te::ClipTrack*> (&track);
        if (ct == nullptr || range.isEmpty())
            return;

        const auto fade = te::TimeDuration::fromSeconds (joinFadeMs / 1000.0);
        juce::ReferenceCountedArray<te::Clip> candidates;

        for (auto c : ct->getClips())
            if (getNumTakes (*c) > takeIndex && c->getEditTimeRange().overlaps (range))
                candidates.add (c);

        for (auto c : candidates)
        {
            te::Clip::Ptr piece = c;

            // Split off the part before the range.
            if (piece->getEditTimeRange().getStart() < range.getStart())
                if (auto right = ct->splitClip (*piece, range.getStart()))
                    piece = right;

            // Split off the part after the range.
            if (piece->getEditTimeRange().getEnd() > range.getEnd())
                ct->splitClip (*piece, range.getEnd());

            if (auto wc = dynamic_cast<te::WaveAudioClip*> (piece.get()))
            {
                setActiveTake (*wc, takeIndex);
                wc->setFadeIn (fade);
                wc->setFadeOut (fade);
            }
        }

        // Short fades on the neighbours at the joins, so switching takes doesn't click.
        for (auto c : ct->getClips())
        {
            if (auto wc = dynamic_cast<te::WaveAudioClip*> (c))
            {
                auto r = wc->getEditTimeRange();
                if (std::abs ((r.getEnd() - range.getStart()).inSeconds()) < 1e-6 && wc->getFadeOut() < fade)
                    wc->setFadeOut (fade);
                if (std::abs ((r.getStart() - range.getEnd()).inSeconds()) < 1e-6 && wc->getFadeIn() < fade)
                    wc->setFadeIn (fade);
            }
        }
    }

    juce::Array<te::Clip*> unpackTakesToTracks (te::WaveAudioClip& clip)
    {
        juce::Array<te::Clip*> created;
        auto& edit = clip.edit;
        auto files = getTakeFiles (clip);
        auto* after = clip.getTrack();
        auto pos = clip.getPosition();

        for (int i = 0; i < files.size(); ++i)
        {
            auto t = TrackOps::addAudioTrack (edit, nullptr, after,
                                              (after != nullptr ? after->getName() : juce::String ("Take")) + " take " + juce::String (i + 1));
            if (t == nullptr)
                continue;

            if (auto nc = t->insertWaveClip (clip.getName() + " take " + juce::String (i + 1), files[i], { pos.time, pos.offset }, false))
                created.add (nc.get());

            after = t.get();
        }

        return created;
    }

    void removeTakes (te::WaveAudioClip& clip)
    {
        auto takes = clip.state.getChildWithName (te::IDs::TAKES);

        if (takes.isValid())
            clip.state.removeChild (takes, &clip.edit.getUndoManager());
    }
}
