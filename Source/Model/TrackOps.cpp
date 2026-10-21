#include "TrackOps.h"
#include "EditViewState.h"
#include "DrumMapLibrary.h"
#include "../UI/LookAndFeel/Palette.h"

namespace cdaw::TrackOps
{
    TrackKind getKind (const te::Track& t)
    {
        if (auto ft = dynamic_cast<const te::FolderTrack*> (&t))
            return ft->isSubmixFolder() ? TrackKind::group : TrackKind::folder;

        if (auto at = dynamic_cast<const te::AudioTrack*> (&t))
        {
            auto type = t.state[ViewIDs::cdawType].toString();

            if (type == "instrument")   return TrackKind::instrument;
            if (type == "fx")           return TrackKind::fx;

            // Recognise tracks from projects made before the tag existed.
            for (auto p : const_cast<te::AudioTrack*> (at)->pluginList.getPlugins())
                if (dynamic_cast<te::AuxReturnPlugin*> (p))
                    return TrackKind::fx;

            return TrackKind::audio;
        }

        return TrackKind::other;
    }

    juce::String getKindName (TrackKind k)
    {
        switch (k)
        {
            case TrackKind::audio:      return "Audio";
            case TrackKind::instrument: return "Instrument";
            case TrackKind::fx:         return "FX";
            case TrackKind::group:      return "Group";
            case TrackKind::folder:     return "Folder";
            default:                    return "Track";
        }
    }

    juce::Array<te::Track*> getArrangeTracks (te::Edit& edit)
    {
        juce::Array<te::Track*> result;

        edit.visitAllTracksRecursive ([&] (te::Track& t)
        {
            if (t.isAudioTrack() || t.isFolderTrack())
            {
                // Hide children of collapsed folders.
                bool hidden = false;
                for (auto p = t.getParentFolderTrack(); p != nullptr; p = p->getParentFolderTrack())
                    if ((bool) p->state.getProperty (ViewIDs::cdawCollapsed, false))
                        hidden = true;

                if (! hidden)
                    result.add (&t);
            }

            return true;
        });

        return result;
    }

    int getDepth (const te::Track& t)
    {
        int depth = 0;
        for (auto p = t.getParentFolderTrack(); p != nullptr; p = p->getParentFolderTrack())
            ++depth;
        return depth;
    }

    juce::String makeUniqueName (te::Edit& edit, const juce::String& base)
    {
        juce::StringArray names;
        edit.visitAllTracksRecursive ([&] (te::Track& t) { names.add (t.getName()); return true; });

        for (int i = 1;; ++i)
        {
            auto candidate = base + " " + juce::String (i);
            if (! names.contains (candidate))
                return candidate;
        }
    }

    juce::Colour nextColour (te::Edit& edit)
    {
        int count = 0;
        edit.visitAllTracksRecursive ([&] (te::Track& t) { if (t.isAudioTrack() || t.isFolderTrack()) ++count; return true; });
        return Palette::trackColour (count);
    }

    static te::TrackInsertPoint insertPointAfter (te::Edit& edit, te::Track* after)
    {
        if (after != nullptr)
            return te::TrackInsertPoint (*after, false);

        return te::TrackInsertPoint::getEndOfTracks (edit);
    }

    te::AudioTrack::Ptr addAudioTrack (te::Edit& edit, te::SelectionManager* sm, te::Track* after, const juce::String& name)
    {
        auto colour = nextColour (edit);
        auto track = edit.insertNewAudioTrack (insertPointAfter (edit, after), sm);

        if (track != nullptr)
        {
            track->setName (name.isNotEmpty() ? name : makeUniqueName (edit, "Audio"));
            track->setColour (colour);
        }

        return track;
    }

    te::AudioTrack::Ptr addInstrumentTrack (te::Edit& edit, te::SelectionManager* sm, const juce::PluginDescription* desc, te::Track* after)
    {
        auto colour = nextColour (edit);
        auto track = edit.insertNewAudioTrack (insertPointAfter (edit, after), sm);

        if (track == nullptr)
            return {};

        track->state.setProperty (ViewIDs::cdawType, "instrument", nullptr);
        track->setColour (colour);
        track->setName (desc != nullptr ? makeUniqueName (edit, desc->name) : makeUniqueName (edit, "Instrument"));

        if (desc != nullptr)
        {
            auto type = te::PluginManager::isBuiltInPlugin (*desc) ? desc->fileOrIdentifier : juce::String (te::ExternalPlugin::xmlTypeName);

            if (auto plugin = edit.getPluginCache().createNewPlugin (type, *desc))
                track->pluginList.insertPlugin (plugin, 0, nullptr);

            auto map = DrumMapLibrary::guessForTrack (*track);
            if (map.isNotEmpty())
                track->state.setProperty (ViewIDs::cdawDrumMap, map, nullptr);
        }

        // Play from every connected MIDI keyboard by default.
        for (auto* in : edit.getAllInputDevices())
        {
            if (in->getInputDevice().getDeviceID() == "all_midi_in")
            {
                assignInput (*track, in);
                setArmed (*track, true);
                break;
            }
        }

        return track;
    }

