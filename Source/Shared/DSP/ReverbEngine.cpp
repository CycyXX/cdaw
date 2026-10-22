#include "ReverbEngine.h"

namespace cdaw
{
    namespace
    {
        // Delay network line lengths at 48 kHz (mutually prime, ~32-64 ms), scaled by size.
        constexpr int baseLengths[8] { 1559, 1867, 2053, 2251, 2399, 2617, 2851, 3089 };
        constexpr double lfoRates[8] { 0.13, 0.21, 0.29, 0.37, 0.17, 0.27, 0.33, 0.41 };

        // Input diffusers (ms), slightly different per side for a wide image.
        constexpr double diffuserMs[2][4] { { 4.77, 3.59, 12.73, 9.30 }, { 5.11, 3.83, 13.61, 9.97 } };

        // Early reflections after the pre-delay (ms at full size), per side.
        constexpr double tapMs[2][10] { { 3.1, 7.9, 11.6, 17.3, 23.8, 31.1, 38.7, 47.9, 58.2, 69.4 },
                                        { 4.4, 9.2, 13.8, 19.6, 26.1, 33.9, 41.2, 51.3, 61.7, 73.0 } };

        constexpr double maxPreDelayMs = 250.0, maxTapMs = 80.0;
        constexpr double maxSizeScale = 1.6, maxModulationSamples48k = 10.0;

        double sizeScale (double size)              { return 0.35 + 1.25 * size; }
        double tapScale (double size)               { return 0.4 + 0.9 * size; }
    }

    double ReverbEngine::getDecayAt (const Settings& settings, double hz)
    {
        // Same split as the network: lows below 250 Hz, highs above the damping frequency.
        const double rt = settings.decay;
        const double wLow = 1.0 / (1.0 + std::pow (hz / lowCrossover, 2.0));
        const double wHigh = 1.0 / (1.0 + std::pow (settings.dampFreq / hz, 2.0));
        const double wMid = juce::jmax (0.0, 1.0 - wLow - wHigh);
        const double multiplier = (wLow * settings.lowDecay + wHigh * settings.highDecay + wMid) / (wLow + wHigh + wMid);
        return rt * multiplier;
    }

    double ReverbEngine::getCutsDb (const Settings& settings, double hz, bool post, double rate)
    {
        const double sr = rate;
        const float lo = (post ? settings.postLowCut : settings.preLowCut);
        const float hi = (post ? settings.postHighCut : settings.preHighCut);
        double db = 0.0;

        if (lo > lowCutOff + 0.5f)
            db += EqMath::design (EqMath::BandType::lowCut, 1, lo, 0.0, 0.7071, sr).magnitudeDb (hz, sr);
        if (hi < highCutOff - 1.0f)
            db += EqMath::design (EqMath::BandType::highCut, 1, hi, 0.0, 0.7071, sr).magnitudeDb (hz, sr);

        return db;
    }

    std::vector<ReverbEngine::Tap> ReverbEngine::getEarlyTaps (const Settings& settings)
    {
        std::vector<Tap> taps;
        const double scale = tapScale (settings.size);
        const float level = settings.early;

        for (int side = 0; side < 2; ++side)
            for (int k = 0; k < numTaps; ++k)
                taps.push_back ({ tapMs[side][k] * scale * 0.001, level * std::pow (0.8f, (float) k) });

        return taps;
    }

    //==============================================================================
    void ReverbEngine::prepare (double sampleRate, int blockSize, const Settings& settings)
    {
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        wetFifo->sampleRate = rate;
        const double k = rate / 48000.0;

        for (auto& d : preDelayLine)
            d.setMaxSamples ((int) std::ceil ((maxPreDelayMs + maxTapMs * tapScale (1.0)) * 0.001 * rate) + 4);

        for (int side = 0; side < 2; ++side)
            for (int i = 0; i < numDiffusers; ++i)
                diffusers[side][i].setMaxSamples ((int) std::ceil (diffuserMs[side][i] * 0.001 * rate) + 2);

        for (int i = 0; i < numLines; ++i)
        {
            lines[i].setMaxSamples ((int) std::ceil (baseLengths[i] * maxSizeScale * k + maxModulationSamples48k * k) + 8);
            lineLength[i] = baseLengths[i] * sizeScale (settings.size) * k;
        }

        wetBuffer.setSize (2, juce::jmax (512, blockSize), false, true, false);
        inputBuffer.setSize (2, juce::jmax (512, blockSize), false, true, false);
        for (auto& c : designedCuts)
            c = -1.0f;

        reset();
    }

