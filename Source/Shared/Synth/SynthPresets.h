#pragma once

#include "SynthParameters.h"

namespace cdaw::SynthPresets
{
    /** A C-Synth sound. Factory presets list only what differs from the parameters' defaults;
        user presets (XML files) list every parameter by ID. */
    struct Preset
    {
        juce::String name, category;
        std::vector<std::pair<int, float>> values;
        juce::StringArray macroNames;           // four
        juce::File file;                        // set for user presets

        bool isUser() const                     { return file != juce::File(); }
    };

    /** The built-in sounds, by category (Init first). */
    const std::vector<Preset>& factory();

    /** Where user presets live: %APPDATA%\CDAW\Presets\C-Synth (CDAW and the VST3 share it). */
    juce::File getUserFolder();

    /** Uses another folder for user presets (tests); an empty File goes back to the usual one. */
    void setUserFolder (const juce::File&);

    /** The user presets in that folder, by name. */
    std::vector<Preset> loadUser();

    /** Saves the current values as a user preset (replacing one of the same name). */
    juce::Result saveUser (const juce::String& name, const std::function<float (int)>& valueOf, const juce::StringArray& macroNames);

    /** Every parameter's value for a preset (the default wherever it says nothing). */
    std::vector<float> valuesOf (const Preset&);

    /** Factory then user presets, as one list (what the browser steps through). */
    std::vector<Preset> everything();
}
