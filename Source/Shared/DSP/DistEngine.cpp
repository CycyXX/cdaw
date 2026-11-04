#include "DistEngine.h"

namespace cdaw
{
    namespace
    {
        constexpr double pi = juce::MathConstants<double>::pi;
        constexpr double twoPi = juce::MathConstants<double>::twoPi;

        using Voicing = DistEngine::Voicing;
        using Curve = DistEngine::Curve;

        // name, stages, gain range (dB), curve, bias, Character's bias, pre-emphasis (Hz, dB, Q),
        // own high pass, low pass between stages, post EQ (Hz, dB, Q), post low pass, sag.
        const DistEngine::Profile profiles[DistEngine::numVoicings] {
            { "Boost",  1,  0.0f, 24.0f, Curve::soft,  0.05f, 0.35f, 2500.0f, 3.0f, 0.7f,   0.0f, 18000.0f, 1000.0f,  0.0f, 1.0f, 18000.0f, 0.04f },
            { "Crunch", 2,  6.0f, 40.0f, Curve::tube,  0.15f, 0.50f,  900.0f, 4.0f, 0.8f,   0.0f,  7000.0f, 2200.0f,  2.0f, 1.0f, 10000.0f, 0.12f },
            { "Lead",   2, 12.0f, 52.0f, Curve::diode, 0.02f, 0.15f,  720.0f, 9.0f, 0.7f,   0.0f,  5000.0f, 3000.0f,  0.0f, 1.0f,  7500.0f, 0.08f },
            { "Modern", 3, 18.0f, 62.0f, Curve::hard,  0.02f, 0.15f, 1400.0f, 5.0f, 0.9f, 120.0f,  6500.0f,  600.0f, -5.0f, 0.8f,  8500.0f, 0.06f },
            { "Fuzz",   2, 24.0f, 60.0f, Curve::fuzz,  0.12f, 0.40f,  250.0f, 3.0f, 0.6f,   0.0f,  4500.0f, 1000.0f, -6.0f, 0.6f,  6000.0f, 0.20f },
            { "Bass",   2, 10.0f, 48.0f, Curve::diode, 0.08f, 0.40f, 1200.0f, 4.0f, 0.8f,   0.0f,  6000.0f, 2000.0f,  3.0f, 1.0f,  7000.0f, 0.08f },
        };

        double raw (Curve curve, double v) noexcept
        {
            switch (curve)
            {
                case Curve::tube:   return v >= 0.0 ? std::tanh (v) : 0.75 * std::tanh (v / 0.75);
                case Curve::diode:  return v / std::sqrt (1.0 + v * v);
                case Curve::hard:
                {
                    const double a = std::abs (v);
                    return a < 0.8 ? v : std::copysign (0.8 + 0.2 * std::tanh ((a - 0.8) / 0.2), v);
                }
                case Curve::fuzz:   return v >= 0.0 ? 0.55 * std::tanh (v / 0.55) : std::tanh (v);
                case Curve::soft:
                default:            return std::tanh (v);
            }
        }

        double logCosh (double v) noexcept
        {
            const double a = std::abs (v);
            return a + std::log1p (std::exp (-2.0 * a)) - 0.69314718055994531;
        }

        /** Each curve's antiderivative (zero at zero), for the anti-aliased stages. */
        double antiderivative (Curve curve, double v) noexcept
        {
            switch (curve)
            {
                case Curve::tube:   return v >= 0.0 ? logCosh (v) : 0.5625 * logCosh (v / 0.75);
                case Curve::diode:  return std::sqrt (1.0 + v * v) - 1.0;
                case Curve::hard:
                {
                    const double a = std::abs (v);
                    return a < 0.8 ? 0.5 * a * a : 0.32 + 0.8 * (a - 0.8) + 0.04 * logCosh ((a - 0.8) / 0.2);
                }
                case Curve::fuzz:   return v >= 0.0 ? 0.3025 * logCosh (v / 0.55) : logCosh (v);
                case Curve::soft:
                default:            return logCosh (v);
            }
        }

