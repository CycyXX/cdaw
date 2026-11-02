#include "CubaseProject.h"
#include "CubaseArchive.h"
#include "CubaseStockPlugins.h"
#include <map>
#include <set>

namespace cdaw::CubaseProject
{
    using namespace Cubase;

    //==============================================================================
    double faderToDb (double position)
    {
        if (position <= 0.0)
            return -100.0;

        // Measured from Cubase projects, which store each fader's dB next to its position: below
        // about -6.5 dB the gain is 1.53716 * (position / 32767)^2; above, this table (0 dB at 25856,
        // +6.02 dB at the top, 32767).
        static constexpr std::pair<double, double> table[] = {
            { 18203.891, -6.4765 }, { 18647.268, -6.0585 }, { 18974.938, -5.6797 }, { 19332.543, -5.2727 },
            { 19630.275, -4.9478 }, { 19894.250, -4.6696 }, { 20186.814, -4.3714 }, { 20479.375, -4.0830 },
            { 20736.830, -3.8369 }, { 20994.426, -3.5975 }, { 21268.625, -3.3497 }, { 21596.295, -3.0625 },
            { 21923.963, -2.7846 }, { 22251.633, -2.5152 }, { 22579.303, -2.2540 }, { 22850.123, -2.0439 },
            { 23222.018, -1.7633 }, { 23562.312, -1.5144 }, { 23889.982, -1.2812 }, { 24217.652, -1.0541 },
            { 24545.320, -0.8328 }, { 24797.600, -0.6662 }, { 25125.268, -0.4544 }, { 25452.938, -0.2477 },
            { 25703.455, -0.0929 }, { 25856.000,  0.0000 }, { 26108.277,  0.3114 }, { 26511.340,  0.7869 },
            { 26839.010,  1.1551 }, { 27166.680,  1.5084 }, { 27494.348,  1.8478 }, { 27822.018,  2.1745 },
            { 28086.000,  2.4291 }, { 28465.936,  2.7828 }, { 28805.027,  3.0867 }, { 29132.697,  3.3707 },
            { 29460.367,  3.6457 }, { 29788.037,  3.9122 }, { 30115.705,  4.1708 }, { 30473.312,  4.4445 },
            { 30800.982,  4.6879 }, { 31426.385,  5.1344 }, { 32439.330,  5.8122 }, { 32767.000,  6.0206 } };

        if (position < table[0].first)
        {
            auto u = position / 32767.0;
            return juce::jmax (-100.0, 20.0 * std::log10 (1.53716 * u * u));
        }

        for (size_t i = 1; i < std::size (table); ++i)
        {
            if (position <= table[i].first)
            {
                auto [x0, y0] = table[i - 1];
                auto [x1, y1] = table[i];
                return y0 + (y1 - y0) * (position - x0) / (x1 - x0);
            }
        }

        return table[std::size (table) - 1].second;
    }

    namespace
    {
        constexpr double ticksPerBeat = 480.0;

        //==============================================================================
        // What we read from the file

        struct Plugin
        {
            juce::String name, classId;
            juce::MemoryBlock component, controller;
            bool enabled = true, frozen = false;
        };

        struct Send
        {
            int bus = 0;
            double db = 0.0;
            bool pre = false, on = false;
        };

        struct AutomationPoint { double ticks, value; };       // value: fader position 0..1 (volume) or pan 0..1

        struct Channel
        {
            juce::String name;
            int kind = 0;
            double volumeDb = 0.0, pan = 0.5;       // pan 0 (left) .. 1 (right)
            bool mute = false, solo = false;
            int ownBus = 0, outputBus = 0;
            std::vector<Plugin> inserts;
            std::optional<Plugin> instrument;
            std::vector<Send> sends;
        };

        struct AudioEvent
        {
            double start = 0.0;                     // ticks
            double length = 0.0, offset = 0.0;      // samples
            double sampleRate = 44100.0;
            double fadeIn = 0.0, fadeOut = 0.0;     // samples
            float gain = 1.0f;
            int channels = 2;
            juce::String name, fileName;
            juce::File file;
            std::optional<std::pair<double, double>> window;     // ticks: the audio part it sits in
            bool muted = false;
            int lane = 0;           // takes stack downwards: the lowest lane (highest number) plays
        };

        /** Event flags (the u16 every event starts with): bit 1 = muted. */
        constexpr int mutedFlag = 2;

        struct Note       { double pos, length; int key, velocity; };
        struct Controller { double pos; int type, value; };     // type: CC number, or one of these:
        enum { pitchBend = 1000, channelPressure, programChange };

        struct MidiPart
        {
            double start = 0.0, length = 0.0, offset = 0.0;       // ticks
            juce::String name;
            std::vector<Note> notes;
            std::vector<Controller> controllers;
            bool muted = false;
        };

        enum class Kind { audio, instrument, midi, folder, group, fx };

        struct Track
        {
            Kind kind = Kind::audio;
            juce::String name;
            std::optional<Channel> channel;
            std::vector<AudioEvent> audio;
            std::vector<MidiPart> parts;
            std::vector<Track> children;
            std::vector<AutomationPoint> volumeAutomation, panAutomation;

            juce::String trackId, channelId;        // filled in for the translation
            juce::String volumeId, panId;
        };

        struct TempoPoint { double ticks, seconds, bpm; bool ramp; };
        struct Signature  { double ticks; int numerator, denominator; };
        struct Marker     { double ticks; juce::String name; };

        struct Project
        {
            juce::String appVersion;
            std::vector<TempoPoint> tempo;
            std::vector<Signature> signatures;
            std::vector<Marker> markers;
            std::vector<Track> tracks;
            std::optional<Channel> master;
            Track masterAutomation;         // the Stereo Out device track's automation
            std::map<int, juce::String> otherOutputs;       // bus -> name of a Cubase output other than the main one
        };

        //==============================================================================
        /** Tick <-> seconds, from the tempo points (which carry their own time in seconds). */
        struct TempoMap
        {
            std::vector<TempoPoint> points;

            double secondsAt (double ticks) const
            {
                if (points.empty())
                    return ticks / ticksPerBeat * 0.5;

                size_t i = 0;
                while (i + 1 < points.size() && points[i + 1].ticks <= ticks)
                    ++i;

                auto& p = points[i];
                auto beats = (ticks - p.ticks) / ticksPerBeat;

                if (p.ramp && i + 1 < points.size() && beats > 0.0)
                {
                    // Tempo changing linearly over the segment: integrate in small steps.
                    auto& q = points[i + 1];
                    const int steps = 64;
                    double s = 0.0;
                    for (int k = 0; k < steps; ++k)
                    {
                        auto b = (k + 0.5) * beats / steps;
                        auto frac = juce::jlimit (0.0, 1.0, b * ticksPerBeat / (q.ticks - p.ticks));
                        s += 60.0 / (p.bpm + (q.bpm - p.bpm) * frac) * beats / steps;
                    }
                    return p.seconds + s;
                }

                return p.seconds + beats * 60.0 / p.bpm;
            }
        };

        //==============================================================================
        struct Decoder
        {
            Decoder (const Archive& a, const juce::File& src, DawProject::Report& r)
                : archive (a), source (src), projectFolder (src.getParentDirectory()), report (r) {}

            const Archive& archive;
            juce::File source, projectFolder;
            DawProject::Report& report;
            Project project;

            std::map<juce::String, juce::File> audioFileCache;
            std::multimap<juce::String, juce::File> filesByName;   // under the project folder, lower-case name
            bool indexed = false;

