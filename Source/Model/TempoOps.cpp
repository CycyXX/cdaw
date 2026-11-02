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

    juce::String formatBarsBeats (te::Edit& edit, te::TimePosition t)
    {
        const auto bb = edit.tempoSequence.toBarsAndBeats (t);
        int beat = bb.getWholeBeats();
        int ticks = (int) std::round (bb.getFractionalBeats().inBeats() * 960.0);
        if (ticks >= 960)
        {
            // Rounded up to the next beat: name that beat instead (it starts the next bar when it's past the last).
            return formatBarsBeats (edit, edit.tempoSequence.toTime (edit.tempoSequence.toBeats (t) + te::BeatDuration::fromBeats (0.5 / 960.0)));
        }
        return juce::String (bb.bars + 1) + "." + juce::String (beat + 1) + "." + juce::String (ticks).paddedLeft ('0', 3);
    }

    std::optional<te::TimePosition> parsePosition (te::Edit& edit, const juce::String& text)
    {
        const auto s = text.trim();
        if (s.isEmpty())
            return std::nullopt;

        // A time: m:ss.x or seconds with an "s".
        if (s.containsChar (':') || s.endsWithIgnoreCase ("s"))
        {
            double seconds = 0.0;
            if (s.containsChar (':'))
            {
                const auto minutes = s.upToFirstOccurrenceOf (":", false, false).trim();
                const auto rest = s.fromFirstOccurrenceOf (":", false, false).trim();
                if (! minutes.containsOnly ("0123456789") || ! rest.containsOnly ("0123456789.") || rest.isEmpty())
                    return std::nullopt;
                seconds = minutes.getIntValue() * 60.0 + rest.getDoubleValue();
            }
            else
            {
                const auto number = s.dropLastCharacters (1).trim();
                if (number.isEmpty() || ! number.containsOnly ("0123456789."))
                    return std::nullopt;
                seconds = number.getDoubleValue();
            }
            return te::TimePosition::fromSeconds (juce::jmax (0.0, seconds));
        }

        // Bars, beats and ticks, from 1.
        juce::StringArray parts;
        parts.addTokens (s, ".", {});
        if (parts.isEmpty() || parts.size() > 3)
            return std::nullopt;
        for (auto& p : parts)
            if (p.isEmpty() || ! p.containsOnly ("0123456789"))
                return std::nullopt;

        const int bar = parts[0].getIntValue();
        const int beat = parts.size() > 1 ? parts[1].getIntValue() : 1;
        const int ticks = parts.size() > 2 ? parts[2].getIntValue() : 0;
        if (bar < 1 || beat < 1 || ticks >= 960)
            return std::nullopt;

        const auto& ts = edit.tempoSequence;
        const auto barStart = ts.toTime (te::tempo::BarsAndBeats { bar - 1, {} });
        const auto beats = ts.toBeats (barStart) + te::BeatDuration::fromBeats ((beat - 1) + ticks / 960.0);
        return ts.toTime (beats);
    }

    juce::String formatTime (te::TimePosition t)
    {
        const double seconds = juce::jmax (0.0, t.inSeconds());
        const int minutes = (int) (seconds / 60.0);
        const double rest = seconds - minutes * 60.0;
        return juce::String (minutes) + ":" + juce::String (rest, 3).paddedLeft ('0', 6);
    }
}
