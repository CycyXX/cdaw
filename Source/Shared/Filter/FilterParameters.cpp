#include "FilterParameters.h"
#include "../DSP/TempoSync.h"

namespace cdaw::FilterParameters
{
    namespace
    {
        using namespace SpecText;

        juce::String octaves (float v, bool bipolar)
        {
            if (std::abs (v) < 0.005f)
                return "Off";

            auto oct = v * FilterEngine::maxModOctaves;
            return (bipolar ? (oct > 0.0f ? "+" : "") : juce::String::fromUTF8 ("\xc2\xb1")) + juce::String (oct, 1) + " oct";
        }

        float parseOctaves (const juce::String& s)
        {
            return s.retainCharacters ("-0123456789.").getFloatValue() / FilterEngine::maxModOctaves;
        }

        ParameterSpecs makeSpecs()
        {
            const auto types = getTypeNames(), shapes = getShapeNames(), divisions = TempoSync::getNames();
            const juce::StringArray slopes { "12 dB", "24 dB" };
            const float minHz = FilterEngine::minHz, maxHz = FilterEngine::maxHz;

            ParameterSpecs specs ((size_t) count);
            // Not "type": Tracktion keeps the plugin's own type name in a property of that name.
            specs[type]        = { "filtertype",  "Type",        0.0f,  3.0f,   0.0f,    0.0f,    1.0f, choice (types), parseChoice (types) };
            specs[slope]       = { "slope",       "Slope",       0.0f,  1.0f,   0.0f,    0.0f,    1.0f, choice (slopes), parseChoice (slopes) };
            specs[cutoff]      = { "frequency",   "Cutoff",      minHz, maxHz,  2000.0f, 1000.0f, 0.0f, hz, parseHz };
            specs[resonance]   = { "resonance",   "Resonance",   0.0f,  1.0f,   0.3f,    0.0f,    0.0f, percent, parsePercent };
            specs[drive]       = { "drive",       "Drive",       0.0f,  1.0f,   0.0f,    0.0f,    0.0f, percent, parsePercent };
            specs[lfoRate]     = { "lforate",     "LFO rate",    0.02f, 20.0f,  1.0f,    1.5f,    0.0f, hz, parseHz };
            specs[lfoSync]     = { "lfosync",     "LFO sync",    0.0f,  1.0f,   0.0f,    0.0f,    1.0f, onOff, parseOnOff };
            specs[lfoDivision] = { "lfodivision", "LFO note",    0.0f,  (float) (TempoSync::divisions.size() - 1), 9.0f, 0.0f, 1.0f,
                                   choice (divisions), parseChoice (divisions) };
            specs[lfoAmount]   = { "lfoamount",   "LFO amount",  0.0f,  1.0f,   0.0f,    0.0f,    0.0f,
                                   [] (float v) { return octaves (v, false); }, parseOctaves };
            specs[lfoShape]    = { "lfoshape",    "LFO shape",   0.0f,  (float) ((int) FilterEngine::Shape::numShapes - 1), 0.0f, 0.0f, 1.0f,
                                   choice (shapes), parseChoice (shapes) };
            specs[lfoStereo]   = { "lfostereo",   "LFO stereo",  0.0f,  1.0f,   0.0f,    0.0f,    0.0f,
                                   [] (float v) { return juce::String (juce::roundToInt (v * 180.0f)) + juce::String::fromUTF8 ("\xc2\xb0"); },
                                   [] (const juce::String& s) { return s.getFloatValue() / 180.0f; } };
            specs[envAmount]   = { "envamount",   "Envelope",    -1.0f, 1.0f,   0.0f,    0.0f,    0.0f,
                                   [] (float v) { return octaves (v, true); }, parseOctaves };
            specs[envRelease]  = { "envrelease",  "Env release", 10.0f, 2000.0f, 150.0f, 200.0f,  0.0f, ms, parseFloat };
            specs[mix]         = { "mix",         "Mix",         0.0f,  1.0f,   1.0f,    0.0f,    0.0f, percent, parsePercent };
            specs[output]      = { "output",      "Output",      -24.0f, 24.0f, 0.0f,    0.0f,    0.0f, db, parseFloat };
            return specs;
        }
    }

    juce::StringArray getTypeNames()    { return { "Low pass", "High pass", "Band pass", "Notch" }; }
    juce::StringArray getShapeNames()   { return { "Sine", "Triangle", "Ramp up", "Ramp down", "Square", "Random" }; }

    const ParameterSpecs& all()
    {
        static const auto specs = makeSpecs();
        return specs;
    }

    const ParameterSpec& get (ID id)
    {
        return all()[(size_t) id];
    }

    FilterEngine::Settings toSettings (const std::function<float (int)>& valueOf)
    {
        FilterEngine::Settings s;
        s.type = (FilterEngine::Type) juce::jlimit (0, 3, juce::roundToInt (valueOf (type)));
        s.steep = valueOf (slope) >= 0.5f;
        s.cutoff = valueOf (cutoff);
        s.resonance = valueOf (resonance);
        s.drive = valueOf (drive);
        s.lfoRate = valueOf (lfoRate);
        s.lfoSync = valueOf (lfoSync) >= 0.5f;
        s.lfoDivision = juce::roundToInt (valueOf (lfoDivision));
        s.lfoAmount = valueOf (lfoAmount);
        s.lfoShape = (FilterEngine::Shape) juce::jlimit (0, (int) FilterEngine::Shape::numShapes - 1, juce::roundToInt (valueOf (lfoShape)));
        s.lfoStereo = valueOf (lfoStereo);
        s.envAmount = valueOf (envAmount);
        s.envRelease = valueOf (envRelease);
        s.mix = valueOf (mix);
        s.outputDb = valueOf (output);
        return s;
    }
}
