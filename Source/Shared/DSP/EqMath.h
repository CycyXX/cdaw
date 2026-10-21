#pragma once

#include <array>
#include <cmath>
#include <complex>

namespace cdaw::EqMath
{
    inline constexpr double pi = 3.14159265358979323846;

    /** Filter shapes of one EQ band. Stored as integers in projects: append only. */
    enum class BandType { lowCut = 0, lowShelf, peak, highShelf, highCut, notch, numTypes };

    inline const char* getTypeName (BandType t)
    {
        switch (t)
        {
            case BandType::lowCut:    return "Low Cut";
            case BandType::lowShelf:  return "Low Shelf";
            case BandType::peak:      return "Peak";
            case BandType::highShelf: return "High Shelf";
            case BandType::highCut:   return "High Cut";
            case BandType::notch:     return "Notch";
            case BandType::numTypes:  break;
        }
        return "";
    }

    inline bool hasGain (BandType t)    { return t == BandType::lowShelf || t == BandType::peak || t == BandType::highShelf; }
    inline bool isCut (BandType t)      { return t == BandType::lowCut || t == BandType::highCut; }

    /** Cut slopes in dB/octave, by slope index. */
    inline constexpr std::array<int, 4> slopes { 6, 12, 24, 48 };

    /** Normalised biquad (a0 = 1). */
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;

