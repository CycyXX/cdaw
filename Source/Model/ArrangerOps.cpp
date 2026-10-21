#include "ArrangerOps.h"

namespace cdaw::ArrangerOps
{
    te::ArrangerTrack* getTrack (te::Edit& edit)
    {
        return edit.getArrangerTrack();
    }

    juce::Array<te::ArrangerClip*> getSections (te::Edit& edit)
    {
        juce::Array<te::ArrangerClip*> sections;

        if (auto track = getTrack (edit))
            for (auto c : track->getClips())
                if (auto ac = dynamic_cast<te::ArrangerClip*> (c))
                    sections.add (ac);

        std::sort (sections.begin(), sections.end(), [] (te::ArrangerClip* a, te::ArrangerClip* b)
                   { return a->getPosition().getStart() < b->getPosition().getStart(); });
        return sections;
    }

    te::ArrangerClip* findSectionAt (te::Edit& edit, te::TimePosition t)
    {
        for (auto s : getSections (edit))
            if (s->getEditTimeRange().containsInclusive (t) && t < s->getPosition().getEnd())
                return s;

        return nullptr;
    }

    const juce::StringArray& getPresetNames()
    {
        static const juce::StringArray names { "Intro", "Verse", "Pre-Chorus", "Chorus", "Post-Chorus",
                                               "Bridge", "Breakdown", "Solo", "Interlude", "Outro" };
        return names;
    }

    juce::Colour getColourForName (const juce::String& name)
    {
        // Strip a trailing number so "Verse 2" matches "Verse".
        auto base = name.trim().trimCharactersAtEnd ("0123456789 ").toLowerCase();

        if (base == "intro")        return juce::Colour (0xff4fd6c8);
        if (base == "verse")        return juce::Colour (0xff4fa3ff);
        if (base == "pre-chorus")   return juce::Colour (0xff9c8cff);
        if (base == "chorus")       return juce::Colour (0xffff8a4f);
        if (base == "post-chorus")  return juce::Colour (0xffff6fae);
        if (base == "bridge")       return juce::Colour (0xff8bd34f);
        if (base == "breakdown")    return juce::Colour (0xfff2b134);
        if (base == "solo")         return juce::Colour (0xffd77cff);
        if (base == "interlude")    return juce::Colour (0xff6fb7c9);
        if (base == "outro")        return juce::Colour (0xff8a9bb0);

        static const juce::uint32 others[] = { 0xffe5383b, 0xff3ccf6e, 0xffc9a227, 0xff5fa8d3, 0xffb07cc6 };
        return juce::Colour (others[(size_t) (base.hashCode() & 0x7fffffff) % juce::numElementsInArray (others)]);
    }

    static void trimSectionsUnder (te::Edit& edit, te::TimeRange range, te::ArrangerClip* except)
    {
        for (auto s : getSections (edit))
            if (s != except && s->getEditTimeRange().overlaps (range))
                te::deleteRegionOfClip (*s, range);
    }

    te::ArrangerClip* addSection (te::Edit& edit, te::TimeRange range, const juce::String& name)
    {
        auto track = getTrack (edit);

        if (track == nullptr || range.getLength() <= te::TimeDuration::fromSeconds (0.01))
            return nullptr;

        trimSectionsUnder (edit, range, nullptr);

        auto clip = dynamic_cast<te::ArrangerClip*> (track->insertNewClip (te::TrackItem::Type::arranger, name, range, nullptr));

        if (clip != nullptr)
            clip->setColour (getColourForName (name));

        return clip;
    }

    void renameSection (te::ArrangerClip& section, const juce::String& name)
    {
        section.setName (name);
        section.setColour (getColourForName (name));
    }

    void resizeSection (te::ArrangerClip& section, te::TimeRange newRange)
    {
        auto& edit = section.edit;
        auto lower = te::TimePosition();
        auto upper = te::TimePosition::fromSeconds (1.0e7);

        for (auto s : getSections (edit))
        {
            if (s == &section)
                continue;

            if (s->getPosition().getEnd() <= section.getPosition().getStart())
                lower = juce::jmax (lower, s->getPosition().getEnd());
            else if (s->getPosition().getStart() >= section.getPosition().getEnd())
                upper = juce::jmin (upper, s->getPosition().getStart());
        }

        auto start = juce::jlimit (lower, upper, newRange.getStart());
        auto end = juce::jlimit (lower, upper, newRange.getEnd());

        if (end - start < te::TimeDuration::fromSeconds (0.05))
            return;

        section.setPosition ({ { start, end }, {} });
    }

    //==============================================================================
    namespace
    {
        /** Automation in the copied range, captured before the gap is opened. */
        struct CurveCopy
        {
            te::AutomatableParameter::Ptr param;
            float startValue = 0, endValue = 0, valueAtDestination = 0;
            juce::Array<std::pair<te::TimeDuration, std::pair<float, float>>> points;   // offset, (value, curve)
        };

