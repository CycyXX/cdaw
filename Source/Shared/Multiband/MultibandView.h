#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "MultibandParameters.h"
#include "../UI/ParamKnob.h"

namespace cdaw
{
    /** What the C-Multicomp view needs from whoever hosts it: CDAW's built-in plugin or the VST3. */
    struct MultibandModel
    {
        virtual ~MultibandModel() = default;

        /** A parameter by its index in MultibandParameters. */
        virtual std::shared_ptr<KnobParameter> parameter (int index) = 0;
        virtual MultibandEngine& getEngine() = 0;

        /** Groups the following changes into one undo step (where the host has undo). */
        virtual void beginUndoTransaction (const juce::String&) {}
    };

    /** C-Multicomp's editor, shared by both builds:
        - a spectrum graph (after processing, optionally before) split into the four band regions,
          with draggable crossovers and each band's gain reduction hanging from the top
        - a panel per band: on / solo, a level meter with the threshold as a draggable handle,
          gain-reduction meter, the band's transfer curve with a live dot, and its knobs
        - output meters and gain. */
    class MultibandView : public juce::Component,
                          private juce::Timer
    {
    public:
        explicit MultibandView (MultibandModel&);
        ~MultibandView() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        void visibilityChanged() override;

        static juce::Colour getBandColour (int band);

        /** One frame of meter values (the timer reads them from the engine; tests feed their own). */
        struct Levels { float bandIn[4], bandOut[4], reduction[4], in[2], out[2]; };
        void tick (const Levels&);

        /** Reads new audio into the spectrum (the timer does this while showing). */
        void updateSpectrum();

        static constexpr int defaultWidth = 1080, defaultHeight = 660, minimumWidth = 900, minimumHeight = 560;

        class Graph;
        class BandPanel;

    private:
        void timerCallback() override;
        void selectBand (int);
        void drawOutputMeter (juce::Graphics&, juce::Rectangle<float>);

        // The parameter values, for the panels and the graph.
        float value (int index) const;
        void toggle (int index);
        MultibandEngine::Settings getSettings() const;
        std::array<float, MultibandEngine::numBands - 1> crossovers() const;
        juce::Range<float> bandRange (int band) const;
        bool isOn (int band) const;
        bool isSoloed (int band) const;
        bool anySoloed() const;

        MultibandModel& model;
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
