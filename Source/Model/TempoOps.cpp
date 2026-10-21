#include "TempoOps.h"

namespace cdaw::TempoOps
{
    void setTempoAt (te::Edit& edit, te::TimePosition t, double bpm)
    {
        auto& ts = edit.tempoSequence;
        bpm = juce::jlimit (20.0, 400.0, bpm);
        auto beat = te::BeatPosition::fromBeats (std::round (ts.toBeats (t).inBeats()));

        // Edit an existing change at this beat rather than stacking a new one.
        for (int i = 0; i < ts.getNumTempos(); ++i)
        {
            if (auto* tempo = ts.getTempo (i); std::abs ((tempo->getStartBeat() - beat).inBeats()) < 0.01)
            {
                tempo->setBpm (bpm);
                return;
            }
        }

        if (beat.inBeats() <= 0.0)
            ts.getTempo (0)->setBpm (bpm);
        else
            ts.insertTempo (beat, bpm, 0.0f);
    }

    void setTimeSigAt (te::Edit& edit, te::TimePosition t, const juce::String& sig)
    {
        auto& ts = edit.tempoSequence;
        auto bb = ts.toBarsAndBeats (t);
        auto barStart = ts.toTime (te::tempo::BarsAndBeats { bb.bars, {} });
        auto beat = ts.toBeats (barStart);

        for (int i = 0; i < ts.getNumTimeSigs(); ++i)
        {
            if (auto* s = ts.getTimeSig (i); std::abs ((s->getStartBeat() - beat).inBeats()) < 0.01)
            {
                s->setStringTimeSig (sig);
                return;
            }
        }

        if (beat.inBeats() <= 0.0)
            ts.getTimeSig (0)->setStringTimeSig (sig);
        else if (auto newSig = ts.insertTimeSig (beat))
            newSig->setStringTimeSig (sig);
    }

    juce::ReferenceCountedArray<te::MarkerClip> getMarkers (te::Edit& edit)
    {
        return edit.getMarkerManager().getMarkers();
    }

    te::MarkerClip* addMarker (te::Edit& edit, te::TimePosition t, const juce::String& name)
    {
        edit.ensureMarkerTrack();
        auto& mm = edit.getMarkerManager();
        auto id = mm.getNextUniqueID();
        auto marker = mm.createMarker (id, t, te::TimeDuration(), nullptr);

        if (marker != nullptr)
            marker->setName (name.isNotEmpty() ? name : "Marker " + juce::String (id));

        return dynamic_cast<te::MarkerClip*> (marker.get());
    }

    te::TimePosition getNextMarkerTime (te::Edit& edit, te::TimePosition from, bool forwards)
    {
        auto& mm = edit.getMarkerManager();
        auto eps = te::TimeDuration::fromSeconds (0.001);

        if (forwards)
        {
            if (auto m = mm.getNextMarker (from + eps))
                return m->getPosition().getStart();

            return te::TimePosition() + edit.getLength();
        }

        if (auto m = mm.getPrevMarker (from - eps))
            return m->getPosition().getStart();

        return te::TimePosition();
    }
}
