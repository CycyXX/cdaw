#include "DawProject.h"
#include "ArrangerOps.h"
#include "AutomationOps.h"
#include "../Plugins/CompressorPlugin.h"
#include "../Plugins/EqPlugin.h"
#include "EditViewState.h"
#include "MixerOps.h"
#include "TempoOps.h"
#include "TrackOps.h"
#include <map>
#include <set>

namespace cdaw::DawProject
{
    juce::String Report::summary() const
    {
        juce::String s;
        s << tracks << " track(s), " << clips << " clip(s), " << plugins << " plugin(s), "
          << automationLanes << " automation lane(s), " << markers << " marker(s).";

        if (! notes.isEmpty())
            s << "\n\nDidn't translate exactly:\n- " << notes.joinIntoString ("\n- ");

        return s;
    }

    namespace
    {
        //==============================================================================
        // Shared helpers

        juce::String num (double v)
        {
            // Plain decimal, no locale, enough precision for sample-accurate positions.
            auto s = juce::String (v, 9);
            if (s.containsChar ('.'))
                s = s.trimCharactersAtEnd ("0").trimCharactersAtEnd (".");
            return s.isEmpty() || s == "-0" ? juce::String ("0") : s;
        }

        juce::String colourToString (juce::Colour c)
        {
            return "#" + c.toDisplayString (false).toLowerCase();
        }

        std::optional<juce::Colour> colourFromString (const juce::String& s)
        {
            auto hex = s.trim().trimCharactersAtStart ("#");
            if (hex.length() < 6 || ! hex.containsOnly ("0123456789abcdefABCDEF"))
                return {};

            return juce::Colour ((juce::uint32) (0xff000000u | (juce::uint32) hex.substring (0, 6).getHexValue32()));
        }

        float gainToDb (double gain)        { return juce::Decibels::gainToDecibels ((float) gain, -100.0f); }
        double dbToGain (double db)         { return juce::Decibels::decibelsToGain (db, -100.0); }

        /** Insert index just before a track's own fader (VolumeAndPan / meter). */
        int preFaderIndex (te::PluginList& list)
        {
            int i = 0;
            for (auto p : list.getPlugins())
            {
                if (dynamic_cast<te::VolumeAndPanPlugin*> (p) != nullptr || dynamic_cast<te::LevelMeterPlugin*> (p) != nullptr)
                    return i;
                ++i;
            }
            return -1;
        }

        te::VolumeAndPanPlugin* volumeOf (te::Track& t)
        {
            if (auto at = dynamic_cast<te::AudioTrack*> (&t))   return at->getVolumePlugin();
            if (auto ft = dynamic_cast<te::FolderTrack*> (&t))  return ft->getVolumePlugin();
            return nullptr;
        }

        //==============================================================================
        // Export

        struct Exporter
        {
            Exporter (te::Edit& e, Report& r) : edit (e), report (r), ts (e.tempoSequence) {}

            te::Edit& edit;
            Report& report;
            te::TempoSequence& ts;

            int nextId = 0;
            juce::String newId()    { return "id" + juce::String (nextId++); }

            struct FileEntry { juce::File source; juce::String path; };
            std::vector<FileEntry> audioFiles;
            std::vector<std::pair<juce::MemoryBlock, juce::String>> pluginStates;
            std::map<juce::String, juce::String> pathForSource;     // full path -> path in zip

            std::map<te::Track*, juce::String> trackIds, channelIds;
            std::map<int, juce::String> fxChannelForBus;
            std::map<te::AutomatableParameter*, juce::String> paramIds;     // for automation targets
            juce::String masterTrackId, masterChannelId, tempoId, timeSigId;

            double beatsOf (te::TimePosition t) const          { return ts.toBeats (t).inBeats(); }

            juce::String addAudioFile (const juce::File& f)
            {
                auto key = f.getFullPathName();
                if (auto it = pathForSource.find (key); it != pathForSource.end())
                    return it->second;

                // Unique name inside the zip.
                auto base = f.getFileNameWithoutExtension(), ext = f.getFileExtension();
                auto path = "audio/" + base + ext;
                for (int n = 2; std::any_of (audioFiles.begin(), audioFiles.end(), [&] (auto& e) { return e.path == path; }); ++n)
                    path = "audio/" + base + " (" + juce::String (n) + ")" + ext;

                audioFiles.push_back ({ f, path });
                pathForSource[key] = path;
                return path;
            }

            static juce::XmlElement* addRealParam (juce::XmlElement& parent, const char* tag, const juce::String& id, const juce::String& name,
                                                   const char* unit, double value, std::optional<double> min = {}, std::optional<double> max = {})
            {
                auto* p = parent.createNewChildElement (tag);
                if (max) p->setAttribute ("max", num (*max));
                if (min) p->setAttribute ("min", num (*min));
                p->setAttribute ("unit", unit);
                p->setAttribute ("value", num (value));
                if (id.isNotEmpty()) p->setAttribute ("id", id);
                if (name.isNotEmpty()) p->setAttribute ("name", name);
                return p;
            }

            static juce::XmlElement* addBoolParam (juce::XmlElement& parent, const char* tag, const juce::String& id, const juce::String& name, bool value)
            {
                auto* p = parent.createNewChildElement (tag);
                p->setAttribute ("value", value ? "true" : "false");
                if (id.isNotEmpty()) p->setAttribute ("id", id);
                if (name.isNotEmpty()) p->setAttribute ("name", name);
                return p;
            }

            //==============================================================================
            void writeDevice (juce::XmlElement& devices, te::Plugin& plugin)
            {
                if (auto ext = dynamic_cast<te::ExternalPlugin*> (&plugin))
                {
                    if (! ext->desc.pluginFormatName.equalsIgnoreCase ("VST3"))
                    {
                        report.note ("Only VST3 plugins can be exported; skipped " + ext->getName() + " (" + ext->desc.pluginFormatName + ")");
                        return;
                    }

                    auto* d = devices.createNewChildElement ("Vst3Plugin");
                    auto id = newId();
                    juce::MemoryBlock preset;

                    if (auto* instance = ext->getAudioPluginInstance())
                    {
                        if (auto* vst3 = instance->getVST3Client())
                        {
                            // getPreset() skips JUCE's pending parameter changes; getStateInformation() flushes them.
                            juce::MemoryBlock flush;
                            instance->getStateInformation (flush);
                            preset = vst3->getPreset();
                        }
                    }

                    // The .vstpreset header holds the plugin's class ID: 'VST3', version, 32 hex chars.
                    juce::String classId;
                    if (preset.getSize() >= 40 && std::memcmp (preset.getData(), "VST3", 4) == 0)
                        classId = juce::String::fromUTF8 (static_cast<const char*> (preset.getData()) + 8, 32);

                    if (classId.isNotEmpty())   d->setAttribute ("deviceID", classId);
                    d->setAttribute ("deviceName", ext->desc.name);
                    d->setAttribute ("deviceRole", ext->isSynth() ? "instrument" : "audioFX");
                    if (ext->desc.manufacturerName.isNotEmpty()) d->setAttribute ("deviceVendor", ext->desc.manufacturerName);
                    d->setAttribute ("loaded", "true");
                    if (ext->desc.version.isNotEmpty()) d->setAttribute ("pluginVersion", ext->desc.version);
                    d->setAttribute ("id", id);
                    d->setAttribute ("name", ext->getName());

                    writePluginParameters (*d, *ext);
                    addBoolParam (*d, "Enabled", newId(), "On/Off", ext->isEnabled());

                    if (preset.getSize() > 0)
                    {
                        auto path = "plugins/" + juce::Uuid().toDashedString() + ".vstpreset";
                        pluginStates.push_back ({ preset, path });
                        d->createNewChildElement ("State")->setAttribute ("path", path);
                    }
                    else
                    {
                        report.note ("Couldn't read the state of " + ext->getName() + " (it isn't loaded?) - exported without its settings");
                    }

                    ++report.plugins;
                    return;
                }

                if (auto comp = dynamic_cast<te::CompressorPlugin*> (&plugin))
                {
                    auto* d = devices.createNewChildElement ("Compressor");
                    d->setAttribute ("deviceName", "Compressor");
                    d->setAttribute ("deviceRole", "audioFX");
                    d->setAttribute ("id", newId());
                    d->setAttribute ("name", comp->getName());
                    addBoolParam (*d, "Enabled", newId(), "On/Off", comp->isEnabled());

                    auto ratioParam = comp->ratio.getCurrentValue();
                    addRealParam (*d, "Attack", newId(), "Attack", "seconds", comp->attackMs.getCurrentValue() / 1000.0);
                    addRealParam (*d, "OutputGain", newId(), "Output", "decibel", comp->outputDb.getCurrentValue());
                    addRealParam (*d, "Ratio", newId(), "Ratio", "linear", ratioParam > 0.001f ? 1.0 / ratioParam : 100.0);
                    addRealParam (*d, "Release", newId(), "Release", "seconds", comp->releaseMs.getCurrentValue() / 1000.0);
                    addRealParam (*d, "Threshold", newId(), "Threshold", "decibel", gainToDb (comp->thresholdGain.getCurrentValue()));

                    if (MixerOps::getSidechainSource (*comp) != nullptr)
                        report.note ("Sidechain routing isn't part of DAWproject; " + comp->getName() + " exported without it");

                    ++report.plugins;
                    return;
                }

                if (auto eq = dynamic_cast<EqPlugin*> (&plugin))
                {
                    auto* d = devices.createNewChildElement ("Equalizer");
                    d->setAttribute ("deviceName", "Equalizer");
                    d->setAttribute ("deviceRole", "audioFX");
                    d->setAttribute ("id", newId());
                    d->setAttribute ("name", eq->getName());
                    addBoolParam (*d, "Enabled", newId(), "On/Off", eq->isEnabled());

                    for (int i = 0; i < EqPlugin::numBands; ++i)
                    {
                        auto& band = eq->getBand (i);
                        auto type = eq->getType (i);
                        static constexpr int orders[] { 1, 2, 4, 8 };
                        const char* name = "bell";

                        switch (type)
                        {
                            case EqMath::BandType::lowCut:    name = "highPass"; break;
                            case EqMath::BandType::highCut:   name = "lowPass"; break;
                            case EqMath::BandType::lowShelf:  name = "lowShelf"; break;
                            case EqMath::BandType::highShelf: name = "highShelf"; break;
                            case EqMath::BandType::notch:     name = "notch"; break;
                            case EqMath::BandType::peak:
                            case EqMath::BandType::numTypes:  break;
                        }

                        auto* b = d->createNewChildElement ("Band");
                        if (EqMath::isCut (type))
                            b->setAttribute ("order", orders[eq->getSlope (i)]);
                        b->setAttribute ("type", name);
                        addRealParam (*b, "Freq", newId(), "Frequency", "hertz", band.freq.getCurrentValue());
                        addRealParam (*b, "Gain", newId(), "Gain", "decibel", band.gain.getCurrentValue());
                        addRealParam (*b, "Q", newId(), "Q", "linear", band.q.getCurrentValue());
                        addBoolParam (*b, "Enabled", newId(), "On", eq->isOn (i));
                    }

                    addRealParam (*d, "OutputGain", newId(), "Output", "decibel", eq->output.getCurrentValue());
                    ++report.plugins;
                    return;
                }

                if (auto eq = dynamic_cast<te::EqualiserPlugin*> (&plugin))
                {
                    auto* d = devices.createNewChildElement ("Equalizer");
                    d->setAttribute ("deviceName", "Equalizer");
                    d->setAttribute ("deviceRole", "audioFX");
                    d->setAttribute ("id", newId());
                    d->setAttribute ("name", eq->getName());
                    addBoolParam (*d, "Enabled", newId(), "On/Off", eq->isEnabled());

                    auto band = [&] (const char* type, te::AutomatableParameter& f, te::AutomatableParameter& gain, te::AutomatableParameter& q)
                    {
                        auto* b = d->createNewChildElement ("Band");
                        b->setAttribute ("type", type);
                        addRealParam (*b, "Freq", newId(), "Frequency", "hertz", f.getCurrentValue());
                        addRealParam (*b, "Gain", newId(), "Gain", "decibel", gain.getCurrentValue());
                        addRealParam (*b, "Q", newId(), "Q", "linear", q.getCurrentValue());
                        addBoolParam (*b, "Enabled", newId(), "On", true);
                    };

                    band ("lowShelf",  *eq->loFreq,   *eq->loGain,   *eq->loQ);
                    band ("bell",      *eq->midFreq1, *eq->midGain1, *eq->midQ1);
                    band ("bell",      *eq->midFreq2, *eq->midGain2, *eq->midQ2);
                    band ("highShelf", *eq->hiFreq,   *eq->hiGain,   *eq->hiQ);

                    ++report.plugins;
                    return;
                }

                report.note ("The built-in " + plugin.getName() + " has no DAWproject equivalent and was left out");
            }

