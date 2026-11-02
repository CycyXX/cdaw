#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "PeakMeter.h"
#include <atomic>

namespace cdaw
{
    /** C-Esser's DSP, a de-esser: the sibilant band (low to high frequency) of the summed channels
        drives a fast peak follower; above the threshold (fixed, or following the programme's level
        with auto) it reduces by up to `range` dB, at 4:1 with a soft knee. Split mode applies the
        reduction as a bell over that band only (unchanged everywhere when nothing is reduced),
        wideband mode to the whole signal. Listen plays the band, or what is taken away. */
    class EsserEngine
    {
    public:
        enum class Mode { split, wideband };
        enum class Listen { off, band, removed };

        static constexpr float ratio = 4.0f, kneeDb = 6.0f;
        static constexpr float autoOffsetDb = -6.0f;    // auto: the threshold sits this far below the programme's level
        static constexpr float autoFloorDb = -50.0f;

        struct Settings
        {
            float thresholdDb = -24.0f;
            bool autoThreshold = false;
            float rangeDb = 12.0f;          // the most it reduces
            float releaseMs = 100.0f;
            float lowHz = 4000.0f, highHz = 10000.0f;
            Mode mode = Mode::split;
            Listen listen = Listen::off;
            float outputDb = 0.0f;
        };

        void prepare (double sampleRate, int maxBlockSize);
        void reset();
        /** Processes `n` samples from `start` in place. */
        void process (juce::AudioBuffer<float>&, int start, int n, const Settings&);

        double getSampleRate() const        { return rate; }

        //==============================================================================
        /** Reduction (dB, >= 0) for a band level (dB) against a threshold. */
        static float getReductionDb (const Settings&, float levelDb, float thresholdDb);

        /** Response (dB) at `hz` while reducing by `reductionDb` (without the output gain). */
        static double getResponseDb (const Settings&, float reductionDb, double hz, double sampleRate);

        /** The centre and bandwidth (Q) of the band. */
        static double getCentreHz (const Settings&);
        static double getQ (const Settings&);

        PeakMeter inputPeak, outputPeak;
        PeakMeter reduction;                            // dB, the most since the editor looked
        PeakMeter bandLevel;                            // gain, the detector's peak
        std::atomic<float> thresholdNow { -24.0f };     // dB: the threshold in use (moves with auto)

    private:
        /** A TPT state variable filter (Zavalishin): low, band and high pass from one state. */
        struct Svf
        {
            void set (double hz, double q, double sampleRate);
            void reset()                    { s1 = s2 = 0.0; }
            struct Out { double low, band, high; };
            Out tick (double x)
            {
                const double hp = (x - (k + g) * s1 - s2) * a;
                const double v1 = g * hp, bp = v1 + s1;
                s1 = bp + v1;
                const double v2 = g * bp, lp = v2 + s2;
                s2 = lp + v2;
                return { lp, bp, hp };
            }

            double g = 0.0, k = 1.0, a = 1.0, s1 = 0.0, s2 = 0.0;
        };

        void updateFilters (const Settings&);

        double rate = 48000.0;
        Svf bell[2], listenHigh[2], listenLow[2];   // per channel: the bell's band pass, the listened band
        Svf detectHigh, detectLow;                  // on the sum: high pass at low, low pass at high
        float lastLow = 0.0f, lastHigh = 0.0f, lastOutputGain = 1.0f;
        double envelope = 0.0, programme = 0.0;     // detector peak (gain), programme mean square
        double attackCoeff = 0.0, programmeCoeff = 0.0;
    };
}
