#pragma once

#include "../PCH.h"

namespace cdaw
{
    /** What kind of track a te::Track is from the user's point of view. */
    enum class TrackKind { audio, instrument, fx, group, folder, other };

    /** What an audio track records: one input channel (played centred, pannable) or an input pair. */
    enum class ChannelMode { mono, stereo };

    namespace TrackOps
    {
        TrackKind getKind (const te::Track&);
        juce::String getKindName (TrackKind);

        /** The kind's name, plus "Mono"/"Stereo" for audio tracks. */
        juce::String getKindName (const te::Track&);

        /** All tracks shown in the arrangement (audio + folder tracks, depth-first). */
        juce::Array<te::Track*> getArrangeTracks (te::Edit&);
        int getDepth (const te::Track&);

        /** Inserts after `after` (or at the end) and selects the new track. */
        te::AudioTrack::Ptr addAudioTrack (te::Edit&, te::SelectionManager*, te::Track* after = nullptr, const juce::String& name = {},
                                           ChannelMode = ChannelMode::mono);
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

        //==============================================================================
        // Mono / stereo audio inputs. The hardware channels are grouped into mono inputs and
        // stereo pairs on demand: picking "In 1+2" for a stereo track pairs channels 1 and 2,
        // picking "In 2" for a mono track splits them again.

        /** The track's mode. Tracks from before the property existed follow their input. */
        ChannelMode getChannelMode (te::AudioTrack&);

        /** Switches the mode and moves the track to the matching input (In 3 <-> In 3+4). */
        void setChannelMode (te::AudioTrack&, ChannelMode);

        struct AudioInputChoice
        {
            juce::String name;
            int firstChannel = 0, numChannels = 1;   // hardware channels, 0-based
        };

        /** Mono channels or channel pairs of the audio interface, depending on the mode. */
        juce::Array<AudioInputChoice> getAudioInputChoices (te::Engine&, ChannelMode);

        /** Hardware channels [first, first + num) of a wave input, or {} for other inputs. */
        std::optional<std::pair<int, int>> getChannelRange (const te::InputDevice&);

        /** Groups the hardware channels as asked (pairing or splitting inputs) and assigns that
            input to the track. Tracks whose input disappeared in the regrouping lose it; their
            names are returned (and shown in the status bar). */
        juce::StringArray assignAudioInput (te::AudioTrack&, int firstChannel, int numChannels);

        /** Groups hardware channels [first, first + num) into one input (pairs or splits inputs),
            unassigning the tracks whose inputs that removes. Returns their names. */
        juce::StringArray regroupInputs (te::Edit&, int firstChannel, int numChannels);

        /** Number of hardware input channels on the audio interface. */
        int getNumHardwareInputs (te::Engine&);

        /** The input menu for a track: its mode's audio inputs, MIDI inputs, mono/stereo switch. */
        juce::PopupMenu createInputMenu (te::AudioTrack&);
    }
}