            //==============================================================================
            juce::Result run()
            {
                readVersion();

                auto* arrangement = archive.root ("Arrangement1");
                auto* pa = arrangement != nullptr ? arrangement->child ("PArrangement") : nullptr;
                auto* root = pa != nullptr ? pa->child ("MRoot") : nullptr;
                auto* list = root != nullptr ? root->child ("MTrackList") : nullptr;

                if (list == nullptr)
                    return juce::Result::fail ("The project's track list couldn't be read");

                readTempo (*arrangement);
                readSignatures (*arrangement);
                readMixer();
                readTrackList (*list, project.tracks);

                if (automationReadOff > 0)
                    report.note (juce::String (automationReadOff) + " automation lane(s) had Read switched off in Cubase and were left out");
                if (automationUnsupported > 0)
                    report.note (juce::String (automationUnsupported) + " automation lane(s) for plugin or other parameters were left out "
                                 "(volume and pan automation came across)");

                return juce::Result::ok();
            }

            int automationReadOff = 0, automationUnsupported = 0;

            /** Volume and pan automation: MAutomationNode > MAutomationTrackEvent > { MAutoListNode >
                MParamEvent (f64 ticks, f32 value), MAutomationTrack (u16 read, u32 kind: 2 volume,
                7 a parameter path such as "Panner") }. */
            void readAutomation (const Node& trackEvent, Track& t)
            {
                for (auto* node : trackEvent.childrenOf ("MAutomationNode"))
                {
                    for (auto* lane : node->childrenOf ("MAutomationTrackEvent"))
                    {
                        auto* target = lane->child ("MAutomationTrack");
                        auto* list = lane->child ("MAutoListNode");
                        if (target == nullptr || list == nullptr)
                            continue;

                        auto events = list->childrenOf ("MParamEvent");
                        if (events.empty())
                            continue;

                        auto r = archive.readerFor (*target);
                        auto read = r.u16();
                        auto kind = r.u32();
                        auto path = kind == 7 ? r.string() : juce::String();

                        std::vector<AutomationPoint>* points = nullptr;
                        if (kind == 2)                              points = &t.volumeAutomation;
                        else if (kind == 7 && path == "Panner")     points = &t.panAutomation;

                        if (! r.ok || points == nullptr)
                        {
                            ++automationUnsupported;
                            continue;
                        }

                        if ((read & 1) == 0)
                        {
                            ++automationReadOff;
                            continue;
                        }

                        for (auto* e : events)
                        {
                            auto er = archive.readerFor (*e);
                            auto ticks = er.f64();
                            auto value = er.f32();
                            if (er.ok && std::isfinite (ticks) && std::isfinite (value))
                                points->push_back ({ ticks, juce::jlimit (0.0, 1.0, (double) value) });
                        }
                    }
                }
            }

            void readVersion()
            {
                if (auto* v = archive.root ("Version"))
                {
                    if (! v->children.empty())
                    {
                        auto r = archive.readerFor (*v->children.front());
                        auto app = r.string();
                        auto version = r.string();
                        if (r.ok)
                            project.appVersion = app + " " + version;
                    }
                }
            }

            //==============================================================================
            void readTempo (const Node& arrangement)
            {
                auto* t = arrangement.find ("MTempoTrackEvent");
                if (t == nullptr)
                    return;

                auto r = archive.readerFor (*t);
                auto n = r.u32();
                std::vector<TempoPoint> points;

                for (juce::uint32 i = 0; i < n && i < 100000 && r.ok; ++i)
                {
                    auto secondsPerBeat = r.f32();
                    auto seconds = r.f64();
                    auto ticks = r.f64();
                    auto type = r.u16();

                    if (r.ok && secondsPerBeat > 0.0f)
                        points.push_back ({ ticks, seconds, 60.0 / secondsPerBeat, type == 1 });
                }

                auto fixed = r.f32();
                auto trackOff = r.u16();

                if (r.ok && trackOff == 1 && fixed > 10.0f && fixed < 1000.0f)
                {
                    // The tempo track is switched off: Cubase plays the fixed tempo.
                    points = { { 0.0, 0.0, (double) fixed, false } };
                }

                if (! points.empty())
                    points.front().ticks = points.front().seconds = 0.0;

                project.tempo = points;
            }

            void readSignatures (const Node& arrangement)
            {
                auto* s = arrangement.find ("MSignatureTrackEvent");
                if (s == nullptr)
                    return;

                auto r = archive.readerFor (*s);
                auto n = r.u32();

                for (juce::uint32 i = 0; i < n && i < 10000 && r.ok; ++i)
                {
                    auto ticks = r.u32();
                    auto num = r.u16();
                    auto den = r.u16();
                    r.skip (8);

                    if (r.ok && num > 0 && num < 100 && juce::isPowerOfTwo ((int) den) && den <= 64)
                        project.signatures.push_back ({ (double) ticks, num, den });
                }
            }

            //==============================================================================
            juce::String trackName (const Node& n) const
            {
                auto r = archive.readerFor (n);
                r.skip (2 + 3 * 8 + 8);
                auto name = r.string();
                return r.ok ? name : juce::String();
            }

            void readTrackList (const Node& list, std::vector<Track>& into)
            {
                for (auto& c : list.children)
                    readTrackEvent (*c, into);
            }

            void readTrackEvent (const Node& n, std::vector<Track>& into)
            {
                const auto& cls = n.cls;
                Track t;
                t.name = trackName (n);

                if (cls == "MFolderTrack")
                {
                    t.kind = Kind::folder;
                    auto* sub = n.child ("MTrackList");

                    if (sub != nullptr)
                        readTrackList (*sub, t.children);

                    // Cubase's own folder of input/output device tracks ends up empty: leave it out.
                    if (t.children.empty() && sub != nullptr && ! sub->children.empty())
                        return;
                }
                else if (cls == "MAudioTrackEvent")
                {
                    t.kind = Kind::audio;
                    t.channel = channelOf (n.child ("MAudioTrack"));

                    for (auto& e : n.children)
                    {
                        if (e->cls == "MAudioEvent")
                            readAudioEvent (*e, 0.0, t);
                        else if (e->cls == "MAudioPartEvent")
                            readAudioPart (*e, t);
                    }
                }
                else if (cls == "MInstrumentTrackEvent" || cls == "MMidiTrackEvent")
                {
                    t.kind = cls == "MMidiTrackEvent" ? Kind::midi : Kind::instrument;
                    t.channel = channelOf (n.child (cls == "MMidiTrackEvent" ? "MMidiTrack" : "MInstrumentTrack"));

                    for (auto& e : n.children)
                        if (e->cls == "MMidiPartEvent")
                            readMidiPart (*e, t);

                    if (t.kind == Kind::midi)
                        report.note ("MIDI track \"" + t.name + "\" was imported as an instrument track without its instrument "
                                     "(Cubase rack instruments aren't brought across)");
                }
                else if (cls == "MDeviceTrackEvent")
                {
                    t.channel = channelOf (n.child ("MTrack"));

                    if (! t.channel)
                        return;

                    if (t.channel->kind == 4)       t.kind = Kind::group;
                    else if (t.channel->kind == 5)  t.kind = Kind::fx;
                    else
                    {
                        // Input / output devices; the main output's track carries the master's automation.
                        if (project.master && t.name == project.master->name)
                            readAutomation (n, project.masterAutomation);
                        return;
                    }
                }
                else if (cls == "MMarkerTrackEvent")
                {
                    readMarkers (n);
                    return;
                }
                else if (cls == "MVideoTrackEvent")
                {
                    report.note ("Video tracks aren't supported and were left out");
                    return;
                }
                else if (cls == "MSamplerTrackEvent" || cls == "MSamplerTrackWrapperEvent")
                {
                    report.note ("Sampler track \"" + t.name + "\" isn't supported and was left out");
                    return;
                }
                else
                {
                    return;     // tempo / signature / chord / arranger / ruler tracks and the like
                }

                if (t.channel)
                    readAutomation (n, t);

                into.push_back (std::move (t));
            }