        std::vector<CurveCopy> captureAutomation (te::Edit& edit, te::TimeRange source, te::TimePosition destination)
        {
            std::vector<CurveCopy> copies;

            for (auto item : te::getAllAutomatableEditItems (edit))
            {
                for (auto param : item->getAutomatableParameters())
                {
                    auto& curve = param->getCurve();

                    if (curve.getNumPoints() == 0)
                        continue;

                    CurveCopy cc;
                    cc.param = param;
                    const float fallback = param->getCurrentBaseValue();
                    cc.startValue = curve.getValueAt (source.getStart(), fallback);
                    cc.endValue = curve.getValueAt (source.getEnd(), fallback);
                    cc.valueAtDestination = curve.getValueAt (destination, fallback);

                    for (auto& p : curve.getPointsInRegion (source))
                    {
                        auto offset = te::toTime (p.time, edit.tempoSequence) - source.getStart();

                        // The end value is written separately; a point right on the end would collide with it.
                        if (offset < source.getLength() - te::TimeDuration::fromSeconds (0.002))
                            cc.points.add ({ offset, { p.value, p.curve } });
                    }

                    copies.push_back (std::move (cc));
                }
            }

            return copies;
        }

        void pasteAutomation (te::Edit& edit, const std::vector<CurveCopy>& copies, te::TimePosition at, te::TimeDuration length)
        {
            auto* um = &edit.getUndoManager();
            const auto tiny = te::TimeDuration::fromSeconds (0.001);

            for (auto& cc : copies)
            {
                auto& curve = cc.param->getCurve();

                // The gap is empty, but stop short of its end: the point that was at `at`
                // before the gap opened now sits exactly there.
                curve.removePointsInRegion ({ at, at + length - tiny * 0.5 }, um);

                // Step into the copy and back out again, so neither side ramps into the other.
                if (at.inSeconds() > tiny.inSeconds())
                    curve.addPoint (at - tiny, cc.valueAtDestination, 0.0f, um);

                curve.addPoint (at, cc.startValue, 0.0f, um);

                for (auto& [offset, vc] : cc.points)
                    curve.addPoint (at + offset, vc.first, vc.second, um);

                curve.addPoint (at + length - tiny, cc.endValue, 0.0f, um);
                curve.addPoint (at + length, cc.valueAtDestination, 0.0f, um);
            }
        }
    }

    void copyRange (te::Edit& edit, te::TimeRange source, te::TimePosition destination)
    {
        const auto length = source.getLength();

        if (length <= te::TimeDuration::fromSeconds (0.001))
            return;

        // Copying into the middle of the source itself would split it; treat that as "after".
        if (destination > source.getStart() && destination < source.getEnd())
            destination = source.getEnd();

        auto automation = captureAutomation (edit, source, destination);

        te::insertSpaceIntoEdit (edit, { destination, length });

        // Where the source is now (it moved right if the gap opened before it).
        auto from = destination <= source.getStart() ? source + length : source;
        auto shift = destination - from.getStart();
        auto end = destination + length;

        for (auto ct : te::getTracksOfType<te::ClipTrack> (edit, true))
        {
            juce::ReferenceCountedArray<te::Clip> sources;

            for (auto c : ct->getClips())
                if (c->getEditTimeRange().overlaps (from))
                    sources.add (c);

            for (auto c : sources)
            {
                if (auto copy = te::duplicateClip (*c))
                {
                    auto pos = copy->getPosition();
                    pos.time = pos.time + shift;
                    copy->setPosition (pos);

                    // Keep only the part inside the copied range.
                    if (copy->getPosition().getStart() < destination)
                        copy->setStart (destination, true, false);

                    if (copy->getPosition().getEnd() > end)
                        copy->setEnd (end, true);
                }
            }
        }

        pasteAutomation (edit, automation, destination, length);
    }

    void duplicateSection (te::Edit& edit, te::ArrangerClip& section)
    {
        auto range = section.getEditTimeRange();
        copyRange (edit, range, range.getEnd());
    }

    void moveSection (te::Edit& edit, te::ArrangerClip& section, te::TimePosition destination)
    {
        auto range = section.getEditTimeRange();

        if (destination >= range.getStart() && destination <= range.getEnd())
            return;

        copyRange (edit, range, destination);

        // The original moved right by the section length if the copy went in before it.
        auto original = destination < range.getStart() ? range + range.getLength() : range;
        te::deleteRegionOfTracks (edit, original, false, te::CloseGap::yes, nullptr);
    }

    void deleteSection (te::Edit& edit, te::ArrangerClip& section, te::SelectionManager* sm)
    {
        auto range = section.getEditTimeRange();
        te::deleteRegionOfTracks (edit, range, false, te::CloseGap::yes, sm);
    }
}