        double starved (double u, float starve) noexcept
        {
            return starve > 0.0f ? std::copysign (juce::jmax (0.0, std::abs (u) - starve), u) : u;
        }

        /** How the stages are biased and scaled for a signal of `level` arriving at the drive:
            the bias is a share of each stage's own input level (so the clipping stays uneven
            however hard it's driven), and playing harder moves it further and lowers the gain (sag). */
        struct Operating
        {
            float gain = 1.0f, shift = 0.0f;
            float bias[DistEngine::maxStages] {};
        };

        Operating operatingPoint (const DistEngine::Profile& p, float stageGain, float share, float level) noexcept
        {
            Operating o;
            o.shift = std::tanh (level * stageGain);
            o.gain = stageGain / (1.0f + 2.0f * p.sag * o.shift);
            const float fraction = juce::jlimit (0.0f, 0.6f, share + p.sag * o.shift);
            float amplitude = level * o.gain;
            for (int st = 0; st < p.stages; ++st)
            {
                o.bias[st] = fraction * amplitude;
                amplitude = juce::jmin (amplitude, 1.0f) * o.gain;
            }
            return o;
        }

        double besselI0 (double x)
        {
            double sum = 1.0, term = 1.0;
            for (int k = 1; k < 32; ++k)
            {
                term *= (x / (2.0 * k)) * (x / (2.0 * k));
                sum += term;
            }
            return sum;
        }

        double w (double hz, double sampleRate)    { return twoPi * juce::jlimit (5.0, sampleRate * 0.45, hz) / sampleRate; }

        float bias (const DistEngine::Settings& s)
        {
            const auto& p = DistEngine::profile (s.voicing);
            return p.asymmetry + juce::jlimit (0.0f, 1.0f, s.character) * p.characterBias;
        }

        /** Fuzz's starve: the input level (at the drive) under which nothing gets through, up to
            -34 dB at full Character, so the attack plays and the tail sputters out. */
        float starveOf (const DistEngine::Settings& s)
        {
            return s.voicing == Voicing::fuzz ? juce::square (juce::jlimit (0.0f, 1.0f, s.character)) * 0.02f : 0.0f;
        }
    }

    const DistEngine::Profile& DistEngine::profile (Voicing v)
    {
        return profiles[juce::jlimit (0, numVoicings - 1, (int) v)];
    }

    juce::StringArray DistEngine::getVoicingNames()
    {
        juce::StringArray names;
        for (auto& p : profiles)
            names.add (p.name);
        return names;
    }

    //==============================================================================
    void DistEngine::HalfBand::design (int length, double beta)
    {
        taps.assign ((size_t) length, 0.0f);
        centre = (length - 1) / 2;
        double sum = 0.0;
        for (int n = 0; n < length; ++n)
        {
            const double x = (n - centre) * 0.5;
            const double sinc = n == centre ? 1.0 : std::sin (pi * x) / (pi * x);
            const double r = (double) (n - centre) / centre;
            const double window = besselI0 (beta * std::sqrt (juce::jmax (0.0, 1.0 - r * r))) / besselI0 (beta);
            taps[(size_t) n] = (float) (sinc * window);
            sum += taps[(size_t) n];
        }

        nonZero.clear();
        for (int n = 0; n < length; ++n)
        {
            taps[(size_t) n] = (float) (taps[(size_t) n] / sum);
            if (std::abs (taps[(size_t) n]) > 1.0e-9f)
                nonZero.push_back (n);
        }
    }

    float DistEngine::HalfBand::filter (const State& s) const noexcept
    {
        float sum = 0.0f;
        for (int k : nonZero)
            sum += taps[(size_t) k] * s.history[(size_t) ((s.write - 1 - k) & 127)];
        return sum;
    }

    float DistEngine::HalfBand::centreOnly (const State& s) const noexcept
    {
        return taps[(size_t) centre] * s.history[(size_t) ((s.write - 1 - centre) & 127)];
    }