            void writePluginParameters (juce::XmlElement& device, te::ExternalPlugin& ext)
            {
                // Only parameters that are automated: they need ids so their automation can target them.
                juce::XmlElement* params = nullptr;

                for (auto p : ext.getAutomatableParameters())
                {
                    if (! p->getCurve().getNumPoints())
                        continue;

                    if (params == nullptr)
                        params = device.createNewChildElement ("Parameters");

                    auto id = newId();
                    auto* rp = addRealParam (*params, "RealParameter", id, p->getParameterName(), "normalized", p->getCurrentValue(), 0.0, 1.0);

                    if (p->paramID.containsOnly ("0123456789") && p->paramID.isNotEmpty())
                        rp->setAttribute ("parameterID", p->paramID.getIntValue());

                    paramIds[p] = id;
                }
            }

            //==============================================================================
            void writeChannel (juce::XmlElement& trackXml, te::Track& track, const juce::String& role,
                               const juce::String& destination, te::VolumeAndPanPlugin* vol, te::PluginList* plugins)
            {
                auto* ch = trackXml.createNewChildElement ("Channel");
                auto chId = newId();
                channelIds[&track] = chId;

                ch->setAttribute ("audioChannels", 2);
                if (destination.isNotEmpty())
                    ch->setAttribute ("destination", destination);
                ch->setAttribute ("role", role);
                ch->setAttribute ("solo", track.isSolo (false) ? "true" : "false");
                ch->setAttribute ("id", chId);

                if (plugins != nullptr)
                {
                    auto* devices = ch->createNewChildElement ("Devices");
                    for (auto p : plugins->getPlugins())
                        if (MixerOps::isInsertPlugin (*p))
                            writeDevice (*devices, *p);
                }

                addBoolParam (*ch, "Mute", newId(), "Mute", track.isMuted (false));

                auto panId = newId(), volId = newId();
                addRealParam (*ch, "Pan", panId, "Pan", "normalized", vol != nullptr ? (vol->getPan() + 1.0) * 0.5 : 0.5, 0.0, 1.0);

                // Sends (FX tracks get the matching Send destination later; written here with their bus).
                if (auto at = dynamic_cast<te::AudioTrack*> (&track))
                {
                    auto sends = MixerOps::getSends (*at);

                    if (! sends.isEmpty())
                    {
                        auto* sendsXml = ch->createNewChildElement ("Sends");

                        for (auto s : sends)
                        {
                            auto* sx = sendsXml->createNewChildElement ("Send");
                            sx->setAttribute ("type", MixerOps::isPostFader (*s) ? "post" : "pre");
                            sx->setAttribute ("id", newId());
                            sx->setAttribute ("name", edit.getAuxBusName (s->getBusNumber()));
                            sx->setAttribute ("cdawBus", s->getBusNumber());     // resolved to a destination once all FX channels are known
                            addBoolParam (*sx, "Enable", newId(), "On", s->isEnabled());
                            addRealParam (*sx, "Volume", newId(), "Send", "linear", dbToGain (s->getGainDb()), 0.0, 2.0);
                        }
                    }
                }

                addRealParam (*ch, "Volume", volId, "Volume", "linear", vol != nullptr ? dbToGain (vol->getVolumeDb()) : 1.0, 0.0, 2.0);

                if (vol != nullptr)
                {
                    paramIds[vol->volParam.get()] = volId;
                    paramIds[vol->panParam.get()] = panId;
                }
            }

            void writeTrack (juce::XmlElement& parent, te::Track& track, const juce::String& destination)
            {
                auto kind = TrackOps::getKind (track);
                auto* tx = parent.createNewChildElement ("Track");
                auto id = newId();
                trackIds[&track] = id;

                auto* folder = dynamic_cast<te::FolderTrack*> (&track);
                const bool isGroup = folder != nullptr && folder->isSubmixFolder();

                tx->setAttribute ("contentType", folder != nullptr ? "tracks" : kind == TrackKind::instrument ? "notes" : "audio");
                tx->setAttribute ("loaded", "true");
                tx->setAttribute ("id", id);
                tx->setAttribute ("name", track.getName());
                tx->setAttribute ("color", colourToString (track.getColour()));

                if (auto at = dynamic_cast<te::AudioTrack*> (&track))
                {
                    // Output to another track (a bus) rather than master.
                    auto dest = destination;
                    if (auto* out = at->getOutput().getDestinationTrack(); out != nullptr && channelIds.count (out))
                        dest = channelIds[out];

                    writeChannel (*tx, track, kind == TrackKind::fx ? "effect" : "regular", dest, at->getVolumePlugin(), &at->pluginList);

                    if (kind == TrackKind::fx)
                        for (auto p : at->pluginList.getPlugins())
                            if (auto ret = dynamic_cast<te::AuxReturnPlugin*> (p))
                                fxChannelForBus[ret->busNumber.get()] = channelIds[&track];
                }
                else if (isGroup)
                {
                    writeChannel (*tx, track, "submix", destination, folder->getVolumePlugin(), &folder->pluginList);
                }

                ++report.tracks;

                // Children route into the group's channel (or straight to master for a plain folder).
                auto childDestination = isGroup ? channelIds[&track] : destination;
                for (auto sub : track.getAllSubTracks (false))
                    if (sub->isAudioTrack() || sub->isFolderTrack())
                        writeTrack (*tx, *sub, childDestination);
            }