        std::complex<double> response (double w) const
        {
            const std::complex<double> z1 = std::polar (1.0, -w), z2 = std::polar (1.0, -2.0 * w);
            return (b0 + b1 * z1 + b2 * z2) / (1.0 + a1 * z1 + a2 * z2);
        }
    };

    /** Up to four sections: a 48 dB/oct cut is four cascaded biquads. */
    struct BandFilter
    {
        std::array<Biquad, 4> sections;
        int numSections = 0;

        /** Gain in dB at `hz`. */
        double magnitudeDb (double hz, double sampleRate) const
        {
            const double w = 2.0 * pi * hz / sampleRate;
            std::complex<double> h (1.0, 0.0);

            for (int i = 0; i < numSections; ++i)
                h *= sections[(size_t) i].response (w);

            return 20.0 * std::log10 (std::max (1.0e-9, std::abs (h)));
        }
    };

    namespace detail
    {
        inline Biquad normalise (double b0, double b1, double b2, double a0, double a1, double a2)
        {
            return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
        }

        // RBJ "Audio EQ Cookbook" designs.
        inline Biquad peak (double w0, double q, double gainDb)
        {
            const double A = std::pow (10.0, gainDb / 40.0), alpha = std::sin (w0) / (2.0 * q), c = std::cos (w0);
            return normalise (1 + alpha * A, -2 * c, 1 - alpha * A, 1 + alpha / A, -2 * c, 1 - alpha / A);
        }

        inline Biquad lowShelf (double w0, double q, double gainDb)
        {
            const double A = std::pow (10.0, gainDb / 40.0), alpha = std::sin (w0) / (2.0 * q), c = std::cos (w0);
            const double s = 2.0 * std::sqrt (A) * alpha;
            return normalise (A * ((A + 1) - (A - 1) * c + s), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - s),
                              (A + 1) + (A - 1) * c + s, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - s);
        }

        inline Biquad highShelf (double w0, double q, double gainDb)
        {
            const double A = std::pow (10.0, gainDb / 40.0), alpha = std::sin (w0) / (2.0 * q), c = std::cos (w0);
            const double s = 2.0 * std::sqrt (A) * alpha;
            return normalise (A * ((A + 1) + (A - 1) * c + s), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - s),
                              (A + 1) - (A - 1) * c + s, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - s);
        }

        inline Biquad highPass (double w0, double q)
        {
            const double alpha = std::sin (w0) / (2.0 * q), c = std::cos (w0);
            return normalise ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + alpha, -2 * c, 1 - alpha);
        }

        inline Biquad lowPass (double w0, double q)
        {
            const double alpha = std::sin (w0) / (2.0 * q), c = std::cos (w0);
            return normalise ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + alpha, -2 * c, 1 - alpha);
        }

        inline Biquad notch (double w0, double q)
        {
            const double alpha = std::sin (w0) / (2.0 * q), c = std::cos (w0);
            return normalise (1, -2 * c, 1, 1 + alpha, -2 * c, 1 - alpha);
        }

        inline Biquad allPass (double w0, double q)
        {
            const double alpha = std::sin (w0) / (2.0 * q), c = std::cos (w0);
            return normalise (1 - alpha, -2 * c, 1 + alpha, 1 + alpha, -2 * c, 1 - alpha);
        }

        // First order (6 dB/oct), bilinear transform.
        inline Biquad highPass1 (double w0)
        {
            const double k = std::tan (w0 / 2.0);
            return { 1.0 / (1.0 + k), -1.0 / (1.0 + k), 0.0, (k - 1.0) / (k + 1.0), 0.0 };
        }

        inline Biquad lowPass1 (double w0)
        {
            const double k = std::tan (w0 / 2.0);
            return { k / (1.0 + k), k / (1.0 + k), 0.0, (k - 1.0) / (k + 1.0), 0.0 };
        }
    }

    /** Designs one band. 12 dB/oct cuts use the band's Q as resonance; 24 and 48 are Butterworth. */
    inline BandFilter design (BandType type, int slopeIndex, double hz, double gainDb, double q, double sampleRate)
    {
        BandFilter f;
        const double nyquistSafe = sampleRate * 0.49;
        hz = std::min (std::max (hz, 10.0), nyquistSafe);
        q = std::max (q, 0.05);
        const double w0 = 2.0 * pi * hz / sampleRate;

        auto cut = [&] (bool high)
        {
            static constexpr double q24[] { 0.54119610, 1.30656296 };
            static constexpr double q48[] { 0.50979558, 0.60134489, 0.89997622, 2.56291545 };

            switch (slopeIndex)
            {
                case 0:  f.sections[0] = high ? detail::lowPass1 (w0) : detail::highPass1 (w0); f.numSections = 1; break;
                case 1:  f.sections[0] = high ? detail::lowPass (w0, q) : detail::highPass (w0, q); f.numSections = 1; break;
                case 2:
                    for (int i = 0; i < 2; ++i)
                        f.sections[(size_t) i] = high ? detail::lowPass (w0, q24[i]) : detail::highPass (w0, q24[i]);
                    f.numSections = 2;
                    break;
                default:
                    for (int i = 0; i < 4; ++i)
                        f.sections[(size_t) i] = high ? detail::lowPass (w0, q48[i]) : detail::highPass (w0, q48[i]);
                    f.numSections = 4;
                    break;
            }
        };

        switch (type)
        {
            case BandType::lowCut:    cut (false); break;
            case BandType::highCut:   cut (true); break;
            case BandType::lowShelf:  f.sections[0] = detail::lowShelf (w0, q, gainDb);  f.numSections = 1; break;
            case BandType::highShelf: f.sections[0] = detail::highShelf (w0, q, gainDb); f.numSections = 1; break;
            case BandType::notch:     f.sections[0] = detail::notch (w0, q);             f.numSections = 1; break;
            case BandType::peak:
            case BandType::numTypes:
            default:                  f.sections[0] = detail::peak (w0, q, gainDb);      f.numSections = 1; break;
        }

        return f;
    }

    /** Linkwitz-Riley 4th-order crossover at `hz`: low and high outputs are each the given
        biquad applied twice; their sum equals `allPass` once (flat magnitude), so a band that
        doesn't pass through this crossover is run through `allPass` to stay in phase. */
    struct Crossover
    {
        Biquad lowPass, highPass, allPass;
    };

    inline Crossover linkwitzRiley (double hz, double sampleRate)
    {
        hz = std::min (std::max (hz, 10.0), sampleRate * 0.45);
        const double w0 = 2.0 * pi * hz / sampleRate, q = 0.70710678118654752;
        return { detail::lowPass (w0, q), detail::highPass (w0, q), detail::allPass (w0, q) };
    }
}
