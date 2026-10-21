#pragma once

#include "ArrangeHost.h"
#include "../Common/LevelMeter.h"
#include "../Common/PanKnob.h"

namespace cdaw
{
    /** Small text toggle used for M / S / R / monitor. */
    class HeaderToggle : public juce::TextButton
    {
    public:
        HeaderToggle (const juce::String& text, juce::Colour onColour, const juce::String& tip);
    };

    /** Left-hand header for one track. */
    class TrackHeader : public juce::Component,
                        private juce::Timer,
                        private juce::ChangeListener
    {
    public:
        TrackHeader (ArrangeHost&, te::Track::Ptr);
        ~TrackHeader() override;

        te::Track& getTrack()                       { return *track; }
        void startRename()                          { nameLabel.showEditor(); }

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;

        static te::VolumeAndPanPlugin* getVolumePlugin (te::Track&);
        static te::LevelMeterPlugin* getMeterPlugin (te::Track&);

    private:
        void timerCallback() override;
        void changeListenerCallback (juce::ChangeBroadcaster*) override
        {
            // Selection: repaint only when this track's selected state flips.
            auto selected = viewState.selectionManager.isSelected (track.get());
            if (selected != lastSelected) { lastSelected = selected; repaint(); }
        }
        bool lastSelected = false;
        void refreshButtons();
        void showInputMenu();
        bool isOnResizeEdge (int y) const           { return y >= getHeight() - 5; }

        ArrangeHost& host;
        EditViewState& viewState;
        te::Track::Ptr track;

        juce::Label nameLabel;
        HeaderToggle muteButton { "M", juce::Colour (0xfff2b134), "Mute" },
                     soloButton { "S", juce::Colour (0xff3ccf6e), "Solo" },
                     armButton { "R", juce::Colour (0xffe5383b), "Record enable" },
                     monitorButton { "I", juce::Colour (0xff4fa3ff), "Input monitoring: Auto (follows record enable) / On / Off" };
        juce::TextButton inputButton, collapseButton;
        PanKnob panSlider;
        LevelMeter meter;

        bool resizing = false;
        int resizeStartHeight = 0;
        bool updatingSliders = false;
    };
}