            //==============================================================================
            std::optional<Channel> channelOf (const Node* n)
            {
                if (n == nullptr)
                    return {};

                auto data = Cubase::readChannel (archive, *n);
                if (! data)
                    return {};

                return channelFrom (data->attrs, data->kind);
            }

            static double volumeOf (const Attr& volume)
            {
                // Cubase keeps the dB next to the fader position.
                if (auto* anchor = volume.get ("AnchorValue"); anchor != nullptr && anchor->type == Attr::Type::real)
                    return juce::jmax (-100.0, anchor->real);

                return faderToDb (volume.getReal ("Value", 25856.0));
            }

            static std::optional<Plugin> pluginFrom (const Attr& slot)
            {
                auto* p = slot.get ("Plugin");
                if (p == nullptr)
                    return {};

                Plugin plugin;
                plugin.name = p->getString ("Plugin Name");
                plugin.classId = p->getString ("Plugin UID/GUID");

                if (auto* b = p->getBlob ("audioComponent"))    plugin.component = *b;
                if (auto* b = p->getBlob ("editController"))    plugin.controller = *b;

                // A frozen track's plugins are switched off while Cubase plays its freeze file; CDAW
                // runs them live, as they were before freezing.
                plugin.frozen = slot.getInt ("State", 1) == 2;
                const bool active = plugin.frozen ? slot.getInt ("WasEnableBeforeFreeze", 1) != 0
                                                  : p->getInt ("Active", 1) != 0;
                plugin.enabled = active && slot.getInt ("Bypass", 0) == 0;

                if (plugin.name.isEmpty() && plugin.classId.isEmpty())
                    return {};

                return plugin;
            }

            static Channel channelFrom (const Attr& a, int kind)
            {
                Channel c;
                c.kind = kind;
                c.name = a.getString ("Name/String");

                if (auto* v = a.get ("Volume"))
                    c.volumeDb = volumeOf (*v);

                // The standard panner's state starts with its position (little-endian float, 0..1).
                if (auto* blob = a.getBlob ("Panner/audioComponent"); blob != nullptr && blob->getSize() >= 4)
                {
                    float p;
                    auto bits = juce::ByteOrder::littleEndianInt (blob->getData());
                    std::memcpy (&p, &bits, sizeof (p));
                    if (std::isfinite (p))
                        c.pan = juce::jlimit (0.0, 1.0, (double) p);
                }

                auto flags = a.getInt ("SoloFlag", 0);
                c.mute = (flags & 1) != 0;
                c.solo = (flags & 2) != 0;

                c.ownBus = (int) a.getInt ("OwnInputBus/Bus UID", 0);
                c.outputBus = (int) a.getInt ("OutputBusValue/Value", 0);

                if (auto* slots = a.getArray ("InsertFolder/Slot"))
                    for (auto& s : *slots)
                        if (auto p = pluginFrom (s))
                            c.inserts.push_back (std::move (*p));

                if (a.getInt ("InsertFolder/Bypass", 0) != 0)
                    for (auto& p : c.inserts)
                        p.enabled = false;

                if (auto* synth = a.get ("Synth Slot"))
                    c.instrument = pluginFrom (*synth);

                if (auto* sends = a.getArray ("SendFolder/Slot"))
                {
                    for (auto& s : *sends)
                    {
                        Send send;
                        send.bus = (int) s.getInt ("Output/Value", 0);
                        if (send.bus == 0)
                            continue;

                        send.on = s.getInt ("On", 0) != 0;
                        send.pre = s.getInt ("Pre", 0) != 0;
                        if (auto* v = s.get ("Volume"))
                            send.db = volumeOf (*v);
                        c.sends.push_back (send);
                    }
                }

                return c;
            }

            /** Stereo Out and the other outputs live in the Devices chunk's VST Mixer. */
            void readMixer()
            {
                auto* devices = archive.root ("Devices");
                auto* attrsNode = devices != nullptr ? devices->child ("FAttributes") : nullptr;
                if (attrsNode == nullptr)
                    return;

                auto r = archive.readerFor (*attrsNode);
                auto attrs = readAttributes (r, true);

                if (auto* outputs = attrs.getArray ("VST Mixer/Output Channels"))
                {
                    for (size_t i = 0; i < outputs->size(); ++i)
                    {
                        auto& o = (*outputs)[i];

                        if (i == 0)
                            project.master = channelFrom (o, 0);
                        else
                            project.otherOutputs[(int) o.getInt ("OwnInputBus/Bus UID", 0)] = o.getString ("Name/String");
                    }
                }
            }

            //==============================================================================
            /** A fade object holds an interpolator: u32 count, then (x, y) pairs; x in samples. */
            double fadeLength (const Node* fade) const
            {
                if (fade == nullptr || fade->children.empty())
                    return 0.0;

                auto r = archive.readerFor (*fade->children.front());
                auto n = r.u32();
                double length = 0.0;

                for (juce::uint32 i = 0; i < n && i < 10000 && r.ok; ++i)
                {
                    auto x = r.f64();
                    r.f64();
                    if (r.ok && std::isfinite (x))
                        length = juce::jmax (length, x);
                }

                return length;
            }

            void readAudioEvent (const Node& e, double partOffsetTicks, Track& t,
                                 std::optional<std::pair<double, double>> window = {})
            {
                auto r = archive.readerFor (e);
                AudioEvent ev;
                ev.muted = (r.u16() & mutedFlag) != 0;
                ev.start = r.f64() + partOffsetTicks;
                ev.length = r.f64();
                ev.offset = r.f64();
                auto* clip = archive.readSlot (r, e);

                if (! r.ok || clip == nullptr || clip->cls != "PAudioClip")
                {
                    report.note ("An audio event on \"" + t.name + "\" had no readable audio file and was left out");
                    return;
                }

                if (e.version >= 5)
                {
                    r.u32();
                    r.u32();
                    auto* fadeIn = archive.readSlot (r, e);
                    auto* fadeOut = archive.readSlot (r, e);

                    if (r.ok)
                    {
                        ev.fadeIn = fadeLength (fadeIn);
                        ev.fadeOut = fadeLength (fadeOut);
                        r.f64();
                        ev.name = r.string();
                        r.u32();
                        auto gain = r.f32();
                        if (r.ok && std::isfinite (gain) && gain >= 0.0f && gain < 16.0f)
                            ev.gain = gain;

                        auto lane = r.u32();
                        if (r.ok && lane < 10000)
                            ev.lane = (int) lane;
                    }
                }

                readClip (*clip, ev);
                ev.window = window;

                if (ev.length > 0.0)
                    t.audio.push_back (std::move (ev));
            }

            /** An audio part: its events sit relative to the part's start minus its offset, and only
                play inside the part. */
            void readAudioPart (const Node& pe, Track& t)
            {
                auto r = archive.readerFor (pe);
                r.u16();
                auto start = r.f64();
                auto length = r.f64();
                auto offset = r.f64();
                auto* part = archive.readSlot (r, pe);

                if (! r.ok || part == nullptr)
                    return;

                const auto before = t.audio.size();

                for (auto& e : part->children)
                    if (e->cls == "MAudioEvent")
                        readAudioEvent (*e, start - offset, t, std::make_pair (start, start + length));

                if (t.audio.size() > before)
                    report.note ("Audio parts on \"" + t.name + "\" were flattened into separate clips");
            }

            void readClip (const Node& clip, AudioEvent& ev)
            {
                if (auto* af = clip.find ("AudioFile"))
                {
                    auto r = archive.readerFor (*af);
                    r.skip (8);
                    r.u16();
                    r.u16();
                    auto channels = r.u16();
                    auto rate = r.f32();
                    if (r.ok && rate > 1000.0f && rate < 1.0e6f)
                        ev.sampleRate = rate;
                    if (r.ok && channels > 0 && channels <= 64)
                        ev.channels = channels;
                }

                if (auto* path = clip.find ("FNPath"))
                {
                    auto r = archive.readerFor (*path);
                    auto name = r.string();
                    r.skip (4);             // file type, e.g. 'WAVE'
                    r.string();
                    r.string();
                    r.string();
                    r.u32();
                    r.u16();
                    auto dir = r.string();

                    ev.fileName = name;
                    ev.file = findAudio (r.ok ? dir : juce::String(), name);
                }

                if (ev.name.isEmpty())
                    ev.name = juce::File::createFileWithoutCheckingPath (ev.fileName).getFileNameWithoutExtension();
            }

