#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <functional>
#include <vector>

namespace cdaw
{
    /** One parameter of a CDAW effect, defined once for both builds (CDAW's built-in plugin and
        the VST3): ID, name, range, default and display text. The IDs are stored in projects and
        presets: never change one. Used by C-Delay, C-Chorus, C-Phaser and C-Filter. */
    struct ParameterSpec
    {
        const char* id;
        const char* name;
        float minimum, maximum, defaultValue;
        float skewCentre = 0.0f;        // 0 = linear
        float interval = 0.0f;          // 1 for switches and choices
        std::function<juce::String (float)> toText;
        std::function<float (const juce::String&)> fromText;

        juce::NormalisableRange<float> getRange() const
        {
            juce::NormalisableRange<float> range (minimum, maximum, interval);
            if (skewCentre > minimum && skewCentre < maximum)
                range.setSkewForCentre (skewCentre);
            return range;
        }
    };

    using ParameterSpecs = std::vector<ParameterSpec>;

    /** Display text and parsing shared by the effects' parameter lists. */
    namespace SpecText
    {
        inline float parseFloat (const juce::String& s)     { return s.getFloatValue(); }
        inline juce::String percent (float v)               { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }
        inline float parsePercent (const juce::String& s)   { return s.getFloatValue() / 100.0f; }
        inline juce::String onOff (float v)                 { return v >= 0.5f ? juce::String ("On") : juce::String ("Off"); }
        inline float parseOnOff (const juce::String& s)     { return s.trim().equalsIgnoreCase ("on") || s.getFloatValue() >= 0.5f ? 1.0f : 0.0f; }

        inline juce::String hz (float v)
        {
            return v >= 1000.0f ? juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz"
                                : juce::String (v, v < 10.0f ? 2 : (v < 100.0f ? 1 : 0)) + " Hz";
        }

        inline float parseHz (const juce::String& s)
        {
            auto v = s.getFloatValue();
            return s.containsIgnoreCase ("k") ? v * 1000.0f : v;
        }

        inline juce::String ms (float v)            { return juce::String (v, v < 10.0f ? 2 : (v < 100.0f ? 1 : 0)) + " ms"; }

        inline juce::String db (float v)            { return (v > 0.04f ? "+" : "") + juce::String (v, 1) + " dB"; }

        /** A choice from a list of names (the value is the index). */
        inline std::function<juce::String (float)> choice (juce::StringArray names)
        {
            return [names] (float v) { return names[juce::jlimit (0, names.size() - 1, juce::roundToInt (v))]; };
        }

        inline std::function<float (const juce::String&)> parseChoice (juce::StringArray names)
        {
            return [names] (const juce::String& s)
            {
                auto i = names.indexOf (s.trim(), true);
                return (float) (i >= 0 ? i : juce::jlimit (0, names.size() - 1, s.getIntValue()));
            };
        }
    }
}