            //==============================================================================
            void writeClip (juce::XmlElement& clips, te::Clip& clip)
            {
                auto start = clip.getPosition().getStart(), end = clip.getPosition().getEnd();
                auto* cx = clips.createNewChildElement ("Clip");
                cx->setAttribute ("time", num (beatsOf (start)));
                cx->setAttribute ("duration", num (beatsOf (end) - beatsOf (start)));

                if (auto mc = dynamic_cast<te::MidiClip*> (&clip))
                {
                    cx->setAttribute ("contentTimeUnit", "beats");

                    if (mc->isLooping())
                    {
                        auto loopStart = mc->getLoopStartBeats().inBeats();
                        cx->setAttribute ("playStart", num (loopStart + mc->getOffsetInBeats().inBeats()));
                        cx->setAttribute ("loopStart", num (loopStart));
                        cx->setAttribute ("loopEnd", num (loopStart + mc->getLoopLengthBeats().inBeats()));
                    }
                    else
                    {
                        cx->setAttribute ("playStart", num (mc->getOffsetInBeats().inBeats()));
                    }

                    if (mc->isMuted()) cx->setAttribute ("enable", "false");
                    cx->setAttribute ("name", clip.getName());
                    cx->setAttribute ("color", colourToString (clip.getColour()));

                    auto* notes = cx->createNewChildElement ("Notes");
                    notes->setAttribute ("id", newId());
                    const int channel = juce::jlimit (0, 15, mc->getMidiChannel().getChannelNumber() - 1);

                    for (auto n : mc->getSequence().getNotes())
                    {
                        auto* nx = notes->createNewChildElement ("Note");
                        nx->setAttribute ("time", num (n->getStartBeat().inBeats()));
                        nx->setAttribute ("duration", num (n->getLengthBeats().inBeats()));
                        nx->setAttribute ("channel", channel);
                        nx->setAttribute ("key", n->getNoteNumber());
                        nx->setAttribute ("vel", num (n->getVelocity() / 127.0));
                    }

                    ++report.clips;
                    return;
                }

                if (auto ac = dynamic_cast<te::AudioClipBase*> (&clip))
                {
                    auto file = ac->getAudioFile().getFile();
                    te::AudioFile audioFile (edit.engine, file);
                    auto info = audioFile.getInfo();

                    cx->setAttribute ("contentTimeUnit", "seconds");
                    cx->setAttribute ("playStart", num (clip.getPosition().getOffset().inSeconds() * ac->getSpeedRatio()));
                    cx->setAttribute ("fadeTimeUnit", "seconds");
                    cx->setAttribute ("fadeInTime", num (ac->getFadeIn().inSeconds()));
                    cx->setAttribute ("fadeOutTime", num (ac->getFadeOut().inSeconds()));
                    if (ac->isMuted()) cx->setAttribute ("enable", "false");
                    cx->setAttribute ("name", clip.getName());
                    cx->setAttribute ("color", colourToString (clip.getColour()));

                    auto* ax = cx->createNewChildElement ("Audio");
                    ax->setAttribute ("channels", juce::jmax (1, info.numChannels));
                    ax->setAttribute ("sampleRate", juce::roundToInt (info.sampleRate > 0 ? info.sampleRate : 48000.0));
                    ax->setAttribute ("duration", num (audioFile.getLength()));
                    ax->setAttribute ("id", newId());
                    ax->createNewChildElement ("File")->setAttribute ("path", addAudioFile (file));

                    if (std::abs (ac->getGainDB()) > 0.01f)
                        report.note ("Clip gain isn't part of DAWproject (e.g. \"" + clip.getName() + "\" at " + juce::String (ac->getGainDB(), 1) + " dB)");
                    if (ac->getIsReversed())
                        report.note ("Reversed clips are exported playing forwards (\"" + clip.getName() + "\")");
                    if (std::abs (ac->getSpeedRatio() - 1.0) > 0.001)
                        report.note ("Clip speed changes aren't exported (\"" + clip.getName() + "\")");
                    if (auto wac = dynamic_cast<te::WaveAudioClip*> (ac); wac != nullptr && wac->getNumTakes (false) > 1)
                        report.note ("Takes: only the current take / comp of each clip is exported");

                    ++report.clips;
                    return;
                }

                clips.removeChildElement (cx, true);
            }

            void writeAutomation (juce::XmlElement& lanes, te::AutomatableParameter& param, const juce::String& paramId, bool isVolume, bool isPan)
            {
                auto& curve = param.getCurve();
                if (curve.getNumPoints() == 0)
                    return;

                auto* px = lanes.createNewChildElement ("Points");
                px->setAttribute ("unit", isVolume ? "linear" : "normalized");
                px->setAttribute ("id", newId());
                px->createNewChildElement ("Target")->setAttribute ("parameter", paramId);

                for (int i = 0; i < curve.getNumPoints(); ++i)
                {
                    auto value = curve.getPointValue (i);
                    double out = value;

                    if (isVolume)   out = dbToGain (te::volumeFaderPositionToDB (value));
                    else if (isPan) out = (value + 1.0) * 0.5;
                    else            out = param.valueRange.convertTo0to1 (value);

                    auto* p = px->createNewChildElement ("RealPoint");
                    p->setAttribute ("time", num (beatsOf (curve.getPointTime (i))));
                    p->setAttribute ("value", num (out));
                    p->setAttribute ("interpolation", "linear");
                }

                ++report.automationLanes;
            }

            //==============================================================================
            std::unique_ptr<juce::XmlElement> buildProject()
            {
                auto project = std::make_unique<juce::XmlElement> ("Project");
                project->setAttribute ("version", "1.0");

                auto* app = project->createNewChildElement ("Application");
                app->setAttribute ("name", "CDAW");
                app->setAttribute ("version", juce::JUCEApplicationBase::getInstance() != nullptr
                                                  ? juce::JUCEApplicationBase::getInstance()->getApplicationVersion() : juce::String ("0.1"));

                // Transport: the tempo and signature at the start; changes go in the arrangement.
                auto* transport = project->createNewChildElement ("Transport");
                tempoId = newId();
                timeSigId = newId();
                addRealParam (*transport, "Tempo", tempoId, "Tempo", "bpm", ts.getTempo (0)->getBpm(), 20.0, 666.0);
                auto* sig = transport->createNewChildElement ("TimeSignature");
                sig->setAttribute ("denominator", ts.getTimeSig (0)->denominator.get());
                sig->setAttribute ("numerator", ts.getTimeSig (0)->numerator.get());
                sig->setAttribute ("id", timeSigId);

                // Structure: master first (so every channel can point at it), then the tracks.
                auto* structure = project->createNewChildElement ("Structure");
                auto* masterXml = new juce::XmlElement ("Track");
                masterTrackId = newId();
                masterXml->setAttribute ("contentType", "audio notes");
                masterXml->setAttribute ("loaded", "true");
                masterXml->setAttribute ("id", masterTrackId);
                masterXml->setAttribute ("name", "Master");

                auto* masterChannel = masterXml->createNewChildElement ("Channel");
                masterChannelId = newId();
                masterChannel->setAttribute ("audioChannels", 2);
                masterChannel->setAttribute ("role", "master");
                masterChannel->setAttribute ("solo", "false");
                masterChannel->setAttribute ("id", masterChannelId);
                {
                    auto* devices = masterChannel->createNewChildElement ("Devices");
                    for (auto p : edit.getMasterPluginList().getPlugins())
                        if (MixerOps::isInsertPlugin (*p))
                            writeDevice (*devices, *p);

                    auto master = edit.getMasterVolumePlugin();
                    addBoolParam (*masterChannel, "Mute", newId(), "Mute", false);
                    auto panId = newId(), volId = newId();
                    addRealParam (*masterChannel, "Pan", panId, "Pan", "normalized", master != nullptr ? (master->getPan() + 1.0) * 0.5 : 0.5, 0.0, 1.0);
                    addRealParam (*masterChannel, "Volume", volId, "Volume", "linear", master != nullptr ? dbToGain (master->getVolumeDb()) : 1.0, 0.0, 2.0);

                    if (master != nullptr)
                    {
                        paramIds[master->volParam.get()] = volId;
                        paramIds[master->panParam.get()] = panId;
                    }
                }

                // FX returns must exist before sends point at them: write tracks, then resolve sends.
                for (auto t : edit.getTrackList().objects)
                    if (t->isAudioTrack() || t->isFolderTrack())
                        writeTrack (*structure, *t, masterChannelId);

                structure->addChildElement (masterXml);

                // Resolve sends to their FX channel ids (and drop sends to buses with no FX track).
                std::function<void (juce::XmlElement&)> resolveSends = [&] (juce::XmlElement& e)
                {
                    for (auto* c : e.getChildIterator())
                    {
                        if (c->hasTagName ("Send") && c->hasAttribute ("cdawBus"))
                        {
                            auto bus = c->getIntAttribute ("cdawBus");
                            c->removeAttribute ("cdawBus");

                            if (fxChannelForBus.count (bus))
                                c->setAttribute ("destination", fxChannelForBus[bus]);
                            else
                                report.note ("A send to bus " + juce::String (bus + 1) + " has no FX track and points nowhere");
                        }

                        resolveSends (*c);
                    }
                };
                resolveSends (*structure);

                // Arrangement: one lane per track with its clips and automation.
                auto* arrangement = project->createNewChildElement ("Arrangement");
                arrangement->setAttribute ("id", newId());
                auto* lanes = arrangement->createNewChildElement ("Lanes");
                lanes->setAttribute ("timeUnit", "beats");
                lanes->setAttribute ("id", newId());

                edit.visitAllTracksRecursive ([&] (te::Track& t)
                {
                    if (! trackIds.count (&t))
                        return true;

                    auto* tl = lanes->createNewChildElement ("Lanes");
                    tl->setAttribute ("track", trackIds[&t]);
                    tl->setAttribute ("id", newId());

                    if (auto ct = dynamic_cast<te::ClipTrack*> (&t); ct != nullptr && ! ct->getClips().isEmpty())
                    {
                        auto* clips = tl->createNewChildElement ("Clips");
                        clips->setAttribute ("id", newId());

                        for (auto c : ct->getClips())
                            writeClip (*clips, *c);
                    }

                    writeTrackAutomation (*tl, t);
                    return true;
                });

                // Master automation.
                {
                    auto* ml = lanes->createNewChildElement ("Lanes");
                    ml->setAttribute ("track", masterTrackId);
                    ml->setAttribute ("id", newId());

                    if (auto master = edit.getMasterVolumePlugin())
                    {
                        writeAutomation (*ml, *master->volParam, paramIds[master->volParam.get()], true, false);
                        writeAutomation (*ml, *master->panParam, paramIds[master->panParam.get()], false, true);
                    }

                    for (auto p : edit.getMasterPluginList().getPlugins())
                        for (auto param : p->getAutomatableParameters())
                            if (paramIds.count (param))
                                writeAutomation (*ml, *param, paramIds[param], false, false);
                }

                writeMarkers (*arrangement);
                writeTempoChanges (*arrangement);

                project->createNewChildElement ("Scenes");
                return project;
            }