    void ReverbEngine::reset()
    {
        for (auto& d : preDelayLine) d.clear();
        for (auto& side : diffusers) for (auto& d : side) d.clear();
        for (auto& d : lines) d.clear();

        std::fill (std::begin (lowState), std::end (lowState), 0.0f);
        std::fill (std::begin (lfoPhase), std::end (lfoPhase), 0.0);
        lastWetPeak = 0.0f;
        silentSeconds = 1.0e9;      // nothing in the network yet
        std::fill (std::begin (dampState), std::end (dampState), 0.0f);

        for (auto* f : { &preLow, &preHigh, &postLow, &postHigh })
            for (auto& ch : f->z)
                ch[0] = ch[1] = 0.0;
    }

    void ReverbEngine::updateFilters (const Settings& settings)
    {
        const float now[4] { settings.preLowCut, settings.preHighCut, settings.postLowCut, settings.postHighCut };
        Filter* filters[4] { &preLow, &preHigh, &postLow, &postHigh };

        for (int i = 0; i < 4; ++i)
        {
            if (now[i] == designedCuts[i])
                continue;

            const bool lowCut = (i % 2) == 0;
            auto& f = *filters[i];
            const bool wasActive = f.active;
            f.active = lowCut ? now[i] > lowCutOff + 0.5f : now[i] < highCutOff - 1.0f;

            if (f.active)
                f.c = EqMath::design (lowCut ? EqMath::BandType::lowCut : EqMath::BandType::highCut, 1, now[i], 0.0, 0.7071, rate).sections[0];

            if (f.active && ! wasActive)
                for (auto& ch : f.z)
                    ch[0] = ch[1] = 0.0;

            designedCuts[i] = now[i];
        }
    }

