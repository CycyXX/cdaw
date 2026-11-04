#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "EqMath.h"
#include "FractionalDelay.h"
#include "PeakMeter.h"
#include "SilenceTimer.h"
#include "TempoSync.h"
#include <array>
#include <atomic>
#include <complex>

namespace cdaw
{
    /** C-Chorus's DSP: one to four voices, each a delay tap swept by its own LFO (the voices run at
        slightly different speeds, so several sound like an ensemble rather than one wide chorus).
        Spread offsets the right side's sweep and pans the voices apart. Feedback around the
        delay turns short settings into a flanger; tone darkens the voices; dry stays at full
        level up to 50 % mix (classic chorus). */
    class ChorusEngine
    {
    public:
        static constexpr int maxVoices = 4;
        static constexpr float maxModulationMs = 7.0f, toneOff = 20000.0f;

        struct Settings
        {
            int voices = 2;
            float rateHz = 0.6f;
            float depth = 0.45f;            // 0..1 of maxModulationMs
            float delayMs = 9.0f;           // the sweep's shortest delay
            float feedback = 0.0f;          // -0.95..0.95
            float spread = 0.7f;            // 0..1
            float tone = 12000.0f;          // Hz; 20k = off
            float width = 1.0f;             // 0..2
            float mix = 0.5f;               // 0..1
        };

        void prepare (double sampleRate, int maxBlockSize);
        void reset();

        /** Processes `n` samples from `start` in place. The voices' sweeps keep in step with the song (see LfoClock). */
        void process (juce::AudioBuffer<float>&, int start, int n, const Settings&, const TempoSync::SongPosition& = {});

        double getSampleRate() const        { return rate; }

        //==============================================================================
        /** How much faster or slower each voice's LFO runs than the rate. */
        static constexpr std::array<double, maxVoices> rateMultipliers { 1.0, 1.17, 0.86, 1.31 };

        /** A voice's delay (ms) for an LFO phase (cycles) on one side (0 left, 1 right). */
        static float getVoiceDelayMs (const Settings&, double phase, int channel);

        /** The LFO phase of a voice on the right side, given its left phase. */
        static double rightPhase (const Settings& s, double leftPhase)    { return leftPhase + 0.25 * juce::jlimit (0.0f, 1.0f, s.spread); }

        /** Where a voice sits, -1 (left) to 1 (right). */
        static float getVoicePan (const Settings&, int voice);

        /** The response (dB) of the left side at a frequency with the voices at these delays (ms). */
        static double getResponseDb (const Settings&, const float* voiceDelaysMs, double hz, double sampleRate);

        /** Each voice's current LFO phase (cycles, left side), for the editor. */
        std::array<std::atomic<double>, maxVoices> voicePhase {};

        PeakMeter inputPeak, wetPeak;

    private:
        struct Filter
        {
            EqMath::Biquad c;
            double z[2][2] {};

            float process (int ch, float x) noexcept
            {
                const double y = c.b0 * x + z[ch][0];
                z[ch][0] = c.b1 * x - c.a1 * y + z[ch][1];
                z[ch][1] = c.b2 * x - c.a2 * y;
                return (float) y;
            }
        };

        double rate = 48000.0;
        FractionalDelay lines[2];
        std::array<double, maxVoices> phases {};
        TempoSync::LfoClock clock;
        std::array<Smoothed, maxVoices> voiceGain;
        Filter tone;
        float designedTone = -1.0f;
        Smoothed wetGain, dryGain, delayMs, depthMs;
        bool primed = false;
        SilenceTimer silence;
    };
}
