#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "AnalyserFifo.h"
#include "EqMath.h"
#include "FractionalDelay.h"
#include "PeakMeter.h"
#include "SilenceTimer.h"
#include <array>
#include <atomic>

namespace cdaw
{
    /** C-Dist's DSP, a distortion pedal voiced after modern guitar amps:
        input gain -> noise gate -> tight (a high pass before the drive; in the Bass voicing a
        crossover keeping the lows clean) -> the voicing's pre-emphasis -> 1 to 3 gain stages at
        four times the rate (each a gain, a curve made asymmetric by Character, a DC block and a
        low pass between stages; a slow envelope moves the bias with how hard you play, for bloom
        and sag) -> back down -> the voicing's own post EQ -> bass / mid / treble / smooth ->
        automatic gain compensation, level and dry / wet blend.
        The oversampling filters are linear phase, so the plugin has a fixed latency (reported to
        the host) and the dry path is delayed to match it. */
    class DistEngine
    {
    public:
        enum class Voicing { boost = 0, crunch, lead, modern, fuzz, bass, numVoicings };
        enum class Curve { soft = 0, tube, diode, hard, fuzz };
        static constexpr int numVoicings = (int) Voicing::numVoicings, maxStages = 3, oversampling = 4;
        static constexpr int latencySamples = 41;       // the two half-band stages: 35 + 12 / 2

        struct Settings
        {
            Voicing voicing = Voicing::lead;
            float inputDb = 0.0f;
            float drive = 0.5f;             // 0..1
            float tight = 90.0f;            // Hz: high pass before the drive (the crossover in Bass)
            float character = 0.3f;         // 0..1: bias / asymmetry (Fuzz: starves it)
            float bassDb = 0.0f, midDb = 0.0f, midHz = 800.0f, trebleDb = 0.0f;
            float smooth = 9000.0f;         // Hz: high cut after the drive
            float levelDb = 0.0f;
            float blend = 1.0f;             // 0 dry .. 1 wet
            bool gateOn = false;
            float gateThresholdDb = -60.0f, gateRelease = 0.08f;
        };

        /** What makes a voicing. */
        struct Profile
        {
            const char* name;
            int stages;
            float minGainDb, maxGainDb;     // the drive's total gain, shared out between the stages
            Curve curve;
            float asymmetry;                // the bias at Character 0
            float characterBias;            // what Character adds to it
            float preHz, preDb, preQ;       // pre-emphasis (a peak)
            float internalHighPassHz;       // a first-order high pass of its own (0: none)
            float interstageHz;             // the low pass after each stage
            float postHz, postDb, postQ;    // post EQ (a peak; 0 dB: none)
            float postLowPassHz;
            float sag;                      // how far playing harder moves the bias and lowers the gain
        };

        static const Profile& profile (Voicing);
        static juce::StringArray getVoicingNames();

        DistEngine();

        void prepare (double sampleRate, int maxBlockSize);
        void reset();

        /** Processes `n` samples from `start` in place (one or two channels). */
        void process (juce::AudioBuffer<float>&, int start, int n, const Settings&);

        int getLatencySamples() const       { return latencySamples; }
        double getSampleRate() const        { return rate; }

        //==============================================================================
        /** The gain stages as a static curve: the output (in the stages' units, about -1..1) for
            an input `x` arriving at the drive while the signal there is at `level` (its bias and
            sag follow the level, as in the engine). */
        static float transferCurve (const Settings&, float x, float level);

        /** The levels (dB against the fundamental) of the first harmonics a sine of `amplitude`
            gets from the static curve (at `level`, or its own amplitude if negative); [0] is the
            fundamental (0 dB). */
        static std::array<float, 16> harmonicsOf (const Settings&, float amplitude, float level = -1.0f);

        /** The small-signal response (dB) of everything linear in the chain: tight, the voicing's
            filters, the oversampling's DC blocks and the tone controls (without the drive's gain). */
        static double getResponseDb (const Settings&, double hz, double sampleRate);

        /** The gain compensation (linear) that keeps a reference signal's level as the drive changes. */
        static float compensationFor (const Settings&, double sampleRate);

        /** Each stage's gain (linear) at these settings. */
        static float stageGain (const Settings&);

        /** One of the curves at a bias, as the stages apply it (the operating point taken out). */
        static float shape (Curve, float u, float bias, float starve);

        //==============================================================================
        // For the editor.
        static constexpr int pairLength = 4096;
        std::array<std::atomic<float>, pairLength> pairIn {}, pairOut {};      // samples into the drive and out of it (left), aligned
        std::atomic<int> pairWrite { 0 };

