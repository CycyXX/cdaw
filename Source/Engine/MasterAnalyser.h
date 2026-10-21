#pragma once

#include "../PCH.h"
#include "../Shared/DSP/AnalyserFifo.h"

namespace cdaw
{
    /** Taps the Edit's master output (after the master inserts and fader) for analysis.
        The tap is added to each playback graph as it's built, on the default output device. */
    class MasterAnalyser : private te::TransportControl::Listener
    {
    public:
        explicit MasterAnalyser (te::Edit&);
        ~MasterAnalyser() override;

        std::shared_ptr<AnalyserFifo> getFifo() const      { return fifo; }

        /** Only collects audio while something is showing it. */
        void setActive (bool shouldBeActive)                { fifo->active = shouldBeActive; }

    private:
        void playbackContextChanged() override              { install(); }
        void install();

        te::Edit& edit;
        std::shared_ptr<AnalyserFifo> fifo = std::make_shared<AnalyserFifo>();
    };
}
