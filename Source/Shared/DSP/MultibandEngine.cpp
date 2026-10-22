#include "MultibandEngine.h"

namespace cdaw
{
    bool MultibandEngine::Settings::anySoloed() const
    {
        for (auto& b : bands)
            if (b.solo)
                return true;

        return false;
    }

    MultibandEngine::MultibandEngine()
    {
        preFifo = std::make_shared<AnalyserFifo>();
        postFifo = std::make_shared<AnalyserFifo>();
    }

    std::array<float, MultibandEngine::numBands - 1> MultibandEngine::orderCrossovers (std::array<float, numBands - 1> f)
    {
        // A third of an octave apart at least, in order, whatever the parameters say.
        constexpr float minRatio = 1.26f;

        for (int i = 1; i < numBands - 1; ++i)
            f[(size_t) i] = juce::jmax (f[(size_t) i], f[(size_t) i - 1] * minRatio);

        return f;
    }

    juce::Range<float> MultibandEngine::getBandRange (const Settings& settings, int band)
    {
        auto f = orderCrossovers (settings.crossovers);
        return { band == 0 ? 20.0f : f[(size_t) band - 1], band == numBands - 1 ? 20000.0f : f[(size_t) band] };
    }

    //==============================================================================
    void MultibandEngine::prepare (double rate, int maxBlockSize)
    {
        sampleRate = rate > 0.0 ? rate : 48000.0;
        preFifo->sampleRate = sampleRate;
        postFifo->sampleRate = sampleRate;

        for (auto& b : bandBuffers)
            b.setSize (2, juce::jmax (512, maxBlockSize), false, true, false);

        designedRate = 0.0;
        reset();
    }

    void MultibandEngine::reset()
    {
        for (auto& s : stages)
            for (auto* stage : { &s.lp1, &s.lp2, &s.hp1, &s.hp2, &s.ap[0], &s.ap[1] })
                stage->state = {};

        envelopeDb = {};
    }

    void MultibandEngine::designCrossovers (const std::array<float, numBands - 1>& f)
    {
        if (sampleRate == designedRate && f == designedFrequencies)
            return;

        for (int i = 0; i < numBands - 1; ++i)
        {
            auto x = EqMath::linkwitzRiley (f[(size_t) i], sampleRate);
            auto& s = stages[(size_t) i];
            s.lp1.coeffs = s.lp2.coeffs = x.lowPass;
            s.hp1.coeffs = s.hp2.coeffs = x.highPass;
            s.ap[0].coeffs = s.ap[1].coeffs = x.allPass;
        }

        designedRate = sampleRate;
        designedFrequencies = f;
    }

    double MultibandEngine::run (Stage& stage, int ch, double x) noexcept
    {
        auto& c = stage.coeffs;
        auto& s = stage.state[(size_t) ch];
        const double y = c.b0 * x + s.z1;
        s.z1 = c.b1 * x - c.a1 * y + s.z2;
        s.z2 = c.b2 * x - c.a2 * y;
        return y;
    }