    void DistEngine::HalfBand::up (State& s, const float* in, int n, float* out) const noexcept
    {
        // Zero-stuffed (doubled, as half the samples are zero), then filtered. Besides the centre,
        // the taps sit an odd distance from it. So on every other output they all land on the
        // stuffed zeros and only the centre counts: which outputs depends on where the centre is.
        const bool centreIsOdd = (centre & 1) != 0;

        for (int i = 0; i < n; ++i)
        {
            s.push (2.0f * in[i]);
            out[2 * i] = centreIsOdd ? filter (s) : centreOnly (s);
            s.push (0.0f);
            out[2 * i + 1] = centreIsOdd ? centreOnly (s) : filter (s);
        }
    }

    void DistEngine::HalfBand::down (State& s, const float* in, int n, float* out) const noexcept
    {
        // Every second sample of the filtered signal: the even ones, so the delays add up to
        // whole samples at the lower rate.
        for (int i = 0; i < n / 2; ++i)
        {
            s.push (in[2 * i]);
            out[i] = filter (s);
            s.push (in[2 * i + 1]);
        }
    }

    //==============================================================================
    void DistEngine::Channel::clear()
    {
        for (auto* b : { &tight, &internal, &pre, &crossLow[0], &crossLow[1], &crossHigh[0], &crossHigh[1],
                         &post, &postLow, &toneBass, &toneMid, &toneTreble, &smooth })
            b->clear();
        for (auto* st : { &up1, &up2, &down2, &down1 })
            st->clear();
        for (auto& st : stages)
            st.clear();
        dry.clear();
        lows.clear();
        drive.clear();
        envelope = finalDcIn = finalDcOut = 0.0;
    }

    DistEngine::DistEngine()
    {
        stage1.design (71, 9.0);    // 2x <-> 1x: flat to 20 kHz, closed from 28 kHz (at 48 kHz)
        stage2.design (25, 8.0);    // 4x <-> 2x: only has to keep what folds below 24 kHz out
        jassert (stage1.centre + stage2.centre / 2 == latencySamples);
    }

    void DistEngine::prepare (double sampleRate, int maxBlockSize)
    {
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        blockSize = juce::jmax (32, maxBlockSize);

        for (auto* v : { &gateGains, &wet, &dryScratch, &driveIn, &lowScratch, &envelopes, &makeUps, &levels, &blends })
            v->assign ((size_t) blockSize, 0.0f);
        upsampled1.assign ((size_t) blockSize * 2, 0.0f);
        upsampled2.assign ((size_t) blockSize * 4, 0.0f);

        for (auto& c : channels)
            for (auto& g : c.gain)
                g.reset (rate * oversampling, 0.02);
        level.reset (rate, 0.02);
        blend.reset (rate, 0.02);
        makeUp.reset (rate, 0.05);

        inputFifo->sampleRate = rate;
        outputFifo->sampleRate = rate;
        hasDesign = false;
        reset();
    }

    void DistEngine::reset()
    {
        for (auto& c : channels)
            c.clear();
        gateEnvelope = 0.0;
        gateLevel = 0.0;
        open = false;
        holdLeft = 0;
        historyCountdown = 0;
        historyPeak = 0.0f;
        primed = false;
        silence.reset();
        for (int s = 0; s < maxStages; ++s)
            heatIn[s] = heatOut[s] = 0.0f;
    }

    //==============================================================================
    float DistEngine::stageGain (const Settings& s)
    {
        const auto& p = profile (s.voicing);
        const float db = p.minGainDb + juce::jlimit (0.0f, 1.0f, s.drive) * (p.maxGainDb - p.minGainDb);
        return juce::Decibels::decibelsToGain (db / (float) p.stages);
    }

    float DistEngine::shape (Curve curve, float u, float b, float starve)
    {
        // A starved stage (Fuzz) lets nothing through under a threshold: decays sputter and stop.
        return (float) (raw (curve, starved (u, starve) + b) - raw (curve, b));
    }

    float DistEngine::transferCurve (const Settings& s, float x, float level)
    {
        return curveThrough (s, x, level, true);
    }

