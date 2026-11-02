#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <vector>

namespace cdaw
{
    /** C-Synth's wavetables: six banks of 16 frames, generated from harmonic spectra (no sample
        files), each frame stored band-limited at 11 levels (all harmonics, then half as many...
        down to the fundamental), so a note reads the level whose top harmonic stays below
        Nyquist and nothing aliases. Built once, on first use (call get() off the audio thread
        first, e.g. from prepare()). */
    class Wavetables
    {
    public:
        enum class Bank { analog = 0, vowel, glass, organ, air, pulse, numBanks };

        static constexpr int tableSize = 2048, numFrames = 16, numLevels = 11, maxHarmonics = 1000;
        static constexpr int numBanks = (int) Bank::numBanks;

        static const Wavetables& get();
        static juce::StringArray getBankNames();

        /** The level for a fundamental: the first whose harmonics all stay below 0.45 x the rate. */
        static int levelFor (double hz, double sampleRate) noexcept;

        /** One band-limited frame, tableSize + 1 samples (the last repeats the first). */
        const float* frame (int bank, int frameIndex, int level) const noexcept
        {
            return data.data() + ((size_t) ((bank * numFrames + frameIndex) * numLevels + level)) * (tableSize + 1);
        }

        /** Two neighbouring frames and how far between them: `position` 0..1 across the bank. */
        struct Morph
        {
            const float* first;
            const float* second;
            float blend;
        };

        Morph morphAt (int bank, float position, int level) const noexcept
        {
            const float f = juce::jlimit (0.0f, 1.0f, position) * (float) (numFrames - 1);
            const int f0 = juce::jmin ((int) f, numFrames - 2);
            return { frame (bank, f0, level), frame (bank, f0 + 1, level), f - (float) f0 };
        }

        /** A sample at `phase` (0..1). */
        static float read (const Morph& morph, double phase) noexcept
        {
            const double x = phase * tableSize;
            const int i = (int) x;
            const float t = (float) (x - i);
            const float* a = morph.first + i;
            const float* b = morph.second + i;
            const float va = a[0] + (a[1] - a[0]) * t, vb = b[0] + (b[1] - b[0]) * t;
            return va + (vb - va) * morph.blend;
        }

        float read (int bank, float position, int level, double phase) const noexcept
        {
            return read (morphAt (bank, position, level), phase);
        }

        /** The first 128 harmonics' amplitudes of a frame (relative to its strongest), for displays. */
        const std::vector<float>& harmonics (int bank, int frameIndex) const   { return spectra[(size_t) (bank * numFrames + frameIndex)]; }

        /** The harmonic limit of each level. */
        static int harmonicsAt (int level) noexcept                        { return level == 0 ? maxHarmonics : (512 >> (level - 1)); }

    private:
        Wavetables();

        std::vector<float> data;
        std::vector<std::vector<float>> spectra;
    };
}
