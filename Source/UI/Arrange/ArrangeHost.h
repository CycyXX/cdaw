#pragma once

#include "../../PCH.h"
#include "../../Model/EditViewState.h"

namespace cdaw
{
    /** What clip and lane components need from the arrangement view that owns them.
        Mouse events passed here are already relative to the host. */
    struct ArrangeHost
    {
        virtual ~ArrangeHost() = default;

        virtual EditViewState& getViewState() = 0;
        virtual juce::Component& asComponent() = 0;

        // Clip moving (ghost drag, applied on mouse-up)
        virtual void beginClipDrag (te::Clip& grabbed, const juce::MouseEvent&) = 0;
        virtual void continueClipDrag (const juce::MouseEvent&) = 0;
        virtual void endClipDrag (const juce::MouseEvent&) = 0;

        // Range selection / lane clicks
        virtual void laneMouseDown (te::Track&, const juce::MouseEvent&) = 0;
        virtual void laneMouseDrag (te::Track&, const juce::MouseEvent&) = 0;
        virtual void laneMouseUp (te::Track&, const juce::MouseEvent&) = 0;

        virtual void showClipMenu (te::Clip&) = 0;
        virtual void openClipEditor (te::Clip&) = 0;
        virtual void showTrackMenu (te::Track&) = 0;
        virtual void trackHeightChanged() = 0;
    };
}
