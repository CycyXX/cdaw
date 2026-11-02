#include "DelayEngine.h"
#include "TempoSync.h"

namespace cdaw
{
    namespace
    {
        constexpr double twoPi = juce::MathConstants<double>::twoPi;

        // Wobble at full: a slow wow, a faster flutter and a wandering drift (ms of delay change).
        constexpr double wowHz = 0.55, flutterHz = 6.3, wowMs = 2.2, flutterMs = 0.12, driftMs = 0.9;

        bool lowCutOn (const DelayEngine::Settings& s)    { return s.lowCut > DelayEngine::lowCutOff + 0.5f; }
        bool highCutOn (const DelayEngine::Settings& s)   { return s.highCut < DelayEngine::highCutOff - 1.0f; }
    }

    float DelayEngine::saturate (float x, float drive) noexcept
    {
        // Clean up to full scale, then a soft limit (so a full feedback loop stays bounded).
        const float a = std::abs (x);
        const float clean = a <= 1.0f ? x : std::copysign (1.0f + 0.5f * softClip (2.0f * (a - 1.0f)), x);

        if (drive <= 0.0f)
            return clean;

        // Drive blends in a saturator that rounds off loud repeats earlier and earlier.
        const float k = 1.0f + 4.0f * drive;
        return clean + drive * (2.0f / k * softClip (x * k * 0.5f) - clean);
    }

    void DelayEngine::prepare (double sampleRate, int)
    {
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;

        // Room for the longest time, stretched by spread, plus wobble.
        const int maxSamples = (int) std::ceil ((maxSeconds * 1.5 + 0.02) * rate);
        for (auto& line : lines)
            line.setMaxSamples (maxSamples);

        linesAreClear = true;

        for (auto& d : delaySamples)
            d.reset (rate, 0.12);

        wetGain.reset (rate, 0.02);
        dryGain.reset (rate, 0.02);
        duckGain.reset (rate, 0.04);
        designedCuts[0] = designedCuts[1] = -1.0f;
        reset();
    }

    void DelayEngine::reset()
    {
        // Megabytes, and the synths call this on the audio thread when their delay is switched on.
        if (! linesAreClear)
            for (auto& line : lines)
                line.clear();

        linesAreClear = true;
        lowCut.clear();
        highCut.clear();
        envelope = 0.0f;
        duckGain.value = 1.0f;
        primed = false;
        drift[0] = drift[1] = driftTarget[0] = driftTarget[1] = 0.0f;
    }

    //==============================================================================
    double DelayEngine::getTimeSeconds (const Settings& s, double bpm, int channel)
    {
        const double base = juce::jlimit (0.001, maxSeconds, s.sync ? TempoSync::toSeconds (juce::jmin (s.division, TempoSync::numDelayDivisions - 1), bpm)
                                                                    : s.timeMs * 0.001);
        return channel == 0 ? base : base * (1.0 + 0.5 * juce::jlimit (-1.0f, 1.0f, s.spread));
    }

    double DelayEngine::getLoopFilterDb (const Settings& s, double hz, double sampleRate)
    {
        double db = 0.0;
        const double w0Low = juce::jlimit (10.0, sampleRate * 0.45, (double) s.lowCut);
        const double w0High = juce::jlimit (10.0, sampleRate * 0.45, (double) s.highCut);

        if (lowCutOn (s))
        {
            EqMath::BandFilter f;
            f.sections[0] = EqMath::detail::highPass (2.0 * EqMath::pi * w0Low / sampleRate, 0.7071);
            f.numSections = 1;
            db += f.magnitudeDb (hz, sampleRate);
        }

        if (highCutOn (s))
        {
            EqMath::BandFilter f;
            f.sections[0] = EqMath::detail::lowPass (2.0 * EqMath::pi * w0High / sampleRate, 0.7071);
            f.numSections = 1;
            db += f.magnitudeDb (hz, sampleRate);
        }

        return db;
    }