            void writeTrackAutomation (juce::XmlElement& lane, te::Track& t)
            {
                if (auto vol = volumeOf (t))
                {
                    writeAutomation (lane, *vol->volParam, paramIds[vol->volParam.get()], true, false);
                    writeAutomation (lane, *vol->panParam, paramIds[vol->panParam.get()], false, true);
                }

                for (auto p : t.pluginList.getPlugins())
                    for (auto param : p->getAutomatableParameters())
                        if (paramIds.count (param) && dynamic_cast<te::VolumeAndPanPlugin*> (p) == nullptr)
                            writeAutomation (lane, *param, paramIds[param], false, false);

                for (auto param : AutomationOps::getAutomatableParameters (t))
                    if (param->getCurve().getNumPoints() > 0 && ! paramIds.count (param))
                        report.note ("Automation of \"" + AutomationOps::getDisplayName (*param) + "\" on " + t.getName() + " has no DAWproject target and was left out");
            }

            void writeMarkers (juce::XmlElement& arrangement)
            {
                // Markers, plus the starts of song sections (DAWproject has no arranger track).
                std::vector<std::pair<double, juce::String>> all;

                for (auto m : edit.getMarkerManager().getMarkers())
                    all.push_back ({ beatsOf (m->getPosition().getStart()), m->getName() });

                auto sections = ArrangerOps::getSections (edit);
                for (auto s : sections)
                    all.push_back ({ beatsOf (s->getPosition().getStart()), s->getName() });

                if (! sections.isEmpty())
                    report.note ("Song sections are exported as markers (DAWproject has no arranger track)");

                if (all.empty())
                    return;

                std::sort (all.begin(), all.end(), [] (auto& a, auto& b) { return a.first < b.first; });

                auto* markers = arrangement.createNewChildElement ("Markers");
                markers->setAttribute ("id", newId());

                for (auto& [beat, name] : all)
                {
                    auto* m = markers->createNewChildElement ("Marker");
                    m->setAttribute ("time", num (beat));
                    m->setAttribute ("name", name);
                    ++report.markers;
                }
            }

            void writeTempoChanges (juce::XmlElement& arrangement)
            {
                if (ts.getNumTempos() > 1)
                {
                    auto* px = arrangement.createNewChildElement ("TempoAutomation");
                    px->setAttribute ("unit", "bpm");
                    px->setAttribute ("timeUnit", "beats");
                    px->setAttribute ("id", newId());
                    px->createNewChildElement ("Target")->setAttribute ("parameter", tempoId);

                    for (int i = 0; i < ts.getNumTempos(); ++i)
                    {
                        auto* t = ts.getTempo (i);
                        auto* p = px->createNewChildElement ("RealPoint");
                        p->setAttribute ("time", num (t->getStartBeat().inBeats()));
                        p->setAttribute ("value", num (t->getBpm()));
                        p->setAttribute ("interpolation", std::abs (t->getCurve()) >= 0.999f ? "hold" : "linear");

                        if (std::abs (t->getCurve()) > 0.001f && std::abs (t->getCurve()) < 0.999f)
                            report.note ("Curved tempo ramps are exported as straight ramps");
                    }
                }

                if (ts.getNumTimeSigs() > 1)
                {
                    auto* px = arrangement.createNewChildElement ("TimeSignatureAutomation");
                    px->setAttribute ("timeUnit", "beats");
                    px->setAttribute ("id", newId());
                    px->createNewChildElement ("Target")->setAttribute ("parameter", timeSigId);

                    for (int i = 0; i < ts.getNumTimeSigs(); ++i)
                    {
                        auto* sig = ts.getTimeSig (i);
                        auto* p = px->createNewChildElement ("TimeSignaturePoint");
                        p->setAttribute ("time", num (sig->getStartBeat().inBeats()));
                        p->setAttribute ("numerator", sig->numerator.get());
                        p->setAttribute ("denominator", sig->denominator.get());
                    }
                }
            }
        };

        //==============================================================================
        // Import

        /** Positions inside a timeline: in beats or seconds, relative to where its content starts. */
        struct Frame
        {
            te::TimePosition originTime;     // edit time at which content position `originPos` plays
            double originPos = 0.0;
            bool beats = true;

            te::TimePosition toTime (double pos, const te::TempoSequence& ts) const
            {
                if (! beats)
                    return originTime + te::TimeDuration::fromSeconds (pos - originPos);

                return ts.toTime (ts.toBeats (originTime) + te::BeatDuration::fromBeats (pos - originPos));
            }

            Frame withUnit (const juce::XmlElement& e) const
            {
                auto f = *this;
                if (e.hasAttribute ("timeUnit"))
                    f.beats = e.getStringAttribute ("timeUnit") != "seconds";
                return f;
            }
        };

        struct Importer
        {
            Importer (te::Edit& e, juce::ZipFile& z, const juce::File& src, const juce::File& media, Report& r)
                : edit (e), zip (z), source (src), mediaFolder (media), report (r), ts (e.tempoSequence) {}

            te::Edit& edit;
            juce::ZipFile& zip;
            juce::File source, mediaFolder;
            Report& report;
            te::TempoSequence& ts;

            std::map<juce::String, te::Track::Ptr> trackForTrackId, trackForChannelId;
            std::map<juce::String, int> busForChannelId;

            enum class ParamKind { volume, pan, plugin, tempo };
            struct ParamTarget { te::AutomatableParameter::Ptr param; ParamKind kind; double min = 0, max = 1; };
            std::map<juce::String, ParamTarget> params;
            std::map<juce::String, juce::File> extractedFiles;
            juce::String tempoId, timeSigId;

            //==============================================================================
            /** "./audio/x.wav" or "/audio/x.wav" -> "audio/x.wav". Only those prefixes go: a
                blanket trim of '.' and '/' would also turn "../x" into "x" and ".x" into "x". */
            static juce::String normaliseZipPath (juce::String path)
            {
                path = path.replaceCharacter ('\\', '/');

                for (;;)
                {
                    if (path.startsWith ("./"))         path = path.substring (2);
                    else if (path.startsWith ("/"))     path = path.substring (1);
                    else                                return path;
                }
            }

            std::optional<juce::MemoryBlock> readZipEntry (juce::String path)
            {
                path = normaliseZipPath (path);

                for (int i = 0; i < zip.getNumEntries(); ++i)
                {
                    if (zip.getEntry (i)->filename.replaceCharacter ('\\', '/').equalsIgnoreCase (path))
                    {
                        if (auto stream = std::unique_ptr<juce::InputStream> (zip.createStreamForEntry (i)))
                        {
                            juce::MemoryBlock data;
                            stream->readIntoMemoryBlock (data);
                            return data;
                        }
                    }
                }

                return {};
            }

            /** The audio file for a <File path="..."/>, extracted from the zip into the media folder. */
            juce::File resolveMedia (const juce::XmlElement& fileXml)
            {
                auto path = fileXml.getStringAttribute ("path");
                auto key = path + (fileXml.getBoolAttribute ("external") ? "#ext" : "");

                if (auto it = extractedFiles.find (key); it != extractedFiles.end())
                    return it->second;

                juce::File result;

                if (fileXml.getBoolAttribute ("external"))
                {
                    result = juce::File::isAbsolutePath (path) ? juce::File (path) : source.getParentDirectory().getChildFile (path);
                }
                else if (auto stream = findZipStream (path))
                {
                    mediaFolder.createDirectory();
                    auto name = juce::File::createLegalFileName (path.replaceCharacter ('\\', '/').fromLastOccurrenceOf ("/", false, false));
                    result = mediaFolder.getNonexistentChildFile (juce::File (name).getFileNameWithoutExtension(), juce::File (name).getFileExtension(), false);

                    juce::FileOutputStream out (result);
                    if (out.openedOk())
                        out.writeFromInputStream (*stream, -1);
                }

                if (! result.existsAsFile())
                    report.note ("Audio file missing from the project: " + path);

                extractedFiles[key] = result;
                return result;
            }

            std::unique_ptr<juce::InputStream> findZipStream (juce::String path)
            {
                path = normaliseZipPath (path);
                for (int i = 0; i < zip.getNumEntries(); ++i)
                    if (zip.getEntry (i)->filename.replaceCharacter ('\\', '/').equalsIgnoreCase (path))
                        return std::unique_ptr<juce::InputStream> (zip.createStreamForEntry (i));
                return {};
            }

            //==============================================================================
            void clearEdit()
            {
                juce::Array<te::Track*> tracks;
                for (auto t : edit.getTrackList().objects)
                    if (t->isAudioTrack() || t->isFolderTrack())
                        tracks.add (t);

                TrackOps::deleteTracks (edit, tracks);

                for (auto m : TempoOps::getMarkers (edit))
                    m->removeFromParent();

                for (auto s : ArrangerOps::getSections (edit))
                    s->removeFromParent();
            }

