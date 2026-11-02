#include "TemplateOps.h"
#include "EditViewState.h"
#include "ProjectVersions.h"
#include "SongNotes.h"
#include "../App/AppSettings.h"

namespace cdaw::TemplateOps
{
    namespace
    {
        /** Kept on sends and returns in a track preset, so they can find their bus again by name
            in another project (bus numbers differ from song to song). */
        const juce::Identifier busNameID ("cdawBusName");

        bool writeXml (const juce::ValueTree& state, const juce::File& destination)
        {
            auto xml = state.createXml();
            destination.getParentDirectory().createDirectory();
            return xml != nullptr && xml->writeTo (destination);
        }

        template <typename Fn>
        void visitPlugins (juce::ValueTree v, Fn&& fn)
        {
            if (v.hasType (te::IDs::PLUGIN))
                fn (v);

            for (auto child : v)
                visitPlugins (child, fn);
        }

        bool isSend (const juce::ValueTree& p)      { return p[te::IDs::type].toString() == te::AuxSendPlugin::xmlTypeName; }
        bool isReturn (const juce::ValueTree& p)    { return p[te::IDs::type].toString() == te::AuxReturnPlugin::xmlTypeName; }
    }

    juce::File getTemplatesFolder()
    {
        auto f = AppSettings::getSettingsFolder().getChildFile ("Templates");
        f.createDirectory();
        return f;
    }

    juce::File getTrackPresetsFolder()
    {
        auto f = AppSettings::getSettingsFolder().getChildFile ("TrackPresets");
        f.createDirectory();
        return f;
    }

    juce::Array<juce::File> listFiles (const juce::File& folder, const char* suffix)
    {
        auto files = folder.findChildFiles (juce::File::findFiles, false, "*" + juce::String (suffix));
        std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
                   { return a.getFileName().compareNatural (b.getFileName()) < 0; });
        return files;
    }

    void stripContent (juce::ValueTree& v)
    {
        for (int i = v.getNumChildren(); --i >= 0;)
        {
            auto child = v.getChild (i);

            if (te::Clip::isClipState (child) || child.hasType (te::IDs::AUTOMATIONCURVE))
            {
                v.removeChild (i, nullptr);
                continue;
            }

            if (child.hasType (te::IDs::TRANSPORT))
                child.setProperty (te::IDs::position, 0.0, nullptr);

            if (child.hasType (ViewIDs::CDAWVIEW))
                for (auto& id : { ViewIDs::viewStart, ViewIDs::cursorPos, ViewIDs::rangeStart, ViewIDs::rangeEnd, ViewIDs::scrollY })
                    child.removeProperty (id, nullptr);

            stripContent (child);
        }

        for (auto& id : { te::IDs::frozen, te::IDs::frozenIndividually, SongNotes::notesID, ProjectVersions::noteID })
            v.removeProperty (id, nullptr);
    }

    bool saveTemplate (te::Edit& edit, const juce::File& destination)
    {
        edit.flushState();
        auto state = edit.state.createCopy();
        stripContent (state);

        // Each project made from it gets its own identity.
        state.removeProperty (te::IDs::projectID, nullptr);
        return writeXml (state, destination);
    }

    bool createProjectFromTemplate (const juce::File& templateFile, const juce::File& editFile)
    {
        auto xml = juce::parseXML (templateFile);

        if (xml == nullptr || ! xml->hasTagName (te::IDs::EDIT.toString()))
            return false;

        editFile.getParentDirectory().createDirectory();
        return xml->writeTo (editFile);
    }

    bool saveTrackPreset (te::Track& track, const juce::File& destination)
    {
        auto& edit = track.edit;
        edit.flushState();
        auto state = track.state.createCopy();
        stripContent (state);

        // An output to another track ("track 3") would land on whatever track has that number in
        // the next song: send it to the default output instead. A folder's own tracks still feed it.
        std::function<void (juce::ValueTree)> clearTrackOutputs = [&] (juce::ValueTree v)
        {
            if (v.hasType (te::IDs::DEVICE) && v.getParent().hasType (te::IDs::OUTPUTDEVICES)
                 && v[te::IDs::name].toString().startsWith ("track "))
                v.setProperty (te::IDs::name, juce::String(), nullptr);

            for (auto child : v)
                clearTrackOutputs (child);
        };
        clearTrackOutputs (state);

        visitPlugins (state, [&edit] (juce::ValueTree p)
        {
            if (isSend (p) || isReturn (p))
                p.setProperty (busNameID, edit.getAuxBusName ((int) p[te::IDs::busNum]), nullptr);
        });

        return writeXml (state, destination);
    }

    te::Track::Ptr insertTrackPreset (te::Edit& edit, const juce::File& preset, te::Track* after, te::SelectionManager* sm)
    {
        auto xml = juce::parseXML (preset);

        if (xml == nullptr)
            return {};

        auto state = juce::ValueTree::fromXml (*xml);

        if (! (state.hasType (te::IDs::TRACK) || state.hasType (te::IDs::FOLDERTRACK)))
            return {};

        te::EditItemID::remapIDs (state, nullptr, edit);

        // FX returns: keep their bus if it's free here, otherwise move them to a free one.
        juce::BigInteger usedBuses;
        for (auto p : te::getAllPlugins (edit, false))
            if (auto r = dynamic_cast<te::AuxReturnPlugin*> (p))
                usedBuses.setBit (r->busNumber.get());

        std::map<int, int> movedBuses;
        std::map<int, juce::String> returnNames;   // set once the tracks exist

        visitPlugins (state, [&] (juce::ValueTree p)
        {
            if (! isReturn (p))
                return;

            int bus = p[te::IDs::busNum];

            if (usedBuses[bus])
            {
                int free = 0;
                while (usedBuses[free] && free < 31)
                    ++free;

                movedBuses[bus] = free;
                p.setProperty (te::IDs::busNum, free, nullptr);
                bus = free;
            }

            usedBuses.setBit (bus);
            returnNames[bus] = p[busNameID].toString();
        });

        // Sends: follow a return that moved, else find the bus by name, else keep the number.
        visitPlugins (state, [&] (juce::ValueTree p)
        {
            if (! isSend (p))
                return;

            int bus = p[te::IDs::busNum];

            if (auto it = movedBuses.find (bus); it != movedBuses.end())
            {
                p.setProperty (te::IDs::busNum, it->second, nullptr);
                return;
            }

            auto name = p[busNameID].toString();

            if (name.isNotEmpty() && returnNames.count (bus) == 0)
                for (int b = 0; b < 32; ++b)
                    if (edit.getAuxBusName (b).equalsIgnoreCase (name))
                        { p.setProperty (te::IDs::busNum, b, nullptr); break; }
        });

        auto insertPoint = after != nullptr ? te::TrackInsertPoint (*after, false) : te::TrackInsertPoint::getEndOfTracks (edit);
        auto track = edit.insertTrack (insertPoint, state, sm);

        if (track != nullptr)
            for (auto& [bus, name] : returnNames)
                if (name.isNotEmpty())
                    edit.setAuxBusName (bus, name);

        return track;
    }
}
