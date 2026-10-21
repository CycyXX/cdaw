#pragma once

#include "../../PCH.h"
#include "../../Model/EditViewState.h"
#include "../Common/FlaggedAsyncUpdater.h"

namespace cdaw
{
    class LevelMeter;
    class InsertSlots;
    class SendSlots;

    /** One mixer channel: name, routing, inserts, sends, pan, fader, meter, M/S/R. */
    class ChannelStrip : public juce::Component,
                         private juce::Timer,
                         private juce::ChangeListener
    {
    public:
        ChannelStrip (EditViewState&, te::Track::Ptr);              // track strip
        explicit ChannelStrip (EditViewState&);                      // master strip
        ~ChannelStrip() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;

        static constexpr int width = 104;

    private:
        void init();
        void timerCallback() override;
        void changeListenerCallback (juce::ChangeBroadcaster*) override  { repaint(); }
        te::VolumeAndPanPlugin* getVolumePlugin() const;

        EditViewState& viewState;
        te::Track::Ptr track;     // null = master
        std::unique_ptr<InsertSlots> inserts;
        std::unique_ptr<SendSlots> sends;
        std::unique_ptr<LevelMeter> meter;
        juce::Slider fader, pan;
        juce::Label dbLabel, nameLabel;
        juce::TextButton muteButton { "M" }, soloButton { "S" }, armButton { "R" }, monitorButton { "I" }, outputButton, inputButton;
        bool updating = false;
    };

    /** All channel strips side by side, master on the right. */
    class MixerView : public juce::Component,
                      private juce::ValueTree::Listener,
                      private FlaggedAsyncUpdater
    {
    public:
        explicit MixerView (EditViewState&);
        ~MixerView() override;

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        void rebuild();
        void handleAsyncUpdate() override               { if (compareAndReset (needsRebuild)) rebuild(); }
        void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree& c) override         { if (te::TrackList::isTrack (c)) markAndUpdate (needsRebuild); }
        void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree& c, int) override  { if (te::TrackList::isTrack (c)) markAndUpdate (needsRebuild); }
        void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override            { markAndUpdate (needsRebuild); }
        void valueTreePropertyChanged (juce::ValueTree& v, const juce::Identifier& id) override
        {
            if (te::TrackList::isTrack (v) && (id == ViewIDs::cdawCollapsed))
                markAndUpdate (needsRebuild);
        }

        EditViewState& viewState;
        juce::Viewport viewport;
        juce::Component stripHolder;
        juce::OwnedArray<ChannelStrip> strips;
        std::unique_ptr<ChannelStrip> master;
        bool needsRebuild = false;
    };
}
