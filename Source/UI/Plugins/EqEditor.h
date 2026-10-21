#pragma once

#include "../../PCH.h"
#include "../../Shared/UI/ParamKnob.h"

namespace cdaw
{
    class EqPlugin;
    struct AnalyserFifo;

    /** Editor for CDAW's 8-band EQ, in the spirit of Cubase's Frequency:
        - a large graph with the live spectrum (after the EQ, and optionally before it), the
          overall EQ curve and one numbered node per band
        - drag a node to change frequency and gain (Shift for fine), mouse wheel for Q,
          double-click a node to switch it on or off, double-click empty space to add a band,
          right-click for shape, slope and reset
        - a strip per band below with on/off, shape, frequency, gain and Q, plus output gain. */
    class EqEditor : public te::Plugin::EditorComponent,
                     private juce::Timer
    {
    public:
        explicit EqEditor (EqPlugin&);
        ~EqEditor() override;

        bool allowWindowResizing() override                                 { return true; }
        juce::ComponentBoundsConstrainer* getBoundsConstrainer() override   { return &constrainer; }

        void paint (juce::Graphics&) override;
        void resized() override;
        void visibilityChanged() override;

        static juce::Colour getBandColour (int band);

        /** Reads new audio into the spectrum (the timer does this while showing). */
        void updateSpectrum();

        class Graph;
        class BandStrip;

    private:
        void timerCallback() override;
        void selectBand (int band);

        EqPlugin& plugin;
        te::Plugin::Ptr pluginRef;
        juce::ComponentBoundsConstrainer constrainer;

        std::unique_ptr<Graph> graph;
        juce::OwnedArray<BandStrip> strips;
        std::unique_ptr<ParamKnob> outputKnob;
        juce::TextButton preButton { "Pre" }, postButton { "Post" }, rangeButton;
        int selectedBand = -1;
    };
}
