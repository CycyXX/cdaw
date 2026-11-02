#include "EsserParameters.h"

namespace cdaw::EsserParameters
{
    namespace
    {
        using namespace SpecText;

        float parseDb (const juce::String& s)       { return s.getFloatValue(); }

        ParameterSpecs makeSpecs()
        {
            const juce::StringArray modes { "Split", "Wideband" };
            const juce::StringArray listens { "Off", "Band", "Removed" };

            ParameterSpecs specs ((size_t) count);
            specs[threshold]     = { "threshold", "Threshold", -60.0f,  0.0f,     -24.0f,  0.0f,    0.0f, db, parseDb };
            specs[autoThreshold] = { "auto",      "Auto",      0.0f,    1.0f,     0.0f,    0.0f,    1.0f, onOff, parseOnOff };
            specs[range]         = { "range",     "Range",     0.0f,    24.0f,    12.0f,   0.0f,    0.0f,
                                     [] (float v) { return juce::String (v, 1) + " dB"; }, parseDb };
            specs[release]       = { "release",   "Release",   1.0f,    500.0f,   100.0f,  60.0f,   0.0f, ms, parseFloat };
            specs[lowfreq]       = { "lowfreq",   "Low",       1000.0f, 16000.0f, 4000.0f, 5000.0f, 0.0f, hz, parseHz };
            specs[highfreq]      = { "highfreq",  "High",      2000.0f, 20000.0f, 10000.0f, 9000.0f, 0.0f, hz, parseHz };
            specs[mode]          = { "mode",      "Mode",      0.0f,    1.0f,     0.0f,    0.0f,    1.0f, choice (modes), parseChoice (modes) };
            specs[listen]        = { "listen",    "Listen",    0.0f,    2.0f,     0.0f,    0.0f,    1.0f, choice (listens), parseChoice (listens) };
            specs[output]        = { "output",    "Output",    -12.0f,  12.0f,    0.0f,    0.0f,    0.0f, db, parseDb };
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

    EsserEngine::Settings toSettings (const std::function<float (int)>& valueOf)
    {
        EsserEngine::Settings s;
        s.thresholdDb = valueOf (threshold);
        s.autoThreshold = valueOf (autoThreshold) >= 0.5f;
        s.rangeDb = valueOf (range);
        s.releaseMs = valueOf (release);
        s.lowHz = valueOf (lowfreq);
        s.highHz = valueOf (highfreq);
        s.mode = valueOf (mode) >= 0.5f ? EsserEngine::Mode::wideband : EsserEngine::Mode::split;
        s.listen = (EsserEngine::Listen) juce::jlimit (0, 2, juce::roundToInt (valueOf (listen)));
        s.outputDb = valueOf (output);
        return s;
    }
}