        std::array<std::atomic<float>, maxStages> stageHeat {};                // 0..1: how hard each stage clips now
        std::atomic<float> biasShift { 0.0f }, compensation { 1.0f }, inputLevel { 0.0f };

        static constexpr int gateHistoryLength = 512, gateHistoryInterval = 256;
        std::array<std::atomic<float>, gateHistoryLength> gateLevelDb {}, gateGainHistory {};
        std::atomic<int> gateWrite { 0 };
        std::atomic<bool> gateOpen { true };
        std::atomic<float> gateGain { 1.0f };

        PeakMeter inputPeak[2], outputPeak[2];
        std::shared_ptr<AnalyserFifo> inputFifo = std::make_shared<AnalyserFifo>(), outputFifo = std::make_shared<AnalyserFifo>();

    private:
        struct BiquadState
        {
            double z1 = 0.0, z2 = 0.0;
            double process (const EqMath::Biquad& b, double x) noexcept
            {
                const double y = b.b0 * x + z1;
                z1 = b.b1 * x - b.a1 * y + z2;
                z2 = b.b2 * x - b.a2 * y;
                return y;
            }
            void clear()        { z1 = z2 = 0.0; }
        };

        /** A linear-phase half-band filter for doubling or halving the rate. */
        struct HalfBand
        {
            std::vector<float> taps;
            std::vector<int> nonZero;
            int centre = 0;

            struct State
            {
                std::array<float, 128> history {};
                int write = 0;
                void push (float x) noexcept    { history[(size_t) write] = x; write = (write + 1) & 127; }
                void clear()                    { history.fill (0.0f); write = 0; }
            };

            void design (int length, double beta);
            float filter (const State&) const noexcept;
            float centreOnly (const State&) const noexcept;
            void up (State&, const float* in, int n, float* out) const noexcept;      // n -> 2n
            void down (State&, const float* in, int n, float* out) const noexcept;    // n -> n / 2
        };

        struct Delay
        {
            std::array<float, 64> line {};
            int write = 0;
            float process (float x) noexcept
            {
                line[(size_t) write] = x;
                write = (write + 1) & 63;
                return line[(size_t) ((write - 1 - latencySamples) & 63)];
            }
            void clear()        { line.fill (0.0f); write = 0; }
        };

        struct Stage
        {
            double dcIn = 0.0, dcOut = 0.0, low = 0.0, previousV = 0.0, previousF = 0.0;
            void clear()        { dcIn = dcOut = low = previousV = previousF = 0.0; }
        };

        struct Channel
        {
            BiquadState tight, internal, pre, crossLow[2], crossHigh[2];
            BiquadState post, postLow, toneBass, toneMid, toneTreble, smooth;
            HalfBand::State up1, up2, down2, down1;
            Stage stages[maxStages];
            Delay dry, lows, drive;
            double envelope = 0.0, finalDcIn = 0.0, finalDcOut = 0.0;
            Smoothed gain[maxStages];
            void clear();
        };

        struct Coefficients
        {
            EqMath::Biquad tight, internal, pre, post, postLow, toneBass, toneMid, toneTreble, smooth;
            EqMath::Crossover crossover;
            bool usePost = false, useSmooth = false, useInternal = false;
            double interstage = 1.0, dc = 0.0, dcBase = 0.0;
        };

        void updateCoefficients (const Settings&);
        void runGate (const Settings&, const juce::AudioBuffer<float>&, int start, int n, float inputGain);
        void runChannel (int ch, const Settings&, float* data, int n, float inputGain);
        static Coefficients designFor (const Settings&, double sampleRate);
        static float curveThrough (const Settings&, float x, float level, bool withStarve);

        double rate = 48000.0;
        int blockSize = 512;
        Coefficients coefficients;
        Settings designed;
        bool hasDesign = false;
        float compensationTarget = 1.0f;

        HalfBand stage1, stage2;
        Channel channels[2];
        std::vector<float> gateGains, upsampled1, upsampled2, wet, dryScratch, driveIn, lowScratch, envelopes, makeUps, levels, blends;
        bool primed = false;

        // The gate (linked across the channels).
        double gateEnvelope = 0.0, gateLevel = 0.0;
        bool open = false;
        int holdLeft = 0, historyCountdown = 0;
        float historyPeak = 0.0f;

        Smoothed level, blend, makeUp;
        float heatIn[maxStages] {}, heatOut[maxStages] {};
        SilenceTimer silence;
    };
}