            /** The audio file: where Cubase had it, else in this project's folder (projects get moved). */
            juce::File findAudio (const juce::String& dir, const juce::String& name)
            {
                if (name.isEmpty())
                    return {};

                auto key = dir + "|" + name;
                if (auto it = audioFileCache.find (key); it != audioFileCache.end())
                    return it->second;

                juce::File result;
                juce::Array<juce::File> candidates;

                if (dir.isNotEmpty())
                    candidates.add (juce::File::isAbsolutePath (dir) ? juce::File (dir).getChildFile (name)
                                                                     : projectFolder.getChildFile (dir).getChildFile (name));

                auto trimmedDir = dir.replaceCharacter ('\\', '/').trimCharactersAtEnd ("/");
                auto lastFolder = trimmedDir.fromLastOccurrenceOf ("/", false, false);

                candidates.add (projectFolder.getChildFile ("Audio").getChildFile (name));
                if (lastFolder.isNotEmpty())
                    candidates.add (projectFolder.getChildFile (lastFolder).getChildFile (name));

                for (auto& c : candidates)
                {
                    if (c.existsAsFile())
                    {
                        result = c;
                        break;
                    }
                }

                if (result == juce::File())
                {
                    indexProjectFolder();
                    auto range = filesByName.equal_range (name.toLowerCase());
                    if (range.first != range.second)
                        result = range.first->second;
                }

                if (result == juce::File())
                    report.note ("Audio file not found: " + (dir.isNotEmpty() ? juce::File::addTrailingSeparator (dir) : juce::String()) + name);

                audioFileCache[key] = result;
                return result;
            }

            void indexProjectFolder()
            {
                if (indexed)
                    return;

                indexed = true;
                int count = 0;

                for (const auto& entry : juce::RangedDirectoryIterator (projectFolder, true, "*", juce::File::findFiles))
                {
                    filesByName.insert ({ entry.getFile().getFileName().toLowerCase(), entry.getFile() });
                    if (++count > 50000)
                        break;
                }
            }

            //==============================================================================
            void readMidiPart (const Node& pe, Track& t)
            {
                auto r = archive.readerFor (pe);
                MidiPart part;
                part.muted = (r.u16() & mutedFlag) != 0;
                part.start = r.f64();
                part.length = r.f64();
                part.offset = r.f64();
                auto* p = archive.readSlot (r, pe);

                if (! r.ok || p == nullptr || p->cls != "MMidiPart")
                {
                    report.note ("A MIDI part on \"" + t.name + "\" couldn't be read and was left out");
                    return;
                }

                readMidiEvents (*p, part);
                t.parts.push_back (std::move (part));
            }

            /** Event records: status, f64 position (ticks, from the part's content start), channel,
                data1, data2, 5 bytes, n tagged fields of 14 bytes; notes then add f64 length + 17 bytes,
                sysex a u32-sized blob. */
            void readMidiEvents (const Node& node, MidiPart& part)
            {
                auto r = archive.readerFor (node);
                part.name = r.string();
                r.skip (12);
                auto count = r.u32();
                auto numClasses = r.u8();

                if (numClasses > 0)
                {
                    if (node.children.size() < numClasses)
                        return;
                    r.pos = node.children[numClasses - 1]->end;
                }

                for (juce::uint32 i = 0; i < count && r.ok; ++i)
                {
                    auto status = r.u8();
                    if (status < 0x80)
                    {
                        report.note ("Part of a MIDI part (\"" + part.name + "\") couldn't be read");
                        break;
                    }

                    auto pos = r.f64();
                    r.u8();
                    auto d1 = r.u8();
                    auto d2 = r.u8();
                    r.skip (5);
                    r.skip ((size_t) r.u8() * 14);

                    if (status == 0xf0)
                    {
                        r.skip (r.u32());
                        continue;
                    }

                    switch (status & 0xf0)
                    {
                        case 0x90:
                        {
                            auto length = r.f64();
                            r.skip (17);
                            if (r.ok && std::isfinite (pos) && std::isfinite (length))
                                part.notes.push_back ({ pos, length, d1 & 0x7f, d2 & 0x7f });
                            break;
                        }

                        case 0xb0:  part.controllers.push_back ({ pos, d1 & 0x7f, d2 & 0x7f });                         break;
                        case 0xe0:  part.controllers.push_back ({ pos, pitchBend, (d1 & 0x7f) | ((d2 & 0x7f) << 7) });   break;
                        case 0xd0:  part.controllers.push_back ({ pos, channelPressure, d1 & 0x7f });                   break;
                        case 0xc0:  part.controllers.push_back ({ pos, programChange, d1 & 0x7f });                     break;
                        default:    break;
                    }
                }
            }

            //==============================================================================
            void readMarkers (const Node& track)
            {
                for (auto& m : track.children)
                {
                    if (m->cls != "MMarkerEvent" && m->cls != "MRangeMarkerEvent" && m->cls != "MRegionMarker")
                        continue;

                    auto r = archive.readerFor (*m);
                    auto name = r.string();
                    r.u32();
                    r.u16();
                    auto ticks = r.f64();

                    if (r.ok && std::isfinite (ticks) && ticks >= 0.0)
                        project.markers.push_back ({ ticks, name });
                }
            }
        };

        //==============================================================================
        // DAWproject output

        juce::String num (double v)
        {
            auto s = juce::String (v, 9);
            if (s.containsChar ('.'))
                s = s.trimCharactersAtEnd ("0").trimCharactersAtEnd (".");
            return s.isEmpty() || s == "-0" ? juce::String ("0") : s;
        }

        /** Effects that come with Cubase (its "Cubase Plug-in Set" bundle) and only run inside it. */
        bool isCubaseOnlyPlugin (const juce::String& name)
        {
            static const juce::StringArray names {
                "AutoPan", "Amp Simulator", "AmpSimulator", "BitCrusher", "Bitcrusher", "Brickwall Limiter", "Chopper", "Chorus", "Cloner",
                "Compressor", "CurveEQ", "DaTube", "DeEsser", "De-Esser", "Distortion", "DJ-EQ", "DualFilter", "EnvelopeShaper",
                "Envelope Shaper", "Expander", "Flanger", "Frequency", "Frequency 2", "Gate", "GEQ-10", "GEQ-30", "Grungelizer", "Imager",
                "Limiter", "LoopMash FX", "Magneto II", "Maximizer", "Metalizer", "MIDI Gate", "ModMachine", "MonoDelay", "MonoToStereo",
                "MorphFilter", "Multiband Compressor", "MultibandCompressor", "Multiband Envelope Shaper", "Multiband Expander",
                "MultiScope", "MultiTap Delay", "Octaver", "Phaser", "Pitch Correct", "PingPongDelay", "PostFilter", "Quadrafuzz v2",
                "REVerence", "RingModulator", "RoomWorks", "RoomWorks SE", "Rotary", "SoftClipper", "Squasher", "StepFilter",
                "StereoDelay", "StereoEnhancer", "StudioChorus", "StudioEQ", "Tranceformer", "Tremolo", "Tube Compressor",
                "Tuner", "UV22HR", "Vibrato", "VintageCompressor", "VST Amp Rack", "VST Bass Amp", "VSTDynamics", "WahWah" };

            return names.contains (name);
        }

