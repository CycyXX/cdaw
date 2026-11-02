#pragma once

#include "LeadParameters.h"

namespace cdaw::LeadPresets
{
    /** A C-Lead sound. Factory presets list only what differs from the parameters' defaults;
        user presets (XML files) list every parameter by ID. */
    struct Preset
    {
        juce::String name, category;
        std::vector<std::pair<int, float>> values;
        juce::StringArray macroNames;           // four
        juce::File file;                        // set for user presets

        bool isUser() const                     { return file != juce::File(); }
    };

    const std::vector<Preset>& factory();

    /** Where user presets live: %APPDATA%\CDAW\Presets\C-Lead (CDAW and the VST3 share it). */
    juce::File getUserFolder();
    void setUserFolder (const juce::File&);     // tests; an empty File goes back to the usual one

    std::vector<Preset> loadUser();
    juce::Result saveUser (const juce::String& name, const std::function<float (int)>& valueOf, const juce::StringArray& macroNames);
    std::vector<float> valuesOf (const Preset&);
    std::vector<Preset> everything();
}
