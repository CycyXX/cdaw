#pragma once

#include "ArrangeHost.h"

namespace cdaw
{
    /** One clip in a track lane. Draws the waveform / MIDI notes, fades and takes, and handles:
        - selecting and moving (via the host), Alt-drag copies
        - trimming the edges
        - fade handles at the top corners (right-click a fade for its curve shape)
        - the gain handle at the top centre (drag up / down, double-click resets)
        - slip editing: Ctrl+Alt-drag moves the content inside the clip */
    class ClipComponent : public juce::Component,
                          private juce::ValueTree::Listener,
                          private juce::ChangeListener
    {
    public:
        ClipComponent (ArrangeHost&, te::Clip::Ptr);
        ~ClipComponent() override;

        te::Clip& getClip()                     { return *clip; }
        te::Clip::Ptr getClipPtr()              { return clip; }

        void paint (juce::Graphics&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;

        /** Time range covered by the visible part of this component. */
        te::TimeRange getVisibleTimeRange() const;

        static constexpr int headerHeight = 15;

        void mouseExit (const juce::MouseEvent&) override;
        void mouseEnter (const juce::MouseEvent&) override;

    private:
        enum class Zone { body, trimStart, trimEnd, fadeIn, fadeOut, gain, slip };

        Zone getZone (juce::Point<int>, juce::ModifierKeys) const;
        void updateCursor (Zone);
        void updateThumbnail();
        void drawAudio (juce::Graphics&, te::AudioClipBase&, juce::Rectangle<int>);
        void drawWaveform (juce::Graphics&, juce::Rectangle<int> lane, juce::Rectangle<int> columns,
                           const std::function<double (int)>& xToSourceSeconds, int channel, float gain);
        void drawMidi (juce::Graphics&, te::MidiClip&, juce::Rectangle<int>);
        void drawFades (juce::Graphics&, te::AudioClipBase&, juce::Rectangle<int>);
        void drawGainHandle (juce::Graphics&, te::AudioClipBase&);
        void drawLoopMarks (juce::Graphics&, te::MidiClip&, juce::Rectangle<int>);
        void drawDragInfo (juce::Graphics&);
        juce::Rectangle<int> getGainHandleBounds() const;
        bool isOverFade (juce::Point<int>, bool fadeIn) const;
        void showFadeMenu (bool fadeIn);

        void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
        void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override        { repaint(); }
        void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { repaint(); }
        void changeListenerCallback (juce::ChangeBroadcaster*) override;   // selection: repaint only when ours flips

        ArrangeHost& host;
        EditViewState& viewState;
        te::Clip::Ptr clip;
        std::unique_ptr<te::SmartThumbnail> thumbnail;
        juce::File thumbnailFile;

        Zone dragZone = Zone::body, hoverZone = Zone::body;
        bool lastSelected = false;
        void repaintHandles();      // the strip at the top holding the fade and gain handles
        bool dragging = false;
        float dragStartGainDb = 0.0f;
        te::TimeDuration dragStartOffset;
        juce::String dragInfo;          // read-out shown while dragging (fade length, gain, offset ...)
        std::unique_ptr<te::TransportControl::ReallocationInhibitor> inhibitor;
    };
}
