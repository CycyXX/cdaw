#pragma once

#include "ArrangeHost.h"

namespace cdaw
{
    /** Red "being recorded" clip with a live waveform, shown while the transport records. */
    class RecordingClipComponent : public juce::Component,
                                   private juce::Timer
    {
    public:
        RecordingClipComponent (te::Track::Ptr, EditViewState&);
        void paint (juce::Graphics&) override;

    private:
        void timerCallback() override;
        void updatePosition();
        void fetchThumbnail();

        te::Track::Ptr track;
        EditViewState& viewState;
        te::RecordingThumbnailManager::Thumbnail::Ptr thumbnail;
        te::TimePosition punchInTime = te::TimePosition::fromSeconds (-1.0);
        te::TimeRange shownRange;
    };
}
