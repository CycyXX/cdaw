#include "PhaserParameters.h"
#include "../DSP/TempoSync.h"

namespace cdaw::PhaserParameters
{
    namespace
    {
        using namespace SpecText;

        juce::String signedPercent (float v)    { return (v > 0.005f ? "+" : "") + juce::String (juce::roundToInt (v * 100.0f)) + " %"; }
        juce::String degrees (float v)          { return juce::String (juce::roundToInt (v * 180.0f)) + juce::String::fromUTF8 ("\xc2\xb0"); }

        ParameterSpecs makeSpecs()
        {
            const auto divisions = TempoSync::getNames();

            ParameterSpecs specs ((size_t) count);
            specs[stages]   = { "stages",   "Stages",   2.0f,   12.0f,  6.0f,   0.0f,   2.0f,
                                [] (float v) { return juce::String (juce::roundToInt (v)) + " stages"; }, parseFloat };
            specs[rate]     = { "rate",     "Rate",     0.02f,  10.0f,  0.35f,  1.0f,   0.0f, hz, parseHz };
            specs[sync]     = { "sync",     "Sync",     0.0f,   1.0f,   0.0f,   0.0f,   1.0f, onOff, parseOnOff };
            specs[division] = { "division", "Note",     0.0f,   (float) (TempoSync::divisions.size() - 1), 15.0f, 0.0f, 1.0f,
                                choice (divisions), parseChoice (divisions) };
            specs[depth]    = { "depth",    "Depth",    0.0f,   1.0f,   0.6f,   0.0f,   0.0f, percent, parsePercent };
            specs[centre]   = { "centre",   "Centre",   100.0f, 6000.0f, 900.0f, 800.0f, 0.0f, hz, parseHz };
            specs[feedback] = { "feedback", "Feedback", -0.9f,  0.9f,   0.45f,  0.0f,   0.0f, signedPercent, parsePercent };
            specs[spread]   = { "spread",   "Spread",   0.0f,   1.0f,   0.5f,   0.0f,   0.0f, degrees,
                                [] (const juce::String& s) { return s.getFloatValue() / 180.0f; } };
            specs[mix]      = { "mix",      "Mix",      0.0f,   1.0f,   0.5f,   0.0f,   0.0f, percent, parsePercent };
            return specs;
        }
    }

    const ParameterSpecs& all()
    {
        static const auto specs = makeSpecs();
        return specs;
    }

    const ParameterSpec& get (ID id)
    {
        return all()[(size_t) id];
    }

    PhaserEngine::Settings toSettings (const std::function<float (int)>& valueOf)
    {
        PhaserEngine::Settings s;
        s.stages = juce::jlimit (2, PhaserEngine::maxStages, 2 * juce::roundToInt (valueOf (stages) * 0.5f));
        s.rateHz = valueOf (rate);
        s.sync = valueOf (sync) >= 0.5f;
        s.division = juce::roundToInt (valueOf (division));
        s.depth = valueOf (depth);
        s.centreHz = valueOf (centre);
        s.feedback = valueOf (feedback);
        s.spread = valueOf (spread);
        s.mix = valueOf (mix);
        return s;
    }
}
