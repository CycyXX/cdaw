#include "CompressorParameters.h"

namespace cdaw::CompressorParameters
{
    namespace
    {
        float parseFloat (const juce::String& s)    { return s.getFloatValue(); }
        juce::String formatDb (float v)             { return juce::String (v, 1) + " dB"; }
        juce::String formatMs (float v)             { return juce::String (v, 1) + " ms"; }

        std::vector<Spec> makeSpecs()
        {
            std::vector<Spec> specs ((size_t) count);
            specs[threshold]     = { "threshold", "Threshold", -40.0f, 0.0f, -6.0f, 0.0f, formatDb, parseFloat };
            specs[ratio]         = { "ratio", "Ratio", 1.0f, limitRatio, 2.0f, 4.0f,
                                     [] (float v) { return v >= limitRatio ? juce::String ("Limit") : juce::String (v, 1) + ":1"; }, parseFloat };
            specs[knee]          = { "knee", "Knee", 0.0f, 24.0f, 6.0f, 0.0f, formatDb, parseFloat };
            specs[attack]        = { "attack", "Attack", 0.3f, 200.0f, 100.0f, 10.0f, formatMs, parseFloat };
            specs[release]       = { "release", "Release", 10.0f, 300.0f, 100.0f, 80.0f, formatMs, parseFloat };
            specs[makeUp]        = { "makeup", "Make-up", -10.0f, 24.0f, 0.0f, 0.0f, formatDb, parseFloat };
            specs[sidechainGain] = { "scgain", "Sidechain gain", -24.0f, 24.0f, 0.0f, 0.0f, formatDb, parseFloat };
            return specs;
        }
    }

    juce::NormalisableRange<float> Spec::getRange() const
    {
        juce::NormalisableRange<float> range (minimum, maximum);
        if (skewCentre > minimum && skewCentre < maximum)
            range.setSkewForCentre (skewCentre);
        return range;
    }

    const Spec& get (ID id)
    {
        static const auto specs = makeSpecs();
        return specs[(size_t) id];
    }

    CompressorEngine::Settings toSettings (const std::function<float (ID)>& valueOf)
    {
        CompressorEngine::Settings s;
        s.thresholdDb = valueOf (threshold);
        s.ratio = engineRatio (valueOf (ratio));
        s.kneeDb = valueOf (knee);
        s.attackMs = valueOf (attack);
        s.releaseMs = valueOf (release);
        s.makeUpDb = valueOf (makeUp);
        s.sidechainDb = valueOf (sidechainGain);
        return s;
    }
}
