#pragma once

#include "ArrangeHost.h"

namespace cdaw
{
    /** One clip in a track lane. Draws the waveform / MIDI notes, fades and takes,
        and handles selecting, moving (via the host), trimming and fade handles. */
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

    private:
        enum class Zone { body, trimStart, trimEnd, fadeIn, fadeOut };

        Zone getZone (juce::Point<int>) const;
        void updateCursor (Zone);
        void updateThumbnail();
        void drawAudio (juce::Graphics&, te::AudioClipBase&, juce::Rectangle<int>);
        void drawMidi (juce::Graphics&, te::MidiClip&, juce::Rectangle<int>);
        void drawFades (juce::Graphics&, te::AudioClipBase&, juce::Rectangle<int>);

        void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
        void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override        { repaint(); }
        void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { repaint(); }
        void changeListenerCallback (juce::ChangeBroadcaster*) override               { repaint(); }

        ArrangeHost& host;
        EditViewState& viewState;
        te::Clip::Ptr clip;
        std::unique_ptr<te::SmartThumbnail> thumbnail;
        juce::File thumbnailFile;

        Zone dragZone = Zone::body;
        bool dragging = false;
        std::unique_ptr<te::TransportControl::ReallocationInhibitor> inhibitor;
    };
}