    float DistEngine::curveThrough (const Settings& s, float x, float level, bool withStarve)
    {
        const auto& p = profile (s.voicing);
        const auto o = operatingPoint (p, stageGain (s), bias (s), juce::jmax (0.0f, level));
        const float starve = withStarve ? starveOf (s) : 0.0f;

        float y = x;
        for (int st = 0; st < p.stages; ++st)
            y = shape (p.curve, y * o.gain, o.bias[st], st == 0 ? starve * o.gain : 0.0f);
        return y;
    }

    std::array<float, 16> DistEngine::harmonicsOf (const Settings& s, float amplitude, float level)
    {
        constexpr int n = 512;
        std::array<float, n> cycle {};
        const float at = level >= 0.0f ? level : amplitude;
        for (int i = 0; i < n; ++i)
            cycle[(size_t) i] = transferCurve (s, amplitude * (float) std::sin (twoPi * i / n), at);

        std::array<double, 16> magnitude {};
        for (int h = 1; h <= 16; ++h)
        {
            double re = 0.0, im = 0.0;
            for (int i = 0; i < n; ++i)
            {
                re += cycle[(size_t) i] * std::cos (twoPi * h * i / n);
                im -= cycle[(size_t) i] * std::sin (twoPi * h * i / n);
            }
            magnitude[(size_t) h - 1] = std::sqrt (re * re + im * im);
        }

        std::array<float, 16> db {};
        for (int h = 0; h < 16; ++h)
            db[(size_t) h] = (float) (20.0 * std::log10 (juce::jmax (1.0e-12, magnitude[(size_t) h]) / juce::jmax (1.0e-12, magnitude[0])));
        return db;
    }

    DistEngine::Coefficients DistEngine::designFor (const Settings& s, double sampleRate)
    {
        using namespace EqMath::detail;
        const auto& p = profile (s.voicing);
        Coefficients c;
        constexpr double butterworth = 0.70710678118654752;

        c.tight = highPass (w (s.tight, sampleRate), butterworth);
        c.useInternal = p.internalHighPassHz > 0.0f;
        if (c.useInternal)
            c.internal = highPass1 (w (p.internalHighPassHz, sampleRate));
        c.pre = peak (w (p.preHz, sampleRate), p.preQ, p.preDb);
        c.crossover = EqMath::linkwitzRiley (s.tight, sampleRate);

        c.usePost = std::abs (p.postDb) > 0.01f;
        if (c.usePost)
            c.post = peak (w (p.postHz, sampleRate), p.postQ, p.postDb);
        c.postLow = lowPass (w (p.postLowPassHz, sampleRate), butterworth);

        c.toneBass = lowShelf (w (120.0, sampleRate), 0.7, s.bassDb);
        c.toneMid = peak (w (s.midHz, sampleRate), 0.8, s.midDb);
        c.toneTreble = highShelf (w (3200.0, sampleRate), 0.7, s.trebleDb);
        c.useSmooth = s.smooth < 19900.0f;
        if (c.useSmooth)
            c.smooth = lowPass (w (s.smooth, sampleRate), butterworth);

        const double inner = sampleRate * oversampling;
        c.interstage = 1.0 - std::exp (-twoPi * juce::jmin ((double) p.interstageHz, inner * 0.45) / inner);
        c.dc = std::exp (-twoPi * 10.0 / inner);
        c.dcBase = std::exp (-twoPi * 5.0 / sampleRate);
        return c;
    }

