#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "AnalyserFifo.h"
#include "EqMath.h"
#include "PeakMeter.h"
#include <memory>
#include <vector>

namespace cdaw
{
    /** The reverb DSP shared by C-Verb and the simple Reverb: pre-delay -> early reflections ->
        input diffusion -> an 8-line feedback delay network (Hadamard mixing, slowly modulated
        delays) with separate decay times for lows, mids and highs. Low / high cuts on the signal
        going into the reverb ("pre") and on the reverb coming out ("post"), stereo width, freeze
        and an equal-power dry / wet mix. */
    class ReverbEngine
    {
    public:
        /** Everything the sound depends on, in plain units. */
        struct Settings
        {
            float mix = 0.3f;               // 0..1
            float preDelay = 20.0f;         // ms
            float size = 0.55f;             // 0..1
            float decay = 2.2f;             // RT60, s
            float diffusion = 0.75f;        // 0..1
            float modulation = 0.3f;        // 0..1
            float lowDecay = 1.1f;          // decay multiplier below 250 Hz
            float highDecay = 0.5f;         // decay multiplier above dampFreq
            float dampFreq = 5000.0f;       // Hz
            float early = 0.4f;             // early reflections level, 0..1
            float width = 1.0f;             // 0..2
            float preLowCut = 20.0f, preHighCut = 20000.0f, postLowCut = 100.0f, postHighCut = 12000.0f;   // Hz; 20 / 20k = off
            bool freeze = false;
        };

        void prepare (double sampleRate, int maxBlockSize, const Settings&);
        void reset();

        /** Processes `n` samples from `start` in place (stereo, or mono in / mono out). */
        void process (juce::AudioBuffer<float>&, int start, int n, const Settings&);

        double getSampleRate() const        { return rate; }

        //==============================================================================
        /** Decay time (RT60, seconds) at a frequency, from decay and the low / high multipliers. */
        static double getDecayAt (const Settings&, double hz);

        /** Combined response (dB) of the pre or post cuts at a frequency. */
        static double getCutsDb (const Settings&, double hz, bool post, double sampleRate);

        /** Early reflection taps: time after the pre-delay (s) and gain, for displays. */
        struct Tap { double seconds; float gain; };
        static std::vector<Tap> getEarlyTaps (const Settings&);

        static constexpr float lowCutOff = 20.0f, highCutOff = 20000.0f;    // at these the cut is off
        static constexpr double lowCrossover = 250.0;                      // lows decay by lowDecay below this

        // Meters for editors: dry input and reverb (wet) output peaks, and the reverb's output
        // (after the post cuts, before the mix) for a spectrum.
        PeakMeter dryPeak, wetPeak;
        std::shared_ptr<AnalyserFifo> wetFifo = std::make_shared<AnalyserFifo>();

    private:
        struct Delay
        {
            std::vector<float> data;
            int write = 0;

            void setMaxSamples (int n)              { data.assign ((size_t) juce::jmax (4, n), 0.0f); write = 0; }
            void clear()                            { std::fill (data.begin(), data.end(), 0.0f); }
            void push (float x) noexcept            { data[(size_t) write] = x; write = (write + 1) % (int) data.size(); }
            float read (int samplesAgo) const noexcept
            {
                auto i = write - 1 - samplesAgo;
                while (i < 0) i += (int) data.size();
                return data[(size_t) i];
            }

            /** Schroeder allpass using this line as its delay (its whole length). */
            float allPass (float x, float g) noexcept
            {
                const float stored = data[(size_t) write];      // the oldest sample: delayed by the full length
                const float y = stored - g * x;
                data[(size_t) write] = x + g * stored;
                write = (write + 1) % (int) data.size();
                return y;
            }

            float readFractional (double samplesAgo) const noexcept
            {
                const auto whole = (int) samplesAgo;
                const auto frac = (float) (samplesAgo - whole);
                return read (whole) * (1.0f - frac) + read (whole + 1) * frac;
            }
        };

        struct Filter   // one biquad, two channels
        {
            EqMath::Biquad c;
            double z[2][2] {};
            bool active = false;

            float process (int ch, float x) noexcept
            {
                const double y = c.b0 * x + z[ch][0];
                z[ch][0] = c.b1 * x - c.a1 * y + z[ch][1];
                z[ch][1] = c.b2 * x - c.a2 * y;
                return (float) y;
            }
        };

        void updateFilters (const Settings&);

        static constexpr int numLines = 8, numDiffusers = 4, numTaps = 10;

        static void hadamard (float* x) noexcept
        {
            for (int h = 1; h < numLines; h <<= 1)
                for (int i = 0; i < numLines; i += h * 2)
                    for (int j = i; j < i + h; ++j)
                    {
                        const float a = x[j], b = x[j + h];
                        x[j] = a + b;
                        x[j + h] = a - b;
                    }
        }

        double rate = 48000.0;

        Delay preDelayLine[2];
        Delay diffusers[2][numDiffusers];
        Delay lines[numLines];
        double lineLength[numLines] {};                         // smoothed, samples
        double lfoPhase[numLines] {};
        float lowState[numLines] {}, dampState[numLines] {};    // one-pole states per line
        Filter preLow, preHigh, postLow, postHigh;
        float designedCuts[4] { -1.0f, -1.0f, -1.0f, -1.0f };
        float lastDry = 1.0f, lastWet = 0.0f;
        float lastWetPeak = 0.0f;                           // the tail's level in the last block
        double silentSeconds = 1.0e9;                       // how long the input has been silent
        juce::AudioBuffer<float> wetBuffer, inputBuffer;    // reverb output; diffused input to the network
    };
}
