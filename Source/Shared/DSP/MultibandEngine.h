#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "AnalyserFifo.h"
#include "CompressorMath.h"
#include "EqMath.h"
#include "PeakMeter.h"
#include <array>
#include <memory>

namespace cdaw
{
    /** The DSP of C-Multicomp, a 4-band compressor, shared by CDAW's built-in plugin and the VST3.
        Three Linkwitz-Riley crossovers (24 dB/oct, phase-aligned, so the bands add back up flat
        when nothing is compressing) split the signal; each band has its own compressor (threshold,
        ratio, attack, release, make-up; soft knee as in CDAW's compressor) and can be switched to
        pass through uncompressed or soloed. Meters every band and feeds pre- and post-processing
        audio to the editor's spectrum. No latency. */
    class MultibandEngine
    {
    public:
        static constexpr int numBands = 4;
        static constexpr float kneeDb = 6.0f;
        static constexpr float minCrossover = 30.0f, maxCrossover = 16000.0f;

        struct BandSettings
        {
            bool on = false, solo = false;
            float threshold = -18.0f, ratio = 3.0f, attack = 10.0f, release = 150.0f, gain = 0.0f;
        };

        /** Everything the audio needs from the parameters (in their own units: dB, Hz, ms). */
        struct Settings
        {
            std::array<BandSettings, numBands> bands;
            std::array<float, numBands - 1> crossovers { 120.0f, 1000.0f, 6000.0f };
            float output = 0.0f;

            bool anySoloed() const;
        };

        /** Meters for the editor: band level before compression, gain reduction (dB), level after. */
        struct BandMeters { PeakMeter inputPeak, reduction, outputPeak; };

        MultibandEngine();

        void prepare (double sampleRate, int maxBlockSize);
        void reset();

        /** Processes `numSamples` of the buffer from `start`, in place (one or two channels). */
        void process (juce::AudioBuffer<float>&, int start, int numSamples, const Settings&);

        /** Crossover frequencies, kept in order with at least a third of an octave between them. */
        static std::array<float, numBands - 1> orderCrossovers (std::array<float, numBands - 1>);

        /** A band's range in Hz (20 Hz / 20 kHz at the ends). */
        static juce::Range<float> getBandRange (const Settings&, int band);

        static float transfer (float inDb, float thresholdDb, float ratio)  { return CompressorMath::transfer (inDb, thresholdDb, ratio, kneeDb); }

        double getSampleRate() const                        { return sampleRate; }

        std::array<BandMeters, numBands> bandMeters;
        PeakMeter inputPeak[2], outputPeak[2];
        std::shared_ptr<AnalyserFifo> preFifo, postFifo;

    private:
        struct State { double z1 = 0, z2 = 0; };
        struct Stage { EqMath::Biquad coeffs; std::array<State, 2> state; };

        static double run (Stage&, int channel, double x) noexcept;
        void designCrossovers (const std::array<float, numBands - 1>& frequencies);
        void processChunk (juce::AudioBuffer<float>& view, const Settings&);

        // Crossover i: lowPass x2, highPass x2, and the all-pass used on bands below it.
        struct CrossoverStages { Stage lp1, lp2, hp1, hp2; std::array<Stage, 2> ap; };
        std::array<CrossoverStages, numBands - 1> stages;
        std::array<float, numBands - 1> designedFrequencies {};
        double designedRate = 0.0;
        double sampleRate = 48000.0;

        std::array<double, numBands> envelopeDb {};
        std::array<juce::AudioBuffer<float>, numBands> bandBuffers;
        float lastOutputGain = 1.0f;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MultibandEngine)
    };
}
