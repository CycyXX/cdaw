#pragma once

#include "../PCH.h"

namespace cdaw::BuiltInPlugins
{
    /** Registers CDAW's own plugin types (compressor, 8-band EQ, multiband compressor, C-Verb, Reverb) with the engine.
        Call once after the engine is created. */
    void registerTypes (te::Engine&);

    juce::PluginDescription getCompressorDescription();
    juce::PluginDescription getEqDescription();
    juce::PluginDescription getMultibandDescription();
    juce::PluginDescription getReverbDescription();         // C-Verb
    juce::PluginDescription getSimpleReverbDescription();   // Reverb

    /** Converts plugins saved by earlier builds to CDAW's versions before an edit is loaded:
        Tracktion's compressor (same parameters) and its 4-band EQ (mapped onto bands 2, 3, 5
        and 7 of the 8-band EQ) and its reverb (to the simple Reverb: size, damping and mix carried over).
        Automation of the old EQ and reverb is dropped. Returns the number converted. */
    int migrateEditState (juce::ValueTree& editState);
}
