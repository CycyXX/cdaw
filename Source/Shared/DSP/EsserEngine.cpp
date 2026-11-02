#include "EsserEngine.h"
#include <complex>

namespace cdaw
{
    namespace
    {
        constexpr double pi = juce::MathConstants<double>::pi;
        constexpr double attackMs = 0.5;
        constexpr double programmeMs = 300.0;
        constexpr double butterworthQ = 0.7071;

        double coefficientFor (double ms, double sampleRate)
        {
            return std::exp (-1.0 / (juce::jmax (0.01, ms) * 0.001 * sampleRate));
        }

        /** The band's edges, kept apart and inside the audible range. */
        std::pair<double, double> edgesOf (const EsserEngine::Settings& s)
        {
            const double low = juce::jlimit (500.0, 16000.0, (double) s.lowHz);
            return { low, juce::jlimit (low * 1.2, 22000.0, (double) s.highHz) };
        }
    }

    void EsserEngine::Svf::set (double hz, double q, double sampleRate)
    {
        g = std::tan (pi * juce::jlimit (10.0, sampleRate * 0.49, hz) / sampleRate);
        k = 1.0 / q;
        a = 1.0 / (1.0 + g * (g + k));
    }

    void EsserEngine::prepare (double sampleRate, int)
    {
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        attackCoeff = coefficientFor (attackMs, rate);
        programmeCoeff = coefficientFor (programmeMs, rate);
        lastLow = lastHigh = 0.0f;
        reset();
    }

    void EsserEngine::reset()
    {
        for (auto* filters : { bell, listenHigh, listenLow })
            for (int ch = 0; ch < 2; ++ch)
                filters[ch].reset();

        detectHigh.reset();
        detectLow.reset();
        envelope = programme = 0.0;
    }

    double EsserEngine::getCentreHz (const Settings& s)
    {
        auto [low, high] = edgesOf (s);
        return std::sqrt (low * high);
    }

    double EsserEngine::getQ (const Settings& s)
    {
        auto [low, high] = edgesOf (s);
        return juce::jlimit (0.3, 8.0, std::sqrt (low * high) / (high - low));
    }

    float EsserEngine::getReductionDb (const Settings& s, float levelDb, float thresholdDb)
    {
        const float over = levelDb - thresholdDb;
        const float slope = 1.0f - 1.0f / ratio;
        float reductionDb = 0.0f;

        if (over >= 0.5f * kneeDb)
            reductionDb = slope * over;
        else if (over > -0.5f * kneeDb)
            reductionDb = slope * (over + 0.5f * kneeDb) * (over + 0.5f * kneeDb) / (2.0f * kneeDb);

        return juce::jlimit (0.0f, juce::jmax (0.0f, s.rangeDb), reductionDb);
    }

    double EsserEngine::getResponseDb (const Settings& s, float reductionDb, double hz, double sampleRate)
    {
        if (s.mode == Mode::wideband)
            return -reductionDb;

        // The bell: x - (1 - gain) * a constant-peak band pass, on the bilinear (prewarped) frequency scale.
        const double warp = std::tan (pi * juce::jmin (hz, sampleRate * 0.49) / sampleRate)
                            / std::tan (pi * juce::jmin (getCentreHz (s), sampleRate * 0.49) / sampleRate);
        const std::complex<double> j (0.0, warp);
        const double k = 1.0 / getQ (s);
        const auto band = k * j / (j * j + k * j + 1.0);
        const double gain = juce::Decibels::decibelsToGain ((double) -reductionDb);
        return 20.0 * std::log10 (juce::jmax (1.0e-6, std::abs (1.0 - (1.0 - gain) * band)));
    }

    void EsserEngine::updateFilters (const Settings& s)
    {
        if (juce::exactlyEqual (s.lowHz, lastLow) && juce::exactlyEqual (s.highHz, lastHigh))
            return;

        lastLow = s.lowHz;
        lastHigh = s.highHz;
        auto [low, high] = edgesOf (s);

        for (int ch = 0; ch < 2; ++ch)
        {
            bell[ch].set (getCentreHz (s), getQ (s), rate);
            listenHigh[ch].set (low, butterworthQ, rate);
            listenLow[ch].set (high, butterworthQ, rate);
        }

        detectHigh.set (low, butterworthQ, rate);
        detectLow.set (high, butterworthQ, rate);
    }

    void EsserEngine::process (juce::AudioBuffer<float>& buffer, int start, int n, const Settings& s)
    {
        const int numChannels = juce::jmin (2, buffer.getNumChannels());
        if (numChannels == 0 || n <= 0)
            return;

        updateFilters (s);

        const double releaseCoeff = coefficientFor (juce::jlimit (1.0, 2000.0, (double) s.releaseMs), rate);
        const double bellK = bell[0].k;
        const float targetOutput = juce::Decibels::decibelsToGain (s.outputDb);
        const float outputStep = (targetOutput - lastOutputGain) / (float) n;

        float* data[2] { buffer.getWritePointer (0, start), buffer.getWritePointer (numChannels > 1 ? 1 : 0, start) };
        float inPeak = 0.0f, outPeak = 0.0f, mostReduction = 0.0f;
        double mostLevel = 0.0;
        float thresholdDb = s.thresholdDb;

        for (int i = 0; i < n; ++i)
        {
            const double left = data[0][i];
            const double right = numChannels > 1 ? (double) data[1][i] : left;
            const double mono = 0.5 * (left + right);
            inPeak = juce::jmax (inPeak, (float) std::abs (left), (float) std::abs (right));

            // Detector: the band's peak, quick to rise, falling at the release time.
            const double band = detectLow.tick (detectHigh.tick (mono).high).low;
            const double level = std::abs (band);
            envelope = level + (envelope - level) * (level > envelope ? attackCoeff : releaseCoeff);
            mostLevel = juce::jmax (mostLevel, envelope);

            programme = mono * mono + (programme - mono * mono) * programmeCoeff;
            if (s.autoThreshold)
                thresholdDb = juce::jmax (autoFloorDb, (float) (10.0 * std::log10 (programme + 1.0e-12)) + autoOffsetDb);

            const float reductionDb = getReductionDb (s, juce::Decibels::gainToDecibels ((float) envelope, -120.0f), thresholdDb);
            const double gain = juce::exactlyEqual (reductionDb, 0.0f) ? 1.0 : juce::Decibels::decibelsToGain ((double) -reductionDb);
            mostReduction = juce::jmax (mostReduction, reductionDb);

            lastOutputGain += outputStep;

            for (int ch = 0; ch < numChannels; ++ch)
            {
                const double x = data[ch][i];
                const double bellBand = bellK * bell[ch].tick (x).band;
                const double processed = s.mode == Mode::split ? x - (1.0 - gain) * bellBand : x * gain;
                const double heard = listenLow[ch].tick (listenHigh[ch].tick (x).high).low;

                double y = processed;
                if (s.listen == Listen::band)          y = heard;
                else if (s.listen == Listen::removed)  y = x - processed;

                data[ch][i] = (float) y * lastOutputGain;
                outPeak = juce::jmax (outPeak, std::abs (data[ch][i]));
            }
        }

        lastOutputGain = targetOutput;
        inputPeak.push (inPeak);
        outputPeak.push (outPeak);
        reduction.push (mostReduction);
        bandLevel.push ((float) mostLevel);
        thresholdNow.store (thresholdDb, std::memory_order_relaxed);
    }
}