    std::vector<DelayEngine::Echo> DelayEngine::getEchoes (const Settings& s, double bpm, double sampleRate, float floorDb, int maxEchoes)
    {
        std::vector<Echo> echoes;
        const double passDb = getLoopFilterDb (s, 1000.0, sampleRate);
        const double feedbackDb = s.feedback > 0.0001f ? 20.0 * std::log10 ((double) s.feedback) : -200.0;
        const double left = getTimeSeconds (s, bpm, 0), right = getTimeSeconds (s, bpm, 1);

        auto level = [&] (int passes) { return (float) ((passes - 1) * feedbackDb + passes * passDb); };

        if (s.mode == Mode::pingPong)
        {
            // Left, right, left...: the times add up alternately.
            double t = 0.0;
            for (int k = 1; k <= maxEchoes; ++k)
            {
                t += (k % 2 == 1) ? left : right;
                auto db = level (k);
                if (db < floorDb || t > 60.0)
                    break;
                echoes.push_back ({ t, (k % 2 == 1) ? 0 : 1, db, k });
            }
        }
        else
        {
            const bool same = std::abs (left - right) < 1.0e-4;

            for (int k = 1; k <= maxEchoes; ++k)
            {
                auto db = level (k);
                if (db < floorDb || k * left > 60.0)
                    break;

                if (same)
                {
                    echoes.push_back ({ k * left, 2, db, k });
                }
                else
                {
                    echoes.push_back ({ k * left, 0, db, k });
                    echoes.push_back ({ k * right, 1, db, k });
                }
            }

            std::sort (echoes.begin(), echoes.end(), [] (const Echo& a, const Echo& b) { return a.seconds < b.seconds; });
        }

        return echoes;
    }

    double DelayEngine::getTailSeconds (const Settings& s, double bpm)
    {
        const double longest = juce::jmax (getTimeSeconds (s, bpm, 0), getTimeSeconds (s, bpm, 1));
        if (s.feedback < 0.01f)
            return longest * 2.0;
        if (s.feedback >= 0.999f)
            return 30.0;

        const double repeats = std::log (0.001) / std::log ((double) s.feedback);
        return juce::jmin (30.0, longest * (repeats + 1.0) * (s.mode == Mode::pingPong ? 2.0 : 1.0));
    }

    //==============================================================================
    void DelayEngine::updateFilters (const Settings& s)
    {
        if (designedCuts[0] != s.lowCut)
        {
            designedCuts[0] = s.lowCut;
            const bool wasActive = lowCut.active;
            lowCut.active = lowCutOn (s);
            lowCut.c = EqMath::detail::highPass (2.0 * EqMath::pi * juce::jlimit (10.0, rate * 0.45, (double) s.lowCut) / rate, 0.7071);
            if (lowCut.active && ! wasActive)
                lowCut.clear();
        }

        if (designedCuts[1] != s.highCut)
        {
            designedCuts[1] = s.highCut;
            const bool wasActive = highCut.active;
            highCut.active = highCutOn (s);
            highCut.c = EqMath::detail::lowPass (2.0 * EqMath::pi * juce::jlimit (10.0, rate * 0.45, (double) s.highCut) / rate, 0.7071);
            if (highCut.active && ! wasActive)
                highCut.clear();
        }
    }

