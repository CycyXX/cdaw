#pragma once

#include "ArrangeHost.h"
#include "TimeRuler.h"
#include "TrackHeader.h"
#include "TrackLane.h"
#include "AutomationLane.h"
#include "../Common/FlaggedAsyncUpdater.h"

namespace cdaw
{
    class ProjectSession;

    /** The main arrangement: ruler, track headers, clip lanes, playhead. */
    class ArrangeView : public juce::Component,
                        public ArrangeHost,
                        public juce::FileDragAndDropTarget,
                        private juce::ValueTree::Listener,
                        private juce::ChangeListener,
                        private juce::ScrollBar::Listener,
                        private FlaggedAsyncUpdater
    {
    public:
        ArrangeView (ProjectSession&, EditViewState&);
        ~ArrangeView() override;

        // Component
        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
        void mouseDown (const juce::MouseEvent&) override;

        // View commands
        void zoomBy (double factor);
        void zoomToFit();
        void zoomTrackHeights (double factor);
        void scrollToTime (te::TimePosition);
        te::Track* getFirstSelectedTrack() const;

        /** Called to open the editor for a clip (double-click). */
        std::function<void (te::Clip&)> onOpenClipEditor;
        /** Called to add a track of a kind from the track context menu (command id). */
        std::function<void (int commandID)> onTrackCommand;

        // FileDragAndDropTarget
        bool isInterestedInFileDrag (const juce::StringArray&) override;
        void fileDragMove (const juce::StringArray&, int x, int y) override;
        void fileDragExit (const juce::StringArray&) override;
        void filesDropped (const juce::StringArray&, int x, int y) override;

        // ArrangeHost
        EditViewState& getViewState() override              { return viewState; }
        juce::Component& asComponent() override              { return *this; }
        void beginClipDrag (te::Clip&, const juce::MouseEvent&) override;
        void continueClipDrag (const juce::MouseEvent&) override;
        void endClipDrag (const juce::MouseEvent&) override;
        void laneMouseDown (te::Track&, const juce::MouseEvent&) override;
        void laneMouseDrag (te::Track&, const juce::MouseEvent&) override;
        void laneMouseUp (te::Track&, const juce::MouseEvent&) override;
        void showClipMenu (te::Clip&) override;
        void openClipEditor (te::Clip&) override;
        void showTrackMenu (te::Track&) override;
        void toggleAutomationLanes (const juce::Array<te::Track*>&);
        void trackHeightChanged() override                   { markAndUpdate (needsLayout); }

        static constexpr int headerWidth = 240;
        static constexpr int rulerHeight = 58;
        static constexpr int scrollBarSize = 12;

    private:
        struct Overlay;
        struct EmptyArea;

        ProjectSession& session;
        te::Edit& edit;
        EditViewState& viewState;

        TimeRuler ruler;
        juce::Component headersArea, lanesArea;
        std::unique_ptr<EmptyArea> emptyArea;
        std::unique_ptr<Overlay> overlay;
        juce::ScrollBar vScroll { true }, hScroll { false };
        juce::OwnedArray<TrackHeader> headers;
        juce::OwnedArray<TrackLane> lanes;
        juce::OwnedArray<AutomationLaneHeader> autoHeaders;
        juce::OwnedArray<AutomationLane> autoLanes;
        juce::Array<int> autoLaneTrackIndex;     // which track row each automation lane belongs to
        std::unique_ptr<juce::VBlankAttachment> vblank;

        // Clip drag
        juce::Array<te::Clip*> dragClips;
        juce::Array<te::TimeRange> dragOriginalRanges;
        juce::Array<int> dragOriginalTrackIndex;
        te::TimePosition dragAnchorStart;
        juce::Point<int> dragStartPos;
        int dragStartTrackIndex = 0;
        te::TimeDuration dragDelta;
        int dragTrackDelta = 0;
        bool dragCopy = false;

        // Lane drags
        enum class LaneDrag { none, rubberBand, range, drawMidi };
        LaneDrag laneDrag = LaneDrag::none;
        te::TimePosition laneDragStartTime;
        juce::Point<int> laneDragStart;
        te::Track::Ptr laneDragTrack;

        int fileDragX = -1, fileDragY = -1;
        double lastPlayheadX = -1.0;
        bool needsRebuild = false, needsLayout = false;

        void buildTracks();
        void layoutTracks();
        void updateScrollBars();
        void onVBlank();
        int getTrackIndexAt (int y) const;           // y relative to this; -1 if none
        te::Track* getTrackAt (int y) const;
        juce::Rectangle<int> getLanesBounds() const  { return lanesArea.getBounds(); }
        int getTotalTracksHeight() const;
        double getContentLengthSeconds() const;

        void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
        void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
        void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
        void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override;
        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;
        void handleAsyncUpdate() override;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ArrangeView)
    };
}
