#pragma once

#include "../PCH.h"

namespace cdaw::BuiltInPlugins
{
    /** Registers CDAW's own plugin types (C-Comp, the 8-band EQ, C-Multicomp, C-Verb, Reverb,
        C-Delay, C-Chorus, C-Phaser, C-Filter, C-Dist, C-Esser, C-Synth and C-Lead) with the engine. Call once after the engine is created. */
    void registerTypes (te::Engine&);

    juce::PluginDescription getCompressorDescription();
    juce::PluginDescription getEqDescription();
    juce::PluginDescription getMultibandDescription();
    juce::PluginDescription getReverbDescription();         // C-Verb
    juce::PluginDescription getSimpleReverbDescription();   // Reverb
    juce::PluginDescription getDelayDescription();          // C-Delay
    juce::PluginDescription getChorusDescription();         // C-Chorus
    juce::PluginDescription getPhaserDescription();         // C-Phaser
    juce::PluginDescription getEsserDescription();          // C-Esser
    juce::PluginDescription getFilterDescription();         // C-Filter
    juce::PluginDescription getDistDescription();           // C-Dist
    juce::PluginDescription getSynthDescription();          // C-Synth (an instrument)
    juce::PluginDescription getLeadDescription();           // C-Lead (an instrument)

    /** Converts plugins saved by earlier builds to CDAW's versions before an edit is loaded:
        Tracktion's compressor (same parameters), its 4-band EQ (mapped onto bands 2, 3, 5 and
        7 of the 8-band EQ), its reverb (to the simple Reverb: size, damping and mix carried
        over), and its delay, chorus, phaser and low / high-pass filter (to C-Delay, C-Chorus,
        C-Phaser and C-Filter, sounding as close as their settings allow). Automation of the
        old plugins is dropped, except the filter's frequency. Returns the number converted. */
    int migrateEditState (juce::ValueTree& editState);
}
