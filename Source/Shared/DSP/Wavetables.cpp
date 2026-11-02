#include "Wavetables.h"
#include <juce_dsp/juce_dsp.h>

namespace cdaw
{
    namespace
    {
        constexpr double pi = juce::MathConstants<double>::pi;
        constexpr int spectrumShown = 128;      // harmonics kept for displays

        /** One frame's harmonics: signed amplitude per harmonic (index 1..), and a phase per
            harmonic shared by the whole bank (so neighbouring frames blend without cancelling). */
        using Amplitudes = std::vector<double>;

        double lerp (double a, double b, double t)  { return a + (b - a) * t; }

        Amplitudes analog (double t)
        {
            // Sine -> triangle -> saw -> square.
            Amplitudes a ((size_t) Wavetables::maxHarmonics + 1, 0.0);
            auto sine = [] (int h)      { return h == 1 ? 1.0 : 0.0; };
            auto triangle = [] (int h)  { return h % 2 == 1 ? 8.0 / (pi * pi) * (((h - 1) / 2) % 2 == 0 ? 1.0 : -1.0) / (h * h) : 0.0; };
            auto saw = [] (int h)       { return 2.0 / pi * (h % 2 == 1 ? 1.0 : -1.0) / h; };
            auto square = [] (int h)    { return h % 2 == 1 ? 4.0 / (pi * h) : 0.0; };

            const double segment = t * 3.0;
            for (int h = 1; h <= Wavetables::maxHarmonics; ++h)
            {
                if (segment < 1.0)      a[(size_t) h] = lerp (sine (h), triangle (h), segment);
                else if (segment < 2.0) a[(size_t) h] = lerp (triangle (h), saw (h), segment - 1.0);
                else                    a[(size_t) h] = lerp (saw (h), square (h), juce::jmin (1.0, segment - 2.0));
            }
            return a;
        }

        Amplitudes vowel (double t)
        {
            // A voice-like source through the formants of A, E, I, O, U (in turn across the bank).
            struct Vowel { double f[3]; };
            static constexpr Vowel vowels[] { { { 730, 1090, 2440 } }, { { 530, 1840, 2480 } }, { { 270, 2290, 3010 } },
                                              { { 570, 840, 2410 } }, { { 300, 870, 2240 } } };
            constexpr double fundamental = 130.0, widths[3] { 90.0, 110.0, 160.0 }, gains[3] { 1.0, 0.55, 0.28 };

            const double pos = t * 4.0;
            const int i = juce::jmin (3, (int) pos);
            const double blend = pos - i;

            Amplitudes a ((size_t) Wavetables::maxHarmonics + 1, 0.0);
            for (int h = 1; h <= Wavetables::maxHarmonics; ++h)
            {
                const double hz = h * fundamental;
                double envelope = 0.015;
                for (int f = 0; f < 3; ++f)
                {
                    const double centre = lerp (vowels[i].f[f], vowels[i + 1].f[f], blend);
                    envelope += gains[f] * std::exp (-0.5 * juce::square ((hz - centre) / widths[f]));
                }
                a[(size_t) h] = envelope / std::pow ((double) h, 0.6) * std::exp (-hz / 9000.0);
            }
            return a;
        }

        Amplitudes glass (double t)
        {
            // Sparse, bell-like partials (a Fibonacci set), brightening across the bank.
            static const std::array<int, 11> partials { 1, 2, 3, 5, 8, 13, 21, 34, 55, 89, 144 };
            Amplitudes a ((size_t) Wavetables::maxHarmonics + 1, 0.0);
            const double reach = 2.0 + 60.0 * t * t;

            for (int h = 1; h <= Wavetables::maxHarmonics; ++h)
            {
                const bool strong = std::find (partials.begin(), partials.end(), h) != partials.end();
                a[(size_t) h] = std::exp (-(h - 1) / reach) * (strong ? 1.0 : 0.04 * t) / std::sqrt ((double) h);
            }
            return a;
        }

        Amplitudes organ (double t)
        {
            // Drawbar registrations: flute, then fuller, then a bright mixture.
            static constexpr int bars[] { 1, 2, 3, 4, 5, 6, 8 };
            static constexpr double registrations[5][7] { { 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 },
                                                          { 1.0, 0.5, 0.0, 0.0, 0.0, 0.0, 0.0 },
                                                          { 1.0, 0.7, 0.5, 0.4, 0.0, 0.0, 0.0 },
                                                          { 0.8, 0.8, 0.8, 0.8, 0.6, 0.6, 0.6 },
                                                          { 0.3, 0.2, 1.0, 0.3, 0.6, 0.6, 0.5 } };
            const double pos = t * 4.0;
            const int i = juce::jmin (3, (int) pos);
            const double blend = pos - i;

            Amplitudes a ((size_t) Wavetables::maxHarmonics + 1, 0.0);
            for (int b = 0; b < 7; ++b)
                a[(size_t) bars[b]] = lerp (registrations[i][b], registrations[i + 1][b], blend);
            return a;
        }

