#include "CVerbParameters.h"

namespace cdaw::CVerbParameters
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

        float parseFloat (const juce::String& s)    { return s.getFloatValue(); }
        juce::String percent (float v)              { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }
        float parsePercent (const juce::String& s)  { return s.getFloatValue() / 100.0f; }
        juce::String multiplier (float v)           { return "x" + juce::String (v, 2); }
        float parseMultiplier (const juce::String& s) { return s.trimCharactersAtStart ("x").getFloatValue(); }

        juce::String lowCutText (float v)   { return v <= ReverbEngine::lowCutOff + 0.5f ? juce::String ("Off") : formatHz (v); }
        juce::String highCutText (float v)  { return v >= ReverbEngine::highCutOff - 1.0f ? juce::String ("Off") : formatHz (v); }

        std::vector<Spec> makeSpecs()
        {
            const float lowOff = ReverbEngine::lowCutOff, highOff = ReverbEngine::highCutOff;

            std::vector<Spec> specs ((size_t) count);
            specs[mix]         = { "mix",         "Mix",               0.0f,   1.0f,    0.3f,    0.0f,    0.0f, percent, parsePercent };
            specs[preDelay]    = { "predelay",    "Pre-delay",         0.0f,   250.0f,  20.0f,   40.0f,   0.0f,
                                   [] (float v) { return juce::String (juce::roundToInt (v)) + " ms"; }, parseFloat };
            specs[size]        = { "size",        "Size",              0.0f,   1.0f,    0.55f,   0.0f,    0.0f, percent, parsePercent };
            specs[decay]       = { "decay",       "Decay",             0.2f,   20.0f,   2.2f,    2.5f,    0.0f,
                                   [] (float v) { return juce::String (v, v < 10.0f ? 2 : 1) + " s"; }, parseFloat };
            specs[diffusion]   = { "diffusion",   "Diffusion",         0.0f,   1.0f,    0.75f,   0.0f,    0.0f, percent, parsePercent };
            specs[modulation]  = { "modulation",  "Modulation",        0.0f,   1.0f,    0.3f,    0.0f,    0.0f, percent, parsePercent };
            specs[lowDecay]    = { "lowdecay",    "Low decay",         0.5f,   2.0f,    1.1f,    1.0f,    0.0f, multiplier, parseMultiplier };
            specs[highDecay]   = { "highdecay",   "High decay",        0.1f,   1.0f,    0.5f,    0.0f,    0.0f, multiplier, parseMultiplier };
            specs[dampFreq]    = { "dampfreq",    "Damping frequency", 1000.0f, 16000.0f, 5000.0f, 4000.0f, 0.0f, formatHz, parseHz };
            specs[early]       = { "early",       "Early reflections", 0.0f,   1.0f,    0.4f,    0.0f,    0.0f, percent, parsePercent };
            specs[width]       = { "width",       "Width",             0.0f,   2.0f,    1.0f,    0.0f,    0.0f, percent, parsePercent };
            specs[preLowCut]   = { "prelowcut",   "Pre low cut",       lowOff, 1000.0f, lowOff,  150.0f,  0.0f, lowCutText, parseHz };
            specs[preHighCut]  = { "prehighcut",  "Pre high cut",      1000.0f, highOff, highOff, 6000.0f, 0.0f, highCutText, parseHz };
            specs[postLowCut]  = { "postlowcut",  "Post low cut",      lowOff, 1000.0f, 100.0f,  150.0f,  0.0f, lowCutText, parseHz };
            specs[postHighCut] = { "posthighcut", "Post high cut",     1000.0f, highOff, 12000.0f, 6000.0f, 0.0f, highCutText, parseHz };
            specs[freeze]      = { "freeze",      "Freeze",            0.0f,   1.0f,    0.0f,    0.0f,    1.0f,
                                   [] (float v) { return v >= 0.5f ? juce::String ("On") : juce::String ("Off"); }, parseFloat };
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

    const Spec& get (ID id)
    {
        static const auto specs = makeSpecs();
        return specs[(size_t) id];
    }

    ReverbEngine::Settings toSettings (const std::function<float (ID)>& valueOf)
    {
        ReverbEngine::Settings s;
        s.mix = valueOf (mix);
        s.preDelay = valueOf (preDelay);
        s.size = valueOf (size);
        s.decay = valueOf (decay);
        s.diffusion = valueOf (diffusion);
        s.modulation = valueOf (modulation);
        s.lowDecay = valueOf (lowDecay);
        s.highDecay = valueOf (highDecay);
        s.dampFreq = valueOf (dampFreq);
        s.early = valueOf (early);
        s.width = valueOf (width);
        s.preLowCut = valueOf (preLowCut);
        s.preHighCut = valueOf (preHighCut);
        s.postLowCut = valueOf (postLowCut);
        s.postHighCut = valueOf (postHighCut);
        s.freeze = valueOf (freeze) >= 0.5f;
        return s;
    }
}
