#pragma once

#include "ArrangeHost.h"

namespace cdaw
{
    /** Take rows shown under a track's clips. Swipe across a row to comp that take in. */
    class TakeLanes : public juce::Component
    {
    public:
        TakeLanes (ArrangeHost&, te::Track::Ptr);
        ~TakeLanes() override;

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;

        static constexpr int rowHeight = 28;

    private:
        te::SmartThumbnail& getThumbnail (const juce::File&);
        int rowAt (int y) const              { return y / rowHeight; }

        ArrangeHost& host;
        EditViewState& viewState;
        te::Track::Ptr track;
        std::map<juce::String, std::unique_ptr<te::SmartThumbnail>> thumbnails;

        int swipeRow = -1, hoverRow = -1;
        te::TimePosition swipeStart, swipeEnd;
    };
}