        struct Translator
        {
            Translator (Project& p, CubaseProject::Translation& t, DawProject::Report& r, bool similar)
                : project (p), out (t), report (r), matchSimilarPlugins (similar)
            {
                tempo.points = p.tempo;
            }

            Project& project;
            CubaseProject::Translation& out;
            DawProject::Report& report;
            TempoMap tempo;

            int nextId = 0;
            juce::String newId (const char* prefix = "id")      { return prefix + juce::String (nextId++); }

            std::map<int, Track*> trackForBus;
            int masterBus = 0;
            juce::String masterChannelId;
            const bool matchSimilarPlugins;
            std::map<juce::String, juce::StringArray> cubaseOnlyPlugins;    // name -> tracks it was on

            /** Cubase's own plugins replaced by CDAW's: the tracks, what didn't come across exactly. */
            struct Replaced { juce::String by; juce::StringArray tracks, notes; };
            std::map<juce::String, Replaced> sameSound, similarSound;
            int hiddenEvents = 0, tracksWithHiddenEvents = 0;
            juce::StringArray frozenTracks;

            //==============================================================================
            void run()
            {
                std::function<void (const Track&)> countFeeders = [&] (const Track& t)
                {
                    if (t.channel && t.channel->outputBus != 0)
                        ++feedersOfBus[t.channel->outputBus];
                    for (auto& c : t.children)
                        countFeeders (c);
                };

                for (auto& t : project.tracks)
                    countFeeders (t);

                for (auto& t : project.tracks)
                    mergeGroups (t);

                assignIds (project.tracks);

                if (project.master)
                    masterBus = project.master->ownBus;

                auto xml = std::make_unique<juce::XmlElement> ("Project");
                xml->setAttribute ("version", "1.0");

                auto* app = xml->createNewChildElement ("Application");
                app->setAttribute ("name", "Cubase");
                app->setAttribute ("version", project.appVersion);

                writeTransport (*xml);

                auto* structure = xml->createNewChildElement ("Structure");
                writeMaster (*structure);

                for (auto& t : project.tracks)
                    writeTrack (*structure, t, {});

                auto* arrangement = xml->createNewChildElement ("Arrangement");
                arrangement->setAttribute ("id", newId());
                auto* lanes = arrangement->createNewChildElement ("Lanes");
                lanes->setAttribute ("timeUnit", "beats");
                lanes->setAttribute ("id", newId());

                std::function<void (Track&)> writeContent = [&] (Track& t)
                {
                    writeLanes (*lanes, t);
                    for (auto& c : t.children)
                        writeContent (c);
                };

                for (auto& t : project.tracks)
                    writeContent (t);

                if (project.master)
                    writeLanes (*lanes, project.masterAutomation);

                writeMarkers (*arrangement);
                writeTempoChanges (*arrangement);

                if (! frozenTracks.isEmpty())
                    report.note ("Frozen in Cubase, now playing live with their plugins: " + frozenTracks.joinIntoString (", "));

                if (hiddenEvents > 0)
                    report.note (juce::String (hiddenEvents) + " audio event(s) on " + juce::String (tracksWithHiddenEvents)
                                 + " track(s) lay completely under other events (e.g. older takes), so they don't play, and were left out");

                reportReplaced (sameSound, false);
                reportReplaced (similarSound, true);

                for (auto& [name, tracks] : cubaseOnlyPlugins)
                {
                    auto counterpart = CubaseStockPlugins::counterpartOf (name);
                    report.note ("Cubase's own " + name + " only runs inside Cubase and was left out (" + onTracks (tracks) + ")"
                                 + (counterpart && counterpart->similar && ! matchSimilarPlugins
                                        ? ". CDAW's " + counterpart->plugin + " sounds similar: import again and let CDAW match the plugins" : ""));
                }

                out.project = std::move (xml);
            }

            static juce::String onTracks (const juce::StringArray& tracks)
            {
                return tracks.size() == 1 ? "on " + tracks[0] : "on " + juce::String (tracks.size()) + " tracks";
            }

            void reportReplaced (const std::map<juce::String, Replaced>& replaced, bool similar)
            {
                for (auto& [name, r] : replaced)
                {
                    auto details = r.notes.joinIntoString ("; ");
                    report.note ("Cubase's own " + name + " replaced by CDAW's " + r.by
                                 + (similar ? ", which sounds similar - worth a listen (" : " (") + onTracks (r.tracks) + ")"
                                 + (details.isEmpty() ? juce::String() : ". " + details.substring (0, 1).toUpperCase() + details.substring (1)));
                }
            }

            //==============================================================================
            std::map<int, int> feedersOfBus;

            /** A folder holding one group channel, fed only by tracks inside the folder, becomes a CDAW
                group: the folder takes over the group's channel. Other groups become bus tracks. */
            void mergeGroups (Track& t)
            {
                for (auto& c : t.children)
                    mergeGroups (c);

                if (t.kind != Kind::folder || t.channel)
                    return;

                int groupIndex = -1, groups = 0;
                for (int i = 0; i < (int) t.children.size(); ++i)
                {
                    if (t.children[(size_t) i].kind == Kind::group)
                    {
                        groupIndex = i;
                        ++groups;
                    }
                }

                if (groups != 1)
                    return;

                // Every track inside must end up in the group (directly, or through groups inside the
                // folder), and nothing outside may feed it: then CDAW's folder group sounds the same.
                auto bus = t.children[(size_t) groupIndex].channel->ownBus;
                std::set<int> busesInside { bus };

                std::function<void (const Track&)> collectBuses = [&] (const Track& x)
                {
                    if (x.channel && x.channel->ownBus != 0 && (x.kind == Kind::group || x.kind == Kind::folder))
                        busesInside.insert (x.channel->ownBus);
                    for (auto& c : x.children)
                        collectBuses (c);
                };

                int feeders = 0;
                bool allInside = true;

                std::function<void (const Track&)> check = [&] (const Track& x)
                {
                    if (x.channel)
                    {
                        if (x.channel->outputBus == bus)
                            ++feeders;
                        if (busesInside.count (x.channel->outputBus) == 0)
                            allInside = false;
                    }
                    for (auto& c : x.children)
                        check (c);
                };

                for (int i = 0; i < (int) t.children.size(); ++i)
                    if (i != groupIndex)
                        collectBuses (t.children[(size_t) i]);

                for (int i = 0; i < (int) t.children.size(); ++i)
                    if (i != groupIndex)
                        check (t.children[(size_t) i]);

                if (feeders == 0 || ! allInside || feeders != feedersOfBus[bus])
                    return;

                auto& group = t.children[(size_t) groupIndex];
                t.channel = std::move (group.channel);
                t.volumeAutomation = std::move (group.volumeAutomation);
                t.panAutomation = std::move (group.panAutomation);
                t.children.erase (t.children.begin() + groupIndex);
            }

            void assignIds (std::vector<Track>& tracks)
            {
                for (auto& t : tracks)
                {
                    t.trackId = newId ("t");
                    if (t.channel)
                    {
                        t.channelId = newId ("c");
                        if (t.channel->ownBus != 0 && (t.kind == Kind::group || t.kind == Kind::fx || t.kind == Kind::folder))
                            trackForBus[t.channel->ownBus] = &t;
                    }
                    assignIds (t.children);
                }
            }

            //==============================================================================
            void writeTransport (juce::XmlElement& xml)
            {
                auto* transport = xml.createNewChildElement ("Transport");
                auto* t = transport->createNewChildElement ("Tempo");
                t->setAttribute ("unit", "bpm");
                t->setAttribute ("value", num (project.tempo.empty() ? 120.0 : project.tempo.front().bpm));
                t->setAttribute ("id", "tempo");

                auto* s = transport->createNewChildElement ("TimeSignature");
                s->setAttribute ("numerator", project.signatures.empty() ? 4 : project.signatures.front().numerator);
                s->setAttribute ("denominator", project.signatures.empty() ? 4 : project.signatures.front().denominator);
                s->setAttribute ("id", "timesig");
            }

