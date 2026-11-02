#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "AnalyserFifo.h"
#include "PeakMeter.h"
#include "Svf.h"
#include "TempoSync.h"
#include <atomic>
#include <memory>

namespace cdaw
{
    /** C-Filter's DSP: a resonant state-variable filter (low pass, high pass, band pass or notch;
        12 or 24 dB/oct) whose cutoff moves with an LFO (six shapes, free or tempo-synced, the
        right side optionally offset) and with an envelope that follows the input (auto-wah).
        Drive saturates the input before the filter; mix and output set the level. The
        cutoff is modulated on an octave scale and stays stable however fast it moves. */
    class FilterEngine
    {
    public:
        enum class Type { lowPass = 0, highPass, bandPass, notch };
        enum class Shape { sine = 0, triangle, rampUp, rampDown, square, random, numShapes };

        static constexpr float maxModOctaves = 4.0f;
        static constexpr float minHz = 20.0f, maxHz = 20000.0f;

        struct Settings
        {
            Type type = Type::lowPass;
            bool steep = false;             // 24 dB/oct instead of 12
            float cutoff = 2000.0f;         // Hz
            float resonance = 0.3f;         // 0..1
            float drive = 0.0f;             // 0..1
            float lfoRate = 1.0f;           // Hz
            bool lfoSync = false;
            int lfoDivision = 9;            // TempoSync index (1/4)
            float lfoAmount = 0.0f;         // 0..1 of maxModOctaves, up and down
            Shape lfoShape = Shape::sine;
            float lfoStereo = 0.0f;         // 0..1: the right side up to half a cycle later
            float envAmount = 0.0f;         // -1..1 of maxModOctaves
            float envRelease = 150.0f;      // ms
            float mix = 1.0f;               // 0..1
            float outputDb = 0.0f;
        };

        void prepare (double sampleRate, int maxBlockSize);
        void reset();
        /** Processes `n` samples from `start` in place. The LFO keeps in step with the song (see LfoClock). */
        void process (juce::AudioBuffer<float>&, int start, int n, const Settings&, double bpm, const TempoSync::SongPosition& = {});

        double getSampleRate() const        { return rate; }

        //==============================================================================
        static double getLfoHz (const Settings&, double bpm);

        /** The LFO's value (-1..1) at a phase (cycles). `held` is the random shape's current value. */
        static float lfoValue (Shape, double phase, float held = 0.0f);

        /** The cutoff for an LFO value (-1..1) and envelope (0..1). */
        static double modulatedCutoff (const Settings&, float lfo, float envelope, double sampleRate);

        /** Response (dB) at `hz` with the filter at `cutoffHz` (mix and output included). */
        static double getResponseDb (const Settings&, double cutoffHz, double hz, double sampleRate);

        /** Q of the resonant section for a resonance setting. */
        static double getQ (const Settings&);

        // For the editor: where the cutoff is right now (each side), the envelope (0..1),
        // the LFO phase, input and output peaks, and the output for a spectrum.
        std::atomic<float> currentCutoff[2] { 2000.0f, 2000.0f };
        std::atomic<float> envelopeLevel { 0.0f };
        std::atomic<double> lfoPhase { 0.0 };
        PeakMeter inputPeak, outputPeak;
        std::shared_ptr<AnalyserFifo> outputFifo = std::make_shared<AnalyserFifo>();

    private:
        double filterSample (int ch, double x, double g, const Settings&, double k1, double k2) noexcept;

        double rate = 48000.0;
        double phase = 0.0, lastSidePhase[2] {};
        TempoSync::LfoClock clock;
        float held[2] {}, smoothedLfo[2] {};
        Svf stage1[2], stage2[2];
        double g[2] { 0.1, 0.1 };
        float envelope = 0.0f;
        float lastMix = 1.0f, lastOutput = 1.0f;
        bool primed = false;
        juce::Random random;
    };
}
