#include "DelayParameters.h"
#include "../DSP/TempoSync.h"

namespace cdaw::DelayParameters
{
    namespace
    {
        using namespace SpecText;

        ParameterSpecs makeSpecs()
        {
            const float lowOff = DelayEngine::lowCutOff, highOff = DelayEngine::highCutOff;
            const auto divisions = TempoSync::getNames (TempoSync::numDelayDivisions);
            const juce::StringArray modes { "Stereo", "Ping-pong" };

            ParameterSpecs specs ((size_t) count);
            specs[time]     = { "time",     "Time",     1.0f,   2000.0f, 375.0f,  300.0f,  0.0f, ms, parseFloat };
            specs[sync]     = { "sync",     "Sync",     0.0f,   1.0f,    1.0f,    0.0f,    1.0f, onOff, parseOnOff };
            specs[division] = { "division", "Note",     0.0f,   (float) (TempoSync::numDelayDivisions - 1), 8.0f, 0.0f, 1.0f,
                                choice (divisions), parseChoice (divisions) };
            specs[mode]     = { "mode",     "Mode",     0.0f,   1.0f,    0.0f,    0.0f,    1.0f, choice (modes), parseChoice (modes) };
            specs[spread]   = { "spread",   "Spread",   -1.0f,  1.0f,    0.0f,    0.0f,    0.0f,
                                [] (float v) { return std::abs (v) < 0.005f ? juce::String ("Off") : "R " + juce::String (v > 0 ? "+" : "") + juce::String (juce::roundToInt (v * 50.0f)) + " %"; },
                                [] (const juce::String& s) { return s.retainCharacters ("-0123456789.").getFloatValue() / 50.0f; } };
            specs[feedback] = { "feedback", "Feedback", 0.0f,   1.0f,    0.4f,    0.0f,    0.0f, percent, parsePercent };
            specs[lowCut]   = { "lowcut",   "Low cut",  lowOff, 2000.0f, 120.0f,  200.0f,  0.0f,
                                [lowOff] (float v) { return v <= lowOff + 0.5f ? juce::String ("Off") : hz (v); }, parseHz };
            specs[highCut]  = { "highcut",  "High cut", 500.0f, highOff, 6000.0f, 4000.0f, 0.0f,
                                [highOff] (float v) { return v >= highOff - 1.0f ? juce::String ("Off") : hz (v); }, parseHz };
            specs[drive]    = { "drive",    "Drive",    0.0f,   1.0f,    0.0f,    0.0f,    0.0f, percent, parsePercent };
            specs[wobble]   = { "wobble",   "Wobble",   0.0f,   1.0f,    0.0f,    0.0f,    0.0f, percent, parsePercent };
            specs[duck]     = { "duck",     "Duck",     0.0f,   1.0f,    0.0f,    0.0f,    0.0f, percent, parsePercent };
            specs[width]    = { "width",    "Width",    0.0f,   2.0f,    1.0f,    0.0f,    0.0f, percent, parsePercent };
            specs[mix]      = { "mix",      "Mix",      0.0f,   1.0f,    0.3f,    0.0f,    0.0f, percent, parsePercent };
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

    DelayEngine::Settings toSettings (const std::function<float (int)>& valueOf)
    {
        DelayEngine::Settings s;
        s.timeMs = valueOf (time);
        s.sync = valueOf (sync) >= 0.5f;
        s.division = juce::roundToInt (valueOf (division));
        s.mode = valueOf (mode) >= 0.5f ? DelayEngine::Mode::pingPong : DelayEngine::Mode::stereo;
        s.spread = valueOf (spread);
        s.feedback = valueOf (feedback);
        s.lowCut = valueOf (lowCut);
        s.highCut = valueOf (highCut);
        s.drive = valueOf (drive);
        s.wobble = valueOf (wobble);
        s.duck = valueOf (duck);
        s.width = valueOf (width);
        s.mix = valueOf (mix);
        return s;
    }
}