            void writeTempoChanges (juce::XmlElement& arrangement)
            {
                if (project.tempo.size() > 1)
                {
                    auto* ta = arrangement.createNewChildElement ("TempoAutomation");
                    ta->setAttribute ("timeUnit", "beats");
                    ta->setAttribute ("unit", "bpm");
                    ta->setAttribute ("id", newId());
                    ta->createNewChildElement ("Target")->setAttribute ("parameter", "tempo");

                    for (auto& p : project.tempo)
                    {
                        auto* pt = ta->createNewChildElement ("RealPoint");
                        pt->setAttribute ("time", num (p.ticks / ticksPerBeat));
                        pt->setAttribute ("value", num (p.bpm));
                        pt->setAttribute ("interpolation", p.ramp ? "linear" : "hold");
                    }
                }

                if (project.signatures.size() > 1)
                {
                    auto* sa = arrangement.createNewChildElement ("TimeSignatureAutomation");
                    sa->setAttribute ("timeUnit", "beats");
                    sa->setAttribute ("id", newId());
                    sa->createNewChildElement ("Target")->setAttribute ("parameter", "timesig");

                    for (auto& s : project.signatures)
                    {
                        auto* pt = sa->createNewChildElement ("TimeSignaturePoint");
                        pt->setAttribute ("time", num (s.ticks / ticksPerBeat));
                        pt->setAttribute ("numerator", s.numerator);
                        pt->setAttribute ("denominator", s.denominator);
                    }
                }
            }

            void writeMarkers (juce::XmlElement& arrangement)
            {
                if (project.markers.empty())
                    return;

                auto* markers = arrangement.createNewChildElement ("Markers");
                markers->setAttribute ("timeUnit", "beats");
                markers->setAttribute ("id", newId());

                auto sorted = project.markers;
                std::stable_sort (sorted.begin(), sorted.end(), [] (auto& a, auto& b) { return a.ticks < b.ticks; });

                for (auto& m : sorted)
                {
                    auto* x = markers->createNewChildElement ("Marker");
                    x->setAttribute ("time", num (m.ticks / ticksPerBeat));
                    x->setAttribute ("name", m.name);
                }
            }

            //==============================================================================
            void writeMaster (juce::XmlElement& structure)
            {
                if (! project.master)
                    return;

                auto& m = project.masterAutomation;
                m.trackId = newId ("t");
                masterChannelId = newId ("c");

                auto* track = structure.createNewChildElement ("Track");
                track->setAttribute ("contentType", "audio");
                track->setAttribute ("name", "Stereo Out");
                track->setAttribute ("id", m.trackId);
                writeChannel (*track, *project.master, "master", masterChannelId, "Stereo Out", false, m);
            }

            /** One of Cubase's own plugins as the CDAW plugin standing in for it, if there is one
                (and it sounds the same, or similar ones were asked for). */
            bool writeStockPlugin (juce::XmlElement& devices, const Plugin& p, const juce::String& trackName)
            {
                auto counterpart = CubaseStockPlugins::counterpartOf (p.name);
                if (! counterpart || (counterpart->similar && ! matchSimilarPlugins))
                    return false;

                auto device = CubaseStockPlugins::translate (p.name, CubaseStockPlugins::readParameters (p.component), p.enabled);
                if (device.xml == nullptr)
                    return false;

                device.xml->setAttribute ("id", newId());
                devices.addChildElement (device.xml.release());

                auto& replaced = (counterpart->similar ? similarSound : sameSound)[p.name];
                replaced.by = counterpart->plugin;
                replaced.tracks.addIfNotAlreadyThere (trackName);
                for (auto& n : device.notes)
                    replaced.notes.addIfNotAlreadyThere (n);
                return true;
            }

            void writePlugin (juce::XmlElement& devices, const Plugin& p, bool instrument, const juce::String& trackName)
            {
                if (isCubaseOnlyPlugin (p.name))
                {
                    if (! writeStockPlugin (devices, p, trackName))
                        cubaseOnlyPlugins[p.name].addIfNotAlreadyThere (trackName);
                    return;
                }

                if (p.classId.length() != 32)
                {
                    report.note ("Plugin \"" + p.name + "\" on " + trackName + " has no VST3 ID and was left out");
                    return;
                }

                auto* d = devices.createNewChildElement ("Vst3Plugin");
                d->setAttribute ("deviceID", p.classId);
                d->setAttribute ("deviceName", p.name);
                d->setAttribute ("deviceRole", instrument ? "instrument" : "audioFX");
                d->setAttribute ("loaded", "true");
                d->setAttribute ("id", newId());
                d->setAttribute ("name", p.name);

                auto* en = d->createNewChildElement ("Enabled");
                en->setAttribute ("value", p.enabled ? "true" : "false");
                en->setAttribute ("id", newId());

                if (p.component.getSize() > 0 || p.controller.getSize() > 0)
                {
                    auto path = "plugins/" + newId ("state") + ".vstpreset";
                    out.files.push_back ({ path, DawProject::makeVst3Preset (p.classId, p.component, p.controller) });
                    d->createNewChildElement ("State")->setAttribute ("path", path);
                }
            }

            void writeChannel (juce::XmlElement& track, const Channel& ch, const juce::String& role, const juce::String& channelId,
                               const juce::String& trackName, bool instrumentTrack, Track& owner)
            {
                auto* c = track.createNewChildElement ("Channel");
                c->setAttribute ("role", role);
                c->setAttribute ("audioChannels", 2);
                if (ch.solo)
                    c->setAttribute ("solo", "true");
                c->setAttribute ("id", channelId);

                if ((ch.instrument && ch.instrument->frozen)
                     || std::any_of (ch.inserts.begin(), ch.inserts.end(), [] (auto& p) { return p.frozen; }))
                    frozenTracks.addIfNotAlreadyThere (trackName);

                auto* devices = c->createNewChildElement ("Devices");
                if (instrumentTrack && ch.instrument)
                    writePlugin (*devices, *ch.instrument, true, trackName);
                for (auto& p : ch.inserts)
                    writePlugin (*devices, p, false, trackName);

                auto* mute = c->createNewChildElement ("Mute");
                mute->setAttribute ("value", ch.mute ? "true" : "false");
                mute->setAttribute ("id", newId());

                owner.panId = newId();
                auto* pan = c->createNewChildElement ("Pan");
                pan->setAttribute ("max", "1");
                pan->setAttribute ("min", "0");
                pan->setAttribute ("unit", "normalized");
                pan->setAttribute ("value", num (ch.pan));
                pan->setAttribute ("id", owner.panId);

                owner.volumeId = newId();
                auto* vol = c->createNewChildElement ("Volume");
                vol->setAttribute ("unit", "decibel");
                vol->setAttribute ("value", num (ch.volumeDb));
                vol->setAttribute ("id", owner.volumeId);
            }

            static bool isAncestor (const std::vector<const Track*>& ancestors, const Track* t)
            {
                return std::find (ancestors.begin(), ancestors.end(), t) != ancestors.end();
            }

