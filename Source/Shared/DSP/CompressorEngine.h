#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "CompressorMath.h"
#include "PeakMeter.h"
#include <atomic>

namespace cdaw
{
    /** The DSP of C-Comp, a compressor, shared by CDAW's built-in plugin and the VST3.
        Gain is computed in decibels, so the ratio is a true ratio; soft knee; stereo-linked
        detector that can be fed from a sidechain signal instead; meters input, output, detector
        and gain reduction for the editor. No latency. */
    class CompressorEngine
    {
    public:
        /** Everything the audio needs from the parameters (in their own units: dB, ms). */
        struct Settings
        {
            float thresholdDb = -6.0f;
            float ratio = 2.0f;             // e.g. 4 for 4:1; very large = limiter
            float kneeDb = 6.0f;
            float attackMs = 100.0f, releaseMs = 100.0f;
            float makeUpDb = 0.0f;
            float sidechainDb = 0.0f;       // gain on the sidechain signal
        };

        void prepare (double sampleRate);
        void reset();

        /** Compresses `numSamples` of the buffer from `start`, in place (channels 0 and 1; a
            mono buffer is fine). When `key` is given (numSamples values) it drives the detector
            instead of the buffer's own level. */
        void process (juce::AudioBuffer<float>&, int start, int numSamples, const Settings&, const float* key = nullptr);

        static float transfer (float inDb, const Settings& s)   { return CompressorMath::transfer (inDb, s.thresholdDb, s.ratio, s.kneeDb); }

        // Meters for the editor (linear gain peaks; gain reduction in positive dB).
        PeakMeter inputPeak[2], outputPeak[2], detectorPeak, gainReduction;
        std::atomic<bool> sidechainActive { false };

    private:
        double sampleRate = 48000.0;
        double envelopeDb = 0.0;        // smoothed gain change (<= 0)
    };
}
