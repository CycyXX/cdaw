#include "PhaserEngine.h"
#include "TempoSync.h"
#include <complex>

namespace cdaw
{
    namespace
    {
        constexpr double twoPi = juce::MathConstants<double>::twoPi;
        constexpr int controlInterval = 8;      // samples between coefficient updates (interpolated)
    }

    void PhaserEngine::prepare (double sampleRate, int)
    {
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        reset();
    }

    void PhaserEngine::reset()
    {
        for (auto& channel : state)
            for (auto& s : channel)
                s = 0.0;

        lastOut[0] = lastOut[1] = 0.0;
        phase = 0.0;
        primed = false;
    }

    double PhaserEngine::getRateHz (const Settings& s, double bpm)
    {
        return s.sync ? TempoSync::toHz (s.division, bpm) : juce::jlimit (0.01, 20.0, (double) s.rateHz);
    }

    double PhaserEngine::getSweepHz (const Settings& s, double phase, double sampleRate)
    {
        const double octaves = juce::jlimit (0.0f, 1.0f, s.depth) * maxOctaves * std::sin (twoPi * phase);
        return juce::jlimit (20.0, sampleRate * 0.45, (double) s.centreHz * std::pow (2.0, octaves));
    }

    double PhaserEngine::coefficient (double hz, double sampleRate)
    {
        const double t = std::tan (juce::MathConstants<double>::pi * hz / sampleRate);
        return (t - 1.0) / (t + 1.0);
    }

    double PhaserEngine::getResponseDb (const Settings& s, double sweepHz, double hz, double sampleRate)
    {
        const double a = coefficient (sweepHz, sampleRate);
        const auto z1 = std::polar (1.0, -twoPi * hz / sampleRate);
        const auto stage = (a + z1) / (1.0 + a * z1);

        std::complex<double> chain (1.0, 0.0);
        for (int i = 0; i < juce::jlimit (1, maxStages, s.stages); ++i)
            chain *= stage;

        // Feedback from the last stage, one sample later, into the first.
        const auto wet = chain / (1.0 - (double) s.feedback * z1 * chain);
        const double mix = juce::jlimit (0.0f, 1.0f, s.mix);
        return 20.0 * std::log10 (juce::jmax (1.0e-6, std::abs ((1.0 - mix) + mix * wet)));
    }

    void PhaserEngine::process (juce::AudioBuffer<float>& buffer, int start, int n, const Settings& s, double bpm, const TempoSync::SongPosition& position)
    {
        const int numChannels = juce::jmin (2, buffer.getNumChannels());
        if (numChannels == 0 || n <= 0)
            return;

        // Synced sweeps follow the beats; free ones pick up the song position when playback starts or jumps.
        const bool lock = clock.shouldLock (position, n, rate);
        if (s.sync && position.playing)
            phase = TempoSync::LfoClock::syncedPhaseAt (position, s.division);
        else if (lock)
            phase = TempoSync::LfoClock::phaseAt (position, getRateHz (s, bpm));

        const int stages = juce::jlimit (1, maxStages, s.stages);
        const double feedback = juce::jlimit (-0.95f, 0.95f, s.feedback);
        const double step = getRateHz (s, bpm) / rate;
        const double rightOffset = 0.5 * juce::jlimit (0.0f, 1.0f, s.spread);
        const float mix = juce::jlimit (0.0f, 1.0f, s.mix);

        if (! primed)
        {
            coeff[0] = coefficient (getSweepHz (s, phase, rate), rate);
            coeff[1] = coefficient (getSweepHz (s, phase + rightOffset, rate), rate);
            lastMix = mix;
            primed = true;
        }

        float inPeak = 0.0f, outPeak = 0.0f;

        for (int done = 0; done < n;)
        {
            const int chunk = juce::jmin (controlInterval, n - done);

            // Where the coefficients should be at the end of this chunk; glide there sample by sample.
            const double endPhase = phase + step * chunk;
            const double target[2] { coefficient (getSweepHz (s, endPhase, rate), rate),
                                     coefficient (getSweepHz (s, endPhase + rightOffset, rate), rate) };

            for (int ch = 0; ch < numChannels; ++ch)
            {
                float* data = buffer.getWritePointer (ch, start + done);
                double a = coeff[ch];
                const double da = (target[ch] - a) / chunk;
                double* z = state[ch];
                double fb = lastOut[ch];

                for (int i = 0; i < chunk; ++i)
                {
                    a += da;
                    const double x = data[i];
                    double v = x + feedback * fb;

                    for (int st = 0; st < stages; ++st)
                    {
                        const double y = a * v + z[st];
                        z[st] = v - a * y;
                        v = y;
                    }

                    fb = v;
                    const float mixNow = lastMix + (mix - lastMix) * (float) (done + i + 1) / (float) n;
                    const float out = (float) ((1.0 - mixNow) * x + mixNow * v);
                    inPeak = juce::jmax (inPeak, std::abs ((float) x));
                    outPeak = juce::jmax (outPeak, std::abs (out));
                    data[i] = out;
                }

                // Out of denormals in silence.
                for (int st = 0; st < stages; ++st)
                    if (std::abs (z[st]) < 1.0e-20)
                        z[st] = 0.0;

                lastOut[ch] = std::abs (fb) < 1.0e-20 ? 0.0 : fb;
                coeff[ch] = target[ch];
            }

            // A mono buffer still keeps the right side's sweep moving.
            if (numChannels == 1)
                coeff[1] = target[1];

            phase = endPhase - std::floor (endPhase);
            done += chunk;
        }

        lastMix = mix;
        lfoPhase.store (phase, std::memory_order_relaxed);
        inputPeak.push (inPeak);
        outputPeak.push (outPeak);
    }
}
