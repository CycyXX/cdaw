#pragma once

#include "../../PCH.h"

namespace cdaw
{
    struct AnalyserFifo;

    /** Real-time spectrum of the master output: log frequency axis (20 Hz - 20 kHz),
        smoothed level with a slower peak-hold line, and a read-out under the mouse. */
    class SpectrumView : public juce::Component,
                         private juce::Timer
    {
    public:
        explicit SpectrumView (std::shared_ptr<AnalyserFifo>);
        ~SpectrumView() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void visibilityChanged() override;

        /** Reads new audio and recomputes the curve (the timer does this while the view is showing). */
        void update();

        static constexpr float minDb = -90.0f, maxDb = 6.0f;
        static constexpr float minHz = 20.0f, maxHz = 20000.0f;

    private:
        static constexpr int fftOrder = 13;
        static constexpr int fftSize = 1 << fftOrder;

        void timerCallback() override;
        void computeSpectrum();
        juce::Rectangle<float> getPlotArea() const;
        void drawBackground (juce::Graphics&);
        void drawGrid (juce::Graphics&);
        bool isAtFloor() const;
        juce::Image background;
        juce::Rectangle<int> backgroundSize;
        float backgroundScale = 0.0f;
        float xToHz (float x) const;
        float hzToX (float hz) const;
        float dbToY (float db) const;

        std::shared_ptr<AnalyserFifo> fifo;
        juce::dsp::FFT fft { fftOrder };
        juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann, false };

        std::vector<float> history = std::vector<float> ((size_t) fftSize);   // newest fftSize samples
        std::vector<float> fftData = std::vector<float> ((size_t) fftSize * 2);
        std::vector<float> binDb = std::vector<float> ((size_t) fftSize / 2, minDb);
        std::vector<float> scratch = std::vector<float> ((size_t) fftSize);

        // Per-pixel-column levels
        std::vector<float> level, peak, peakHoldTime;
        double lastTick = 0.0;
        juce::Point<float> mousePos { -1.0f, -1.0f };
    };
}