        Amplitudes air (double t)
        {
            // A soft resonance that rises through the harmonics, over a faint breath.
            Amplitudes a ((size_t) Wavetables::maxHarmonics + 1, 0.0);
            const double centre = 1.0 + 7.5 * t;    // in octaves above the fundamental
            for (int h = 1; h <= Wavetables::maxHarmonics; ++h)
                a[(size_t) h] = std::exp (-0.5 * juce::square ((std::log2 ((double) h) - centre) / 0.8)) / std::sqrt ((double) h) + 0.08 / h;
            return a;
        }

        Amplitudes pulse (double t)
        {
            // Pulse width from 50 % (square) down to a thin 3 %.
            Amplitudes a ((size_t) Wavetables::maxHarmonics + 1, 0.0);
            const double width = 0.5 - 0.47 * t;
            for (int h = 1; h <= Wavetables::maxHarmonics; ++h)
                a[(size_t) h] = 2.0 / (pi * h) * std::sin (pi * h * width);
            return a;
        }

        Amplitudes spectrumOf (Wavetables::Bank bank, double t)
        {
            switch (bank)
            {
                case Wavetables::Bank::vowel:   return vowel (t);
                case Wavetables::Bank::glass:   return glass (t);
                case Wavetables::Bank::organ:   return organ (t);
                case Wavetables::Bank::air:     return air (t);
                case Wavetables::Bank::pulse:   return pulse (t);
                case Wavetables::Bank::analog:
                case Wavetables::Bank::numBanks:
                default:                        return analog (t);
            }
        }

        /** Banks whose harmonics sound smoother with scattered phases (they're not classic shapes). */
        bool scatteredPhases (Wavetables::Bank bank)
        {
            return bank == Wavetables::Bank::vowel || bank == Wavetables::Bank::glass || bank == Wavetables::Bank::air;
        }
    }

    juce::StringArray Wavetables::getBankNames()
    {
        return { "Analog", "Vowel", "Glass", "Organ", "Air", "Pulse" };
    }

    const Wavetables& Wavetables::get()
    {
        static const Wavetables tables;
        return tables;
    }

    int Wavetables::levelFor (double hz, double sampleRate) noexcept
    {
        const double allowed = 0.45 * sampleRate / juce::jmax (1.0, hz);
        for (int level = 0; level < numLevels; ++level)
            if (harmonicsAt (level) <= allowed)
                return level;
        return numLevels - 1;
    }

    Wavetables::Wavetables()
    {
        data.assign ((size_t) numBanks * numFrames * numLevels * (tableSize + 1), 0.0f);
        spectra.resize ((size_t) numBanks * numFrames);

        constexpr int order = 11;
        static_assert ((1 << order) == tableSize);
        juce::dsp::FFT fft (order);
        std::vector<float> work ((size_t) tableSize * 2);

        for (int b = 0; b < numBanks; ++b)
        {
            const auto bank = (Bank) b;
            std::vector<double> phases ((size_t) maxHarmonics + 1, 0.0);
            juce::Random random (4711 + b);
            if (scatteredPhases (bank))
                for (auto& p : phases)
                    p = random.nextDouble() * 2.0 * pi;

            for (int f = 0; f < numFrames; ++f)
            {
                const auto amplitudes = spectrumOf (bank, (double) f / (numFrames - 1));
                float gain = 1.0f;

                for (int level = 0; level < numLevels; ++level)
                {
                    // A sine of each harmonic: bin h = a (sin p, -cos p), the inverse transform gives the wave.
                    std::fill (work.begin(), work.end(), 0.0f);
                    const int top = harmonicsAt (level);
                    for (int h = 1; h <= top; ++h)
                    {
                        const double a = amplitudes[(size_t) h];
                        work[(size_t) (2 * h)] = (float) (a * std::sin (phases[(size_t) h]));
                        work[(size_t) (2 * h + 1)] = (float) (-a * std::cos (phases[(size_t) h]));
                    }

                    fft.performRealOnlyInverseTransform (work.data());

                    // The full-bandwidth level sets the frame's gain (peak 1); the others keep it,
                    // so a note's level doesn't jump as it moves between them.
                    if (level == 0)
                    {
                        float peak = 0.0f;
                        for (int i = 0; i < tableSize; ++i)
                            peak = juce::jmax (peak, std::abs (work[(size_t) i]));
                        gain = peak > 0.0f ? 1.0f / peak : 1.0f;

                        // For displays: the harmonics relative to the strongest.
                        auto& shown = spectra[(size_t) (b * numFrames + f)];
                        float largest = 0.0f;
                        for (int h = 1; h <= spectrumShown; ++h)
                            largest = juce::jmax (largest, (float) std::abs (amplitudes[(size_t) h]));
                        shown.resize ((size_t) spectrumShown);
                        for (int h = 1; h <= spectrumShown; ++h)
                            shown[(size_t) h - 1] = largest > 0.0f ? (float) std::abs (amplitudes[(size_t) h]) / largest : 0.0f;
                    }

                    auto* dest = const_cast<float*> (frame (b, f, level));
                    for (int i = 0; i < tableSize; ++i)
                        dest[i] = work[(size_t) i] * gain;
                    dest[tableSize] = dest[0];
                }
            }
        }
    }
}