    double DistEngine::getResponseDb (const Settings& s, double hz, double sampleRate)
    {
        const auto c = designFor (s, sampleRate);
        const auto& p = profile (s.voicing);
        const double wb = twoPi * hz / sampleRate, wi = wb / oversampling;
        std::complex<double> h (1.0, 0.0);

        if (s.voicing != Voicing::bass)
        {
            h *= c.tight.response (wb);
            if (c.useInternal)
                h *= c.internal.response (wb);
        }
        h *= c.pre.response (wb);

        // Per stage, at the inner rate: the DC block and the low pass after it.
        const std::complex<double> z1 = std::polar (1.0, -wi);
        const auto dc = (1.0 - z1) / (1.0 - c.dc * z1);
        const auto low = c.interstage / (1.0 - (1.0 - c.interstage) * z1);
        for (int st = 0; st < p.stages; ++st)
            h *= dc * low;

        if (c.usePost)
            h *= c.post.response (wb);
        h *= c.postLow.response (wb);
        h *= c.toneBass.response (wb) * c.toneMid.response (wb) * c.toneTreble.response (wb);
        if (c.useSmooth)
            h *= c.smooth.response (wb);

        const std::complex<double> zb = std::polar (1.0, -wb);
        h *= (1.0 - zb) / (1.0 - c.dcBase * zb);
        return 20.0 * std::log10 (juce::jmax (1.0e-9, std::abs (h)));
    }

    float DistEngine::compensationFor (const Settings& s, double sampleRate)
    {
        // A 1 kHz sine at a guitar's usual level (-24 dB RMS) as it reaches the drive (through
        // tight and pre-emphasis), out through the static curve and the voicing's post EQ: the
        // gain that brings it back to its level. (Calibrated on a steady tone this would leave
        // real playing louder with more drive, as the distortion lifts its quieter parts.)
        const auto c = designFor (s, sampleRate);
        const double wb = twoPi * 1000.0 / sampleRate;
        auto pre = c.pre.response (wb);
        if (s.voicing != Voicing::bass)
        {
            pre *= c.tight.response (wb);
            if (c.useInternal)
                pre *= c.internal.response (wb);
        }
        auto post = c.postLow.response (wb);
        if (c.usePost)
            post *= c.post.response (wb);

        constexpr float reference = 0.09f;
        const float amplitude = reference * (float) std::abs (pre);
        constexpr int n = 256;
        double sum = 0.0;
        for (int i = 0; i < n; ++i)
            sum += juce::square (curveThrough (s, amplitude * (float) std::sin (twoPi * (i + 0.5) / n), amplitude, false));     // a starved fuzz is meant to lose its quiet parts
        const double rms = std::sqrt (sum / n) * std::abs (post);
        const auto gain = juce::jlimit (0.01, 4.0, (reference / std::sqrt (2.0)) / juce::jmax (1.0e-6, rms));

        // What a sine can't predict about playing (decays lifted by the clipping, tight taking
        // the low strings' fundamentals, uneven clipping): a trim per voicing across the drive,
        // measured with plucked notes so they come out about 1 dB under the input.
        static constexpr float trims[DistEngine::numVoicings][5] {
            { 1.3f, 1.5f, 2.0f, 2.8f, 3.3f },       // Boost
            { 4.6f, 5.3f, 6.1f, 7.4f, 8.1f },       // Crunch
            { 6.5f, 5.8f, 4.2f, 2.7f, 2.1f },       // Lead
            { 4.6f, -0.1f, -2.0f, -1.2f, -0.1f },   // Modern
            { 0.0f, 3.4f, 5.4f, 6.8f, 5.8f },       // Fuzz
            { 2.1f, 1.5f, 0.8f, 0.7f, 0.7f } };     // Bass
        const auto& row = trims[juce::jlimit (0, numVoicings - 1, (int) s.voicing)];
        const float at = juce::jlimit (0.0f, 1.0f, s.drive) * 4.0f;
        const int i = juce::jmin (3, (int) at);
        const float trim = row[i] + (row[i + 1] - row[i]) * (at - (float) i);
        return (float) gain * juce::Decibels::decibelsToGain (trim);
    }

    void DistEngine::updateCoefficients (const Settings& s)
    {
        const bool same = hasDesign && s.voicing == designed.voicing && s.drive == designed.drive && s.tight == designed.tight
                          && s.character == designed.character && s.bassDb == designed.bassDb && s.midDb == designed.midDb
                          && s.midHz == designed.midHz && s.trebleDb == designed.trebleDb && s.smooth == designed.smooth;
        if (same)
            return;

        // The compensation is slow to work out and doesn't depend on the tone controls.
        const bool sameCompensation = hasDesign && s.voicing == designed.voicing && s.drive == designed.drive
                                      && s.tight == designed.tight && s.character == designed.character;

        coefficients = designFor (s, rate);

        if (! sameCompensation)
            compensationTarget = compensationFor (s, rate);

        designed = s;
        hasDesign = true;
    }

