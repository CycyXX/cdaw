#pragma once

#include <juce_core/juce_core.h>
#include <cmath>
#include <vector>

namespace cdaw
{
    /** A delay line read at fractional positions with 4-point Hermite interpolation, so
        modulated delays (chorus, tape wobble, gliding delay times) stay smooth. Read before
        writing each sample: tap (d) is the input from d samples ago (d >= 3). */
    struct FractionalDelay
    {
        void setMaxSamples (int n)
        {
            const int size = juce::nextPowerOfTwo (juce::jmax (8, n + 4));
            data.assign ((size_t) size, 0.0f);
            mask = size - 1;
            write = 0;
        }

        void clear()                        { std::fill (data.begin(), data.end(), 0.0f); }
        int getMaxDelay() const noexcept    { return (int) data.size() - 4; }

        void push (float x) noexcept
        {
            data[(size_t) write] = x;
            write = (write + 1) & mask;
        }

        float tap (double delaySamples) const noexcept
        {
            // The interpolation reads one sample newer than the position: keep that one written.
            delaySamples = juce::jlimit (3.0, (double) getMaxDelay(), delaySamples);
            const double pos = (double) write - delaySamples;
            const double whole = std::floor (pos);
            const auto f = (float) (pos - whole);
            const int i = (int) whole;

            const float xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
            const float c1 = 0.5f * (x1 - xm1);
            const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
            const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
            return ((c3 * f + c2) * f + c1) * f + x0;
        }

    private:
        float at (int i) const noexcept     { return data[(size_t) (i & mask)]; }

        std::vector<float> data = std::vector<float> (8);
        int mask = 7, write = 0;
    };

    /** Soft saturation: about linear for small signals, approaching +-1 for large ones. */
    inline float softClip (float x) noexcept
    {
        x = juce::jlimit (-3.0f, 3.0f, x);
        return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
    }

    /** A smoothed value for the audio thread: one-pole glide towards a target. */
    struct Smoothed
    {
        void reset (double sampleRate, double seconds)
        {
            coefficient = (float) std::exp (-1.0 / juce::jmax (1.0, seconds * sampleRate));
        }

        float next (float target) noexcept
        {
            value = target + (value - target) * coefficient;
            return value;
        }

        /** What `numSamples` calls of next would do. Rounding soon stops the value moving (often a
            little off the target), and from then on there's nothing left to do. */
        void advance (float target, int numSamples) noexcept
        {
            for (int i = 0; i < numSamples; ++i)
            {
                const auto before = value;

                if (juce::exactlyEqual (next (target), before))
                    return;
            }
        }

        float value = 0.0f, coefficient = 0.0f;
    };
}
