#pragma once

#include "../../PCH.h"
#include "../../Model/EditViewState.h"
#include "../Common/FlaggedAsyncUpdater.h"
#include "../Common/PanKnob.h"

namespace cdaw
{
    class LevelMeter;
    class InsertSlots;
    class SendSlots;
    class SpectrumView;

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

        /** Right-click on a strip: shows the full track menu (set by the owner). */
        std::function<void (te::Track&)> onTrackMenu;

        /** Called when inserts or sends are added or removed, so the mixer can re-balance. */
        std::function<void()> onLayoutChanged;

        /** Slot rows this strip would like (its slots plus the "+" button); 0 if it has none. */
        int getWantedInsertRows() const;
        int getWantedSendRows() const;

        /** Rows reserved for inserts and sends. The mixer gives every strip the same values so
            the faders line up; a list longer than its rows scrolls. */
        void setSectionRows (int insertRows, int sendRows);

        /** Height of everything except the fader, for the given section rows. */
        static int getFixedHeight (int insertRows, int sendRows);

        static constexpr int minFaderHeight = 140;

        /** Master strip only: the button that shows the spectrum analyser. */
        std::function<void()> onToggleSpectrum;
        void setSpectrumShown (bool shown)      { spectrumButton.setToggleState (shown, juce::dontSendNotification); }

    private:
        void init();
        void drawFaderScale (juce::Graphics&);
        void timerCallback() override           { if (isShowing()) refreshState(); }   // nothing to do while hidden
        void visibilityChanged() override       { if (isShowing()) refreshState(); }   // catch up when shown again
        void refreshState();
        void changeListenerCallback (juce::ChangeBroadcaster*) override
        {
            // Selection: repaint only when this strip's selected state flips.
            auto selected = track != nullptr && viewState.selectionManager.isSelected (track.get());
            if (selected != lastSelected) { lastSelected = selected; repaint(); }
        }
        bool lastSelected = false;
        te::VolumeAndPanPlugin* getVolumePlugin() const;

        EditViewState& viewState;
        te::Track::Ptr track;     // null = master
        std::unique_ptr<InsertSlots> inserts;
        std::unique_ptr<SendSlots> sends;
        juce::Viewport insertsView, sendsView;
        int insertRows = 2, sendRows = 2;
        std::unique_ptr<LevelMeter> meter;
        juce::Slider fader;
        PanKnob pan;
        juce::Label dbLabel, nameLabel;
        juce::TextButton muteButton { "M" }, soloButton { "S" }, armButton { "R" }, monitorButton { "I" }, outputButton, inputButton;
        juce::TextButton spectrumButton { "Spectrum" };
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

        std::function<void (te::Track&)> onTrackMenu;

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        void rebuild();
        void layoutStrips();
        void handleAsyncUpdate() override               { if (compareAndReset (needsRebuild)) rebuild(); }
        void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree& c) override         { if (te::TrackList::isTrack (c)) markAndUpdate (needsRebuild); }
        void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree& c, int) override  { if (te::TrackList::isTrack (c)) markAndUpdate (needsRebuild); }
        void valueTreeChildOrderChanged (juce::ValueTree& p, int a, int b) override
        {
            // Only a track moving changes the strip order (not e.g. plugins being reordered).
            if (te::TrackList::isTrack (p.getChild (a)) || te::TrackList::isTrack (p.getChild (b)))
                markAndUpdate (needsRebuild);
        }
        void valueTreePropertyChanged (juce::ValueTree& v, const juce::Identifier& id) override
        {
            if (te::TrackList::isTrack (v) && (id == ViewIDs::cdawCollapsed))
                markAndUpdate (needsRebuild);
            else if (v == viewState.state && id == ViewIDs::showSpectrum)
                syncSpectrum();
        }

        EditViewState& viewState;
        juce::Viewport viewport;
        juce::Component stripHolder;
        juce::OwnedArray<ChannelStrip> strips;
        std::unique_ptr<ChannelStrip> master;
        std::unique_ptr<SpectrumView> spectrum;
        bool needsRebuild = false;

        bool isSpectrumShown() const;
        void syncSpectrum();
    };
}
