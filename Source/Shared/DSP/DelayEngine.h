#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "EqMath.h"
#include "FractionalDelay.h"
#include "PeakMeter.h"
#include <atomic>
#include <vector>

namespace cdaw
{
    /** C-Delay's DSP: a stereo (or ping-pong) delay whose time is free or tempo-synced. Changing
        the time glides the read point like tape, and "wobble" adds wow and flutter. Each repeat
        passes through the loop's low / high cut and soft saturation ("drive"), so repeats get
        darker, thinner and grittier as they fade. The echoes can duck under the dry signal, and
        their stereo width is adjustable. Dry stays at full level up to 50 % mix. */
    class DelayEngine
    {
    public:
        enum class Mode { stereo = 0, pingPong };

        struct Settings
        {
            float timeMs = 375.0f;          // free time
            bool sync = true;
            int division = 8;               // TempoSync index (1/8 D)
            Mode mode = Mode::stereo;
            float spread = 0.0f;            // -1..1: the right side shorter / longer by up to half
            float feedback = 0.4f;          // 0..1 (1 = repeats forever)
            float lowCut = 120.0f, highCut = 6000.0f;      // Hz in the loop; 20 / 20k = off
            float drive = 0.0f;             // 0..1
            float wobble = 0.0f;            // 0..1
            float duck = 0.0f;              // 0..1
            float width = 1.0f;             // 0..2
            float mix = 0.3f;               // 0..1
        };

        static constexpr float lowCutOff = 20.0f, highCutOff = 20000.0f;
        static constexpr double maxSeconds = 4.0;

        void prepare (double sampleRate, int maxBlockSize);
        void reset();

        /** Processes `n` samples from `start` in place (mono or stereo). `bpm` is for synced times. */
        void process (juce::AudioBuffer<float>&, int start, int n, const Settings&, double bpm);

        double getSampleRate() const        { return rate; }

        //==============================================================================
        /** Delay time of one side (0 = left, 1 = right) in seconds. */
        static double getTimeSeconds (const Settings&, double bpm, int channel);

        /** Response (dB) of one pass through the loop's cuts at a frequency. */
        static double getLoopFilterDb (const Settings&, double hz, double sampleRate);

        /** The echoes of a single click, for displays: when, on which side (0 left, 1 right, 2 both),
            how loud at 1 kHz (dB) and how many times it has been through the loop. */
        struct Echo { double seconds; int channel; float gainDb; int passes; };
        static std::vector<Echo> getEchoes (const Settings&, double bpm, double sampleRate, float floorDb = -60.0f, int maxEchoes = 48);

        /** How long the repeats take to fall 60 dB (capped). */
        static double getTailSeconds (const Settings&, double bpm);

        /** The loop's saturation for a drive setting (0..1). */
        static float saturate (float x, float drive) noexcept;

        // Meters for editors: input and echo (wet) peaks per side, and ducking in dB (positive).
        PeakMeter inputPeak[2], wetPeak[2], duckReduction;

    private:
        struct Filter
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

            void clear()        { for (auto& s : z) s[0] = s[1] = 0.0; }
        };

        void updateFilters (const Settings&);

        double rate = 48000.0;
        FractionalDelay lines[2];
        bool linesAreClear = false;
        Filter lowCut, highCut;
        float designedCuts[2] { -1.0f, -1.0f };
        Smoothed delaySamples[2], wetGain, dryGain, duckGain;
        double wowPhase = 0.0, flutterPhase = 0.0;
        float drift[2] {}, driftTarget[2] {};
        int driftCounter = 0;
        float envelope = 0.0f;
        bool primed = false;
        juce::Random random;
    };
}
