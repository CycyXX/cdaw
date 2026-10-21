#pragma once

#include "../../PCH.h"
#include "../../Shared/UI/ParamKnob.h"

namespace cdaw
{
    class SimpleReverbPlugin;
    struct SpectrumAnalyser;

    /** Editor for the simple Reverb, laid out like C-Verb's: frequency along the bottom, time up
        the side. A shape shows how long each frequency rings (warm low end to cool highs, so
        Tone is visible), glowing with the reverb's live level, over a faint spectrum of the
        reverb. Drag up / down to set the decay. Five knobs below. */
    class SimpleReverbEditor : public te::Plugin::EditorComponent,
                               private juce::Timer
    {
    public:
        explicit SimpleReverbEditor (SimpleReverbPlugin&);
        ~SimpleReverbEditor() override;

        bool allowWindowResizing() override                                 { return true; }
        juce::ComponentBoundsConstrainer* getBoundsConstrainer() override   { return &constrainer; }

        void paint (juce::Graphics&) override;
        void resized() override;
        void visibilityChanged() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

        /** One frame of the reverb's output level (gain); the timer reads it from the plugin. */
        void tick (float wet);

        /** Reads new audio into the background spectrum (the timer does this while showing). */
        void updateSpectrum();

    private:
        void timerCallback() override;
        juce::Rectangle<float> plotArea() const;
        float hzToX (double hz) const;
        double xToHz (float x) const;
        float timeToY (double t) const;

        SimpleReverbPlugin& plugin;
        te::Plugin::Ptr pluginRef;
        juce::ComponentBoundsConstrainer constrainer;
        juce::OwnedArray<ParamKnob> knobs;
        std::unique_ptr<SpectrumAnalyser> spectrum;
        std::vector<double> columnHz;
        juce::Rectangle<float> displayArea;
        float glow = 0.0f;
        double timeRange = 4.0;
        bool dragging = false;
        unsigned frame = 0;
        juce::String lastKey;
    };
}
