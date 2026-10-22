#pragma once

#include "ArrangeHost.h"
#include "ClipComponent.h"
#include "../Common/FlaggedAsyncUpdater.h"

namespace cdaw
{
    class RecordingClipComponent;
    class TakeLanes;

    /** The clip area for one track. */
    class TrackLane : public juce::Component,
                      private juce::ValueTree::Listener,
                      private juce::ChangeListener,
                      private FlaggedAsyncUpdater
    {
    public:
        TrackLane (ArrangeHost&, te::Track::Ptr);
        ~TrackLane() override;

        te::Track& getTrack()                       { return *track; }
        void paint (juce::Graphics&) override;
        void resized() override;
        void updateClipPositions();

        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

        ClipComponent* findClipComponent (const te::Clip&) const;

    private:
        void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
        void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
        void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
        void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override;
        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void handleAsyncUpdate() override;

        void buildClips();
        void updateRecordingClip();

        ArrangeHost& host;
        EditViewState& viewState;
        te::Track::Ptr track;
        juce::OwnedArray<ClipComponent> clips;
        std::unique_ptr<RecordingClipComponent> recordingClip;
        // Polls while recording until the input has really started (count-in, device start-up).
        juce::TimedCallback recordingPoll { [this] { updateRecordingClip(); } };
        std::unique_ptr<TakeLanes> takeLanes;
        int lastNumTakes = 0;
        bool lastSelected = false;
        juce::Range<int> lastRangeX;        // x span of the range selection as last painted
        void updateTakeLanes();
        void repaintRangeChange();
        bool needsRebuild = false, needsReposition = false, needsRecordingUpdate = false;
    };
}
