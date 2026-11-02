#include "TestEngine.h"
#include "Model/ArrangerOps.h"
#include "Model/KeyScale.h"
#include "Model/MixerOps.h"
#include "Model/ProjectSession.h"
#include "Model/ProjectVersions.h"
#include "Model/SongNotes.h"
#include "Model/TempoOps.h"
#include "Model/TemplateOps.h"
#include "Model/TrackOps.h"
#include "Plugins/EqPlugin.h"

namespace cdaw::test
{
    struct SongwritingTests : public juce::UnitTest
    {
        SongwritingTests() : juce::UnitTest ("Versions, notes, templates and key", "CDAW") {}

        static te::TimeRange secs (double a, double b)
        {
            return { te::TimePosition::fromSeconds (a), te::TimePosition::fromSeconds (b) };
        }

        static int countClips (te::Edit& edit)
        {
            int n = 0;
            for (auto t : te::getAudioTracks (edit))
                n += t->getClips().size();
            return n;
        }

        static te::Track* findTrack (te::Edit& edit, const juce::String& name)
        {
            te::Track* found = nullptr;
            edit.visitAllTracksRecursive ([&] (te::Track& t) { if (t.getName() == name) found = &t; return found == nullptr; });
            return found;
        }

        static bool hasPlugin (te::Track& t, const juce::String& type)
        {
            for (auto p : t.pluginList.getPlugins())
                if (p->getPluginType() == type)
                    return true;
            return false;
        }

