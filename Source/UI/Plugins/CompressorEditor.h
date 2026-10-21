#pragma once

#include "../../PCH.h"
#include "../../Shared/UI/ParamKnob.h"

namespace cdaw
{
    class CompressorPlugin;

    /** Editor for CDAW's compressor:
        - input and output meters (peak, with hold), the threshold as a handle on the input meter
        - the transfer curve, with handles for threshold and ratio and a dot showing where the
          signal is right now
        - a scrolling history of input level, output level and gain reduction
        - a gain-reduction meter with the deepest recent value
        - knobs for every parameter and the sidechain source. */
    class CompressorEditor : public te::Plugin::EditorComponent,
                             private juce::Timer
    {
    public:
        explicit CompressorEditor (CompressorPlugin&);
        ~CompressorEditor() override;

        bool allowWindowResizing() override                                 { return true; }
        juce::ComponentBoundsConstrainer* getBoundsConstrainer() override   { return &constrainer; }

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;

        /** Feeds one frame of meter values (used by the timer, and by tests / snapshots). */
        void tick (float inL, float inR, float outL, float outR, float detector, float reductionDb);

    private:
        struct Frame { float inDb, outDb, grDb; };
        enum class Handle { none, threshold, ratio, meterThreshold };

        void timerCallback() override;
        void updateSidechainButton();

        void drawMeter (juce::Graphics&, juce::Rectangle<float>, const char* title, float l, float r, float holdL, float holdR, bool withThreshold);
        void drawReductionMeter (juce::Graphics&, juce::Rectangle<float>);
        void drawTransfer (juce::Graphics&, juce::Rectangle<float>);
        void drawHistory (juce::Graphics&, juce::Rectangle<float>);

        juce::Point<float> getThresholdHandle() const;
        juce::Point<float> getRatioHandle() const;
        Handle hitTest (juce::Point<float>) const;

        float transferX (float db) const;
        float transferY (float db) const;
        float meterY (juce::Rectangle<float>, float db) const;

        CompressorPlugin& plugin;
        te::Plugin::Ptr pluginRef;
        juce::ComponentBoundsConstrainer constrainer;

        juce::OwnedArray<ParamKnob> knobs;
        ParamKnob* sidechainKnob = nullptr;
        juce::TextButton sidechainButton;

        juce::Rectangle<float> inMeterArea, outMeterArea, grMeterArea, transferArea, historyArea;

        // Display state (dB), with falling peaks and holds.
        float inLevel[2] { -100.0f, -100.0f }, outLevel[2] { -100.0f, -100.0f };
        float inHold[2] { -100.0f, -100.0f }, outHold[2] { -100.0f, -100.0f };
        float grLevel = 0.0f, grHold = 0.0f, detectorDb = -100.0f;
        double holdTime[3] {};
        std::vector<Frame> history;
        int historyWrite = 0, emptyTicks = 0;

        Handle dragging = Handle::none, hover = Handle::none;
        static constexpr float meterMinDb = -60.0f, meterMaxDb = 6.0f, graphMinDb = -60.0f, maxReductionDb = 30.0f;
    };
}
