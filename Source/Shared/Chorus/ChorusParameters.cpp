#include "ChorusParameters.h"

namespace cdaw::ChorusParameters
{
    namespace
    {
        using namespace SpecText;

        juce::String signedPercent (float v)    { return (v > 0.005f ? "+" : "") + juce::String (juce::roundToInt (v * 100.0f)) + " %"; }

        ParameterSpecs makeSpecs()
        {
            const float toneOff = ChorusEngine::toneOff;

            ParameterSpecs specs ((size_t) count);
            specs[voices]   = { "voices",   "Voices",   1.0f,    4.0f,    2.0f,    0.0f,    1.0f,
                                [] (float v) { auto n = juce::roundToInt (v); return juce::String (n) + (n == 1 ? " voice" : " voices"); }, parseFloat };
            specs[rate]     = { "rate",     "Rate",     0.03f,   10.0f,   0.6f,    1.0f,    0.0f, hz, parseHz };
            specs[depth]    = { "depth",    "Depth",    0.0f,    1.0f,    0.45f,   0.0f,    0.0f, percent, parsePercent };
            specs[delay]    = { "delay",    "Delay",    0.5f,    30.0f,   9.0f,    8.0f,    0.0f, ms, parseFloat };
            specs[feedback] = { "feedback", "Feedback", -0.95f,  0.95f,   0.0f,    0.0f,    0.0f, signedPercent, parsePercent };
            specs[spread]   = { "spread",   "Spread",   0.0f,    1.0f,    0.7f,    0.0f,    0.0f, percent, parsePercent };
            specs[tone]     = { "tone",     "Tone",     1000.0f, toneOff, 12000.0f, 6000.0f, 0.0f,
                                [toneOff] (float v) { return v >= toneOff - 1.0f ? juce::String ("Open") : hz (v); }, parseHz };
            specs[width]    = { "width",    "Width",    0.0f,    2.0f,    1.0f,    0.0f,    0.0f, percent, parsePercent };
            specs[mix]      = { "mix",      "Mix",      0.0f,    1.0f,    0.5f,    0.0f,    0.0f, percent, parsePercent };
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

    ChorusEngine::Settings toSettings (const std::function<float (int)>& valueOf)
    {
        ChorusEngine::Settings s;
        s.voices = juce::roundToInt (valueOf (voices));
        s.rateHz = valueOf (rate);
        s.depth = valueOf (depth);
        s.delayMs = valueOf (delay);
        s.feedback = valueOf (feedback);
        s.spread = valueOf (spread);
        s.tone = valueOf (tone);
        s.width = valueOf (width);
        s.mix = valueOf (mix);
        return s;
    }

    const std::vector<Character>& getCharacters()
    {
        static const std::vector<Character> characters {
            { "Chorus",   "Two voices, gentle and wide",
              { { voices, 2.0f }, { rate, 0.6f }, { depth, 0.45f }, { delay, 9.0f }, { feedback, 0.0f }, { spread, 0.7f }, { mix, 0.5f } } },
            { "Ensemble", "Four slow voices drifting apart: strings and pads",
              { { voices, 4.0f }, { rate, 0.35f }, { depth, 0.3f }, { delay, 14.0f }, { feedback, 0.0f }, { spread, 1.0f }, { mix, 0.55f } } },
            { "Flanger",  "One short, resonant voice: the jet sweep",
              { { voices, 1.0f }, { rate, 0.18f }, { depth, 0.55f }, { delay, 0.8f }, { feedback, 0.7f }, { spread, 0.5f }, { mix, 0.5f } } },
            { "Vibrato",  "Only the swept voice: pitch wobble",
              { { voices, 1.0f }, { rate, 5.0f }, { depth, 0.25f }, { delay, 3.0f }, { feedback, 0.0f }, { spread, 0.0f }, { mix, 1.0f } } },
        };
        return characters;
    }
}