            void readTransport (const juce::XmlElement* transport)
            {
                if (transport == nullptr)
                    return;

                if (auto* tempo = transport->getChildByName ("Tempo"))
                {
                    tempoId = tempo->getStringAttribute ("id");
                    if (auto bpm = tempo->getDoubleAttribute ("value"); bpm > 0.0)
                        ts.getTempo (0)->setBpm (juce::jlimit (te::TempoSetting::minBPM, te::TempoSetting::maxBPM, bpm));
                }

                if (auto* sig = transport->getChildByName ("TimeSignature"))
                {
                    timeSigId = sig->getStringAttribute ("id");
                    auto n = sig->getIntAttribute ("numerator", 4), d = sig->getIntAttribute ("denominator", 4);
                    ts.getTimeSig (0)->setStringTimeSig (juce::String (n) + "/" + juce::String (d));
                }
            }

            //==============================================================================
            static double realValue (const juce::XmlElement* p, double fallback)
            {
                return p != nullptr ? p->getDoubleAttribute ("value", fallback) : fallback;
            }

            /** Volume parameters arrive as linear gain (or dB); returns dB. */
            static float volumeDb (const juce::XmlElement* p)
            {
                if (p == nullptr)
                    return 0.0f;

                auto unit = p->getStringAttribute ("unit");
                auto v = p->getDoubleAttribute ("value", 1.0);
                return unit == "decibel" ? (float) v : gainToDb (v);
            }

            /** Pan arrives normalised (0..1) or bipolar; returns -1..1. */
            static float pan (const juce::XmlElement* p)
            {
                if (p == nullptr)
                    return 0.0f;

                auto v = p->getDoubleAttribute ("value", 0.5);
                auto min = p->getDoubleAttribute ("min", 0.0);
                return (float) juce::jlimit (-1.0, 1.0, min < 0.0 ? v : v * 2.0 - 1.0);
            }

            void applyChannel (te::Track& track, const juce::XmlElement& ch, te::VolumeAndPanPlugin* vol, te::PluginList* plugins)
            {
                auto chId = ch.getStringAttribute ("id");
                if (chId.isNotEmpty())
                    trackForChannelId[chId] = &track;

                if (vol != nullptr)
                {
                    auto* volXml = ch.getChildByName ("Volume");
                    auto* panXml = ch.getChildByName ("Pan");
                    vol->setVolumeDb (volumeDb (volXml));
                    vol->setPan (pan (panXml));

                    if (volXml != nullptr) params[volXml->getStringAttribute ("id")] = { vol->volParam, ParamKind::volume };
                    if (panXml != nullptr) params[panXml->getStringAttribute ("id")] = { vol->panParam, ParamKind::pan,
                                                                                           panXml->getDoubleAttribute ("min", 0.0), panXml->getDoubleAttribute ("max", 1.0) };
                }

                if (auto* mute = ch.getChildByName ("Mute"); mute != nullptr && mute->getBoolAttribute ("value"))
                    track.setMute (true);

                if (ch.getBoolAttribute ("solo"))
                    track.setSolo (true);

                if (plugins != nullptr)
                    if (auto* devices = ch.getChildByName ("Devices"))
                        for (auto* d : devices->getChildIterator())
                            addDevice (*plugins, *d);
            }

            //==============================================================================
            const juce::PluginDescription* findVst3 (const juce::XmlElement& d)
            {
                auto name = d.getStringAttribute ("deviceName");
                auto vendor = d.getStringAttribute ("deviceVendor");
                const juce::PluginDescription* best = nullptr;

                for (auto& desc : edit.engine.getPluginManager().knownPluginList.getTypes())
                {
                    if (! desc.pluginFormatName.equalsIgnoreCase ("VST3") || ! desc.name.equalsIgnoreCase (name))
                        continue;

                    static thread_local juce::PluginDescription found;
                    found = desc;

                    if (vendor.isEmpty() || desc.manufacturerName.equalsIgnoreCase (vendor))
                        return &found;

                    best = &found;
                }

                return best;
            }

            void addDevice (te::PluginList& plugins, const juce::XmlElement& d)
            {
                auto tag = d.getTagName();
                const bool enabled = d.getChildByName ("Enabled") == nullptr || d.getChildByName ("Enabled")->getBoolAttribute ("value", true);
                te::Plugin::Ptr plugin;

                if (tag == "Vst3Plugin")
                {
                    auto* desc = findVst3 (d);

                    if (desc == nullptr)
                    {
                        report.note ("Plugin not installed (or not scanned yet): " + d.getStringAttribute ("deviceName")
                                     + " - add it from the Plugin Manager, then load its preset by hand");
                        return;
                    }

                    plugin = edit.getPluginCache().createNewPlugin (te::ExternalPlugin::xmlTypeName, *desc);

                    if (plugin != nullptr)
                    {
                        plugins.insertPlugin (plugin, preFaderIndex (plugins), nullptr);

                        if (auto* state = d.getChildByName ("State"))
                        {
                            auto data = readZipEntry (state->getStringAttribute ("path"));
                            auto* ext = dynamic_cast<te::ExternalPlugin*> (plugin.get());
                            auto* instance = ext != nullptr ? ext->getAudioPluginInstance() : nullptr;
                            auto* vst3 = instance != nullptr ? instance->getVST3Client() : nullptr;

                            if (data && vst3 != nullptr && vst3->setPreset (*data))
                            {
                                // setPreset() leaves JUCE's parameter values stale; a state round trip resyncs them.
                                juce::MemoryBlock pluginState;
                                instance->getStateInformation (pluginState);
                                instance->setStateInformation (pluginState.getData(), (int) pluginState.getSize());
                                ext->flushPluginStateToValueTree();
                            }
                            else
                                report.note ("Couldn't restore the settings of " + d.getStringAttribute ("deviceName"));
                        }

                        mapPluginParameters (d, *plugin);
                    }
                }
                else if (tag == "Compressor")
                {
                    plugin = edit.getPluginCache().createNewPlugin (CompressorPlugin::xmlTypeName, {});

                    if (auto comp = dynamic_cast<te::CompressorPlugin*> (plugin.get()))
                    {
                        plugins.insertPlugin (plugin, preFaderIndex (plugins), nullptr);

                        auto seconds = [] (const juce::XmlElement* p, double fallbackMs)
                        {
                            if (p == nullptr) return fallbackMs;
                            auto v = p->getDoubleAttribute ("value");
                            return p->getStringAttribute ("unit") == "seconds" ? v * 1000.0 : v;
                        };

                        if (auto* t = d.getChildByName ("Threshold"))
                            comp->thresholdGain.setParameter ((float) juce::jlimit (0.01, 1.0, dbToGain (t->getDoubleAttribute ("value"))), juce::sendNotification);
                        if (auto* r = d.getChildByName ("Ratio"); r != nullptr && r->getDoubleAttribute ("value") >= 1.0)
                            comp->ratio.setParameter ((float) juce::jlimit (0.0, 0.95, 1.0 / r->getDoubleAttribute ("value")), juce::sendNotification);
                        comp->attackMs.setParameter ((float) seconds (d.getChildByName ("Attack"), comp->attackMs.getCurrentValue()), juce::sendNotification);
                        comp->releaseMs.setParameter ((float) seconds (d.getChildByName ("Release"), comp->releaseMs.getCurrentValue()), juce::sendNotification);
                        if (auto* o = d.getChildByName ("OutputGain"))
                            comp->outputDb.setParameter ((float) o->getDoubleAttribute ("value"), juce::sendNotification);
                    }
                }
                else if (tag == "Equalizer")
                {
                    plugin = edit.getPluginCache().createNewPlugin (EqPlugin::xmlTypeName, {});

                    if (auto eq = dynamic_cast<EqPlugin*> (plugin.get()))
                    {
                        plugins.insertPlugin (plugin, preFaderIndex (plugins), nullptr);
                        int index = 0;

                        for (auto* b : d.getChildWithTagNameIterator ("Band"))
                        {
                            auto type = b->getStringAttribute ("type");

                            if (index >= EqPlugin::numBands)
                            {
                                report.note ("An EQ had more than " + juce::String (EqPlugin::numBands) + " bands; the rest were left out");
                                break;
                            }

                            // Cut slopes: DAWproject gives the filter order (1 = 6 dB/oct ... 8 = 48 dB/oct).
                            const int order = b->getIntAttribute ("order", 2);
                            const int slope = order <= 1 ? 0 : order <= 2 ? 1 : order <= 4 ? 2 : 3;
                            EqMath::BandType bandType;

                            if (type == "highPass")         bandType = EqMath::BandType::lowCut;
                            else if (type == "lowPass")     bandType = EqMath::BandType::highCut;
                            else if (type == "lowShelf")    bandType = EqMath::BandType::lowShelf;
                            else if (type == "highShelf")   bandType = EqMath::BandType::highShelf;
                            else if (type == "notch")       bandType = EqMath::BandType::notch;
                            else
                            {
                                if (type != "bell")
                                    report.note ("EQ band shape "" + type + "" was imported as a peak");
                                bandType = EqMath::BandType::peak;
                            }

                            auto& band = eq->getBand (index);
                            eq->setType (index, bandType);
                            eq->setSlope (index, slope);
                            band.freq.setParameter ((float) juce::jlimit ((double) EqPlugin::minFreq, (double) EqPlugin::maxFreq, realValue (b->getChildByName ("Freq"), 1000.0)), juce::sendNotification);
                            band.gain.setParameter ((float) juce::jlimit (-(double) EqPlugin::maxGain, (double) EqPlugin::maxGain, realValue (b->getChildByName ("Gain"), 0.0)), juce::sendNotification);
                            band.q.setParameter ((float) juce::jlimit ((double) EqPlugin::minQ, (double) EqPlugin::maxQ, realValue (b->getChildByName ("Q"), EqPlugin::getDefaultQ (bandType))), juce::sendNotification);

                            auto* bandEnabled = b->getChildByName ("Enabled");
                            eq->setOn (index, bandEnabled == nullptr || bandEnabled->getBoolAttribute ("value", true));
                            ++index;
                        }

                        if (auto* out = d.getChildByName ("OutputGain"))
                            eq->output.setParameter ((float) juce::jlimit (-24.0, 24.0, out->getDoubleAttribute ("value")), juce::sendNotification);
                    }
                }
                else
                {
                    auto kind = tag == "Vst2Plugin" ? "VST2" : tag == "ClapPlugin" ? "CLAP" : tag == "AuPlugin" ? "Audio Unit" : "built-in";
                    report.note (juce::String (kind) + " device \"" + d.getStringAttribute ("deviceName", tag) + "\" isn't supported by CDAW (VST3 only)");
                    return;
                }

                if (plugin != nullptr)
                {
                    plugin->setEnabled (enabled);
                    ++report.plugins;
                }
            }