            void writeRouting (juce::XmlElement& channel, const Track& t, const std::vector<const Track*>& ancestors)
            {
                auto& ch = *t.channel;
                const auto bus = ch.outputBus;

                // The nearest enclosing group: where CDAW sends a track's output by default.
                const Track* enclosingGroup = nullptr;
                for (auto it = ancestors.rbegin(); it != ancestors.rend(); ++it)
                {
                    if ((*it)->channel)
                    {
                        enclosingGroup = *it;
                        break;
                    }
                }

                if (bus == 0 || bus == masterBus)
                {
                    if (enclosingGroup != nullptr)
                        report.note ("\"" + t.name + "\" went straight to Stereo Out in Cubase; in CDAW it plays through its folder's group \""
                                     + enclosingGroup->name + "\"");
                    channel.setAttribute ("destination", masterChannelId);
                }
                else if (auto it = trackForBus.find (bus); it != trackForBus.end())
                {
                    auto* dest = it->second;

                    if (dest == enclosingGroup)
                    {
                        // The default in CDAW: nothing to set.
                    }
                    else if (isAncestor (ancestors, dest))
                    {
                        report.note ("\"" + t.name + "\" skipped a group on its way to \"" + dest->name + "\"; in CDAW it plays through \""
                                     + enclosingGroup->name + "\"");
                    }
                    else
                    {
                        channel.setAttribute ("destination", dest->channelId);
                    }
                }
                else if (auto other = project.otherOutputs.find (bus); other != project.otherOutputs.end())
                {
                    report.note ("\"" + t.name + "\" was routed to the Cubase output \"" + other->second + "\"; it now goes to the master");
                }
                else
                {
                    report.note ("\"" + t.name + "\" was routed somewhere CDAW can't follow; it now goes to the master");
                }

                if (ch.sends.empty())
                    return;

                auto* sends = channel.createNewChildElement ("Sends");

                for (auto& s : ch.sends)
                {
                    auto it = trackForBus.find (s.bus);
                    if (it == trackForBus.end() || it->second->kind != Kind::fx)
                    {
                        if (s.on)
                            report.note ("A send on \"" + t.name + "\" goes somewhere other than an FX channel and was left out");
                        continue;
                    }

                    auto* x = sends->createNewChildElement ("Send");
                    x->setAttribute ("destination", it->second->channelId);
                    x->setAttribute ("type", s.pre ? "pre" : "post");
                    x->setAttribute ("id", newId());

                    auto* v = x->createNewChildElement ("Volume");
                    v->setAttribute ("unit", "decibel");
                    v->setAttribute ("value", num (s.db));
                    v->setAttribute ("id", newId());

                    auto* en = x->createNewChildElement ("Enable");
                    en->setAttribute ("value", s.on ? "true" : "false");
                    en->setAttribute ("id", newId());
                }
            }

            void writeTrack (juce::XmlElement& parent, Track& t, std::vector<const Track*> ancestors)
            {
                auto* x = parent.createNewChildElement ("Track");
                juce::String content, role = "regular";

                switch (t.kind)
                {
                    case Kind::audio:       content = "audio"; break;
                    case Kind::instrument:
                    case Kind::midi:        content = "notes"; break;
                    case Kind::folder:      content = "tracks"; role = "submix"; break;
                    case Kind::group:       content = "audio"; role = "submix"; break;
                    case Kind::fx:          content = "audio"; role = "effect"; break;
                }

                x->setAttribute ("contentType", content);
                x->setAttribute ("loaded", "true");
                x->setAttribute ("id", t.trackId);
                x->setAttribute ("name", t.name);

                if (t.channel)
                {
                    writeChannel (*x, *t.channel, role, t.channelId, "\"" + t.name + "\"", t.kind == Kind::instrument || t.kind == Kind::midi, t);

                    if (auto* channelXml = x->getChildByName ("Channel"))
                        writeRouting (*channelXml, t, ancestors);

                    if (t.kind == Kind::instrument && ! t.channel->instrument)
                        report.note ("Instrument track \"" + t.name + "\" had no instrument plugin");
                }

                ancestors.push_back (&t);
                for (auto& c : t.children)
                    writeTrack (*x, c, ancestors);
            }

            //==============================================================================
            void writeLanes (juce::XmlElement& lanes, Track& t)
            {
                const bool hasClips = (! t.audio.empty() || ! t.parts.empty())
                                      && t.kind != Kind::folder && t.kind != Kind::group && t.kind != Kind::fx;
                const bool hasAutomation = ! t.volumeAutomation.empty() || ! t.panAutomation.empty();

                if (! hasClips && ! hasAutomation)
                    return;

                auto* trackLanes = lanes.createNewChildElement ("Lanes");
                trackLanes->setAttribute ("track", t.trackId);
                trackLanes->setAttribute ("id", newId());

                if (hasClips && ! t.audio.empty())
                    writeAudio (*trackLanes, t);

                if (hasClips && ! t.parts.empty())
                    writeParts (*trackLanes, t);

                writeAutomation (*trackLanes, t.volumeAutomation, t.volumeId, true);
                writeAutomation (*trackLanes, t.panAutomation, t.panId, false);
            }

            void writeAutomation (juce::XmlElement& trackLanes, const std::vector<AutomationPoint>& points, const juce::String& paramId, bool volume)
            {
                if (points.empty() || paramId.isEmpty())
                    return;

                auto* px = trackLanes.createNewChildElement ("Points");
                px->setAttribute ("id", newId());
                px->setAttribute ("unit", volume ? "decibel" : "normalized");
                px->createNewChildElement ("Target")->setAttribute ("parameter", paramId);

                auto sorted = points;
                std::stable_sort (sorted.begin(), sorted.end(), [] (auto& a, auto& b) { return a.ticks < b.ticks; });

                for (auto& p : sorted)
                {
                    auto* x = px->createNewChildElement ("RealPoint");
                    x->setAttribute ("time", num (p.ticks / ticksPerBeat));
                    x->setAttribute ("value", num (volume ? faderToDb (p.value * 32767.0) : p.value));
                    x->setAttribute ("interpolation", "linear");
                }
            }

            /** Where events overlap, Cubase plays the one on the lowest lane (the newest take when
                recording stacked takes); on the same lane, the later one. Each event becomes the clips
                for the stretches no event in front of it covers. */
            void writeAudio (juce::XmlElement& trackLanes, const Track& t)
            {
                auto* clips = trackLanes.createNewChildElement ("Clips");
                clips->setAttribute ("timeUnit", "seconds");
                clips->setAttribute ("id", newId());

                struct Span { double start, end; };
                std::vector<Span> covered;      // sorted, non-overlapping
                std::vector<std::pair<const AudioEvent*, Span>> pieces;
                int hidden = 0;

                // Front to back.
                std::vector<const AudioEvent*> order;
                for (auto& ev : t.audio)
                    order.push_back (&ev);
                std::stable_sort (order.begin(), order.end(), [] (auto* a, auto* b) { return a->lane < b->lane; });

                for (auto it = order.rbegin(); it != order.rend(); ++it)
                {
                    auto& ev = **it;
                    auto start = tempo.secondsAt (ev.start);
                    auto end = start + ev.length / ev.sampleRate;

                    if (ev.window)
                    {
                        start = juce::jmax (start, tempo.secondsAt (ev.window->first));
                        end = juce::jmin (end, tempo.secondsAt (ev.window->second));
                        if (end <= start)
                            continue;
                    }

                    // Visible stretches of [start, end) not covered by later events.
                    std::vector<Span> visible;
                    auto from = start;
                    for (auto& c : covered)
                    {
                        if (c.end <= from)      continue;
                        if (c.start >= end)     break;
                        if (c.start > from)     visible.push_back ({ from, juce::jmin (c.start, end) });
                        from = juce::jmax (from, c.end);
                        if (from >= end)        break;
                    }
                    if (from < end)
                        visible.push_back ({ from, end });

                    constexpr double minLength = 0.001;
                    bool any = false;
                    for (auto& v : visible)
                    {
                        if (v.end - v.start >= minLength)
                        {
                            pieces.push_back ({ &ev, v });
                            any = true;
                        }
                    }
                    if (! any)
                        ++hidden;

                    // Add [start, end) to the covered set; a muted event hides nothing.
                    if (ev.muted)
                        continue;

                    covered.push_back ({ start, end });
                    std::sort (covered.begin(), covered.end(), [] (auto& a, auto& b) { return a.start < b.start; });
                    std::vector<Span> merged;
                    for (auto& c : covered)
                    {
                        if (! merged.empty() && c.start <= merged.back().end)
                            merged.back().end = juce::jmax (merged.back().end, c.end);
                        else
                            merged.push_back (c);
                    }
                    covered = std::move (merged);
                }

                if (hidden > 0)
                {
                    hiddenEvents += hidden;
                    ++tracksWithHiddenEvents;
                }

                std::sort (pieces.begin(), pieces.end(), [] (auto& a, auto& b) { return a.second.start < b.second.start; });

                for (auto& [ev, span] : pieces)
                {
                    if (ev->file == juce::File())
                        continue;

                    const auto eventStart = tempo.secondsAt (ev->start);
                    auto eventEnd = eventStart + ev->length / ev->sampleRate;
                    if (ev->window)
                        eventEnd = juce::jmin (eventEnd, tempo.secondsAt (ev->window->second));

                    auto* clip = clips->createNewChildElement ("Clip");
                    clip->setAttribute ("time", num (span.start));
                    clip->setAttribute ("duration", num (span.end - span.start));
                    clip->setAttribute ("playStart", num (ev->offset / ev->sampleRate + (span.start - eventStart)));
                    clip->setAttribute ("contentTimeUnit", "seconds");
                    clip->setAttribute ("fadeTimeUnit", "seconds");

                    if (std::abs (span.start - eventStart) < 1.0e-6 && ev->fadeIn > 0.0)
                        clip->setAttribute ("fadeInTime", num (ev->fadeIn / ev->sampleRate));
                    if (std::abs (span.end - eventEnd) < 1.0e-6 && ev->fadeOut > 0.0)
                        clip->setAttribute ("fadeOutTime", num (ev->fadeOut / ev->sampleRate));
                    if (std::abs (ev->gain - 1.0f) > 1.0e-4f)
                        clip->setAttribute ("gain", num (ev->gain));
                    if (ev->muted)
                        clip->setAttribute ("enable", "false");

                    clip->setAttribute ("name", ev->name);

                    auto* audio = clip->createNewChildElement ("Audio");
                    audio->setAttribute ("channels", ev->channels);
                    audio->setAttribute ("sampleRate", (int) ev->sampleRate);
                    audio->setAttribute ("id", newId());

                    auto* file = audio->createNewChildElement ("File");
                    file->setAttribute ("external", "true");
                    file->setAttribute ("path", ev->file.getFullPathName());
                }
            }

