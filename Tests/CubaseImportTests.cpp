#include "TestEngine.h"
#include "Model/CubaseArchive.h"
#include "Model/CubaseProject.h"
#include "Model/CubaseStockPlugins.h"
#include "Plugins/CompressorPlugin.h"
#include "Plugins/EqPlugin.h"
#include "Plugins/EsserPlugin.h"

namespace cdaw::test
{
    /** Builds big-endian Cubase archive bytes. */
    struct ArchiveWriter
    {
        juce::MemoryOutputStream out;

        size_t pos()                        { return (size_t) out.getPosition(); }
        void u16 (int v)                    { out.writeShortBigEndian ((short) v); }
        void u32 (juce::uint32 v)           { out.writeIntBigEndian ((int) v); }
        void i64 (juce::int64 v)            { out.writeInt64BigEndian (v); }
        void f64 (double v)                 { out.writeDoubleBigEndian (v); }

        void name (const char* n)
        {
            u32 ((juce::uint32) std::strlen (n) + 1);
            out.write (n, std::strlen (n) + 1);
        }

        void utf8 (const juce::String& s)
        {
            u32 ((juce::uint32) s.getNumBytesAsUTF8() + 4);
            out.write (s.toRawUTF8(), s.getNumBytesAsUTF8());
            const juce::uint8 tail[] = { 0, 0xef, 0xbb, 0xbf };
            out.write (tail, 4);
        }

        void key (const char* k, int type)  { name (k); u16 (type); }
    };

    struct CubaseImportTests : public juce::UnitTest
    {
        CubaseImportTests() : juce::UnitTest ("Cubase project import", "CDAW") {}

        /** RIFF/NUND file with one "Arrangement1" chunk holding `payload`. */
        static juce::MemoryBlock fileWith (const juce::MemoryBlock& payload)
        {
            ArchiveWriter w;
            w.out.write ("RIFF", 4);
            w.u32 (0);
            w.out.write ("NUND", 4);
            w.out.write ("ROOT", 4);
            w.u32 (34);
            w.name ("Arrangement1");
            w.name ("PArrangement");
            w.out.write ("ARCH", 4);
            w.u32 ((juce::uint32) payload.getSize());
            w.out.write (payload.getData(), payload.getSize());
            return w.out.getMemoryBlock();
        }

        /** A stock Cubase plugin's state: a u32, then name[128], u32 id, f64 per parameter (little-endian). */
        static juce::MemoryBlock stockState (std::initializer_list<std::pair<const char*, double>> params)
        {
            juce::MemoryOutputStream out;
            out.writeInt (0);
            int id = 0;
            for (auto& [name, value] : params)
            {
                char record[128] {};
                std::strncpy (record, name, 127);
                out.write (record, 128);
                out.writeInt (id++);
                out.writeDouble (value);
            }
            return out.getMemoryBlock();
        }

        /** The devices on a track of a one-track DAWproject, imported into a new edit. */
        std::unique_ptr<te::Edit> importDevices (std::vector<CubaseStockPlugins::Device>& devices, DawProject::Report& report)
        {
            juce::XmlElement project ("Project");
            project.setAttribute ("version", "1.0");
            auto* structure = project.createNewChildElement ("Structure");
            auto* track = structure->createNewChildElement ("Track");
            track->setAttribute ("contentType", "audio");
            track->setAttribute ("name", "Vox");
            track->setAttribute ("id", "t0");
            auto* channel = track->createNewChildElement ("Channel");
            channel->setAttribute ("role", "regular");
            channel->setAttribute ("id", "c0");
            auto* list = channel->createNewChildElement ("Devices");

            int id = 0;
            for (auto& d : devices)
                if (d.xml != nullptr)
                {
                    d.xml->setAttribute ("id", "d" + juce::String (id++));
                    list->addChildElement (d.xml.release());
                }

            juce::ZipFile::Builder builder;
            auto text = project.toString();
            builder.addEntry (std::make_unique<juce::MemoryInputStream> (text.toRawUTF8(), text.getNumBytesAsUTF8(), true), 0, "project.xml", juce::Time::getCurrentTime());
            juce::MemoryOutputStream zipData;
            builder.writeToStream (zipData, nullptr);
            juce::ZipFile zip (new juce::MemoryInputStream (zipData.getData(), zipData.getDataSize(), false), true);

            auto edit = te::test_utilities::createTestEdit (getEngine(), 1);
            auto result = DawProject::importFromZip (*edit, zip, getTestDir().getChildFile ("stock.cpr"), getTestDir().getChildFile ("stock-media"), report, {});
            expect (result.wasOk(), result.getErrorMessage());
            return edit;
        }

