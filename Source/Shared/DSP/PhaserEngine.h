#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "PeakMeter.h"
#include "TempoSync.h"
#include <atomic>

namespace cdaw
{
    /** C-Phaser's DSP: 2 to 12 first-order allpass stages whose frequency an LFO sweeps up and down
        around a centre, on an octave scale. Mixed with the dry signal they cut notches that
        move through the spectrum; feedback (positive or negative) sharpens them into peaks.
        Spread offsets the right side's sweep for a wide, swirling stereo phaser. The rate is
        free or tempo-synced. */
    class PhaserEngine
    {
    public:
        static constexpr int maxStages = 12;
        static constexpr float maxOctaves = 3.0f;      // depth 1 sweeps +-3 octaves

        struct Settings
        {
            int stages = 6;
            float rateHz = 0.35f;
            bool sync = false;
            int division = 15;              // TempoSync index (1/1)
            float depth = 0.6f;             // 0..1
            float centreHz = 900.0f;
            float feedback = 0.45f;         // -0.95..0.95
            float spread = 0.5f;            // 0..1: the right sweep up to half a cycle later
            float mix = 0.5f;               // 0.5 = deepest notches
        };

        void prepare (double sampleRate, int maxBlockSize);
        void reset();
        /** Processes `n` samples from `start` in place. The sweep keeps in step with the song (see LfoClock). */
        void process (juce::AudioBuffer<float>&, int start, int n, const Settings&, double bpm, const TempoSync::SongPosition& = {});

        double getSampleRate() const        { return rate; }

        //==============================================================================
        static double getRateHz (const Settings&, double bpm);

        /** The stages' frequency at an LFO phase (cycles). */
        static double getSweepHz (const Settings&, double phase, double sampleRate);

        /** Response (dB) at `hz` with the stages at `sweepHz`. */
        static double getResponseDb (const Settings&, double sweepHz, double hz, double sampleRate);

        /** The sweep's phase on the left side (cycles), for the editor. */
        std::atomic<double> lfoPhase { 0.0 };

        PeakMeter inputPeak, outputPeak;

    private:
        static double coefficient (double hz, double sampleRate);

        double rate = 48000.0;
        double phase = 0.0;
        TempoSync::LfoClock clock;
        double state[2][maxStages] {};
        double lastOut[2] {};
        double coeff[2] { -0.9, -0.9 };
        float lastMix = 0.5f;
        bool primed = false;
    };
}
