#pragma once

#include "ArrangeHost.h"
#include "../../Model/CompOps.h"

namespace cdaw
{
    /** Take rows shown under a track's clips, audio or MIDI. Swipe across a row to comp that take in,
        click it to use that take for the piece under the mouse (on the whole group, with group editing on). */
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
        void drawNotes (juce::Graphics&, const CompOps::TakeInfo&, juce::Rectangle<int> area, te::TimeRange shown);
        int rowAt (int y) const              { return y / rowHeight; }

        ArrangeHost& host;
        EditViewState& viewState;
        te::Track::Ptr track;
        std::map<juce::String, std::unique_ptr<te::SmartThumbnail>> thumbnails;

        int swipeRow = -1, hoverRow = -1;
        te::TimePosition swipeStart, swipeEnd;
    };
}