    //==============================================================================
    void DistEngine::runGate (const Settings& s, const juce::AudioBuffer<float>& buffer, int start, int n, float inputGain)
    {
        const int numChannels = juce::jmin (2, buffer.getNumChannels());
        const double follow = std::exp (-1.0 / (0.01 * rate));
        const double attack = 1.0 - std::exp (-1.0 / (0.0005 * rate));
        const double release = std::exp (-6.9 / (juce::jmax (0.005, (double) s.gateRelease) * rate));
        const double openLevel = juce::Decibels::decibelsToGain ((double) s.gateThresholdDb);
        const double closeLevel = openLevel * 0.5;      // 6 dB lower: hysteresis
        const int hold = (int) (0.015 * rate);

        for (int i = 0; i < n; ++i)
        {
            float peak = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
                peak = juce::jmax (peak, std::abs (buffer.getSample (ch, start + i) * inputGain));

            gateEnvelope = peak > gateEnvelope ? (double) peak : gateEnvelope * follow;
            historyPeak = juce::jmax (historyPeak, peak);

            if (! s.gateOn)
            {
                open = true;
                gateLevel = 1.0;
            }
            else
            {
                if (gateEnvelope > openLevel)
                {
                    open = true;
                    holdLeft = hold;
                }
                else if (gateEnvelope < closeLevel)
                {
                    if (holdLeft > 0) --holdLeft;
                    else              open = false;
                }

                gateLevel = open ? gateLevel + (1.0 - gateLevel) * attack : gateLevel * release;
                if (gateLevel < 1.0e-6)
                    gateLevel = 0.0;
            }

            gateGains[(size_t) i] = (float) gateLevel;

            if (--historyCountdown <= 0)
            {
                historyCountdown = gateHistoryInterval;
                const int at = gateWrite.load (std::memory_order_relaxed);
                gateLevelDb[(size_t) at].store (juce::Decibels::gainToDecibels (historyPeak, -100.0f), std::memory_order_relaxed);
                gateGainHistory[(size_t) at].store ((float) gateLevel, std::memory_order_relaxed);
                gateWrite.store ((at + 1) % gateHistoryLength, std::memory_order_relaxed);
                historyPeak = 0.0f;
            }
        }

        gateOpen.store (open, std::memory_order_relaxed);
        gateGain.store ((float) gateLevel, std::memory_order_relaxed);
    }