            void mapPluginParameters (const juce::XmlElement& device, te::Plugin& plugin)
            {
                auto* list = device.getChildByName ("Parameters");
                if (list == nullptr)
                    return;

                auto all = plugin.getAutomatableParameters();

                for (auto* p : list->getChildIterator())
                {
                    auto id = p->getStringAttribute ("id");
                    if (id.isEmpty())
                        continue;

                    te::AutomatableParameter* match = nullptr;
                    auto paramId = p->getStringAttribute ("parameterID");
                    auto name = p->getStringAttribute ("name");

                    for (auto ap : all)
                        if ((paramId.isNotEmpty() && ap->paramID == paramId) || (paramId.isEmpty() && ap->getParameterName() == name))
                            match = ap;

                    if (match != nullptr)
                        params[id] = { match, ParamKind::plugin, p->getDoubleAttribute ("min", 0.0), p->getDoubleAttribute ("max", 1.0) };
                }
            }

            //==============================================================================
            void readTrack (const juce::XmlElement& tx, te::FolderTrack* parent)
            {
                auto id = tx.getStringAttribute ("id");
                auto* ch = tx.getChildByName ("Channel");
                auto role = ch != nullptr ? ch->getStringAttribute ("role") : juce::String();
                auto content = juce::StringArray::fromTokens (tx.getStringAttribute ("contentType"), " ", "");
                bool hasChildren = tx.getChildByName ("Track") != nullptr;

                if (role == "master")
                {
                    if (auto master = edit.getMasterVolumePlugin())
                        applyChannel (*edit.getMasterTrack(), *ch, master.get(), &edit.getMasterPluginList());

                    trackForTrackId[id] = edit.getMasterTrack();
                    return;
                }

                if (! hasChildren && ! content.contains ("audio") && ! content.contains ("notes") && role.isEmpty())
                {
                    if (content.contains ("video"))
                        report.note ("Video tracks aren't supported and were left out");
                    return;     // marker / automation-only tracks
                }

                te::Track::Ptr track;

                if (hasChildren || content.contains ("tracks"))
                {
                    const bool group = ch != nullptr;   // a folder with its own channel is a group (submix)
                    track = TrackOps::addFolderTrack (edit, nullptr, group);
                    if (auto ft = dynamic_cast<te::FolderTrack*> (track.get()); ft != nullptr && ch != nullptr)
                        applyChannel (*ft, *ch, ft->getVolumePlugin(), &ft->pluginList);
                }
                else
                {
                    te::AudioTrack::Ptr at;

                    if (role == "effect")
                    {
                        at = TrackOps::addFxTrack (edit, nullptr);
                        for (auto p : at->pluginList.getPlugins())
                            if (auto ret = dynamic_cast<te::AuxReturnPlugin*> (p))
                                busForChannelId[ch->getStringAttribute ("id")] = ret->busNumber.get();
                    }
                    else if (isInstrumentTrack (tx))
                    {
                        at = TrackOps::addInstrumentTrack (edit, nullptr, nullptr);
                    }
                    else
                    {
                        at = TrackOps::addAudioTrack (edit, nullptr);
                    }

                    track = at;
                    if (at != nullptr && ch != nullptr)
                        applyChannel (*at, *ch, at->getVolumePlugin(), &at->pluginList);
                }

                if (track == nullptr)
                    return;

                track->setName (tx.getStringAttribute ("name", track->getName()));
                if (auto c = colourFromString (tx.getStringAttribute ("color")))
                    track->setColour (*c);

                if (auto ft = dynamic_cast<te::FolderTrack*> (track.get()); ft != nullptr && role == "effect")
                    edit.setAuxBusName (busForChannelId[ch->getStringAttribute ("id")], ft->getName());

                if (role == "effect")
                    if (auto busIt = busForChannelId.find (ch->getStringAttribute ("id")); busIt != busForChannelId.end())
                        edit.setAuxBusName (busIt->second, track->getName());

                if (parent != nullptr)
                {
                    auto subs = parent->getAllSubTracks (false);
                    edit.moveTrack (track, te::TrackInsertPoint (parent, subs.isEmpty() ? nullptr : subs.getLast()));
                }

                trackForTrackId[id] = track;
                ++report.tracks;

                for (auto* child : tx.getChildWithTagNameIterator ("Track"))
                    readTrack (*child, dynamic_cast<te::FolderTrack*> (track.get()));
            }

            static bool isInstrumentTrack (const juce::XmlElement& tx)
            {
                if (tx.getStringAttribute ("contentType").contains ("notes") && ! tx.getStringAttribute ("contentType").contains ("audio"))
                    return true;

                if (auto* ch = tx.getChildByName ("Channel"))
                    if (auto* devices = ch->getChildByName ("Devices"))
                        for (auto* d : devices->getChildIterator())
                            if (d->getStringAttribute ("deviceRole") == "instrument")
                                return true;

                return false;
            }

            /** Second pass, once every channel exists: sends and outputs to other tracks. */
            void readRouting (const juce::XmlElement& structure)
            {
                std::function<void (const juce::XmlElement&, const juce::XmlElement*)> visit =
                    [&] (const juce::XmlElement& tx, const juce::XmlElement* parentTrack)
                {
                    auto it = trackForTrackId.find (tx.getStringAttribute ("id"));
                    auto* ch = tx.getChildByName ("Channel");

                    if (it != trackForTrackId.end() && ch != nullptr)
                    {
                        if (auto at = dynamic_cast<te::AudioTrack*> (it->second.get()))
                        {
                            if (auto* sends = ch->getChildByName ("Sends"))
                            {
                                for (auto* s : sends->getChildWithTagNameIterator ("Send"))
                                {
                                    auto bus = busForChannelId.find (s->getStringAttribute ("destination"));
                                    if (bus == busForChannelId.end())
                                    {
                                        report.note ("A send on " + at->getName() + " goes somewhere other than an FX track and was left out");
                                        continue;
                                    }

                                    auto db = volumeDb (s->getChildByName ("Volume"));
                                    if (auto send = MixerOps::addSend (*at, bus->second, s->getStringAttribute ("type", "post") != "pre", db))
                                        if (auto* en = s->getChildByName ("Enable"); en != nullptr && ! en->getBoolAttribute ("value", true))
                                            send->setEnabled (false);
                                }
                            }

                            // Output to another (non-parent, non-master) track.
                            auto dest = ch->getStringAttribute ("destination");
                            auto destTrack = trackForChannelId.find (dest);
                            const bool toParent = parentTrack != nullptr && parentTrack->getChildByName ("Channel") != nullptr
                                                  && parentTrack->getChildByName ("Channel")->getStringAttribute ("id") == dest;

                            if (destTrack != trackForChannelId.end() && ! toParent && destTrack->second.get() != edit.getMasterTrack())
                            {
                                if (auto destAudio = dynamic_cast<te::AudioTrack*> (destTrack->second.get()))
                                    at->getOutput().setOutputToTrack (destAudio);
                                else
                                    report.note (at->getName() + " is routed to a group it isn't inside; it now goes to the master");
                            }
                        }
                    }

                    for (auto* child : tx.getChildWithTagNameIterator ("Track"))
                        visit (*child, &tx);
                };

                for (auto* tx : structure.getChildWithTagNameIterator ("Track"))
                    visit (*tx, nullptr);
            }

            //==============================================================================
            void readLanes (const juce::XmlElement& lanes, Frame frame, te::Track* track)
            {
                frame = frame.withUnit (lanes);

                if (auto t = lanes.getStringAttribute ("track"); t.isNotEmpty())
                    if (auto it = trackForTrackId.find (t); it != trackForTrackId.end())
                        track = it->second.get();

                for (auto* child : lanes.getChildIterator())
                {
                    auto tag = child->getTagName();

                    if (tag == "Lanes")             readLanes (*child, frame, track);
                    else if (tag == "Clips")        readClips (*child, frame, track, {});
                    else if (tag == "Points")       readPoints (*child, frame);
                    else if (tag == "Notes" || tag == "Audio" || tag == "Warps")
                        report.note ("Some content sat directly on a lane rather than in a clip and was left out");
                }
            }

            /** Reads clips; `window` (if set) trims them to an enclosing clip's visible range. */
            juce::Array<te::Clip*> readClips (const juce::XmlElement& clipsXml, Frame frame, te::Track* track, std::optional<te::TimeRange> window)
            {
                frame = frame.withUnit (clipsXml);
                juce::Array<te::Clip*> created;

                for (auto* cx : clipsXml.getChildWithTagNameIterator ("Clip"))
                    created.addArray (readClip (*cx, frame, track, window));

                return created;
            }

