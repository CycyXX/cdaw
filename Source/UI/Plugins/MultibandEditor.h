#pragma once

#include "../../PCH.h"
#include "../../Shared/UI/ParamKnob.h"

namespace cdaw
{
    class MultibandPlugin;

    /** Editor for CDAW's multiband compressor:
        - a spectrum graph (after processing, optionally before) split into the four band regions,
          with draggable crossovers and each band's gain reduction hanging from the top
        - a panel per band: on / solo, a level meter with the threshold as a draggable handle,
          gain-reduction meter, the band's transfer curve with a live dot, and its knobs
        - output meters and gain. */
    class MultibandEditor : public te::Plugin::EditorComponent,
                            private juce::Timer
    {
    public:
        explicit MultibandEditor (MultibandPlugin&);
        ~MultibandEditor() override;

        bool allowWindowResizing() override                                 { return true; }
        juce::ComponentBoundsConstrainer* getBoundsConstrainer() override   { return &constrainer; }

        void paint (juce::Graphics&) override;
        void resized() override;
        void visibilityChanged() override;

        static juce::Colour getBandColour (int band);

        /** One frame of meter values (the timer reads them from the plugin; tests feed their own). */
        struct Levels { float bandIn[4], bandOut[4], reduction[4], in[2], out[2]; };
        void tick (const Levels&);

        /** Reads new audio into the spectrum (the timer does this while showing). */
        void updateSpectrum();

        class Graph;
        class BandPanel;

    private:
        void timerCallback() override;
        void selectBand (int);
        void drawOutputMeter (juce::Graphics&, juce::Rectangle<float>);

        MultibandPlugin& plugin;
        te::Plugin::Ptr pluginRef;
        juce::ComponentBoundsConstrainer constrainer;

        std::unique_ptr<Graph> graph;
        juce::OwnedArray<BandPanel> panels;
        std::unique_ptr<ParamKnob> outputKnob;
        juce::TextButton preButton { "Pre" }, postButton { "Post" };
        juce::Rectangle<float> outputMeterArea;
        float inLevel = -100.0f, outLevel = -100.0f, outHold = -100.0f;
        double outHoldTime = 0.0;
        int selectedBand = 0, emptyTicks = 0;
        unsigned frame = 0;
    };
}
