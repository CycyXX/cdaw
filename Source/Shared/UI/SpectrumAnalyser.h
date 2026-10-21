#pragma once

#include <juce_dsp/juce_dsp.h>
#include "../DSP/AnalyserFifo.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace cdaw
{
    /** FFT of one analyser feed, reduced to one level per pixel column (log-spaced frequencies
        given by the caller): power averaged over a sixth of an octave, tilted +4.5 dB/oct around
        1 kHz, rising quickly and falling smoothly. Used by the EQ and multiband editors. */
    struct SpectrumAnalyser
    {
        static constexpr int order = 13, size = 1 << order;

        explicit SpectrumAnalyser (std::shared_ptr<AnalyserFifo> f) : fifo (std::move (f)) {}

        /** Pulls new audio; returns false when nothing will change (silence, already at the floor). */
        bool update (const std::vector<double>& columnHz, double sampleRate)
        {
            int total = 0;
            float loudest = 0.0f;

            for (int got; (got = fifo->pull (scratch.data(), size)) > 0;)
            {
                for (int i = 0; i < got; ++i)
                    loudest = juce::jmax (loudest, std::abs (scratch[(size_t) i]));

                std::move (history.begin() + got, history.end(), history.begin());
                std::copy_n (scratch.begin(), got, history.end() - got);
                total += got;
            }

            if (levels.size() != columnHz.size())
                levels.assign (columnHz.size(), floorDb);

            if (loudest < 1.0e-5f && atFloor())
                return false;

            if (total == 0)
            {
                // No audio arriving (stopped): let the display fall away.
                for (auto& l : levels)
                    l = juce::jmax (floorDb, l - 1.5f);
                return true;
            }

            std::copy (history.begin(), history.end(), fft.begin());
            std::fill (fft.begin() + size, fft.end(), 0.0f);
            window.multiplyWithWindowingTable (fft.data(), (size_t) size);
            transform.performFrequencyOnlyForwardTransform (fft.data(), true);

            // Power per bin (Hann window: a full-scale sine peaks at size / 4), and running sums of it.
            const double binHz = sampleRate / size;
            const int numBins = size / 2;
            const double norm = 1.0 / (size * 0.25);

            for (int k = 0; k < numBins; ++k)
            {
                const double mag = fft[(size_t) k] * norm;
                power[(size_t) k] = mag * mag;
                powerSum[(size_t) k + 1] = powerSum[(size_t) k] + power[(size_t) k];
            }

            // Sum of power over a continuous range of bins (fractions of a bin included).
            auto integral = [&] (double pos)
            {
                pos = juce::jlimit (0.0, (double) numBins - 1.0e-6, pos);
                const auto k = (int) pos;
                return powerSum[(size_t) k] + power[(size_t) k] * (pos - k);
            };

            for (size_t c = 0; c < columnHz.size(); ++c)
            {
                // Average power over a sixth of an octave around the column: steady and independent
                // of how many bins a pixel column happens to cover (taking the loudest bin made
                // needles wherever columns are narrow, i.e. in wide windows).
                const double centre = columnHz[c] / binHz;
                const double a = centre * smoothingFactorDown, b = centre * smoothingFactorUp;
                double mean;

                if (b - a < 1.5)
                {
                    // Low frequencies: the band is narrower than the bins, so interpolate.
                    const auto pos = juce::jlimit (1.0, (double) numBins - 2.0, centre);
                    const auto k = (int) pos;
                    const auto frac = pos - k;
                    mean = power[(size_t) k] * (1.0 - frac) + power[(size_t) k + 1] * frac;
                }
                else
                {
                    mean = (integral (b) - integral (a)) / (b - a);
                }

                auto db = (float) (10.0 * std::log10 (juce::jmax (1.0e-14, mean)));

                // +4.5 dB/oct tilt around 1 kHz, so a balanced mix looks roughly flat.
                db += 4.5f * (float) std::log2 (columnHz[c] / 1000.0);

                auto& l = levels[c];
                l = db > l ? l + (db - l) * 0.6f : juce::jmax (db, l - 1.2f);
                l = juce::jmax (floorDb, l);
            }

            return true;
        }

        bool atFloor() const
        {
            return std::all_of (levels.begin(), levels.end(), [] (float l) { return l <= floorDb + 0.01f; });
        }

        static constexpr float floorDb = -100.0f;
        std::shared_ptr<AnalyserFifo> fifo;
        juce::dsp::FFT transform { order };
        juce::dsp::WindowingFunction<float> window { (size_t) size, juce::dsp::WindowingFunction<float>::hann, false };
        std::vector<float> history = std::vector<float> ((size_t) size);
        std::vector<float> scratch = std::vector<float> ((size_t) size);
        std::vector<float> fft = std::vector<float> ((size_t) size * 2);
        std::vector<float> levels;
        std::vector<double> power = std::vector<double> ((size_t) size / 2), powerSum = std::vector<double> ((size_t) size / 2 + 1);

        // A sixth of an octave: +-1/12 octave around each column.
        inline static const double smoothingFactorUp = std::pow (2.0, 1.0 / 12.0), smoothingFactorDown = 1.0 / smoothingFactorUp;
    };
}