    void MultibandEngine::process (juce::AudioBuffer<float>& buffer, int start, int numSamples, const Settings& settings)
    {
        const int numChannels = juce::jmin (2, buffer.getNumChannels());
        const int capacity = bandBuffers[0].getNumSamples();

        if (numChannels == 0 || numSamples <= 0 || capacity == 0)
            return;

        // In blocks no longer than the band buffers (some hosts send more than they announced).
        for (int done = 0; done < numSamples; done += capacity)
        {
            juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), numChannels, start + done, juce::jmin (capacity, numSamples - done));
            processChunk (view, settings);
        }
    }

    void MultibandEngine::processChunk (juce::AudioBuffer<float>& view, const Settings& settings)
    {
        const int numChannels = view.getNumChannels();
        const int n = view.getNumSamples();
        const double rate = sampleRate;

        preFifo->push (view);

        float inPeak[2] {};
        for (int ch = 0; ch < numChannels; ++ch)
            inPeak[ch] = view.getMagnitude (ch, 0, n);

        designCrossovers (orderCrossovers (settings.crossovers));

        // Split: band 0 = LP0, then AP1, AP2;  band 1 = LP1 (HP0), then AP2;  band 2 = LP2 (HP1 (HP0));  band 3 = HP2 (HP1 (HP0)).
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float* in = view.getReadPointer (ch);
            float* b0 = bandBuffers[0].getWritePointer (ch);
            float* b1 = bandBuffers[1].getWritePointer (ch);
            float* b2 = bandBuffers[2].getWritePointer (ch);
            float* b3 = bandBuffers[3].getWritePointer (ch);
            auto& x0 = stages[0];
            auto& x1 = stages[1];
            auto& x2 = stages[2];

            for (int i = 0; i < n; ++i)
            {
                const double x = in[i];
                double low = run (x0.lp2, ch, run (x0.lp1, ch, x));
                double rest = run (x0.hp2, ch, run (x0.hp1, ch, x));
                low = run (x2.ap[0], ch, run (x1.ap[0], ch, low));

                double lowMid = run (x1.lp2, ch, run (x1.lp1, ch, rest));
                rest = run (x1.hp2, ch, run (x1.hp1, ch, rest));
                lowMid = run (x2.ap[1], ch, lowMid);

                b0[i] = (float) low;
                b1[i] = (float) lowMid;
                b2[i] = (float) run (x2.lp2, ch, run (x2.lp1, ch, rest));
                b3[i] = (float) run (x2.hp2, ch, run (x2.hp1, ch, rest));
            }
        }

        // Compress each band (stereo-linked), then sum the audible ones.
        const bool solo = settings.anySoloed();
        view.clear();

        for (int b = 0; b < numBands; ++b)
        {
            auto& band = settings.bands[(size_t) b];
            auto& meters = bandMeters[(size_t) b];
            auto& buf = bandBuffers[(size_t) b];
            float* l = buf.getWritePointer (0);
            float* r = numChannels > 1 ? buf.getWritePointer (1) : nullptr;

            const bool active = band.on;
            const double makeUp = active ? band.gain : 0.0;
            const double attack = std::exp (-1.0 / (juce::jmax (0.05, (double) band.attack) * 0.001 * rate));
            const double release = std::exp (-1.0 / (juce::jmax (1.0, (double) band.release) * 0.001 * rate));
            double& env = envelopeDb[(size_t) b];
            float bandIn = 0.0f, bandOut = 0.0f;
            double most = 0.0;

            for (int i = 0; i < n; ++i)
            {
                const float detector = juce::jmax (std::abs (l[i]), r != nullptr ? std::abs (r[i]) : 0.0f);
                bandIn = juce::jmax (bandIn, detector);

                double target = 0.0;
                if (active)
                {
                    const float inDb = detector > 1.0e-6f ? 20.0f * std::log10 (detector) : -120.0f;
                    target = transfer (inDb, band.threshold, band.ratio) - inDb;
                }

                env = target < env ? attack * env + (1.0 - attack) * target : release * env + (1.0 - release) * target;
                if (env > -1.0e-6)
                    env = 0.0;

                most = juce::jmin (most, env);
                const float g = (float) std::pow (10.0, (env + makeUp) / 20.0);
                l[i] *= g;
                if (r != nullptr) r[i] *= g;
                bandOut = juce::jmax (bandOut, juce::jmax (std::abs (l[i]), r != nullptr ? std::abs (r[i]) : 0.0f));
            }

            meters.inputPeak.push (bandIn);
            meters.outputPeak.push (bandOut);
            meters.reduction.push ((float) -most);

            if (! solo || band.solo)
                for (int ch = 0; ch < numChannels; ++ch)
                    view.addFrom (ch, 0, buf, ch, 0, n);
        }

        const float outGain = juce::Decibels::decibelsToGain (settings.output);
        for (int ch = 0; ch < numChannels; ++ch)
            view.applyGainRamp (ch, 0, n, lastOutputGain, outGain);
        lastOutputGain = outGain;

        for (int ch = 0; ch < 2; ++ch)
        {
            inputPeak[ch].push (inPeak[juce::jmin (ch, numChannels - 1)]);
            outputPeak[ch].push (view.getMagnitude (juce::jmin (ch, numChannels - 1), 0, n));
        }

        postFifo->push (view);
    }
}
