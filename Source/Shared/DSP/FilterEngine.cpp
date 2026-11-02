#include "FilterEngine.h"
#include "FractionalDelay.h"
#include "TempoSync.h"
#include <complex>

namespace cdaw
{
    namespace
    {
        constexpr double pi = juce::MathConstants<double>::pi;
        constexpr int controlInterval = 16;     // samples between cutoff updates (interpolated)

        // The 24 dB/oct filter: a fixed Butterworth section, then the resonant one.
        constexpr double steepFirstQ = 0.5412, steepSecondQ = 1.3066;
    }

    void FilterEngine::prepare (double sampleRate, int)
    {
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        outputFifo->sampleRate = rate;
        reset();
    }

    void FilterEngine::reset()
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            stage1[ch].clear();
            stage2[ch].clear();
        }

        envelope = 0.0f;
        phase = 0.0;
        lastSidePhase[0] = lastSidePhase[1] = 0.0;
        primed = false;
    }

    //==============================================================================
    double FilterEngine::getLfoHz (const Settings& s, double bpm)
    {
        return s.lfoSync ? TempoSync::toHz (s.lfoDivision, bpm) : juce::jlimit (0.01, 40.0, (double) s.lfoRate);
    }

    float FilterEngine::lfoValue (Shape shape, double p, float heldValue)
    {
        p -= std::floor (p);

        switch (shape)
        {
            case Shape::triangle:   { auto q = p + 0.25; q -= std::floor (q); return (float) (1.0 - 4.0 * std::abs (q - 0.5)); }
            case Shape::rampUp:     return (float) (2.0 * p - 1.0);
            case Shape::rampDown:   return (float) (1.0 - 2.0 * p);
            case Shape::square:     return p < 0.5 ? 1.0f : -1.0f;
            case Shape::random:     return heldValue;
            case Shape::sine:
            case Shape::numShapes:
            default:                return (float) std::sin (2.0 * pi * p);
        }
    }

    double FilterEngine::modulatedCutoff (const Settings& s, float lfo, float env, double sampleRate)
    {
        const double octaves = (double) juce::jlimit (0.0f, 1.0f, s.lfoAmount) * maxModOctaves * lfo
                               + (double) juce::jlimit (-1.0f, 1.0f, s.envAmount) * maxModOctaves * env;
        return juce::jlimit ((double) minHz, juce::jmin ((double) maxHz, sampleRate * 0.45), (double) s.cutoff * std::pow (2.0, octaves));
    }

    double FilterEngine::getQ (const Settings& s)
    {
        const double r = juce::jlimit (0.0f, 1.0f, s.resonance);
        return s.steep && (s.type == Type::lowPass || s.type == Type::highPass) ? steepSecondQ * std::pow (12.0, r)
                                                                                 : 0.7071 * std::pow (25.0, r);
    }

    double FilterEngine::getResponseDb (const Settings& s, double cutoffHz, double hz, double sampleRate)
    {
        // The TPT filter matches its analog prototype with the frequency pre-warped.
        const double warped = std::tan (pi * juce::jmin (hz, sampleRate * 0.4999) / sampleRate) / std::tan (pi * cutoffHz / sampleRate);
        const std::complex<double> sj (0.0, warped);

        auto section = [&] (double q)
        {
            const double k = 1.0 / q;
            const auto den = sj * sj + k * sj + 1.0;
            switch (s.type)
            {
                case Type::highPass:  return sj * sj / den;
                case Type::bandPass:  return k * sj / den;
                case Type::notch:     return (sj * sj + 1.0) / den;
                case Type::lowPass:
                default:              return 1.0 / den;
            }
        };

        const double q = getQ (s);
        std::complex<double> h;

        if (! s.steep)
            h = section (q);
        else if (s.type == Type::lowPass || s.type == Type::highPass)
            h = section (steepFirstQ) * section (q);
        else
            h = section (q) * section (q);

        const double mix = juce::jlimit (0.0f, 1.0f, s.mix);
        const auto out = (1.0 - mix) + mix * h;
        return 20.0 * std::log10 (juce::jmax (1.0e-6, std::abs (out))) + s.outputDb;
    }

    //==============================================================================
    double FilterEngine::filterSample (int ch, double x, double gain, const Settings& s, double k1, double k2) noexcept
    {
        double low, band, high;

        auto pick = [&s] (double l, double b, double h, double in)
        {
            switch (s.type)
            {
                case Type::highPass:  return h;
                case Type::bandPass:  return b;
                case Type::notch:     return in - b;
                case Type::lowPass:
                default:              return l;
            }
        };

        stage1[ch].process (x, gain, k1, low, band, high);
        double y = pick (low, band, high, x);

        if (s.steep)
        {
            stage2[ch].process (y, gain, k2, low, band, high);
            y = pick (low, band, high, y);
        }

        return y;
    }

    void FilterEngine::process (juce::AudioBuffer<float>& buffer, int start, int n, const Settings& s, double bpm, const TempoSync::SongPosition& position)
    {
        const int numChannels = juce::jmin (2, buffer.getNumChannels());
        if (numChannels == 0 || n <= 0)
            return;

        // A synced LFO follows the beats; a free one picks up the song position when playback starts or jumps.
        const bool lock = clock.shouldLock (position, n, rate);
        if (s.lfoSync && position.playing)
            phase = TempoSync::LfoClock::syncedPhaseAt (position, s.lfoDivision);
        else if (lock)
            phase = TempoSync::LfoClock::phaseAt (position, getLfoHz (s, bpm));

        const double q = getQ (s);
        const bool steepCut = s.steep && (s.type == Type::lowPass || s.type == Type::highPass);
        const double k1 = steepCut ? 1.0 / steepFirstQ : 1.0 / q, k2 = 1.0 / q;
        const double step = getLfoHz (s, bpm) / rate;
        const double rightOffset = 0.5 * juce::jlimit (0.0f, 1.0f, s.lfoStereo);
        const float driveGain = juce::Decibels::decibelsToGain (24.0f * juce::jlimit (0.0f, 1.0f, s.drive));
        const bool driven = s.drive > 0.001f;
        const float mix = juce::jlimit (0.0f, 1.0f, s.mix);
        const float output = juce::Decibels::decibelsToGain (s.outputDb);
        const float envAttack = (float) std::exp (-1.0 / (0.003 * rate));
        const float envRelease = (float) std::exp (-1.0 / (juce::jlimit (5.0f, 5000.0f, s.envRelease) * 0.001 * rate));
        const float lfoGlide = (float) std::exp (-(double) controlInterval / (0.002 * rate));   // softens square / random steps

        auto cutoffFor = [&] (int ch, double p)
        {
            const double sidePhase = ch == 0 ? p : p + rightOffset;
            const float raw = lfoValue (s.lfoShape, sidePhase, held[ch]);
            smoothedLfo[ch] = raw + (smoothedLfo[ch] - raw) * (s.lfoShape == Shape::square || s.lfoShape == Shape::random ? lfoGlide : 0.0f);
            const double envNorm = juce::jlimit (0.0, 1.0, (juce::Decibels::gainToDecibels (envelope, -100.0f) + 48.0) / 48.0);
            return modulatedCutoff (s, smoothedLfo[ch], (float) envNorm, rate);
        };

        auto gFor = [this] (double hz) { return std::tan (pi * hz / rate); };

        if (! primed)
        {
            for (int ch = 0; ch < 2; ++ch)
            {
                held[ch] = random.nextFloat() * 2.0f - 1.0f;
                smoothedLfo[ch] = lfoValue (s.lfoShape, ch == 0 ? phase : phase + rightOffset, held[ch]);
                g[ch] = gFor (cutoffFor (ch, phase));
            }

            lastMix = mix;
            lastOutput = output;
            primed = true;
        }

        float* data[2] { buffer.getWritePointer (0, start), numChannels > 1 ? buffer.getWritePointer (1, start) : nullptr };
        float inPeak = 0.0f, outPeak = 0.0f;
        double cutoffNow[2] {};

        for (int done = 0; done < n;)
        {
            const int chunk = juce::jmin (controlInterval, n - done);
            const double endPhase = phase + step * chunk;

            // The random shape picks a new value each cycle (on each side).
            for (int ch = 0; ch < 2; ++ch)
            {
                double side = (ch == 0 ? endPhase : endPhase + rightOffset);
                side -= std::floor (side);
                if (side < lastSidePhase[ch])
                    held[ch] = random.nextFloat() * 2.0f - 1.0f;
                lastSidePhase[ch] = side;
            }

            // The envelope follows the input before the drive.
            for (int i = 0; i < chunk; ++i)
            {
                float level = std::abs (data[0][done + i]);
                if (data[1] != nullptr)
                    level = juce::jmax (level, std::abs (data[1][done + i]));
                inPeak = juce::jmax (inPeak, level);
                envelope = level > envelope ? level + (envelope - level) * envAttack : level + (envelope - level) * envRelease;
            }

            for (int ch = 0; ch < numChannels; ++ch)
            {
                cutoffNow[ch] = cutoffFor (ch, endPhase);
                const double target = gFor (cutoffNow[ch]);
                const double dg = (target - g[ch]) / chunk;
                double gain = g[ch];
                float* d = data[ch] + done;

                for (int i = 0; i < chunk; ++i)
                {
                    gain += dg;
                    const double x = d[i];
                    const double in = driven ? 2.0 / driveGain * softClip ((float) x * driveGain * 0.5f) : x;
                    const double y = filterSample (ch, in, gain, s, k1, k2);
                    const float t = (float) (done + i + 1) / (float) n;
                    const float mixNow = lastMix + (mix - lastMix) * t;
                    const float outNow = lastOutput + (output - lastOutput) * t;
                    const float out = (float) ((1.0 - mixNow) * x + mixNow * y) * outNow;
                    outPeak = juce::jmax (outPeak, std::abs (out));
                    d[i] = out;
                }

                g[ch] = target;
            }

            if (numChannels == 1)
                cutoffNow[1] = cutoffFor (1, endPhase);

            phase = endPhase - std::floor (endPhase);
            done += chunk;
        }

        // Out of denormals in silence.
        for (auto* stages : { stage1, stage2 })
            for (int ch = 0; ch < 2; ++ch)
                stages[ch].flushDenormals();

        lastMix = mix;
        lastOutput = output;

        currentCutoff[0].store ((float) cutoffNow[0], std::memory_order_relaxed);
        currentCutoff[1].store ((float) cutoffNow[1], std::memory_order_relaxed);
        envelopeLevel.store ((float) juce::jlimit (0.0, 1.0, (juce::Decibels::gainToDecibels (envelope, -100.0f) + 48.0) / 48.0), std::memory_order_relaxed);
        lfoPhase.store (phase, std::memory_order_relaxed);
        inputPeak.push (inPeak);
        outputPeak.push (outPeak);

        juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), numChannels, start, n);
        outputFifo->push (view);
    }
}
