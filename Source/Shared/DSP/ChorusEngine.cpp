#include "ChorusEngine.h"

namespace cdaw
{
    namespace
    {
        constexpr double twoPi = juce::MathConstants<double>::twoPi;
    }

    void ChorusEngine::prepare (double sampleRate, int)
    {
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        const int maxSamples = (int) std::ceil ((40.0 + maxModulationMs + 2.0) * 0.001 * rate);
        for (auto& line : lines)
            line.setMaxSamples (maxSamples);

        for (auto& g : voiceGain)
            g.reset (rate, 0.03);

        wetGain.reset (rate, 0.02);
        dryGain.reset (rate, 0.02);
        delayMs.reset (rate, 0.05);
        depthMs.reset (rate, 0.05);
        designedTone = -1.0f;
        reset();
    }

    void ChorusEngine::reset()
    {
        for (auto& line : lines)
            line.clear();

        // The voices start spread around the cycle, the same way each time.
        for (int v = 0; v < maxVoices; ++v)
            phases[(size_t) v] = (double) v / maxVoices;

        for (auto& z : tone.z)
            z[0] = z[1] = 0.0;

        primed = false;
        silence.reset();
    }

    //==============================================================================
    float ChorusEngine::getVoiceDelayMs (const Settings& s, double phase, int channel)
    {
        if (channel == 1)
            phase = rightPhase (s, phase);

        const float sweep = 0.5f * (1.0f + (float) std::sin (twoPi * phase));
        return juce::jlimit (0.2f, 40.0f, s.delayMs) + juce::jlimit (0.0f, 1.0f, s.depth) * maxModulationMs * sweep;
    }

    float ChorusEngine::getVoicePan (const Settings& s, int voice)
    {
        const int voices = juce::jlimit (1, maxVoices, s.voices);
        if (voices == 1)
            return 0.0f;

        // Alternate sides so two voices sit left and right, four spread out evenly.
        static constexpr float positions[maxVoices][maxVoices] {
            { 0.0f, 0.0f, 0.0f, 0.0f }, { -1.0f, 1.0f, 0.0f, 0.0f }, { -1.0f, 1.0f, 0.0f, 0.0f }, { -1.0f, 1.0f, -0.33f, 0.33f }
        };
        return positions[voices - 1][juce::jlimit (0, maxVoices - 1, voice)] * juce::jlimit (0.0f, 1.0f, s.spread);
    }

    double ChorusEngine::getResponseDb (const Settings& s, const float* voiceDelaysMs, double hz, double sampleRate)
    {
        const int voices = juce::jlimit (1, maxVoices, s.voices);
        const double w = twoPi * hz / sampleRate;
        std::complex<double> taps (0.0, 0.0), heard (0.0, 0.0);

        for (int v = 0; v < voices; ++v)
        {
            const auto tap = std::polar (1.0, -w * voiceDelaysMs[v] * 0.001 * sampleRate);
            taps += tap;
            heard += tap * (double) juce::jmin (1.0f, 1.0f - getVoicePan (s, v));     // the left side's balance
        }

        // The delay line holds the input plus feedback of the voices' average: Y = X / (1 - fb T).
        const std::complex<double> average = taps / (double) voices;
        const std::complex<double> line = 1.0 / (1.0 - (double) s.feedback * average);
        std::complex<double> wet = heard / std::sqrt ((double) voices) * line;

        if (s.tone < toneOff - 1.0f)
        {
            auto lp = EqMath::detail::lowPass (twoPi * juce::jlimit (200.0, sampleRate * 0.45, (double) s.tone) / sampleRate, 0.7071);
            wet *= lp.response (w);
        }

        const double dry = juce::jmin (1.0, 2.0 * (1.0 - s.mix)), wetLevel = juce::jmin (1.0, 2.0 * s.mix);
        return 20.0 * std::log10 (juce::jmax (1.0e-6, std::abs (dry + wetLevel * wet)));
    }