    te::AudioTrack::Ptr addFxTrack (te::Edit& edit, te::SelectionManager* sm, te::Track* after)
    {
        // Find a free aux bus number.
        juce::BigInteger used;
        for (auto p : te::getAllPlugins (edit, false))
            if (auto r = dynamic_cast<te::AuxReturnPlugin*> (p))
                used.setBit (r->busNumber.get());

        int bus = 0;
        while (used[bus] && bus < 31)
            ++bus;

        auto colour = nextColour (edit);
        auto track = edit.insertNewAudioTrack (insertPointAfter (edit, after), sm);

        if (track == nullptr)
            return {};

        auto name = makeUniqueName (edit, "FX");
        track->state.setProperty (ViewIDs::cdawType, "fx", nullptr);
        track->setName (name);
        track->setColour (colour);
        edit.setAuxBusName (bus, name);

        if (auto ret = edit.getPluginCache().createNewPlugin (te::AuxReturnPlugin::xmlTypeName, {}))
        {
            if (auto r = dynamic_cast<te::AuxReturnPlugin*> (ret.get()))
                r->busNumber = bus;

            track->pluginList.insertPlugin (ret, 0, nullptr);
        }

        return track;
    }

    te::FolderTrack::Ptr addFolderTrack (te::Edit& edit, te::SelectionManager* sm, bool asGroup, te::Track* after)
    {
        auto colour = nextColour (edit);
        auto track = edit.insertNewFolderTrack (insertPointAfter (edit, after), sm, asGroup);

        if (track != nullptr)
        {
            track->setName (makeUniqueName (edit, asGroup ? "Group" : "Folder"));
            track->setColour (colour);
        }

        return track;
    }

    void deleteTracks (te::Edit& edit, const juce::Array<te::Track*>& tracks)
    {
        juce::ReferenceCountedArray<te::Track> keep;
        for (auto t : tracks)
            keep.add (t);

        for (auto t : keep)
            if (t->isAudioTrack() || t->isFolderTrack())
                edit.deleteTrack (t);
    }

    te::Track::Ptr duplicateTrack (te::Edit& edit, te::Track& t)
    {
        auto copy = edit.copyTrack (&t, te::TrackInsertPoint (t, false));

        if (copy != nullptr)
            copy->setName (makeUniqueName (edit, t.getName()));

        return copy;
    }

    void moveTrack (te::Edit& edit, te::Track& t, int delta)
    {
        auto siblings = t.getParentTrack() != nullptr ? t.getParentTrack()->getAllSubTracks (false)
                                                       : te::getTopLevelTracks (edit);
        juce::Array<te::Track*> movable;
        for (auto s : siblings)
            if (s->isMovable())
                movable.add (s);

        auto idx = movable.indexOf (&t);
        auto target = idx + delta;

        if (idx < 0 || target < 0 || target >= movable.size())
            return;

        auto parent = t.getParentTrack();

        if (delta < 0)
        {
            auto preceding = target > 0 ? movable[target - 1] : nullptr;
            edit.moveTrack (&t, te::TrackInsertPoint (parent, preceding));
        }
        else
        {
            edit.moveTrack (&t, te::TrackInsertPoint (parent, movable[target]));
        }
    }

    //==============================================================================
    juce::Array<InputChoice> getInputChoices (te::Edit& edit, bool midi)
    {
        juce::Array<InputChoice> choices;

        for (auto* in : edit.getAllInputDevices())
        {
            auto& dev = in->getInputDevice();
            auto type = dev.getDeviceType();

            if (dev.isTrackDevice())
                continue;

            const bool isMidiDevice = type == te::InputDevice::physicalMidiDevice || type == te::InputDevice::virtualMidiDevice;

            if (isMidiDevice == midi && dev.isEnabled())
                choices.add ({ dev.getAliasOrName(), in, isMidiDevice });
        }

        return choices;
    }

    te::InputDeviceInstance* getAssignedInput (te::AudioTrack& track)
    {
        for (auto* in : track.edit.getAllInputDevices())
            if (! in->getInputDevice().isTrackDevice() && in->getTargets().contains (track.itemID))
                return in;

        return nullptr;
    }

    void assignInput (te::AudioTrack& track, te::InputDeviceInstance* newInput)
    {
        auto* um = &track.edit.getUndoManager();
        bool wasArmed = isArmed (track);

        for (auto* in : track.edit.getAllInputDevices())
            if (in != newInput && in->getTargets().contains (track.itemID))
                [[maybe_unused]] auto r = in->removeTarget (track.itemID, um);

        if (newInput != nullptr && ! newInput->getTargets().contains (track.itemID))
        {
            [[maybe_unused]] auto r = newInput->setTarget (track.itemID, false, um, 0);

            if (wasArmed)
                newInput->setRecordingEnabled (track.itemID, true);
        }
    }

    bool isArmed (te::AudioTrack& track)
    {
        if (auto in = getAssignedInput (track))
            return in->isRecordingEnabled (track.itemID);

        return false;
    }

    void setArmed (te::AudioTrack& track, bool armed)
    {
        if (auto in = getAssignedInput (track))
            in->setRecordingEnabled (track.itemID, armed);
    }

    te::InputDevice::MonitorMode getMonitorMode (te::AudioTrack& track)
    {
        if (auto in = getAssignedInput (track))
            return in->getInputDevice().getMonitorMode();

        return te::InputDevice::MonitorMode::automatic;
    }

    void setMonitorMode (te::AudioTrack& track, te::InputDevice::MonitorMode mode)
    {
        if (auto in = getAssignedInput (track))
            in->getInputDevice().setMonitorMode (mode);
    }

    bool isMonitoringLive (te::AudioTrack& track)
    {
        if (auto in = getAssignedInput (track))
            return in->isLivePlayEnabled (track);

        return false;
    }

    int countTracksSharingInput (te::AudioTrack& track)
    {
        if (auto in = getAssignedInput (track))
            return in->getTargets().size();

        return 0;
    }
}