            void writeParts (juce::XmlElement& trackLanes, const Track& t)
            {
                auto* clips = trackLanes.createNewChildElement ("Clips");
                clips->setAttribute ("timeUnit", "beats");
                clips->setAttribute ("id", newId());

                for (auto& part : t.parts)
                {
                    if (part.length <= 0.0)
                        continue;

                    auto* clip = clips->createNewChildElement ("Clip");
                    clip->setAttribute ("time", num (part.start / ticksPerBeat));
                    clip->setAttribute ("duration", num (part.length / ticksPerBeat));
                    clip->setAttribute ("playStart", num (part.offset / ticksPerBeat));
                    clip->setAttribute ("contentTimeUnit", "beats");
                    clip->setAttribute ("name", part.name.isNotEmpty() ? part.name : t.name);
                    if (part.muted)
                        clip->setAttribute ("enable", "false");

                    auto* notes = clip->createNewChildElement ("Notes");
                    notes->setAttribute ("id", newId());

                    for (auto& n : part.notes)
                    {
                        auto* x = notes->createNewChildElement ("Note");
                        x->setAttribute ("time", num (n.pos / ticksPerBeat));
                        x->setAttribute ("duration", num (juce::jmax (1.0, n.length) / ticksPerBeat));
                        x->setAttribute ("channel", 0);
                        x->setAttribute ("key", n.key);
                        x->setAttribute ("vel", num (n.velocity / 127.0));
                    }

                    writeControllers (*clip, part);
                }
            }

            /** Controllers as DAWproject expression points, one <Points> per controller. */
            void writeControllers (juce::XmlElement& clip, const MidiPart& part)
            {
                std::map<int, std::vector<const Controller*>> byType;
                for (auto& c : part.controllers)
                    byType[c.type].push_back (&c);

                for (auto& [type, events] : byType)
                {
                    auto* points = clip.createNewChildElement ("Points");
                    points->setAttribute ("id", newId());
                    auto* target = points->createNewChildElement ("Target");

                    if (type == pitchBend)              target->setAttribute ("expression", "pitchBend");
                    else if (type == channelPressure)   target->setAttribute ("expression", "channelPressure");
                    else if (type == programChange)     target->setAttribute ("expression", "programChange");
                    else
                    {
                        target->setAttribute ("expression", "channelController");
                        target->setAttribute ("controller", type);
                    }

                    target->setAttribute ("channel", 0);

                    for (auto* e : events)
                    {
                        auto* p = points->createNewChildElement ("RealPoint");
                        p->setAttribute ("time", num (e->pos / ticksPerBeat));
                        p->setAttribute ("value", num (type == pitchBend ? e->value / 16383.0 : e->value / 127.0));
                        p->setAttribute ("interpolation", "hold");
                    }
                }
            }
        };
    }

    //==============================================================================
    juce::Result translate (const juce::File& source, Translation& translation, DawProject::Report& report, bool matchSimilarPlugins)
    {
        Archive archive;
        auto loaded = archive.load (source);
        if (loaded.failed())
            return juce::Result::fail (source.getFileName() + ": " + loaded.getErrorMessage());

        Decoder decoder (archive, source, report);
        auto decoded = decoder.run();
        if (decoded.failed())
            return juce::Result::fail (source.getFileName() + ": " + decoded.getErrorMessage());

        Translator translator (decoder.project, translation, report, matchSimilarPlugins);
        translator.run();
        return juce::Result::ok();
    }

    juce::Result importInto (te::Edit& edit, const juce::File& source, const juce::File& mediaFolder, DawProject::Report& report,
                             const ImportOptions& importOptions)
    {
        const auto& progress = importOptions.progress;

        // Reading the .cpr is the first few percent; the rest is building the project.
        const double readShare = 0.05;
        if (progress)
            progress (0.0, "Reading " + source.getFileName());

        Translation translation;
        auto result = translate (source, translation, report, importOptions.matchSimilarPlugins);
        if (result.failed())
            return result;

        // Hand it to the DAWproject importer as an in-memory zip.
        juce::ZipFile::Builder builder;
        auto text = translation.project->toString();
        builder.addEntry (std::make_unique<juce::MemoryInputStream> (text.toRawUTF8(), text.getNumBytesAsUTF8(), true), 0, "project.xml", juce::Time::getCurrentTime());

        for (auto& [path, data] : translation.files)
            builder.addEntry (std::make_unique<juce::MemoryInputStream> (data, true), 0, path, juce::Time::getCurrentTime());

        juce::MemoryOutputStream zipData;
        if (! builder.writeToStream (zipData, nullptr))
            return juce::Result::fail ("Couldn't prepare the import");

        juce::ZipFile zip (new juce::MemoryInputStream (zipData.getData(), zipData.getDataSize(), false), true);

        DawProject::ImportOptions options;
        options.copyExternalMedia = importOptions.copyAudio;

        if (progress)
            options.progress = [progress, readShare] (double p, const juce::String& message) { progress (readShare + (1.0 - readShare) * p, message); };
        return DawProject::importFromZip (edit, zip, source, mediaFolder, report, options);
    }

    juce::String describe (const juce::File& source, int maxDepth)
    {
        Archive archive;
        auto loaded = archive.load (source);
        if (loaded.failed())
            return loaded.getErrorMessage();

        juce::String s;
        for (auto& name : archive.rootNames())
        {
            s << "== " << name << "\n";
            if (auto* r = archive.root (name))
                s << archive.dump (*r, maxDepth);
        }
        return s;
    }
}