            juce::Array<te::Clip*> readClip (const juce::XmlElement& cx, Frame frame, te::Track* track, std::optional<te::TimeRange> window)
            {
                auto* at = dynamic_cast<te::AudioTrack*> (track);
                if (at == nullptr)
                {
                    if (track != nullptr && track != edit.getMasterTrack())
                        report.note ("Clips on folder or group tracks were left out");
                    return {};
                }

                const double time = cx.getDoubleAttribute ("time");
                const double playStart = cx.getDoubleAttribute ("playStart", 0.0);
                const bool contentBeats = cx.hasAttribute ("contentTimeUnit") ? cx.getStringAttribute ("contentTimeUnit") != "seconds" : frame.beats;

                auto start = frame.toTime (time, ts);
                auto end = cx.hasAttribute ("duration") ? frame.toTime (time + cx.getDoubleAttribute ("duration"), ts)
                                                        : start + te::TimeDuration::fromSeconds (4.0);

                if (window)
                {
                    start = juce::jmax (start, window->getStart());
                    end = juce::jmin (end, window->getEnd());
                }

                if (end <= start)
                    return {};

                // Content positions relative to this clip: content `playStart` plays at the clip's time.
                Frame content { frame.toTime (time, ts), playStart, contentBeats };
                juce::Array<te::Clip*> created;

                if (cx.hasAttribute ("loopEnd") && cx.hasAttribute ("loopStart"))
                {
                    auto loopLen = cx.getDoubleAttribute ("loopEnd") - cx.getDoubleAttribute ("loopStart");
                    auto dur = cx.getDoubleAttribute ("duration", loopLen);

                    if (loopLen > 0.0 && dur > loopLen + 1e-6 && cx.getChildByName ("Notes") == nullptr)
                        report.note ("Looped audio clips are imported playing once (\"" + cx.getStringAttribute ("name") + "\")");
                }

                for (auto* child : cx.getChildIterator())
                {
                    auto tag = child->getTagName();

                    if (tag == "Notes")
                    {
                        if (auto mc = makeMidiClip (*at, cx, *child, start, end, playStart, contentBeats))
                            created.add (mc);
                    }
                    else if (tag == "Audio")
                    {
                        if (auto c = makeAudioClip (*at, cx, *child, start, end, content, nullptr))
                            created.add (c);
                    }
                    else if (tag == "Warps")
                    {
                        if (auto* audio = child->getChildByName ("Audio"))
                            if (auto c = makeAudioClip (*at, cx, *audio, start, end, content, child))
                                created.add (c);
                    }
                    else if (tag == "Clips")
                    {
                        // Clips inside a clip (e.g. Bitwig's audio events): flatten, trimmed to this clip.
                        created.addArray (readClips (*child, content, track, te::TimeRange (start, end)));
                    }
                    else if (tag == "Lanes")
                    {
                        for (auto* c : child->getChildWithTagNameIterator ("Clips"))
                            created.addArray (readClips (*c, content.withUnit (*child), track, te::TimeRange (start, end)));
                    }
                    else if (tag == "Points")
                    {
                        report.note ("Clip-level automation was left out");
                    }
                }

                // Clip-wide settings on whatever was created.
                const bool enabled = cx.getBoolAttribute ("enable", true);
                for (auto c : created)
                {
                    if (! enabled)
                    {
                        if (auto ac = dynamic_cast<te::AudioClipBase*> (c)) ac->setMuted (true);
                        if (auto mc = dynamic_cast<te::MidiClip*> (c))      mc->setMuted (true);
                    }
                }

                return created;
            }

            te::Clip* makeMidiClip (te::AudioTrack& at, const juce::XmlElement& cx, const juce::XmlElement& notes,
                                    te::TimePosition start, te::TimePosition end, double playStart, bool contentBeats)
            {
                auto mc = at.insertMIDIClip ({ start, end }, nullptr);
                if (mc == nullptr)
                    return nullptr;

                mc->setName (cx.getStringAttribute ("name", at.getName()));
                if (auto c = colourFromString (cx.getStringAttribute ("color")))
                    mc->setColour (*c);
                else
                    mc->setColour (at.getColour());

                // Notes are in content beats; the clip shows content from `playStart`.
                auto secondsToBeats = [&] (double s) { return ts.toBeats (start + te::TimeDuration::fromSeconds (s)).inBeats() - ts.toBeats (start).inBeats(); };
                auto contentToBeats = [&] (double v) { return contentBeats ? v : secondsToBeats (v); };

                const bool looping = cx.hasAttribute ("loopStart") && cx.hasAttribute ("loopEnd")
                                     && cx.getDoubleAttribute ("loopEnd") > cx.getDoubleAttribute ("loopStart");
                auto& seq = mc->getSequence();
                const double offset = looping ? 0.0 : contentToBeats (playStart);

                for (auto* n : notes.getChildWithTagNameIterator ("Note"))
                {
                    auto noteStart = contentToBeats (n->getDoubleAttribute ("time")) - offset;
                    auto noteLen = contentToBeats (n->getDoubleAttribute ("duration"));

                    if (! looping && (noteStart + noteLen <= 0.0 || noteStart >= (ts.toBeats (end) - ts.toBeats (start)).inBeats()))
                        continue;

                    auto vel = juce::jlimit (1, 127, juce::roundToInt (n->getDoubleAttribute ("vel", 0.8) * 127.0));
                    seq.addNote (juce::jlimit (0, 127, n->getIntAttribute ("key")), te::BeatPosition::fromBeats (noteStart),
                                 te::BeatDuration::fromBeats (juce::jmax (0.001, noteLen)), vel, 0, nullptr);
                }

                if (looping)
                {
                    auto ls = contentToBeats (cx.getDoubleAttribute ("loopStart"));
                    auto le = contentToBeats (cx.getDoubleAttribute ("loopEnd"));
                    mc->setLoopRangeBeats ({ te::BeatPosition::fromBeats (ls), te::BeatPosition::fromBeats (le) });
                    mc->setOffset (te::TimeDuration::fromSeconds (juce::jmax (0.0, playStart - cx.getDoubleAttribute ("loopStart")) * 60.0 / ts.getTempoAt (start).getBpm()));
                }

                ++report.clips;
                return mc.get();
            }

            te::Clip* makeAudioClip (te::AudioTrack& at, const juce::XmlElement& cx, const juce::XmlElement& audio,
                                     te::TimePosition start, te::TimePosition end, const Frame& content, const juce::XmlElement* warps)
            {
                auto* fileXml = audio.getChildByName ("File");
                if (fileXml == nullptr)
                    return nullptr;

                auto file = resolveMedia (*fileXml);
                if (! file.existsAsFile())
                    return nullptr;

                // Where in the file the clip starts (seconds). With warps, map through the first and
                // last warp points (the clip's content time -> file time).
                double fileOffset = 0.0;
                auto playStart = content.originPos;

                if (warps != nullptr)
                {
                    auto points = warps->getChildWithTagNameIterator ("Warp");
                    std::vector<std::pair<double, double>> w;
                    for (auto* p : points)
                        w.push_back ({ p->getDoubleAttribute ("time"), p->getDoubleAttribute ("contentTime") });

                    if (w.size() >= 2)
                    {
                        auto [t0, c0] = w.front();
                        auto [t1, c1] = w.back();
                        auto filePerContent = (t1 != t0) ? (c1 - c0) / (t1 - t0) : 1.0;
                        fileOffset = c0 + (playStart - t0) * filePerContent;

                        // Compare the warped length with how long the content plays in real time.
                        auto warpedStart = content.toTime (t0, ts), warpedEnd = content.toTime (t1, ts);
                        auto realSeconds = (warpedEnd - warpedStart).inSeconds();
                        auto fileSeconds = (c1 - c0) * (warps->getStringAttribute ("contentTimeUnit") == "seconds" ? 1.0 : 60.0 / ts.getTempoAt (start).getBpm());

                        if (realSeconds > 0.0 && std::abs (fileSeconds / realSeconds - 1.0) > 0.01)
                            report.note ("Time-stretched audio is imported at its original speed (\"" + cx.getStringAttribute ("name", file.getFileNameWithoutExtension()) + "\")");
                    }
                }
                else
                {
                    fileOffset = content.beats ? (content.toTime (playStart, ts) - content.toTime (0.0, ts)).inSeconds() : playStart;
                }

                auto clip = at.insertWaveClip (cx.getStringAttribute ("name", file.getFileNameWithoutExtension()), file,
                                               { { start, end }, te::TimeDuration::fromSeconds (juce::jmax (0.0, fileOffset)) }, false);

                if (clip == nullptr)
                    return nullptr;

                if (auto c = colourFromString (cx.getStringAttribute ("color")))
                    clip->setColour (*c);

                // Fades (in their own unit, default the clip's content unit).
                auto fadeSeconds = [&] (double v)
                {
                    const bool beats = cx.hasAttribute ("fadeTimeUnit") ? cx.getStringAttribute ("fadeTimeUnit") == "beats" : content.beats;
                    return beats ? v * 60.0 / ts.getTempoAt (start).getBpm() : v;
                };

                clip->setFadeIn (te::TimeDuration::fromSeconds (fadeSeconds (cx.getDoubleAttribute ("fadeInTime", 0.0))));
                clip->setFadeOut (te::TimeDuration::fromSeconds (fadeSeconds (cx.getDoubleAttribute ("fadeOutTime", 0.0))));

                ++report.clips;
                return clip.get();
            }