    void DistEngine::runChannel (int ch, const Settings& s, float* data, int n, float inputGain)
    {
        auto& c = channels[ch];
        const auto& k = coefficients;
        const auto& p = profile (s.voicing);
        const bool bass = s.voicing == Voicing::bass;
        const float g = stageGain (s), b = bias (s), starve = starveOf (s);
        const double envelopeUp = 1.0 - std::exp (-1.0 / (0.005 * rate)), envelopeDown = 1.0 - std::exp (-1.0 / (0.12 * rate));

        // 1. To the drive: gate, tight (or the crossover), pre-emphasis.
        for (int i = 0; i < n; ++i)
        {
            const double x = data[i] * inputGain * gateGains[(size_t) i];
            dryScratch[(size_t) i] = c.dry.process ((float) x);

            double y = x;
            if (bass)
            {
                const double low = c.crossLow[1].process (k.crossover.lowPass, c.crossLow[0].process (k.crossover.lowPass, x));
                y = c.crossHigh[1].process (k.crossover.highPass, c.crossHigh[0].process (k.crossover.highPass, x));
                lowScratch[(size_t) i] = c.lows.process ((float) low);
            }
            else
            {
                y = c.tight.process (k.tight, y);
                if (k.useInternal)
                    y = c.internal.process (k.internal, y);
            }
            y = c.pre.process (k.pre, y);

            const double magnitude = std::abs (y);
            c.envelope += (magnitude - c.envelope) * (magnitude > c.envelope ? envelopeUp : envelopeDown);
            envelopes[(size_t) i] = (float) c.envelope;
            driveIn[(size_t) i] = c.drive.process ((float) y);
            wet[(size_t) i] = (float) y;
        }

        // 2. Up to four times the rate, through the stages, back down.
        stage1.up (c.up1, wet.data(), n, upsampled1.data());
        stage2.up (c.up2, upsampled1.data(), n * 2, upsampled2.data());

        // Each stage's curve is anti-aliased with its antiderivative: the average of the curve
        // between this sample and the last, rather than the curve at this sample.
        for (int st = 0; st < p.stages; ++st)
            c.stages[st].previousF = antiderivative (p.curve, c.stages[st].previousV);

        float sumIn[maxStages] {}, sumOut[maxStages] {};
        Operating o;
        double curveAtBias[maxStages] {};
        for (int j = 0; j < n * oversampling; ++j)
        {
            if (j % oversampling == 0)
            {
                o = operatingPoint (p, g, b, envelopes[(size_t) (j / oversampling)]);

                for (int st = 0; st < p.stages; ++st)
                    curveAtBias[st] = raw (p.curve, o.bias[st]);
            }

            double v = upsampled2[(size_t) j];

            for (int st = 0; st < p.stages; ++st)
            {
                auto& stage = c.stages[st];
                const double u = v * c.gain[st].next (o.gain);
                const double in = starved (u, st == 0 ? starve * o.gain : 0.0f) + o.bias[st];
                const double step = in - stage.previousV;
                const double F = antiderivative (p.curve, in);
                const double averaged = std::abs (step) > 1.0e-6 ? (F - stage.previousF) / step : raw (p.curve, in - 0.5 * step);
                stage.previousV = in;
                stage.previousF = F;

                const double shaped = averaged - curveAtBias[st];
                sumIn[st] += (float) std::abs (u);
                sumOut[st] += (float) std::abs (shaped);

                const double blocked = shaped - stage.dcIn + k.dc * stage.dcOut;
                stage.dcIn = shaped;
                stage.dcOut = blocked;
                stage.low += k.interstage * (blocked - stage.low);
                v = stage.low;
            }

            upsampled2[(size_t) j] = (float) v;
        }

        stage2.down (c.down2, upsampled2.data(), n * 4, upsampled1.data());
        stage1.down (c.down1, upsampled1.data(), n * 2, wet.data());

        if (ch == 0)
        {
            for (int st = 0; st < p.stages; ++st)
            {
                heatIn[st] = sumIn[st];
                heatOut[st] = sumOut[st];
            }
            for (int st = p.stages; st < maxStages; ++st)
                heatIn[st] = heatOut[st] = 0.0f;
        }

        // 3. After the drive: level, the clean lows (Bass), the voicing's EQ and the tone, blend.
        int pair = pairWrite.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i)
        {
            double y = wet[(size_t) i];
            if (ch == 0)
            {
                pairIn[(size_t) pair].store (driveIn[(size_t) i], std::memory_order_relaxed);
                pairOut[(size_t) pair].store ((float) y, std::memory_order_relaxed);
                pair = (pair + 1) % pairLength;
            }

            y *= makeUps[(size_t) i];
            if (bass)
                y += lowScratch[(size_t) i];
            if (k.usePost)
                y = c.post.process (k.post, y);
            y = c.postLow.process (k.postLow, y);
            y = c.toneBass.process (k.toneBass, y);
            y = c.toneMid.process (k.toneMid, y);
            y = c.toneTreble.process (k.toneTreble, y);
            if (k.useSmooth)
                y = c.smooth.process (k.smooth, y);

            const double blocked = y - c.finalDcIn + k.dcBase * c.finalDcOut;
            c.finalDcIn = y;
            c.finalDcOut = blocked;

            const float mix = blends[(size_t) i];
            data[i] = (dryScratch[(size_t) i] * (1.0f - mix) + (float) blocked * mix) * levels[(size_t) i];
        }

