#include "MultibandParameters.h"

namespace cdaw::MultibandParameters
{
    namespace
    {
        juce::String formatHz (float hz)
        {
            return hz >= 1000.0f ? juce::String (hz / 1000.0f, hz >= 10000.0f ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (hz)) + " Hz";
        }

        float parseHz (const juce::String& s)
        {
            auto v = s.getFloatValue();
            return s.containsIgnoreCase ("k") ? v * 1000.0f : v;
        }

        juce::String formatDb (float v)             { return (v > 0.04f ? "+" : "") + juce::String (v, 1) + " dB"; }
        float parseFloat (const juce::String& s)    { return s.getFloatValue(); }

        std::vector<Spec> makeSpecs()
        {
            static constexpr float defaultCrossovers[numBands - 1] { 120.0f, 1000.0f, 6000.0f };
            static constexpr float defaultAttack[numBands] { 30.0f, 15.0f, 8.0f, 5.0f };
            static constexpr float defaultRelease[numBands] { 250.0f, 180.0f, 120.0f, 90.0f };

            std::vector<Spec> specs ((size_t) count);

            for (int i = 0; i < numBands - 1; ++i)
                specs[(size_t) crossover (i)] = { "x" + juce::String (i + 1), "Crossover " + juce::String (i + 1),
                                                  MultibandEngine::minCrossover, MultibandEngine::maxCrossover, defaultCrossovers[i], 1000.0f, 0.0f,
                                                  formatHz, parseHz };

            for (int b = 0; b < numBands; ++b)
            {
                auto p = "b" + juce::String (b + 1);
                auto name = bandName (b) + " ";
                auto set = [&] (BandParam what, const char* suffix, const char* label, float lo, float hi, float def, float skew, float interval,
                                std::function<juce::String (float)> toText)
                {
                    specs[(size_t) bandParam (b, what)] = { p + suffix, name + label, lo, hi, def, skew, interval, std::move (toText), parseFloat };
                };

                // Bands start off: the plugin is transparent until a band is switched on.
                set (bandOn,        "on",        "Active",    0.0f,   1.0f,    0.0f,    0.0f,   1.0f,
                     [] (float v) { return v >= 0.5f ? juce::String ("On") : juce::String ("Bypassed"); });
                set (bandSolo,      "solo",      "Solo",      0.0f,   1.0f,    0.0f,    0.0f,   1.0f,
                     [] (float v) { return v >= 0.5f ? juce::String ("Solo") : juce::String ("-"); });
                set (bandThreshold, "threshold", "Threshold", -60.0f, 0.0f,    -18.0f,  0.0f,   0.0f,
                     [] (float v) { return juce::String (v, 1) + " dB"; });
                set (bandRatio,     "ratio",     "Ratio",     1.0f,   20.0f,   3.0f,    4.0f,   0.0f,
                     [] (float v) { return juce::String (v, 1) + ":1"; });
                set (bandAttack,    "attack",    "Attack",    0.1f,   200.0f,  defaultAttack[b], 15.0f, 0.0f,
                     [] (float v) { return juce::String (v, v < 10.0f ? 1 : 0) + " ms"; });
                set (bandRelease,   "release",   "Release",   5.0f,   1000.0f, defaultRelease[b], 150.0f, 0.0f,
                     [] (float v) { return juce::String (juce::roundToInt (v)) + " ms"; });
                set (bandGain,      "gain",      "Gain",      -24.0f, 24.0f,   0.0f,    0.0f,   0.0f, formatDb);
            }

            specs[(size_t) output] = { "output", "Output", -24.0f, 24.0f, 0.0f, 0.0f, 0.0f, formatDb, parseFloat };
            return specs;
        }
    }

    juce::NormalisableRange<float> Spec::getRange() const
    {
        juce::NormalisableRange<float> range (minimum, maximum, interval);
        if (skewCentre > minimum && skewCentre < maximum)
            range.setSkewForCentre (skewCentre);
        return range;
    }

    const Spec& get (int index)
    {
        static const auto specs = makeSpecs();
        return specs[(size_t) index];
    }

    juce::String bandName (int band)
    {
        static const char* names[numBands] { "Low", "Low Mid", "High Mid", "High" };
        return names[juce::jlimit (0, numBands - 1, band)];
    }

    MultibandEngine::Settings toSettings (const std::function<float (int)>& valueOf)
    {
        MultibandEngine::Settings s;

        for (int i = 0; i < numBands - 1; ++i)
            s.crossovers[(size_t) i] = valueOf (crossover (i));

        for (int b = 0; b < numBands; ++b)
        {
            auto& band = s.bands[(size_t) b];
            band.on = valueOf (bandParam (b, bandOn)) >= 0.5f;
            band.solo = valueOf (bandParam (b, bandSolo)) >= 0.5f;
            band.threshold = valueOf (bandParam (b, bandThreshold));
            band.ratio = valueOf (bandParam (b, bandRatio));
            band.attack = valueOf (bandParam (b, bandAttack));
            band.release = valueOf (bandParam (b, bandRelease));
            band.gain = valueOf (bandParam (b, bandGain));
        }

        s.output = valueOf (output);
        return s;
    }
}
