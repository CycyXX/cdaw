#pragma once

#include "../PCH.h"

namespace cdaw
{
    /** What kind of track a te::Track is from the user's point of view. */
    enum class TrackKind { audio, instrument, fx, group, folder, other };

    namespace TrackOps
    {
        TrackKind getKind (const te::Track&);
        juce::String getKindName (TrackKind);

        /** All tracks shown in the arrangement (audio + folder tracks, depth-first). */
        juce::Array<te::Track*> getArrangeTracks (te::Edit&);
        int getDepth (const te::Track&);

        /** Inserts after `after` (or at the end) and selects the new track. */
        te::AudioTrack::Ptr addAudioTrack (te::Edit&, te::SelectionManager*, te::Track* after = nullptr, const juce::String& name = {});
        te::AudioTrack::Ptr addInstrumentTrack (te::Edit&, te::SelectionManager*, const juce::PluginDescription*, te::Track* after = nullptr);
        te::AudioTrack::Ptr addFxTrack (te::Edit&, te::SelectionManager*, te::Track* after = nullptr);
        te::FolderTrack::Ptr addFolderTrack (te::Edit&, te::SelectionManager*, bool asGroup, te::Track* after = nullptr);

        void deleteTracks (te::Edit&, const juce::Array<te::Track*>&);
        te::Track::Ptr duplicateTrack (te::Edit&, te::Track&);
        void moveTrack (te::Edit&, te::Track&, int delta);

        juce::String makeUniqueName (te::Edit&, const juce::String& base);
        juce::Colour nextColour (te::Edit&);

        //==============================================================================
        // Inputs
        struct InputChoice
        {
            juce::String name;
            te::InputDeviceInstance* instance = nullptr;   // null = no input
            bool isMidi = false;
        };

        juce::Array<InputChoice> getInputChoices (te::Edit&, bool midi);
        te::InputDeviceInstance* getAssignedInput (te::AudioTrack&);
        void assignInput (te::AudioTrack&, te::InputDeviceInstance*);   // null clears

        bool isArmed (te::AudioTrack&);
        void setArmed (te::AudioTrack&, bool);

        te::InputDevice::MonitorMode getMonitorMode (te::AudioTrack&);
        void setMonitorMode (te::AudioTrack&, te::InputDevice::MonitorMode);
        bool isMonitoringLive (te::AudioTrack&);
        int countTracksSharingInput (te::AudioTrack&);
    }
}