    //==============================================================================
    void ChorusEngine::process (juce::AudioBuffer<float>& buffer, int start, int n, const Settings& s, const TempoSync::SongPosition& position)
    {
        const int numChannels = juce::jmin (2, buffer.getNumChannels());
        if (numChannels == 0 || n <= 0)
            return;

        if (clock.shouldLock (position, n, rate))
            for (int v = 0; v < maxVoices; ++v)
                phases[(size_t) v] = TempoSync::LfoClock::wrap ((double) v / maxVoices
                                                                + TempoSync::LfoClock::phaseAt (position, juce::jlimit (0.01, 20.0, (double) s.rateHz) * rateMultipliers[(size_t) v]));

        if (designedTone != s.tone)
        {
            designedTone = s.tone;
            tone.c = EqMath::detail::lowPass (twoPi * juce::jlimit (200.0, rate * 0.45, (double) s.tone) / rate, 0.7071);
        }

        const bool toneOn = s.tone < toneOff - 1.0f;
        const int voices = juce::jlimit (1, maxVoices, s.voices);
        const float feedback = juce::jlimit (-0.95f, 0.95f, s.feedback);
        const float width = juce::jlimit (0.0f, 2.0f, s.width);
        const float dryTarget = juce::jmin (1.0f, 2.0f * (1.0f - s.mix)), wetTarget = juce::jmin (1.0f, 2.0f * s.mix);
        const float msToSamples = (float) (0.001 * rate);
        const double phaseOffsetRight = 0.25 * juce::jlimit (0.0f, 1.0f, s.spread);
        const double baseStep = juce::jlimit (0.01, 20.0, (double) s.rateHz) / rate;

        // Voices fade in and out when the count changes; each is balanced into place.
        float voiceTarget[maxVoices], panGain[maxVoices][2];
        for (int v = 0; v < maxVoices; ++v)
        {
            voiceTarget[v] = v < voices ? 1.0f / std::sqrt ((float) voices) : 0.0f;
            const float pan = getVoicePan (s, v);
            panGain[v][0] = juce::jmin (1.0f, 1.0f - pan);
            panGain[v][1] = juce::jmin (1.0f, 1.0f + pan);
        }

        if (! primed)
        {
            for (int v = 0; v < maxVoices; ++v)
                voiceGain[(size_t) v].value = voiceTarget[v];
            dryGain.value = dryTarget;
            wetGain.value = wetTarget;
            delayMs.value = s.delayMs;
            depthMs.value = s.depth * maxModulationMs;
            primed = true;
        }

        // Silence stays silence: once the feedback has died away nothing comes out, and the buffer
        // already holds that. (Well below hearing: what's left in the lines stays there, and returns
        // with the sound.) The sweeps and the smoothed values go on as they would have, step by step
        // as rounding matters here, so sound returns as if nothing had stopped.
        const double passes = std::abs (feedback) > 0.01f ? std::log (1.0e-10) / std::log ((double) std::abs (feedback)) : 1.0;
        const double longestPass = (juce::jlimit (0.2f, 40.0f, s.delayMs) + juce::jlimit (0.0f, 1.0f, s.depth) * maxModulationMs) * 0.001;

        if (silence.isPast (buffer, start, n, numChannels, rate, 0.2 + passes * longestPass))
        {
            for (int v = 0; v < maxVoices; ++v)
            {
                voiceGain[(size_t) v].advance (voiceTarget[v], n);
                auto& phase = phases[(size_t) v];
                const double step = baseStep * rateMultipliers[(size_t) v];

                for (int i = 0; i < n; ++i)
                {
                    phase += step;
                    if (phase >= 1.0)
                        phase -= 1.0;
                }

                voicePhase[(size_t) v].store (phase, std::memory_order_relaxed);
            }

            dryGain.advance (dryTarget, n);
            wetGain.advance (wetTarget, n);
            delayMs.advance (juce::jlimit (0.2f, 40.0f, s.delayMs), n);
            depthMs.advance (juce::jlimit (0.0f, 1.0f, s.depth) * maxModulationMs, n);

            inputPeak.push (0.0f);
            wetPeak.push (0.0f);
            return;
        }

        float* left = buffer.getWritePointer (0, start);
        float* right = numChannels > 1 ? buffer.getWritePointer (1, start) : nullptr;
        float inPeak = 0.0f, outPeak = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            const float x[2] { left[i], right != nullptr ? right[i] : left[i] };
            const float base = delayMs.next (juce::jlimit (0.2f, 40.0f, s.delayMs)) * msToSamples;
            const float depth = depthMs.next (juce::jlimit (0.0f, 1.0f, s.depth) * maxModulationMs) * msToSamples;

            float sum[2] {}, feedbackSum[2] {};
            for (int v = 0; v < maxVoices; ++v)
            {
                const float g = voiceGain[(size_t) v].next (voiceTarget[v]);
                if (g < 1.0e-4f && v >= voices)
                    continue;

                const double phase = phases[(size_t) v];
                for (int ch = 0; ch < 2; ++ch)
                {
                    const double p = ch == 0 ? phase : phase + phaseOffsetRight;
                    const float sweep = 0.5f * (1.0f + (float) std::sin (twoPi * p));
                    const float tap = lines[ch].tap (base + depth * sweep);
                    sum[ch] += tap * g * panGain[v][ch];
                    feedbackSum[ch] += tap * (v < voices ? 1.0f / (float) voices : 0.0f);
                }
            }

            for (int v = 0; v < maxVoices; ++v)
            {
                auto& phase = phases[(size_t) v];
                phase += baseStep * rateMultipliers[(size_t) v];
                if (phase >= 1.0)
                    phase -= 1.0;
            }

            for (int ch = 0; ch < 2; ++ch)
                lines[ch].push (x[ch] + softClip (feedback * feedbackSum[ch]));

            float wet[2] { sum[0], sum[1] };
            if (toneOn)
            {
                wet[0] = tone.process (0, wet[0]);
                wet[1] = tone.process (1, wet[1]);
            }

            const float mid = 0.5f * (wet[0] + wet[1]), side = 0.5f * (wet[0] - wet[1]) * width;
            const float wetLevel = wetGain.next (wetTarget), dryLevel = dryGain.next (dryTarget);
            const float wL = (mid + side) * wetLevel, wR = (mid - side) * wetLevel;

            inPeak = juce::jmax (inPeak, std::abs (x[0]), std::abs (x[1]));
            outPeak = juce::jmax (outPeak, std::abs (wL), std::abs (wR));

            if (right != nullptr)
            {
                left[i] = dryLevel * x[0] + wL;
                right[i] = dryLevel * x[1] + wR;
            }
            else
            {
                left[i] = dryLevel * x[0] + 0.5f * (wL + wR);
            }
        }

        for (auto& z : tone.z)
            for (auto& v : z)
                if (std::abs (v) < 1.0e-20)
                    v = 0.0;

        for (int v = 0; v < maxVoices; ++v)
            voicePhase[(size_t) v].store (phases[(size_t) v], std::memory_order_relaxed);

        inputPeak.push (inPeak);
        wetPeak.push (outPeak);
    }
}
