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
}
