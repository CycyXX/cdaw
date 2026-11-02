#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "CompressorParameters.h"
#include "../UI/ParamKnob.h"

namespace cdaw
{
    /** What the C-Comp view needs from whoever hosts it: CDAW's built-in plugin or the VST3. */
    struct CompressorModel
    {
        virtual ~CompressorModel() = default;

        virtual std::shared_ptr<KnobParameter> parameter (CompressorParameters::ID) = 0;
        virtual CompressorEngine& getEngine() = 0;

        /** Groups the following changes into one undo step (where the host has undo). */
        virtual void beginUndoTransaction (const juce::String&) {}

        /** Where the sidechain comes from (empty: none). The sidechain button calls
            showSidechainMenu: in CDAW it opens a menu of tracks, in a VST3 (where the host does
            the routing) it switches the sidechain on and off. */
        virtual juce::String getSidechainSource() { return {}; }
        virtual bool canChooseSidechain() const { return false; }
        virtual void showSidechainMenu (juce::Component&) {}
    };

    /** C-Comp's editor, shared by both builds:
        - input and output meters (peak, with hold), the threshold as a handle on the input meter
        - the transfer curve, with handles for threshold and ratio and a dot showing where the
          signal is right now
        - a scrolling history of input level, output level and gain reduction
        - a gain-reduction meter with the deepest recent value
        - knobs for every parameter and the sidechain source. */
    class CompressorView : public juce::Component,
                           private juce::Timer
    {
    public:
        explicit CompressorView (CompressorModel&);
        ~CompressorView() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;

        /** Feeds one frame of meter values (used by the timer, and by tests / snapshots). */
        void tick (float inL, float inR, float outL, float outR, float detector, float reductionDb);

        static constexpr int defaultWidth = 860, defaultHeight = 460, minimumWidth = 640, minimumHeight = 380;

    private:
        struct Frame { float inDb, outDb, grDb; };
        enum class Handle { none, threshold, ratio, meterThreshold };

        void timerCallback() override;
        void updateSidechainButton();

        void drawMeter (juce::Graphics&, juce::Rectangle<float>, const char* title, float l, float r, float holdL, float holdR, bool withThreshold);
        void drawReductionMeter (juce::Graphics&, juce::Rectangle<float>);
        void drawTransfer (juce::Graphics&, juce::Rectangle<float>);
        void drawHistory (juce::Graphics&, juce::Rectangle<float>);

        float value (CompressorParameters::ID) const;
        float thresholdDb() const;
        float kneeDb() const;
        float transfer (float inDb) const;

        juce::Point<float> getThresholdHandle() const;
        juce::Point<float> getRatioHandle() const;
        Handle hitTest (juce::Point<float>) const;

        float transferX (float db) const;
        float transferY (float db) const;
        float meterY (juce::Rectangle<float>, float db) const;

        CompressorModel& model;

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