    void DelayEngine::process (juce::AudioBuffer<float>& buffer, int start, int n, const Settings& s, double bpm)
    {
        const int numChannels = juce::jmin (2, buffer.getNumChannels());
        if (numChannels == 0 || n <= 0)
            return;

        updateFilters (s);

        float* left = buffer.getWritePointer (0, start);
        float* right = numChannels > 1 ? buffer.getWritePointer (1, start) : nullptr;

        const float targetDelay[2] { (float) (getTimeSeconds (s, bpm, 0) * rate), (float) (getTimeSeconds (s, bpm, 1) * rate) };
        if (! primed)
        {
            // The first block starts at the set time (no glide from zero).
            delaySamples[0].value = targetDelay[0];
            delaySamples[1].value = targetDelay[1];
            dryGain.value = juce::jmin (1.0f, 2.0f * (1.0f - s.mix));
            wetGain.value = juce::jmin (1.0f, 2.0f * s.mix);
            primed = true;
        }

        const float feedback = juce::jlimit (0.0f, 1.0f, s.feedback);
        const float drive = juce::jlimit (0.0f, 1.0f, s.drive);
        const float width = juce::jlimit (0.0f, 2.0f, s.width);
        const float dryTarget = juce::jmin (1.0f, 2.0f * (1.0f - s.mix)), wetTarget = juce::jmin (1.0f, 2.0f * s.mix);
        const bool pingPong = s.mode == Mode::pingPong && right != nullptr;

        // Wobble: wow, flutter and drift, in samples of delay.
        const double wobble = juce::jlimit (0.0f, 1.0f, s.wobble);
        const double wowDepth = wobble * wowMs * 0.001 * rate, flutterDepth = wobble * flutterMs * 0.001 * rate;
        const double wowStep = twoPi * wowHz / rate, flutterStep = twoPi * flutterHz / rate;

        // Ducking: an envelope on the dry input pulls the echoes down while it plays.
        const float envAttack = (float) std::exp (-1.0 / (0.002 * rate)), envRelease = (float) std::exp (-1.0 / (0.25 * rate));
        const float duck = juce::jlimit (0.0f, 1.0f, s.duck);
        float lowestDuckGain = 1.0f;
        linesAreClear = false;

        float inPeak[2] {}, outPeak[2] {};

        for (int i = 0; i < n; ++i)
        {
            const float xL = left[i];
            const float xR = right != nullptr ? right[i] : xL;

            // A new drift target now and then; drift glides towards it.
            if (--driftCounter <= 0)
            {
                driftCounter = (int) (rate * 0.25);
                for (auto& t : driftTarget)
                    t = (float) (wobble * driftMs * 0.001 * rate) * (random.nextFloat() * 2.0f - 1.0f);
            }

            float echo[2];
            for (int ch = 0; ch < 2; ++ch)
            {
                drift[ch] += (driftTarget[ch] - drift[ch]) * 0.00005f;
                const double phaseOffset = ch == 0 ? 0.0 : 0.9;
                const double mod = wobble > 0.0 ? wowDepth * (1.0 + std::sin (wowPhase + phaseOffset)) + flutterDepth * (1.0 + std::sin (flutterPhase + phaseOffset * 1.7)) + drift[ch]
                                                : 0.0;
                const double d = delaySamples[ch].next (targetDelay[ch]) + mod;
                float e = lines[ch].tap (d);

                if (lowCut.active)   e = lowCut.process (ch, e);
                if (highCut.active)  e = highCut.process (ch, e);
                echo[ch] = e;
            }

            wowPhase += wowStep;
            flutterPhase += flutterStep;
            if (wowPhase > twoPi) wowPhase -= twoPi;
            if (flutterPhase > twoPi) flutterPhase -= twoPi;

            if (pingPong)
            {
                lines[0].push (saturate (0.5f * (xL + xR) + feedback * echo[1], drive));
                lines[1].push (saturate (feedback * echo[0], drive));
            }
            else
            {
                lines[0].push (saturate (xL + feedback * echo[0], drive));
                lines[1].push (saturate (xR + feedback * echo[1], drive));
            }

            // Ducking.
            const float level = juce::jmax (std::abs (xL), std::abs (xR));
            envelope = level > envelope ? level + (envelope - level) * envAttack : level + (envelope - level) * envRelease;
            float duckTarget = 1.0f;
            if (duck > 0.0f)
            {
                const float envDb = juce::Decibels::gainToDecibels (envelope, -100.0f);
                const float reductionDb = duck * 30.0f * juce::jlimit (0.0f, 1.0f, (envDb + 42.0f) / 36.0f);
                duckTarget = juce::Decibels::decibelsToGain (-reductionDb);
            }
            const float duckNow = duckGain.next (duckTarget);
            lowestDuckGain = juce::jmin (lowestDuckGain, duckNow);

            // Width of the echoes, then the mix.
            const float mid = 0.5f * (echo[0] + echo[1]), side = 0.5f * (echo[0] - echo[1]) * width;
            const float wet = wetGain.next (wetTarget) * duckNow;
            const float dry = dryGain.next (dryTarget);
            const float wL = (mid + side) * wet, wR = (mid - side) * wet;

            inPeak[0] = juce::jmax (inPeak[0], std::abs (xL));
            inPeak[1] = juce::jmax (inPeak[1], std::abs (xR));
            outPeak[0] = juce::jmax (outPeak[0], std::abs (wL));
            outPeak[1] = juce::jmax (outPeak[1], std::abs (wR));

            if (right != nullptr)
            {
                left[i] = dry * xL + wL;
                right[i] = dry * xR + wR;
            }
            else
            {
                left[i] = dry * xL + 0.5f * (wL + wR);
            }
        }

        // Keep the loop's filters out of denormals in silence.
        for (auto* f : { &lowCut, &highCut })
            for (auto& z : f->z)
                for (auto& v : z)
                    if (std::abs (v) < 1.0e-20)
                        v = 0.0;

        for (int ch = 0; ch < 2; ++ch)
        {
            inputPeak[ch].push (inPeak[ch]);
            wetPeak[ch].push (outPeak[ch]);
        }

        duckReduction.push (-juce::Decibels::gainToDecibels (lowestDuckGain, -100.0f));
    }
}