            //==============================================================================
            void readPoints (const juce::XmlElement& px, Frame frame)
            {
                frame = frame.withUnit (px);
                auto* target = px.getChildByName ("Target");
                auto it = target != nullptr ? params.find (target->getStringAttribute ("parameter")) : params.end();

                if (it == params.end())
                {
                    if (target != nullptr && target->hasAttribute ("expression"))
                        report.note ("Note-expression / MIDI controller automation was left out");
                    else
                        report.note ("Some automation targets parameters CDAW couldn't match and was left out");
                    return;
                }

                auto& [param, kind, min, max] = it->second;
                auto& curve = param->getCurve();
                auto unit = px.getStringAttribute ("unit");
                auto* um = &edit.getUndoManager();

                auto toParam = [&] (double v) -> float
                {
                    switch (kind)
                    {
                        case ParamKind::volume: return te::decibelsToVolumeFaderPosition (unit == "decibel" ? (float) v : gainToDb (v));
                        case ParamKind::pan:    return (float) juce::jlimit (-1.0, 1.0, min < 0.0 ? v : v * 2.0 - 1.0);
                        case ParamKind::plugin: return param->valueRange.convertFrom0to1 ((float) juce::jlimit (0.0, 1.0, max > min ? (v - min) / (max - min) : v));
                        case ParamKind::tempo:  break;
                    }
                    return (float) v;
                };

                juce::Array<const juce::XmlElement*> points;
                for (auto* p : px.getChildWithTagNameIterator ("RealPoint"))
                    points.add (p);

                for (int i = 0; i < points.size(); ++i)
                {
                    auto t = frame.toTime (points[i]->getDoubleAttribute ("time"), ts);
                    auto value = toParam (points[i]->getDoubleAttribute ("value"));
                    curve.addPoint (t, value, 0.0f, um);

                    // "hold": stay flat until just before the next point.
                    if (points[i]->getStringAttribute ("interpolation") == "hold" && i + 1 < points.size())
                    {
                        auto next = frame.toTime (points[i + 1]->getDoubleAttribute ("time"), ts);
                        if (next - t > te::TimeDuration::fromSeconds (0.002))
                            curve.addPoint (next - te::TimeDuration::fromSeconds (0.001), value, 0.0f, um);
                    }
                }

                if (! points.isEmpty())
                    ++report.automationLanes;
            }

            void readTempoAndSignatureChanges (const juce::XmlElement& arrangement)
            {
                if (auto* tempo = arrangement.getChildByName ("TempoAutomation"))
                {
                    Frame frame = Frame().withUnit (*tempo);
                    juce::Array<const juce::XmlElement*> points;
                    for (auto* p : tempo->getChildWithTagNameIterator ("RealPoint"))
                        points.add (p);

                    for (int i = 0; i < points.size(); ++i)
                    {
                        auto pos = points[i]->getDoubleAttribute ("time");
                        auto beat = frame.beats ? te::BeatPosition::fromBeats (pos) : ts.toBeats (te::TimePosition::fromSeconds (pos));
                        auto bpm = juce::jlimit (te::TempoSetting::minBPM, te::TempoSetting::maxBPM, points[i]->getDoubleAttribute ("value", 120.0));
                        auto curve = points[i]->getStringAttribute ("interpolation") == "linear" ? 0.0f : 1.0f;

                        if (beat <= te::BeatPosition::fromBeats (0.001))
                        {
                            ts.getTempo (0)->setBpm (bpm);
                            ts.getTempo (0)->setCurve (curve);
                        }
                        else
                        {
                            ts.insertTempo (beat, bpm, curve);
                        }
                    }
                }

                if (auto* sigs = arrangement.getChildByName ("TimeSignatureAutomation"))
                {
                    Frame frame = Frame().withUnit (*sigs);

                    for (auto* p : sigs->getChildWithTagNameIterator ("TimeSignaturePoint"))
                    {
                        auto pos = p->getDoubleAttribute ("time");
                        auto beat = frame.beats ? te::BeatPosition::fromBeats (pos) : ts.toBeats (te::TimePosition::fromSeconds (pos));
                        auto text = juce::String (p->getIntAttribute ("numerator", 4)) + "/" + juce::String (p->getIntAttribute ("denominator", 4));

                        if (beat <= te::BeatPosition::fromBeats (0.001))
                            ts.getTimeSig (0)->setStringTimeSig (text);
                        else if (auto sig = ts.insertTimeSig (beat))
                            sig->setStringTimeSig (text);
                    }
                }
            }

            void readMarkers (const juce::XmlElement& arrangement)
            {
                if (auto* markers = arrangement.getChildByName ("Markers"))
                {
                    Frame frame = Frame().withUnit (*markers);

                    for (auto* m : markers->getChildWithTagNameIterator ("Marker"))
                    {
                        TempoOps::addMarker (edit, frame.toTime (m->getDoubleAttribute ("time"), ts), m->getStringAttribute ("name"));
                        ++report.markers;
                    }
                }
            }

            //==============================================================================
            juce::Result run (const juce::XmlElement& project)
            {
                clearEdit();
                readTransport (project.getChildByName ("Transport"));

                auto* structure = project.getChildByName ("Structure");
                auto* arrangement = project.getChildByName ("Arrangement");

                // Tempo first, so every beat position below lands on the right time.
                if (arrangement != nullptr)
                    readTempoAndSignatureChanges (*arrangement);

                if (tempoId.isNotEmpty())
                    params[tempoId] = { nullptr, ParamKind::tempo };

                if (structure != nullptr)
                {
                    for (auto* tx : structure->getChildIterator())
                    {
                        if (tx->hasTagName ("Track"))
                            readTrack (*tx, nullptr);
                        else if (tx->hasTagName ("Channel"))
                            report.note ("A mixer channel without a track was left out");
                    }

                    readRouting (*structure);
                }

                params.erase (tempoId);     // tempo already handled above

                if (arrangement != nullptr)
                {
                    if (auto* lanes = arrangement->getChildByName ("Lanes"))
                        readLanes (*lanes, Frame(), nullptr);

                    readMarkers (*arrangement);
                }

                if (project.getChildByName ("Scenes") != nullptr && project.getChildByName ("Scenes")->getNumChildElements() > 0)
                    report.note ("Clip launcher scenes aren't supported and were left out");

                return juce::Result::ok();
            }
        };
    }

    //==============================================================================
    juce::Result exportTo (te::Edit& edit, const juce::File& destination, Report& report)
    {
        Exporter exporter (edit, report);
        auto project = exporter.buildProject();

        juce::XmlElement::TextFormat format;
        format.customHeader = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>";

        juce::XmlElement metadata ("MetaData");
        metadata.createNewChildElement ("Title")->addTextElement (edit.getName().isNotEmpty() ? edit.getName()
                                                                                               : destination.getFileNameWithoutExtension());

        juce::ZipFile::Builder zip;
        auto projectText = project->toString (format);
        auto metaText = metadata.toString (format);
        zip.addEntry (std::make_unique<juce::MemoryInputStream> (projectText.toRawUTF8(), projectText.getNumBytesAsUTF8(), true), 9, "project.xml", juce::Time::getCurrentTime());
        zip.addEntry (std::make_unique<juce::MemoryInputStream> (metaText.toRawUTF8(), metaText.getNumBytesAsUTF8(), true), 9, "metadata.xml", juce::Time::getCurrentTime());

        for (auto& f : exporter.audioFiles)
        {
            if (! f.source.existsAsFile())
            {
                report.note ("Audio file not found, left out: " + f.source.getFullPathName());
                continue;
            }

            zip.addFile (f.source, 0, f.path);      // audio is already compressed or large: store it
        }

        for (auto& [data, path] : exporter.pluginStates)
            zip.addEntry (std::make_unique<juce::MemoryInputStream> (data, true), 6, path, juce::Time::getCurrentTime());

        auto temp = destination.getSiblingFile (destination.getFileName() + ".part");
        temp.deleteFile();

        {
            juce::FileOutputStream out (temp);
            if (! out.openedOk())
                return juce::Result::fail ("Can't write " + destination.getFullPathName());

            if (! zip.writeToStream (out, nullptr))
                return juce::Result::fail ("Writing the DAWproject failed");
        }

        destination.deleteFile();
        if (! temp.moveFileTo (destination))
            return juce::Result::fail ("Can't replace " + destination.getFullPathName());

        return juce::Result::ok();
    }

    juce::Result importInto (te::Edit& edit, const juce::File& source, const juce::File& mediaFolder, Report& report)
    {
        juce::ZipFile zip (source);

        if (zip.getNumEntries() == 0)
            return juce::Result::fail (source.getFileName() + " isn't a DAWproject file (not a zip archive)");

        std::unique_ptr<juce::XmlElement> project;

        for (int i = 0; i < zip.getNumEntries(); ++i)
        {
            if (zip.getEntry (i)->filename.equalsIgnoreCase ("project.xml"))
                if (auto stream = std::unique_ptr<juce::InputStream> (zip.createStreamForEntry (i)))
                    project = juce::parseXML (stream->readEntireStreamAsString());
        }

        if (project == nullptr || ! project->hasTagName ("Project"))
            return juce::Result::fail (source.getFileName() + " has no readable project.xml");

        if (! project->getStringAttribute ("version").startsWith ("1."))
            report.note ("DAWproject version " + project->getStringAttribute ("version") + " (CDAW reads 1.x); some parts may be missing");

        Importer importer (edit, zip, source, mediaFolder, report);
        return importer.run (*project);
    }
}