        void runTest() override
        {
            auto& engine = getEngine();
            auto dir = getTestDir().getChildFile ("SongwritingProjects");
            dir.deleteRecursively();
            dir.createDirectory();

            //==============================================================================
            beginTest ("Version names: Song -> Song_02 -> Song_03, from any version the next free number");
            {
                using namespace ProjectVersions;
                expectEquals (parseName ("Song").base, juce::String ("Song"));
                expectEquals (parseName ("Song").number, 1);
                expectEquals (parseName ("Song_03").base, juce::String ("Song"));
                expectEquals (parseName ("Song_03").number, 3);
                expectEquals (parseName ("Big_Song_12").base, juce::String ("Big_Song"));
                expectEquals (parseName ("Handky_test01").base, juce::String ("Handky_test01"));
                expectEquals (parseName ("Song_").base, juce::String ("Song_"));
                expectEquals (makeName ("Song", 2), juce::String ("Song_02"));
                expectEquals (makeName ("Song", 104), juce::String ("Song_104"));

                auto folder = dir.getChildFile ("Names");
                folder.createDirectory();
                auto song = folder.getChildFile ("Song.cdaw");
                song.replaceWithText ("<EDIT/>");
                folder.getChildFile ("Other_05.cdaw").replaceWithText ("<EDIT/>");   // another song: ignored

                expectEquals (getNextVersionFile (song).getFileName(), juce::String ("Song_02.cdaw"));
                folder.getChildFile ("Song_02.cdaw").replaceWithText ("<EDIT/>");
                folder.getChildFile ("Song_03.cdaw").replaceWithText ("<EDIT/>");
                expectEquals (getNextVersionFile (song).getFileName(), juce::String ("Song_04.cdaw"));
                expectEquals (getNextVersionFile (folder.getChildFile ("Song_02.cdaw")).getFileName(), juce::String ("Song_04.cdaw"),
                              "Saving a version of an older one continues after the newest");

                auto numbered = folder.getChildFile ("Demo_01.cdaw");
                numbered.replaceWithText ("<EDIT/>");
                expectEquals (getNextVersionFile (numbered).getFileName(), juce::String ("Demo_02.cdaw"));

                auto versions = list (song);
                expectEquals (versions.size(), 3);
                expectEquals (versions[2].file.getFileName(), juce::String ("Song_03.cdaw"));
            }

            beginTest ("Save New Version: the new file shares the audio, keeps its note, the old one stays");
            {
                auto editFile = ProjectSession::createProjectFolder (dir, "Ballad");
                auto audio = editFile.getSiblingFile ("Audio").getChildFile ("take.wav");
                writeSineFile ("ballad_take", 1.0, 0.3f).copyFileTo (audio);

                {
                    ProjectSession s (engine, editFile);
                    auto track = te::getAudioTracks (s.getEdit())[0];
                    track->insertWaveClip ("take", audio, { secs (0.0, 1.0), {} }, false);
                    expect (s.save());

                    TrackOps::addAudioTrack (s.getEdit(), nullptr, nullptr, "Added in v2");
                    auto v2 = s.saveNewVersion ("  Second verse  ");
                    expectEquals (v2.getFileName(), juce::String ("Ballad_02.cdaw"));
                    expect (s.getEditFile() == v2, "Work continues in the new version");
                    expectEquals (s.getName(), juce::String ("Ballad_02"));
                    expect (! s.hasChangedSinceSaved());

                    auto v3 = s.saveNewVersion ({});
                    expectEquals (v3.getFileName(), juce::String ("Ballad_03.cdaw"));
                }

                expectEquals (ProjectVersions::readNote (editFile.getSiblingFile ("Ballad_02.cdaw")), juce::String ("Second verse"));
                expectEquals (ProjectVersions::readNote (editFile.getSiblingFile ("Ballad_03.cdaw")), juce::String(),
                              "A version without a note doesn't inherit the last one");
                expectEquals (ProjectVersions::list (editFile).size(), 3);

                {
                    ProjectSession first (engine, editFile);
                    expect (findTrack (first.getEdit(), "Added in v2") == nullptr, "The first version stays as it was saved");
                }

                ProjectSession second (engine, editFile.getSiblingFile ("Ballad_02.cdaw"));
                expect (findTrack (second.getEdit(), "Added in v2") != nullptr);
                auto clip = dynamic_cast<te::WaveAudioClip*> (te::getAudioTracks (second.getEdit())[0]->getClips().getFirst());
                expect (clip != nullptr && clip->getSourceFileReference().getFile() == audio, "The version plays the same audio file");
            }

            //==============================================================================
            beginTest ("Section lyrics move, copy and undo with their sections");
            {
                auto edit = te::test_utilities::createTestEdit (engine, 1);
                auto* um = &edit->getUndoManager();
                for (int i = 0; i < 3; ++i)
                    ArrangerOps::addSection (*edit, secs (i * 2.0, i * 2.0 + 2.0), juce::String::charToString ((juce::juce_wchar) ('A' + i)));

                auto sections = ArrangerOps::getSections (*edit);
                um->beginNewTransaction ("lyrics");
                SongNotes::setLyrics (*sections[1], "We drive all night\nunder sodium lights\n\n", um);
                SongNotes::setLyrics (*sections[2], "Outro words", um);
                expectEquals (SongNotes::getLyrics (*sections[1]), juce::String ("We drive all night\nunder sodium lights"));

                ArrangerOps::duplicateSection (*edit, *sections[1]);
                sections = ArrangerOps::getSections (*edit);
                expectEquals (sections.size(), 4);
                expectEquals (SongNotes::getLyrics (*sections[2]), SongNotes::getLyrics (*sections[1]), "The copy has the lyrics");

                ArrangerOps::moveSection (*edit, *sections[3], te::TimePosition());
                sections = ArrangerOps::getSections (*edit);
                expectEquals (sections[0]->getName(), juce::String ("C"));
                expectEquals (SongNotes::getLyrics (*sections[0]), juce::String ("Outro words"));

                um->beginNewTransaction ("clear");
                SongNotes::setLyrics (*sections[0], {}, um);
                expect (! sections[0]->state.hasProperty (SongNotes::lyricsID));
                um->undo();
                expectEquals (SongNotes::getLyrics (*ArrangerOps::getSections (*edit)[0]), juce::String ("Outro words"));
            }

            beginTest ("Song notes and lyrics are saved with the project");
            {
                auto editFile = ProjectSession::createProjectFolder (dir, "Notebook");

                {
                    ProjectSession s (engine, editFile);
                    auto& edit = s.getEdit();
                    ArrangerOps::addSection (edit, secs (0.0, 4.0), "Verse");
                    SongNotes::setLyrics (*ArrangerOps::getSections (edit)[0], "Line one\nLine two", nullptr);
                    expect (s.save());

                    SongNotes::setProjectNotes (edit, "Try a half-time bridge.\nCapo 2?");
                    expect (s.hasChangedSinceSaved(), "Typing notes counts as a change");
                    expect (s.save());
                }

                ProjectSession reopened (engine, editFile);
                expectEquals (SongNotes::getProjectNotes (reopened.getEdit()), juce::String ("Try a half-time bridge.\nCapo 2?"));
                expectEquals (SongNotes::getLyrics (*ArrangerOps::getSections (reopened.getEdit())[0]), juce::String ("Line one\nLine two"));
            }

            //==============================================================================
            beginTest ("A template keeps tracks, routing and plugins, and drops the song");
            {
                auto source = ProjectSession::createProjectFolder (dir, "Setup");
                auto templateFile = dir.getChildFile ("Metal demo.cdawtemplate");

                {
                    ProjectSession s (engine, source);
                    auto& edit = s.getEdit();
                    edit.tempoSequence.getTempo (0)->setBpm (168.0);

                    auto gtr = te::getAudioTracks (edit)[0];
                    gtr->setName ("Gtr L");
                    gtr->insertWaveClip ("riff", writeSineFile ("tmpl_riff", 1.0, 0.3f), { secs (0.0, 1.0), {} }, false);
                    gtr->pluginList.insertPlugin (edit.getPluginCache().createNewPlugin (EqPlugin::xmlTypeName, {}), 0, nullptr);

                    auto fx = TrackOps::addFxTrack (edit, nullptr);
                    fx->setName ("Verb");
                    edit.setAuxBusName (0, "Verb");
                    MixerOps::addSend (*gtr, 0, true);

                    auto group = TrackOps::addFolderTrack (edit, nullptr, true);
                    group->setName ("Guitars");
                    edit.moveTrack (gtr, te::TrackInsertPoint (group.get(), nullptr));

                    gtr->getVolumePlugin()->volParam->getCurve().addPoint (te::TimePosition(), 0.5f, 0.0f, nullptr);
                    TempoOps::addMarker (edit, te::TimePosition::fromSeconds (1.0));
                    ArrangerOps::addSection (edit, secs (0.0, 2.0), "Intro");
                    SongNotes::setProjectNotes (edit, "song-specific");

                    expect (TemplateOps::saveTemplate (edit, templateFile));
                    expect (countClips (edit) == 1, "Saving a template leaves the project itself alone");
                }

                auto newFile = ProjectSession::createProjectFolder (dir, "From template");
                expect (TemplateOps::createProjectFromTemplate (templateFile, newFile));

                ProjectSession made (engine, newFile);
                expect (made.isValid());
                auto& edit = made.getEdit();

                expectWithinAbsoluteError (edit.tempoSequence.getTempo (0)->getBpm(), 168.0, 0.001);
                auto gtr = dynamic_cast<te::AudioTrack*> (findTrack (edit, "Gtr L"));
                expect (gtr != nullptr && findTrack (edit, "Verb") != nullptr && findTrack (edit, "Guitars") != nullptr);
                expect (gtr != nullptr && gtr->getParentFolderTrack() != nullptr, "Still inside its group");
                expect (gtr != nullptr && hasPlugin (*gtr, EqPlugin::xmlTypeName), "Inserts kept");
                expect (gtr != nullptr && MixerOps::getSends (*gtr).size() == 1, "Sends kept");
                expectEquals (edit.getAuxBusName (0), juce::String ("Verb"));

                expectEquals (countClips (edit), 0);
                expect (gtr != nullptr && gtr->getVolumePlugin()->volParam->getCurve().getNumPoints() == 0, "No automation");
                expect (ArrangerOps::getSections (edit).isEmpty(), "No sections");
                expect (edit.getMarkerManager().getMarkers().isEmpty(), "No markers");
                expect (SongNotes::getProjectNotes (edit).isEmpty(), "No notes");
            }

            //==============================================================================
            beginTest ("Track presets find their FX bus by name and move returns off used buses");
            {
                auto presets = dir.getChildFile ("Presets");
                auto source = te::test_utilities::createTestEdit (engine, 1);
                auto gtr = te::getAudioTracks (*source)[0];
                gtr->setName ("Lead Gtr");
                gtr->insertWaveClip ("solo", writeSineFile ("preset_solo", 1.0, 0.3f), { secs (0.0, 1.0), {} }, false);
                gtr->pluginList.insertPlugin (source->getPluginCache().createNewPlugin (EqPlugin::xmlTypeName, {}), 0, nullptr);
                auto verb = TrackOps::addFxTrack (*source, nullptr);
                verb->setName ("Verb");
                source->setAuxBusName (0, "Verb");
                MixerOps::addSend (*gtr, 0, true);

                auto folder = TrackOps::addFolderTrack (*source, nullptr, true);
                folder->setName ("Drums");
                for (auto name : { "Kick", "Snare" })
                {
                    auto t = TrackOps::addAudioTrack (*source, nullptr, nullptr, name);
                    source->moveTrack (t, te::TrackInsertPoint (folder.get(), nullptr));
                }

                auto gtrPreset = presets.getChildFile ("Lead Gtr.cdawtrack");
                auto verbPreset = presets.getChildFile ("Verb.cdawtrack");
                auto drumsPreset = presets.getChildFile ("Drums.cdawtrack");
                expect (TemplateOps::saveTrackPreset (*gtr, gtrPreset));
                expect (TemplateOps::saveTrackPreset (*verb, verbPreset));
                expect (TemplateOps::saveTrackPreset (*folder, drumsPreset));

                // Another song, where "Verb" is the third FX bus.
                auto target = te::test_utilities::createTestEdit (engine, 1);
                for (int i = 0; i < 3; ++i)
                    TrackOps::addFxTrack (*target, nullptr);
                target->setAuxBusName (2, "Verb");

                auto inserted = dynamic_cast<te::AudioTrack*> (TemplateOps::insertTrackPreset (*target, gtrPreset, nullptr, nullptr).get());
                expect (inserted != nullptr && inserted->getName() == "Lead Gtr");
                expect (inserted != nullptr && inserted->getClips().isEmpty(), "No clips");
                expect (inserted != nullptr && hasPlugin (*inserted, EqPlugin::xmlTypeName));
                auto sends = inserted != nullptr ? MixerOps::getSends (*inserted) : juce::Array<te::AuxSendPlugin*>();
                expect (sends.size() == 1 && sends[0]->busNumber.get() == 2, "The send goes to the bus called Verb");

                // Bus 0 is taken here: the return moves to the first free bus and keeps its name.
                auto fx = dynamic_cast<te::AudioTrack*> (TemplateOps::insertTrackPreset (*target, verbPreset, nullptr, nullptr).get());
                te::AuxReturnPlugin* ret = nullptr;
                if (fx != nullptr)
                    for (auto p : fx->pluginList.getPlugins())
                        if (auto r = dynamic_cast<te::AuxReturnPlugin*> (p))
                            ret = r;
                expect (ret != nullptr && ret->busNumber.get() == 3);
                expectEquals (target->getAuxBusName (3), juce::String ("Verb"));

                auto drums = dynamic_cast<te::FolderTrack*> (TemplateOps::insertTrackPreset (*target, drumsPreset, nullptr, nullptr).get());
                expect (drums != nullptr && drums->isSubmixFolder());
                expect (drums != nullptr && drums->getAllSubTracks (false).size() == 2, "The folder brings its tracks");

                // Inserting twice gives two independent tracks.
                auto again = TemplateOps::insertTrackPreset (*target, gtrPreset, nullptr, nullptr);
                expect (again != nullptr && again.get() != inserted && again->itemID != inserted->itemID);
            }

            //==============================================================================
            beginTest ("Key and scale: membership, snapping, steps, undo");
            {
                KeyScale::Key aMinor { 9, "Minor" };
                expect (aMinor.isSet());
                expect (aMinor.contains (57) && aMinor.contains (60) && ! aMinor.contains (61));   // A, C in; C# out
                expect (aMinor.isRoot (45) && aMinor.isRoot (69) && ! aMinor.isRoot (60));
                expectEquals (aMinor.getName(), juce::String ("A Minor"));
                expectEquals (aMinor.getShortName(), juce::String ("A min"));

                expectEquals (KeyScale::snapToKey (aMinor, 61, 1), 62);    // C# -> D going up
                expectEquals (KeyScale::snapToKey (aMinor, 61, -1), 60);   // C# -> C going down
                expectEquals (KeyScale::snapToKey (aMinor, 60), 60);
                expectEquals (KeyScale::stepInKey (aMinor, 64, 1), 65);    // E -> F
                expectEquals (KeyScale::stepInKey (aMinor, 65, 1), 67);    // F -> G (skips F#)
                expectEquals (KeyScale::stepInKey (aMinor, 57, -1), 55);   // A -> G

                KeyScale::Key ePent { 4, "Minor Pentatonic" };
                expectEquals (KeyScale::stepInKey (ePent, 40, 1), 43);     // E -> G
                expect (! KeyScale::Key().isSet() && KeyScale::Key().contains (61), "Without a key everything is in");
                expect (! KeyScale::Key { 0, "Nonsense" }.isSet());

                auto edit = te::test_utilities::createTestEdit (engine, 1);
                auto& um = edit->getUndoManager();
                expect (! KeyScale::getKey (*edit).isSet());
                um.beginNewTransaction ("key");
                KeyScale::setKey (*edit, { 4, "Phrygian Dominant" }, &um);
                expectEquals (KeyScale::getKey (*edit).getName(), juce::String ("E Phrygian Dominant"));
                um.undo();
                expect (! KeyScale::getKey (*edit).isSet());
            }
        }
    };

    static SongwritingTests songwritingTests;
}
