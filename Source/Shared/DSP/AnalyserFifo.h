#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <vector>

namespace cdaw
{
    /** Lock-free buffer of audio (summed to mono) for a spectrum display: written on the audio
        thread, read by the UI. Used for the master output and inside CDAW's plugins. */
    struct AnalyserFifo
    {
        static constexpr int capacity = 1 << 15;

        /** Audio thread. Drops samples if nobody is reading. */
        void push (const juce::AudioBuffer<float>&);

        /** Message thread. Returns the number of samples copied into dest. */
        int pull (float* dest, int maxSamples);

        std::atomic<double> sampleRate { 48000.0 };
        std::atomic<bool> active { false };

    private:
        juce::AbstractFifo fifo { capacity };
        std::vector<float> data = std::vector<float> ((size_t) capacity);
    };
}
