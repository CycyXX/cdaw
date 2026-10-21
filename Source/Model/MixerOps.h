#pragma once

#include "../PCH.h"

namespace cdaw::MixerOps
{
    struct Bus
    {
        int number = 0;
        juce::String name;
        te::AudioTrack* returnTrack = nullptr;
    };

    /** FX return buses (tracks with an AuxReturnPlugin). */
    juce::Array<Bus> getFxBuses (te::Edit&);

    /** Sends on a track, in plugin order. */
    juce::Array<te::AuxSendPlugin*> getSends (te::Track&);

    /** Adds a send to `bus`. Post-fader sends go after the volume plugin. */
    te::AuxSendPlugin* addSend (te::Track&, int bus, bool postFader, float gainDb = -6.0f);
    bool isPostFader (te::AuxSendPlugin&);
    void setPostFader (te::AuxSendPlugin&, bool postFader);

    /** Plugins shown as inserts (excludes volume/meter/sends/aux return). */
    juce::Array<te::Plugin*> getInserts (te::Track&);
    bool isInsertPlugin (te::Plugin&);

    /** Output routing: "Master", another track (bus) or a device output. */
    juce::String describeOutput (te::Track&);
    juce::PopupMenu createOutputMenu (te::Track&);

    /** Total plugin latency on a track in ms. */
    double getTrackLatencyMs (te::Track&);

    //==============================================================================
    // Sidechain: a plugin with a sidechain input (e.g. the built-in compressor, or a VST3
    // with a sidechain bus) can be keyed from another track's signal.

    /** True if the plugin has a sidechain input. */
    bool canSidechain (te::Plugin&);

    /** Tracks that can feed the plugin's sidechain (audio tracks other than its own). */
    juce::Array<te::AudioTrack*> getSidechainSources (te::Plugin&);

    /** The track feeding the plugin's sidechain, or null. */
    te::AudioTrack* getSidechainSource (te::Plugin&);

    /** Keys the plugin from `source` (null switches the sidechain off). The built-in
        compressor is also switched to listen to the sidechain. */
    void setSidechainSource (te::Plugin&, te::AudioTrack* source);

    /** Sidechain submenu for a plugin's context menu. */
    juce::PopupMenu createSidechainMenu (te::Plugin&);
}
