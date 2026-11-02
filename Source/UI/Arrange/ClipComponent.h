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
        void updateWaveformPeaks (te::AudioClipBase&, int fromColumn, int toColumn, bool allChannelsTogether);
        bool showingPeaksWhileReloading (te::AudioClipBase&, juce::Rectangle<int> columns, juce::Rectangle<int> area);
        void drawWaveform (juce::Graphics&, juce::Rectangle<int> lane, juce::Rectangle<int> columns, int peaksIndex, float gain);
        void drawMidi (juce::Graphics&, te::MidiClip&, juce::Rectangle<int>);
        void drawFades (juce::Graphics&, te::AudioClipBase&, juce::Rectangle<int>);
        void drawGainHandle (juce::Graphics&, te::AudioClipBase&);
        void drawLoopMarks (juce::Graphics&, te::MidiClip&, juce::Rectangle<int>);
        void drawDragInfo (juce::Graphics&);
        juce::Rectangle<int> getGainHandleBounds() const;
        bool isOverFade (juce::Point<int>, bool fadeIn) const;
        void showFadeMenu (bool fadeIn);

        void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
        void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override        { takesLabelIsCurrent = false; repaint(); }
        void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { takesLabelIsCurrent = false; repaint(); }
        void changeListenerCallback (juce::ChangeBroadcaster*) override;   // selection: repaint only when ours flips

        ArrangeHost& host;
        EditViewState& viewState;
        te::Clip::Ptr clip;
        std::unique_ptr<te::SmartThumbnail> thumbnail;
        juce::File thumbnailFile;

        /** The lowest and highest value under each pixel column of the waveform, counted from the
            clip's start, so scrolling only has to look up the columns it brings into view. */
        struct WaveformPeaks
        {
            struct Key
            {
                double pixelsPerSecond = 0.0, offset = 0.0, speed = 0.0, sourceLength = 0.0, fileLength = 0.0;
                bool reversed = false, allChannelsTogether = false;
                juce::File file;

                bool operator== (const Key&) const = default;
            };

            Key key;
            int first = 0;      // the column the vectors start at
            bool needsRefresh = false;
            std::vector<float> lows[2], highs[2];

            int end() const     { return first + (int) lows[0].size(); }
            void clear()        { for (auto* v : { &lows[0], &lows[1], &highs[0], &highs[1] }) v->clear(); }
        };

        WaveformPeaks waveformPeaks;
        WaveformPeaks::Key makeWaveformKey (te::AudioClipBase&, bool allChannelsTogether) const;

        // "[take 2/5]": reading the takes is too slow to do on every paint.
        juce::String takesLabel;
        bool takesLabelIsCurrent = false;

        Zone dragZone = Zone::body, hoverZone = Zone::body;
        bool lastSelected = false;
        void repaintHandles();      // the strip at the top holding the fade and gain handles
        bool dragging = false;
        te::TimeDuration dragStartOffset;

        /** This clip and those group editing links to it, as they were when the drag started. */
        struct LinkedClip
        {
            te::Clip::Ptr clip;
            float startGainDb = 0.0f;
            te::TimeDuration startOffset;
        };

        std::vector<LinkedClip> linkedClips;
        juce::String dragInfo;          // read-out shown while dragging (fade length, gain, offset ...)
        std::unique_ptr<te::TransportControl::ReallocationInhibitor> inhibitor;
    };
}
