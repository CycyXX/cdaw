#pragma once

namespace cdaw
{
    /** A topology-preserving state-variable filter (Zavalishin / Cytomic): stable however fast its
        cutoff moves. `g` is tan (pi * cutoff / sampleRate), `k` is 1 / Q. Used by C-Filter and
        C-Synth's voices. */
    struct Svf
    {
        double ic1 = 0.0, ic2 = 0.0;
        void clear()            { ic1 = ic2 = 0.0; }

        /** What `g` and `k` come to, for filters that share them (the sides of a stereo voice). */
        struct Coefficients
        {
            Coefficients (double g, double kToUse) noexcept
                : k (kToUse), a1 (1.0 / (1.0 + g * (g + k))), a2 (g * a1), a3 (g * a2) {}

            double k, a1, a2, a3;
        };

        /** One sample; returns low, band (unity peak) and high pass outputs. */
        void process (double x, const Coefficients& c, double& low, double& band, double& high) noexcept
        {
            const double v3 = x - ic2;
            const double v1 = c.a1 * ic1 + c.a2 * v3;
            const double v2 = ic2 + c.a2 * ic1 + c.a3 * v3;
            ic1 = 2.0 * v1 - ic1;
            ic2 = 2.0 * v2 - ic2;
            low = v2;
            band = c.k * v1;
            high = x - c.k * v1 - v2;
        }

        void process (double x, double g, double k, double& low, double& band, double& high) noexcept
        {
            process (x, Coefficients (g, k), low, band, high);
        }

        /** Keeps the state out of denormals in silence. */
        void flushDenormals() noexcept
        {
            if (ic1 < 1.0e-20 && ic1 > -1.0e-20) ic1 = 0.0;
            if (ic2 < 1.0e-20 && ic2 > -1.0e-20) ic2 = 0.0;
        }
    };
}
