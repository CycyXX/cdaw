#include "MixerOps.h"
#include "TrackOps.h"

namespace cdaw::MixerOps
{
    juce::Array<Bus> getFxBuses (te::Edit& edit)
    {
        juce::Array<Bus> buses;

        for (auto t : te::getAudioTracks (edit))
            for (auto p : t->pluginList.getPlugins())
                if (auto r = dynamic_cast<te::AuxReturnPlugin*> (p))
                {
                    auto name = edit.getAuxBusName (r->busNumber.get());
                    buses.add ({ r->busNumber.get(), name.isNotEmpty() ? name : t->getName(), t });
                }

        return buses;
    }

    juce::Array<te::AuxSendPlugin*> getSends (te::Track& track)
    {
        juce::Array<te::AuxSendPlugin*> sends;

        for (auto p : track.pluginList.getPlugins())
            if (auto s = dynamic_cast<te::AuxSendPlugin*> (p))
                sends.add (s);

        return sends;
    }

    static int indexOfVolume (te::Track& track)
    {
        auto plugins = track.pluginList.getPlugins();

        for (int i = 0; i < plugins.size(); ++i)
            if (dynamic_cast<te::VolumeAndPanPlugin*> (plugins[i].get()) != nullptr)
                return i;

        return -1;
    }

    te::AuxSendPlugin* addSend (te::Track& track, int bus, bool postFader, float gainDb)
    {
        auto plugin = track.edit.getPluginCache().createNewPlugin (te::AuxSendPlugin::xmlTypeName, {});
        auto* send = dynamic_cast<te::AuxSendPlugin*> (plugin.get());

        if (send == nullptr)
            return nullptr;

        send->busNumber = bus;
        send->setGainDb (gainDb);

        auto vol = indexOfVolume (track);
        auto index = vol < 0 ? -1 : (postFader ? vol + 1 : vol);
        track.pluginList.insertPlugin (plugin, index, nullptr);
        return send;
    }

    bool isPostFader (te::AuxSendPlugin& send)
    {
        if (auto* track = send.getOwnerTrack())
        {
            auto vol = indexOfVolume (*track);
            return vol >= 0 && track->pluginList.indexOf (&send) > vol;
        }

        return true;
    }

    void setPostFader (te::AuxSendPlugin& send, bool postFader)
    {
        if (isPostFader (send) == postFader)
            return;

        if (auto* track = send.getOwnerTrack())
        {
            te::Plugin::Ptr keep = &send;
            auto state = send.state;
            send.removeFromParent();

            auto vol = indexOfVolume (*track);
            auto index = vol < 0 ? -1 : (postFader ? vol + 1 : vol);
            track->pluginList.insertPlugin (keep, index, nullptr);
        }
    }

    bool isInsertPlugin (te::Plugin& p)
    {
        return dynamic_cast<te::VolumeAndPanPlugin*> (&p) == nullptr
            && dynamic_cast<te::LevelMeterPlugin*> (&p) == nullptr
            && dynamic_cast<te::AuxSendPlugin*> (&p) == nullptr
            && dynamic_cast<te::AuxReturnPlugin*> (&p) == nullptr;
    }

    juce::Array<te::Plugin*> getInserts (te::Track& track)
    {
        juce::Array<te::Plugin*> result;

        for (auto p : track.pluginList.getPlugins())
            if (isInsertPlugin (*p))
                result.add (p);

        return result;
    }

    juce::String describeOutput (te::Track& track)
    {
        if (auto* parent = track.getParentFolderTrack(); parent != nullptr && parent->isSubmixFolder())
            return "-> " + parent->getName();

        if (auto* out = te::getTrackOutput (track))
        {
            if (auto dest = out->getDestinationTrack())
                return "-> " + dest->getName();

            if (out->outputsToNone())
                return "No output";

            if (out->usesDefaultAudioOut())
                return "Master";

            if (auto dev = out->getOutputDevice (false))
                return dev->getAliasOrName();
        }

        return "Master";
    }

    juce::PopupMenu createOutputMenu (te::Track& track)
    {
        juce::PopupMenu m;
        auto* out = te::getTrackOutput (track);

        if (out == nullptr)
        {
            m.addItem ("Routed by its group", false, false, [] {});
            return m;
        }

        te::Track::Ptr t = &track;
        auto& edit = track.edit;
        auto* um = &edit.getUndoManager();

        m.addItem ("Master", true, out->usesDefaultAudioOut() && out->getDestinationTrack() == nullptr, [t, um]
        {
            um->beginNewTransaction ("Set output");
            if (auto o = te::getTrackOutput (*t))
                o->setOutputToDefaultDevice (false);
        });

        // Other audio tracks as buses (not the track itself, nor anything that feeds into it).
        juce::PopupMenu buses;
        for (auto dest : te::getAudioTracks (edit))
        {
            if (dest == t.get())
                continue;

            if (auto destOut = te::getTrackOutput (*dest); destOut != nullptr && destOut->feedsInto (t.get()))
                continue;

            te::Track::Ptr d = dest;
            buses.addItem (dest->getName(), true, out->getDestinationTrack() == dest, [t, d, um]
            {
                um->beginNewTransaction ("Set output");
                if (auto o = te::getTrackOutput (*t))
                    o->setOutputToTrack (dynamic_cast<te::AudioTrack*> (d.get()));
            });
        }

        m.addSubMenu ("Track (bus)", buses, buses.getNumItems() > 0);

        juce::PopupMenu devices;
        auto& dm = edit.engine.getDeviceManager();

        for (int i = 0; i < dm.getNumWaveOutDevices(); ++i)
        {
            if (auto* dev = dm.getWaveOutDevice (i); dev != nullptr && dev->isEnabled())
            {
                auto id = dev->getDeviceID();
                devices.addItem (dev->getAliasOrName(), true, ! out->usesDefaultAudioOut() && out->getOutputDeviceID() == id, [t, id, um]
                {
                    um->beginNewTransaction ("Set output");
                    if (auto o = te::getTrackOutput (*t))
                        o->setOutputToDeviceID (id);
                });
            }
        }

        m.addSubMenu ("Hardware output (e.g. reamping)", devices, devices.getNumItems() > 0);
        m.addItem ("No output", true, out->outputsToNone(), [t, um]
        {
            um->beginNewTransaction ("Set output");
            if (auto o = te::getTrackOutput (*t))
                o->setOutputToNone();
        });

        return m;
    }

    double getTrackLatencyMs (te::Track& track)
    {
        double secs = 0.0;

        for (auto p : track.pluginList.getPlugins())
            if (p->isEnabled())
                secs += p->getLatencySeconds();

        return secs * 1000.0;
    }
}