        if (ch == 0)
            pairWrite.store (pair, std::memory_order_relaxed);
    }

    void DistEngine::process (juce::AudioBuffer<float>& buffer, int start, int n, const Settings& s)
    {
        const int numChannels = juce::jmin (2, buffer.getNumChannels());
        if (n <= 0 || numChannels == 0)
            return;

        updateCoefficients (s);
        const float inputGain = juce::Decibels::decibelsToGain (juce::jlimit (-24.0f, 24.0f, s.inputDb));
        const float levelTarget = juce::Decibels::decibelsToGain (s.levelDb), blendTarget = juce::jlimit (0.0f, 1.0f, s.blend);
        const float g = stageGain (s);

        if (! primed)
        {
            for (auto& c : channels)
                for (auto& sg : c.gain)
                    sg.value = g;
            level.value = levelTarget;
            blend.value = blendTarget;
            makeUp.value = compensationTarget;
            primed = true;
        }

        // Silence stays silence: nothing to work out (it costs as much as playing) once the filters
        // and the envelope that sets the bias have died away. The buffer already holds the output.
        // The smoothed values go on as they would have, so sound returns as if nothing had stopped.
        // The gate keeps running for its display.
        const bool resting = silence.isPast (buffer, start, n, numChannels, rate, 4.0);

        if (resting)
        {
            const int numStages = profile (s.voicing).stages;

            for (int ch = 0; ch < numChannels; ++ch)
                for (int st = 0; st < numStages; ++st)
                    channels[ch].gain[st].advance (g, n * oversampling);

            level.advance (levelTarget, n);
            blend.advance (blendTarget, n);
            makeUp.advance (compensationTarget, n);

            for (int st = 0; st < maxStages; ++st)
                heatIn[st] = heatOut[st] = 0.0f;
        }

        for (int piece = 0; piece < n; piece += blockSize)
        {
            const int length = juce::jmin (blockSize, n - piece);
            const int at = start + piece;

            {
                juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), numChannels, at, length);
                inputFifo->push (view);
                for (int ch = 0; ch < numChannels; ++ch)
                    inputPeak[ch].push (view.getMagnitude (ch, 0, length) * inputGain);
                if (numChannels == 1)
                    inputPeak[1].push (view.getMagnitude (0, 0, length) * inputGain);
            }

            runGate (s, buffer, at, length, inputGain);

            if (resting)
            {
                envelopes[(size_t) (length - 1)] = 0.0f;
            }
            else
            {
                for (int i = 0; i < length; ++i)
                {
                    makeUps[(size_t) i] = makeUp.next (compensationTarget);
                    levels[(size_t) i] = level.next (levelTarget);
                    blends[(size_t) i] = blend.next (blendTarget);
                }

                for (int ch = 0; ch < numChannels; ++ch)
                    runChannel (ch, s, buffer.getWritePointer (ch, at), length, inputGain);
            }

            juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), numChannels, at, length);
            outputFifo->push (view);
            for (int ch = 0; ch < numChannels; ++ch)
                outputPeak[ch].push (view.getMagnitude (ch, 0, length));
            if (numChannels == 1)
                outputPeak[1].push (view.getMagnitude (0, 0, length));

            // Telemetry: how hard each stage clips, the bias as playing has moved it, the input.
            const auto& p = profile (s.voicing);
            for (int st = 0; st < maxStages; ++st)
            {
                const float heat = st < p.stages && heatIn[st] > 1.0e-9f ? juce::jlimit (0.0f, 1.0f, 1.0f - heatOut[st] / heatIn[st]) : 0.0f;
                const float before = stageHeat[(size_t) st].load (std::memory_order_relaxed);
                stageHeat[(size_t) st].store (before + (heat - before) * 0.3f, std::memory_order_relaxed);
            }
            biasShift.store (std::tanh (envelopes[(size_t) (length - 1)] * g), std::memory_order_relaxed);
            compensation.store (compensationTarget, std::memory_order_relaxed);
            inputLevel.store (envelopes[(size_t) (length - 1)], std::memory_order_relaxed);
        }
    }
}
