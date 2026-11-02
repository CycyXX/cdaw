#pragma once

#include "../PCH.h"

namespace cdaw::CubaseStockPlugins
{
    /** Cubase's own effects (which only run inside Cubase) as CDAW's built-in ones, for the
        Cubase import. Their settings are read from the plugin's state (see readParameters) and
        written as a DAWproject device: <Equalizer> for EQs, otherwise CDAW's own
        <BuiltInPlugin type="..."> with a <Param id="..." value="..."/> per parameter, in the
        parameter's own units.

        Some sound the same (EQs, the compressor); others only similar (reverb, delay, limiters,
        tube and vintage compressors, the de-esser), which the import only makes when asked. */
    using Parameters = std::map<juce::String, double>;

    /** A stock plugin's state: a u32, then records of name[128], u32 id, f64 value (little-endian). */
    Parameters readParameters (const juce::MemoryBlock& state);

    struct Counterpart
    {
        juce::String plugin;        // CDAW's plugin, e.g. "C-Verb"
        bool similar = false;       // only similar, not the same sound
    };

    /** The CDAW plugin that stands in for a Cubase plugin, if there is one. */
    std::optional<Counterpart> counterpartOf (const juce::String& cubaseName);

    struct Device
    {
        std::unique_ptr<juce::XmlElement> xml;      // without an id
        juce::StringArray notes;                     // what didn't come across exactly
    };

    /** The device for a Cubase plugin with these settings; no xml when it has no counterpart or
        its state can't be read. `enabled` false (or the plugin's own bypass) switches it off. */
    Device translate (const juce::String& cubaseName, const Parameters&, bool enabled);
}
