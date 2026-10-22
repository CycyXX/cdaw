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

    juce::String getKindName (const te::Track& t)
    {
        auto kind = getKind (t);
        auto name = getKindName (kind);

        if (auto at = dynamic_cast<const te::AudioTrack*> (&t); at != nullptr && kind == TrackKind::audio)
            name << juce::String::fromUTF8 (" \xc2\xb7 ")
                 << (getChannelMode (*const_cast<te::AudioTrack*> (at)) == ChannelMode::stereo ? "Stereo" : "Mono");

        return name;
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

    te::AudioTrack::Ptr addAudioTrack (te::Edit& edit, te::SelectionManager* sm, te::Track* after, const juce::String& name, ChannelMode mode)
    {
        auto colour = nextColour (edit);
        auto track = edit.insertNewAudioTrack (insertPointAfter (edit, after), sm);

        if (track != nullptr)
        {
            track->state.setProperty (ViewIDs::cdawChannels, mode == ChannelMode::stereo ? "stereo" : "mono", nullptr);
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

    //==============================================================================
    ChannelMode getChannelMode (te::AudioTrack& track)
    {
        auto mode = track.state[ViewIDs::cdawChannels].toString();

        if (mode == "stereo")   return ChannelMode::stereo;
        if (mode == "mono")     return ChannelMode::mono;

        if (auto in = getAssignedInput (track))
            if (auto range = getChannelRange (in->getInputDevice()); range && range->second == 2)
                return ChannelMode::stereo;

        return ChannelMode::mono;
    }

    std::optional<std::pair<int, int>> getChannelRange (const te::InputDevice& device)
    {
        if (device.isTrackDevice())
            return {};

        if (auto wi = dynamic_cast<const te::WaveInputDevice*> (&device))
        {
            auto range = wi->deviceDescription.getDeviceChannelRange();
            return std::pair<int, int> { (int) range.first, (int) (range.second - range.first) };
        }

        return {};
    }

    /** The audio interface's input channel names, numbered "Input N" when the driver only
        numbers them (as Tracktion does). */
    static juce::StringArray getHardwareInputNames (te::Engine& engine)
    {
        juce::StringArray names;

        if (auto* device = engine.getDeviceManager().deviceManager.getCurrentAudioDevice())
            names = device->getInputChannelNames();

        bool bareNumbers = true;
        for (int i = 0; i < names.size(); ++i)
            bareNumbers = bareNumbers && names[i].trim() == juce::String (i + 1);

        if (bareNumbers)
            for (int i = 0; i < names.size(); ++i)
                names.set (i, "Input " + juce::String (i + 1));

        return names;
    }

    int getNumHardwareInputs (te::Engine& engine)
    {
        return getHardwareInputNames (engine).size();
    }

    /** "In 1" + "In 2" -> "In 1+2"; names without a shared numbered prefix are joined. */
    static juce::String makePairName (const juce::String& a, const juce::String& b)
    {
        auto prefixA = a.trimCharactersAtEnd ("0123456789");
        auto prefixB = b.trimCharactersAtEnd ("0123456789");

        if (prefixA == prefixB && prefixA.length() < a.length() && prefixB.length() < b.length())
            return a + "+" + b.substring (prefixB.length());

        return a + " + " + b;
    }

    juce::Array<AudioInputChoice> getAudioInputChoices (te::Engine& engine, ChannelMode mode)
    {
        juce::Array<AudioInputChoice> choices;
        auto names = getHardwareInputNames (engine);

        if (mode == ChannelMode::mono)
        {
            for (int i = 0; i < names.size(); ++i)
                choices.add ({ names[i], i, 1 });
        }
        else
        {
            for (int i = 0; i + 1 < names.size(); i += 2)
                choices.add ({ makePairName (names[i], names[i + 1]), i, 2 });
        }

        return choices;
    }

    static te::InputDeviceInstance* findInputInstance (te::Edit& edit, int first, int num)
    {
        for (auto* in : edit.getAllInputDevices())
            if (auto range = getChannelRange (in->getInputDevice()); range && range->first == first && range->second == num)
                return in;

        return nullptr;
    }

    /** Regroups the device's inputs so that one covers exactly [first, first + num). The engine
        rebuilds its inputs (and every edit's input instances) synchronously here. */
    static void groupInputChannels (te::DeviceManager& dm, int first, int num)
    {
        for (int attempt = 0; attempt < 4; ++attempt)
        {
            dm.dispatchPendingUpdates();
            te::WaveInputDevice* containing = nullptr;
            std::pair<int, int> range;

            for (auto* wi : dm.getWaveInputDevices())
            {
                if (auto r = getChannelRange (*wi); r && r->first <= first && first < r->first + r->second)
                {
                    containing = wi;
                    range = *r;
                }
            }

            if (containing == nullptr)
                return;

            if (range.first != first)
            {
                // Shorten the input that straddles `first`, so a new one starts there.
                dm.setDeviceNumChannels (*containing, (uint32_t) (first - range.first));
            }
            else if (range.second != num)
            {
                dm.setDeviceNumChannels (*containing, (uint32_t) num);
            }
            else
            {
                if (! containing->isEnabled())
                    containing->setEnabled (true);

                dm.dispatchPendingUpdates();
                return;
            }
        }
    }

    static juce::StringArray regroupInputs (te::Edit& edit, int first, int num, te::AudioTrack* except)
    {
        juce::StringArray lost;

        if (findInputInstance (edit, first, num) != nullptr)
            return lost;

        // Regrouping removes the inputs that overlap these channels. Unassign them first, so
        // the tracks on them don't silently get them back if the channels are regrouped later.
        // Not undoable: the regrouping itself can't be undone, and undo would otherwise put the
        // tracks back on inputs that no longer exist (which then return if the channels split).
        auto overlaps = [first, num] (std::pair<int, int> r) { return r.first < first + num && first < r.first + r.second; };

        for (auto t : te::getAudioTracks (edit))
        {
            if (auto in = getAssignedInput (*t))
            {
                if (auto r = getChannelRange (in->getInputDevice()); r && overlaps (*r))
                {
                    [[maybe_unused]] auto res = in->removeTarget (t->itemID, nullptr);

                    if (t != except)
                        lost.add (t->getName());
                }
            }
        }

        groupInputChannels (edit.engine.getDeviceManager(), first, num);

        if (! lost.isEmpty())
            edit.engine.getUIBehaviour().showInfoMessage ((lost.size() == 1 ? "No input now on track " : "No input now on tracks ")
                                                          + lost.joinIntoString (", ") + " (its channels were regrouped)");

        return lost;
    }

    juce::StringArray regroupInputs (te::Edit& edit, int firstChannel, int numChannels)
    {
        return regroupInputs (edit, firstChannel, numChannels, nullptr);
    }

    juce::StringArray assignAudioInput (te::AudioTrack& track, int first, int num)
    {
        const bool wasArmed = isArmed (track);
        auto lost = regroupInputs (track.edit, first, num, &track);

        if (auto in = findInputInstance (track.edit, first, num))
        {
            assignInput (track, in);

            if (wasArmed)
                setArmed (track, true);
        }

        return lost;
    }

    void setChannelMode (te::AudioTrack& track, ChannelMode mode)
    {
        auto in = getAssignedInput (track);
        auto range = in != nullptr ? getChannelRange (in->getInputDevice()) : std::nullopt;

        track.edit.getUndoManager().beginNewTransaction (mode == ChannelMode::stereo ? "Make track stereo" : "Make track mono");
        track.state.setProperty (ViewIDs::cdawChannels, mode == ChannelMode::stereo ? "stereo" : "mono", &track.edit.getUndoManager());

        // Move to the matching input: In 3 -> In 3+4, In 3+4 -> In 3.
        if (range && (range->second == 2) != (mode == ChannelMode::stereo))
        {
            auto wanted = mode == ChannelMode::stereo ? (range->first & ~1) : range->first;

            for (auto& c : getAudioInputChoices (track.edit.engine, mode))
                if (c.firstChannel == wanted)
                    assignAudioInput (track, c.firstChannel, c.numChannels);
        }
    }

    juce::PopupMenu createInputMenu (te::AudioTrack& track)
    {
        te::AudioTrack::Ptr at = &track;
        juce::PopupMenu m;
        auto current = getAssignedInput (track);
        auto currentRange = current != nullptr ? getChannelRange (current->getInputDevice()) : std::nullopt;
        auto mode = getChannelMode (track);

        m.addItem ("No input", true, current == nullptr, [at]
        {
            at->edit.getUndoManager().beginNewTransaction ("Set input");
            assignInput (*at, nullptr);
        });

        m.addSectionHeader (mode == ChannelMode::stereo ? "Stereo inputs" : "Mono inputs");

        for (auto& c : getAudioInputChoices (track.edit.engine, mode))
        {
            const bool ticked = currentRange && currentRange->first == c.firstChannel && currentRange->second == c.numChannels;
            m.addItem (c.name, true, ticked, [at, c]
            {
                at->edit.getUndoManager().beginNewTransaction ("Set input");
                assignAudioInput (*at, c.firstChannel, c.numChannels);
            });
        }

        m.addSectionHeader ("MIDI inputs");

        for (auto& c : getInputChoices (track.edit, true))
        {
            auto* inst = c.instance;
            m.addItem (c.name, true, inst == current, [at, inst]
            {
                at->edit.getUndoManager().beginNewTransaction ("Set input");
                assignInput (*at, inst);
            });
        }

        if (getKind (track) == TrackKind::audio)
        {
            m.addSeparator();
            m.addItem ("Mono Track", true, mode == ChannelMode::mono, [at] { setChannelMode (*at, ChannelMode::mono); });
            m.addItem ("Stereo Track", true, mode == ChannelMode::stereo, [at] { setChannelMode (*at, ChannelMode::stereo); });
        }

        return m;
    }
}