    void ReverbEngine::process (juce::AudioBuffer<float>& buffer, int start, int n, const Settings& settings)
    {
        if (n <= 0 || lines[0].data.size() < 8)
            return;

        juce::ScopedNoDenormals noDenormals;
        const int numChannels = buffer.getNumChannels();

        if (numChannels == 0)
            return;

        // A block longer than the one prepared for (some offline renders): process it in pieces
        // rather than passing it through dry.
        if (n > wetBuffer.getNumSamples())
        {
            for (int done = 0; done < n;)
            {
                const int chunk = juce::jmin (n - done, wetBuffer.getNumSamples());
                process (buffer, start + done, chunk, settings);
                done += chunk;
            }

            return;
        }

        // Nothing has come in for longer than the longest possible tail: nothing to do. Skipping
        // also holds the modulation, so the reverb starts the same way whenever sound arrives,
        // however long the host ran it on silence before (otherwise two hosts give slightly
        // different tails).
        {
            float inputPeak = 0.0f;
            for (int ch = 0; ch < juce::jmin (2, numChannels); ++ch)
                inputPeak = juce::jmax (inputPeak, buffer.getMagnitude (ch, start, n));

            silentSeconds = inputPeak == 0.0f ? silentSeconds + n / rate : 0.0;
            const double longestTail = settings.preDelay * 0.001 + 0.15
                                       + 1.5 * settings.decay * juce::jmax (1.0f, settings.lowDecay, settings.highDecay);

            if (silentSeconds > longestTail && lastWetPeak < 1.0e-6f && ! settings.freeze)
            {
                lastDry = std::cos (settings.mix * juce::MathConstants<float>::halfPi);
                lastWet = std::sin (settings.mix * juce::MathConstants<float>::halfPi);
                dryPeak.push (0.0f);
                wetPeak.push (0.0f);
                return;
            }
        }

        updateFilters (settings);

        const double k = rate / 48000.0;
        const bool frozen = settings.freeze;
        const double rt = juce::jmax (0.05f, settings.decay);
        const double lowMult = settings.lowDecay, highMult = settings.highDecay;
        const float lowCoeff = (float) (1.0 - std::exp (-2.0 * EqMath::pi * lowCrossover / rate));
        const float dampCoeff = (float) (1.0 - std::exp (-2.0 * EqMath::pi * juce::jmin ((double) settings.dampFreq, rate * 0.45) / rate));
        const double targetScale = sizeScale (settings.size) * k;
        const double modDepth = settings.modulation * maxModulationSamples48k * k;
        const float diffusionGain = 0.75f * settings.diffusion;
        const int preDelaySamples = (int) (settings.preDelay * 0.001 * rate);
        const float earlyLevel = settings.early;
        const double taps = tapScale (settings.size) * 0.001 * rate;
        const float widthValue = settings.width;
        constexpr float injection = 0.35f;
        constexpr float lineOutput = 0.7f;      // about -3 dB against the dry signal at the default settings

        // Per-line loss for lows / mids / highs (one pass round the loop: 60 dB in RT60 seconds).
        float gainLow[numLines], gainMid[numLines], gainHigh[numLines];
        for (int i = 0; i < numLines; ++i)
        {
            const double secondsPerPass = lineLength[i] / rate;
            gainMid[i]  = frozen ? 1.0f : (float) std::pow (10.0, -3.0 * secondsPerPass / rt);
            gainLow[i]  = frozen ? 1.0f : (float) std::pow (10.0, -3.0 * secondsPerPass / (rt * lowMult));
            gainHigh[i] = frozen ? 1.0f : (float) std::pow (10.0, -3.0 * secondsPerPass / (rt * highMult));
        }

        float* outL = buffer.getWritePointer (0, start);
        float* outR = numChannels > 1 ? buffer.getWritePointer (1, start) : nullptr;
        float* wetL = wetBuffer.getWritePointer (0);
        float* wetR = wetBuffer.getWritePointer (1);
        float* networkIn[2] { inputBuffer.getWritePointer (0), inputBuffer.getWritePointer (1) };
        float dryMax = 0.0f;

        for (int s = 0; s < n; ++s)
            dryMax = juce::jmax (dryMax, std::abs (outL[s]), outR != nullptr ? std::abs (outR[s]) : 0.0f);

        // 1. Per channel over the block: pre cuts, pre-delay, early reflections (into the wet
        //    buffer) and diffusion (into the network's input).
        for (int ch = 0; ch < 2; ++ch)
        {
            const float* dry = ch == 0 || outR == nullptr ? outL : outR;
            float* early = ch == 0 ? wetL : wetR;
            auto& preDelayed = preDelayLine[ch];

            for (int s = 0; s < n; ++s)
            {
                float x = dry[s];
                if (preLow.active)  x = preLow.process (ch, x);
                if (preHigh.active) x = preHigh.process (ch, x);
                preDelayed.push (frozen ? 0.0f : x);

                float reflections = 0.0f;
                if (earlyLevel > 0.0f)
                {
                    float g = 1.0f;
                    for (int t = 0; t < numTaps; ++t, g *= 0.8f)
                        reflections += ((t & 1) ? -g : g) * preDelayed.read (preDelaySamples + (int) (tapMs[ch][t] * taps));
                }
                early[s] = reflections * earlyLevel * 0.45f;

                // Diffusion: allpasses smear the input into a dense wash before the network.
                float y = preDelayed.read (preDelaySamples);
                for (auto& diffuser : diffusers[ch])
                    y = diffuser.allPass (y, diffusionGain);

                networkIn[ch][s] = y;
            }
        }

        // 2. The feedback delay network, sample by sample; its output is added to the early reflections.
        constexpr float hadamardScale = 0.35355339f;    // 1 / sqrt (8)
        const float injectionGain = frozen ? 0.0f : injection;

        for (int s = 0; s < n; ++s)
        {
            float v[numLines], f[numLines];

            for (int i = 0; i < numLines; ++i)
            {
                lineLength[i] += (baseLengths[i] * targetScale - lineLength[i]) * 0.0005;      // glide on size changes
                lfoPhase[i] += lfoRates[i] / rate;
                if (lfoPhase[i] >= 1.0)
                    lfoPhase[i] -= 1.0;

                const double modulationOffset = modDepth > 0.0 ? modDepth * (0.5 + 0.5 * std::sin (2.0 * EqMath::pi * lfoPhase[i])) : 0.0;
                const double readPos = juce::jlimit (1.0, (double) lines[i].data.size() - 3.0, lineLength[i] + modulationOffset);
                v[i] = lines[i].readFractional (readPos);

                // Split into lows / mids / highs and give each its own decay.
                lowState[i] += lowCoeff * (v[i] - lowState[i]);
                dampState[i] += dampCoeff * (v[i] - dampState[i]);
                const float lowPart = lowState[i], highPart = v[i] - dampState[i], midPart = dampState[i] - lowState[i];
                f[i] = frozen ? v[i] : gainLow[i] * lowPart + gainMid[i] * midPart + gainHigh[i] * highPart;
            }

            hadamard (f);

            for (int i = 0; i < numLines; ++i)
            {
                const float input = networkIn[i < 4 ? 0 : 1][s] * ((i & 1) ? -injectionGain : injectionGain);
                lines[i].push (f[i] * hadamardScale + input);
            }

            wetL[s] += lineOutput * (v[0] - v[2] + v[4] - v[6]);
            wetR[s] += lineOutput * (v[1] - v[3] + v[5] - v[7]);
        }

        // 3. Per channel over the block: width (mid / side), then the post cuts.
        for (int s = 0; s < n; ++s)
        {
            const float mid = 0.5f * (wetL[s] + wetR[s]), side = 0.5f * (wetL[s] - wetR[s]) * widthValue;
            wetL[s] = mid + side;
            wetR[s] = mid - side;
        }

        for (int ch = 0; ch < 2; ++ch)
        {
            float* wet = ch == 0 ? wetL : wetR;
            for (int s = 0; s < n; ++s)
            {
                if (postLow.active)  wet[s] = postLow.process (ch, wet[s]);
                if (postHigh.active) wet[s] = postHigh.process (ch, wet[s]);
            }
        }

        {
            juce::AudioBuffer<float> wetView (wetBuffer.getArrayOfWritePointers(), 2, 0, n);
            wetFifo->push (wetView);
        }

        // Equal-power mix, ramped across the block.
        const float m = settings.mix;
        const float dryGain = std::cos (m * juce::MathConstants<float>::halfPi);
        const float wetGain = std::sin (m * juce::MathConstants<float>::halfPi);
        float wetMax = 0.0f;

        for (int s = 0; s < n; ++s)
        {
            const float t = (float) (s + 1) / (float) n;
            const float dg = lastDry + (dryGain - lastDry) * t, wg = lastWet + (wetGain - lastWet) * t;
            wetMax = juce::jmax (wetMax, std::abs (wetL[s]), std::abs (wetR[s]));

            const float dryL = outL[s], dryR = outR != nullptr ? outR[s] : dryL;
            outL[s] = dryL * dg + wetL[s] * wg;
            if (outR != nullptr)
                outR[s] = dryR * dg + wetR[s] * wg;
            else
                outL[s] = dryL * dg + 0.5f * (wetL[s] + wetR[s]) * wg;
        }

        lastDry = dryGain;
        lastWet = wetGain;
        lastWetPeak = wetMax;
        dryPeak.push (dryMax);
        wetPeak.push (wetMax * wetGain);
    }
}