        void runTest() override
        {
            beginTest ("The archive reader finds objects, class references and object references");
            {
                ArchiveWriter w;
                w.u32 (0xfffffffe); w.name ("CmObject"); w.u16 (0);       // base class
                w.u32 (0xffffffff); w.name ("PThing"); w.u16 (2);
                const auto thingLenPos = w.pos();
                w.u32 (0);                                                  // patched below
                const auto thingData = w.pos();
                w.u32 (7);

                const auto childTag = w.pos();
                w.u32 (0xffffffff); w.name ("PChild"); w.u16 (1);
                const auto child1LenPos = w.pos();
                w.u32 (8); w.f64 (1.5);

                w.u32 (0x80000000u | (juce::uint32) childTag);              // another PChild
                w.u32 (8); w.f64 (2.5);

                w.u32 ((juce::uint32) child1LenPos);                        // reference to the first child
                w.u32 (0);                                                  // null
                const auto thingEnd = w.pos();

                auto payload = w.out.getMemoryBlock();
                auto len = juce::ByteOrder::swapIfLittleEndian ((juce::uint32) (thingEnd - thingData));
                std::memcpy (static_cast<char*> (payload.getData()) + thingLenPos, &len, 4);

                Cubase::Archive archive;
                expect (archive.loadFromMemory (fileWith (payload)).wasOk());

                auto* root = archive.root ("Arrangement1");
                expect (root != nullptr && root->children.size() == 1);
                auto& thing = *root->children.front();
                expectEquals (thing.cls, juce::String ("PThing"));
                expectEquals (thing.version, 2);
                expectEquals ((int) thing.children.size(), 2);
                expectEquals (thing.children[1]->cls, juce::String ("PChild"));
                expectEquals (thing.children[1]->version, 1);

                auto r = archive.readerFor (thing);
                expectEquals ((int) r.u32(), 7);
                auto* a = archive.readSlot (r, thing);
                auto* b = archive.readSlot (r, thing);
                auto* c = archive.readSlot (r, thing);
                auto* d = archive.readSlot (r, thing);
                expect (a == thing.children[0].get() && b == thing.children[1].get());
                expect (c == a, "a reference resolves to the object it points at");
                expect (d == nullptr && r.ok);

                auto ra = archive.readerFor (*b);
                expectWithinAbsoluteError (ra.f64(), 2.5, 1.0e-12);
            }

            beginTest ("Attribute trees: values, nesting, arrays; unknown types end a tolerant read");
            {
                ArchiveWriter w;
                w.u32 (4);
                w.key ("Name", 2); w.u16 (6); w.u32 (1);
                    w.key ("String", 8); w.utf8 (juce::String::fromUTF8 ("Gitarre \xc3\xbc"));
                w.key ("Volume", 2); w.u16 (6); w.u32 (2);
                    w.key ("Value", 4); w.f64 (25856.0);
                    w.key ("AnchorValue", 4); w.f64 (-3.5);
                w.key ("Slot", 2); w.u16 (5); w.u32 (2);
                    w.u32 (1); w.key ("State", 1); w.i64 (1);
                    w.u32 (0);
                w.key ("Weird", 99);
                w.f64 (0.0);

                auto data = w.out.getMemoryBlock();
                Cubase::Reader r (static_cast<const juce::uint8*> (data.getData()), 0, data.getSize());
                auto attrs = Cubase::readAttributes (r, true);

                expect (! r.ok, "the unknown type is reported");
                expectEquals (attrs.getString ("Name/String"), juce::String::fromUTF8 ("Gitarre \xc3\xbc"));
                expectWithinAbsoluteError (attrs.getReal ("Volume/AnchorValue"), -3.5, 1.0e-12);
                expect (attrs.getArray ("Slot") != nullptr && attrs.getArray ("Slot")->size() == 2);
                expectEquals ((int) (*attrs.getArray ("Slot"))[0].getInt ("State"), 1);
                expect (attrs.get ("Weird") == nullptr);
            }

            beginTest ("Fader positions map to dB like Cubase's");
            {
                expectWithinAbsoluteError (CubaseProject::faderToDb (25856.0), 0.0, 1.0e-3);
                expectWithinAbsoluteError (CubaseProject::faderToDb (32767.0), 6.0206, 1.0e-3);
                expectWithinAbsoluteError (CubaseProject::faderToDb (14862.02), -10.0, 0.01);
                expectWithinAbsoluteError (CubaseProject::faderToDb (21626.23), -3.0368, 0.02);
                expectEquals (CubaseProject::faderToDb (-1.0), -100.0);
            }

            beginTest ("A file that isn't a Cubase project fails cleanly");
            {
                auto dir = getTestDir().getChildFile ("cubase");
                dir.createDirectory();

                auto notCubase = dir.getChildFile ("not.cpr");
                notCubase.replaceWithText ("hello");
                DawProject::Report report;
                CubaseProject::Translation translation;
                expect (CubaseProject::translate (notCubase, translation, report).failed());

                // A real header with a chunk claiming more bytes than there are.
                ArchiveWriter w;
                w.out.write ("RIFF", 4); w.u32 (0); w.out.write ("NUND", 4);
                w.out.write ("ARCH", 4); w.u32 (100000); w.u32 (0xffffffff);
                auto truncated = dir.getChildFile ("truncated.cpr");
                truncated.replaceWithData (w.out.getData(), w.out.getDataSize());
                expect (CubaseProject::translate (truncated, translation, report).failed());
            }

            beginTest ("Cubase's own effects become the CDAW plugins standing in for them, with their settings");
            {
                using namespace CubaseStockPlugins;

                expect (counterpartOf ("Frequency") && ! counterpartOf ("Frequency")->similar, "Frequency sounds the same as the EQ");
                expect (counterpartOf ("DeEsser") && counterpartOf ("DeEsser")->similar);
                expect (! counterpartOf ("Gate"), "no counterpart for the gate");

                // Frequency: the band types as Cubase shows them (band 1 code 2 = cut 24, band 6 code 2 = high shelf,
                // band 8 code 4 = cut 96, band 8 code 7 = high shelf, band 1 code 5 = low shelf).
                auto frequency = readParameters (stockState ({
                    { "equalizerAtype1", 2 }, { "equalizerAfreq1", 265 }, { "equalizerAgain1", -13 }, { "equalizerAq1", 1 }, { "equalizerAbandon1", 1 },
                    { "equalizerAtype2", 1 }, { "equalizerAfreq2", 180 }, { "equalizerAgain2", -4 }, { "equalizerAq2", 3.9 }, { "equalizerAbandon2", 1 },
                    { "equalizerAtype3", 3 }, { "equalizerAfreq3", 250 }, { "equalizerAgain3", 0 }, { "equalizerAq3", 8 }, { "equalizerAbandon3", 1 },
                    { "equalizerAtype4", 1 }, { "equalizerAfreq4", 500 }, { "equalizerAgain4", 0 }, { "equalizerAq4", 1 }, { "equalizerAbandon4", 0 },
                    { "equalizerAtype5", 1 }, { "equalizerAfreq5", 1000 }, { "equalizerAgain5", 2 }, { "equalizerAq5", 1 }, { "equalizerAbandon5", 1 },
                    { "equalizerAtype6", 2 }, { "equalizerAfreq6", 2000 }, { "equalizerAgain6", 6 }, { "equalizerAq6", 0.5 }, { "equalizerAbandon6", 1 },
                    { "equalizerAtype7", 0 }, { "equalizerAfreq7", 10000 }, { "equalizerAgain7", 1 }, { "equalizerAq7", 1 }, { "equalizerAbandon7", 1 },
                    { "equalizerAtype8", 4 }, { "equalizerAfreq8", 14000 }, { "equalizerAgain8", 0 }, { "equalizerAq8", 1 }, { "equalizerAbandon8", 1 },
                    { "equalizerAoutput", 2 }, { "bypass", 0 } }));
                expectEquals ((int) frequency.size(), 42, "the records read back");

                std::vector<Device> devices;
                devices.push_back (translate ("Frequency", frequency, true));
                expect (devices.back().notes.joinIntoString (" ").contains ("96 dB"), "the 96 dB/oct cut is mentioned");

                devices.push_back (translate ("DeEsser", readParameters (stockState ({
                    { "reduction", 4 }, { "threshold", -25 }, { "release", 150 }, { "autothreshold", 0 },
                    { "lowfreq", 3874 }, { "highfreq", 11376 }, { "listen", 0 }, { "diff", 0 }, { "bypass", 0 } })), true));

                devices.push_back (translate ("Limiter", readParameters (stockState ({ { "input", 3 }, { "release", 0.1 }, { "output", -1 }, { "bypass", 1 } })), true));
                devices.push_back (translate ("Compressor", readParameters (stockState ({
                    { "threshold", -16 }, { "ratio", 4 }, { "attack", 1 }, { "release", 150 }, { "makeUp", 3 }, { "softknee", 0 }, { "bypass", 0 } })), true));
                devices.push_back (translate ("DeEsser", {}, true));
                expect (devices.back().xml == nullptr, "a state that can't be read gives no device");

                DawProject::Report report;
                auto edit = importDevices (devices, report);
                auto tracks = te::getAudioTracks (*edit);
                expectEquals (tracks.size(), 1);
                if (tracks.isEmpty())
                    return;

                EqPlugin* eq = nullptr;
                EsserPlugin* esser = nullptr;
                std::vector<CompressorPlugin*> comps;
                for (auto* plugin : tracks[0]->pluginList)
                {
                    if (auto* e = dynamic_cast<EqPlugin*> (plugin))           eq = e;
                    if (auto* d = dynamic_cast<EsserPlugin*> (plugin))        esser = d;
                    if (auto* c = dynamic_cast<CompressorPlugin*> (plugin))   comps.push_back (c);
                }

                expect (eq != nullptr && esser != nullptr && comps.size() == 2, "EQ, C-Esser and two C-Comps on the track");
                if (eq == nullptr || esser == nullptr || comps.size() != 2)
                    return;

                using BT = EqMath::BandType;
                expect (eq->getType (0) == BT::lowCut && eq->getSlope (0) == 2, "band 1: cut 24 is a 24 dB/oct low cut");
                expectWithinAbsoluteError (eq->getBand (0).freq.getCurrentValue(), 265.0f, 0.5f);
                expect (eq->getType (1) == BT::peak);
                expectWithinAbsoluteError (eq->getBand (1).gain.getCurrentValue(), -4.0f, 0.01f);
                expectWithinAbsoluteError (eq->getBand (1).q.getCurrentValue(), 3.9f, 0.01f);
                expect (eq->getType (2) == BT::notch, "band 3: code 3 is a notch");
                expect (! eq->isOn (3), "band 4 off");
                expect (eq->getType (5) == BT::highShelf, "band 6: code 2 is a high shelf");
                expectWithinAbsoluteError (eq->getBand (5).gain.getCurrentValue(), 6.0f, 0.01f);
                expect (eq->getType (6) == BT::lowShelf, "band 7: code 0 is a low shelf");
                expect (eq->getType (7) == BT::highCut && eq->getSlope (7) == 3, "band 8: cut 96 becomes a 48 dB/oct high cut");
                expectWithinAbsoluteError (eq->output.getCurrentValue(), 2.0f, 0.01f);
                expect (eq->isEnabled());

                const auto es = esser->getSettings();
                expectWithinAbsoluteError (es.thresholdDb, -25.0f, 0.01f);
                expectWithinAbsoluteError (es.rangeDb, 9.6f, 0.01f, "reduction 4 of 10");
                expectWithinAbsoluteError (es.releaseMs, 150.0f, 0.01f);
                expectWithinAbsoluteError (es.lowHz, 3874.0f, 0.5f);
                expectWithinAbsoluteError (es.highHz, 11376.0f, 0.5f);
                expect (! es.autoThreshold && es.listen == EsserEngine::Listen::off);

                auto* limiter = comps[0];
                expectWithinAbsoluteError (limiter->getThresholdDb(), -4.0f, 0.01f, "a -1 dB ceiling after +3 dB input");
                expect (limiter->getRatio() > 100.0f, "a limiter");
                expectWithinAbsoluteError (limiter->outputDb.getCurrentValue(), 3.0f, 0.01f);
                expect (! limiter->isEnabled(), "bypassed in Cubase, off in CDAW");

                auto* comp = comps[1];
                expectWithinAbsoluteError (comp->getThresholdDb(), -16.0f, 0.01f);
                expectWithinAbsoluteError (comp->getRatio(), 4.0f, 0.01f);
                expectWithinAbsoluteError (comp->attackMs.getCurrentValue(), 1.0f, 0.01f);
                expectWithinAbsoluteError (comp->releaseMs.getCurrentValue(), 150.0f, 0.01f);
                expectWithinAbsoluteError (comp->outputDb.getCurrentValue(), 3.0f, 0.01f);
                expectWithinAbsoluteError (comp->getKneeDb(), 0.0f, 0.01f, "hard knee");
                expect (comp->isEnabled());
            }

            // CDAW_CUBASE_SIMILAR=1: the dump and the corpus also match plugins that only sound similar.
            const bool matchSimilar = juce::SystemStats::getEnvironmentVariable ("CDAW_CUBASE_SIMILAR", {}) == "1";

            // Opt-in: CDAW_CUBASE_DUMP=<file.cpr> writes the object tree and the translated project.xml
            // to CDAW_SNAPSHOT_DIR (or the test folder).
            if (auto dump = juce::SystemStats::getEnvironmentVariable ("CDAW_CUBASE_DUMP", {}); dump.isNotEmpty())
            {
                beginTest ("Dump " + dump);
                juce::File source (dump);
                auto outDir = juce::File (juce::SystemStats::getEnvironmentVariable ("CDAW_SNAPSHOT_DIR", getTestDir().getFullPathName()));
                outDir.createDirectory();

                outDir.getChildFile (source.getFileNameWithoutExtension() + ".tree.txt").replaceWithText (CubaseProject::describe (source));

                DawProject::Report report;
                CubaseProject::Translation translation;
                auto result = CubaseProject::translate (source, translation, report, matchSimilar);
                expect (result.wasOk(), result.getErrorMessage());

                if (translation.project != nullptr)
                    outDir.getChildFile (source.getFileNameWithoutExtension() + ".project.xml").replaceWithText (translation.project->toString());

                logMessage (report.notes.joinIntoString ("\n"));
            }

            // Opt-in: CDAW_CUBASE_CORPUS=<folder> imports every .cpr below it (read-only; the audio is
            // used in place). CDAW_CUBASE_STRIDE=n takes every n-th file.
            if (auto corpus = juce::SystemStats::getEnvironmentVariable ("CDAW_CUBASE_CORPUS", {}); corpus.isNotEmpty())
            {
                beginTest ("Cubase import corpus");
                auto files = juce::File (corpus).findChildFiles (juce::File::findFiles, true, "*.cpr");
                files.sort();
                const int stride = juce::jmax (1, juce::SystemStats::getEnvironmentVariable ("CDAW_CUBASE_STRIDE", "1").getIntValue());
                auto& engine = getEngine();
                auto media = getTestDir().getChildFile ("cubase-media");

                // CDAW_CUBASE_PLUGINS=<the app's Settings.xml>: use its scanned plugins (restored afterwards).
                auto& known = engine.getPluginManager().knownPluginList;
                auto previousPlugins = known.createXml();

                if (auto settings = juce::SystemStats::getEnvironmentVariable ("CDAW_CUBASE_PLUGINS", {}); settings.isNotEmpty())
                {
                    if (auto xml = juce::parseXML (juce::File (settings)))
                    {
                        std::function<const juce::XmlElement* (const juce::XmlElement&)> findList = [&] (const juce::XmlElement& e) -> const juce::XmlElement*
                        {
                            if (e.hasTagName ("KNOWNPLUGINS"))
                                return &e;
                            for (auto* c : e.getChildIterator())
                                if (auto* found = findList (*c))
                                    return found;
                            return nullptr;
                        };

                        if (auto* list = findList (*xml))
                            known.recreateFromXml (*list);
                    }
                }

                for (int i = 0; i < files.size(); i += stride)
                {
                    auto& f = files.getReference (i);
                    auto edit = te::test_utilities::createTestEdit (engine, 0);
                    DawProject::Report report;
                    const auto t0 = juce::Time::getMillisecondCounterHiRes();
                    CubaseProject::ImportOptions options;
                    options.copyAudio = false;
                    options.matchSimilarPlugins = matchSimilar;
                    auto result = CubaseProject::importInto (*edit, f, media, report, options);
                    const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;

                    expect (result.wasOk(), f.getFileName() + ": " + result.getErrorMessage());

                    int notes = 0, audioClips = 0, missing = 0;
                    for (auto t : te::getAudioTracks (*edit))
                    {
                        for (auto c : t->getClips())
                        {
                            if (auto mc = dynamic_cast<te::MidiClip*> (c))
                                notes += mc->getSequence().getNumNotes();

                            if (auto wc = dynamic_cast<te::WaveAudioClip*> (c))
                            {
                                ++audioClips;
                                if (! wc->getOriginalFile().existsAsFile())
                                    ++missing;
                            }
                        }
                    }

                    expectEquals (missing, 0, f.getFileName() + ": clips whose audio file is missing");

                    logMessage (f.getFileName() + ": " + juce::String (te::getAllTracks (*edit).size()) + " tracks, "
                                + juce::String (audioClips) + " audio clips, " + juce::String (notes) + " notes, "
                                + juce::String (report.plugins) + " plugins, " + juce::String (report.markers) + " markers, "
                                + juce::String (report.automationLanes) + " automation lanes, "
                                + juce::String (report.notes.size()) + " notes in the report, "
                                + juce::String (juce::roundToInt (ms)) + " ms");

                    if (juce::SystemStats::getEnvironmentVariable ("CDAW_CUBASE_VERBOSE", {}).isNotEmpty())
                        logMessage ("  " + report.notes.joinIntoString ("\n  "));
                }

                if (previousPlugins != nullptr)
                    known.recreateFromXml (*previousPlugins);
            }
        }
    };

    static CubaseImportTests cubaseImportTests;
}
