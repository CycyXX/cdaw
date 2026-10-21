#include "TestEngine.h"
#include "Model/ProjectSession.h"

namespace cdaw::test
{
    struct EngineTests : public juce::UnitTest
    {
        EngineTests() : juce::UnitTest ("Engine basics", "CDAW") {}

        void runTest() override
        {
            auto& engine = getEngine();
            using namespace te;

            beginTest ("Render a single-track edit");
            {
                auto edit = test_utilities::createTestEdit (engine);
                auto file = writeSineFile ("sine_half", 2.0, 0.5f);
                auto track = getAudioTracks (*edit)[0];
                auto clip = track->insertWaveClip ("sine", file, { { 0_tp, 2_td }, {} }, false);
                expect (clip != nullptr);

                auto result = test_utilities::renderToAudioBuffer (*edit);
                expectWithinAbsoluteError (result.buffer.getNumSamples() / result.sampleRate, 2.0, 0.05);
                expectWithinAbsoluteError (result.buffer.getMagnitude (0, result.buffer.getNumSamples()), 0.5f, 0.05f);
            }

            beginTest ("Relative source paths resolve in both Tracktion styles");
            {
                auto dir = getTestDir().getChildFile ("ResolveTest");
                dir.getChildFile ("Audio").createDirectory();
                auto audio = dir.getChildFile ("Audio").getChildFile ("x.wav");
                audio.replaceWithText ("x");
                auto editFile = dir.getChildFile ("Song.tracktionedit");

                expect (ProjectSession::resolveRelativePath (editFile, "Audio/x.wav") == audio);
                expect (ProjectSession::resolveRelativePath (editFile, "../Audio/x.wav") == audio);
                expect (ProjectSession::resolveRelativePath (editFile, audio.getFullPathName()) == audio);
            }

            beginTest ("Projects save as .cdaw and old .tracktionedit projects are converted");
            {
                auto projectsDir = getTestDir().getChildFile ("Legacy");
                projectsDir.deleteRecursively();
                auto editFile = ProjectSession::createProjectFolder (projectsDir, "Old");
                expect (editFile.hasFileExtension ("cdaw"));

                auto audio = editFile.getParentDirectory().getChildFile ("Audio").getChildFile ("old.wav");
                auto legacy = editFile.withFileExtension (".tracktionedit");

                {
                    ProjectSession session (engine, editFile);
                    audio.getParentDirectory().createDirectory();
                    writeSineFile ("old_src", 1.0, 0.25f).copyFileTo (audio);
                    getAudioTracks (session.getEdit())[0]->insertWaveClip ("old", audio, { { 0_tp, 1_td }, {} }, false);
                    expect (session.save());
                }

                // Pretend an earlier build saved it under the Tracktion extension.
                expect (editFile.moveFileTo (legacy));
                expect (ProjectSession::isProjectFile (legacy));

                {
                    ProjectSession session (engine, legacy);
                    expect (session.isValid());
                    expect (session.getEditFile() == editFile, "Continues as " + session.getEditFile().getFullPathName());
                    expect (editFile.existsAsFile(), "Converted file written");
                    expect (! legacy.existsAsFile(), "Old file moved to Backups");
                    expectEquals (session.getBackupsFolder().findChildFiles (juce::File::findFiles, false, "*.tracktionedit").size(), 1);
                    expectEquals (getAudioTracks (session.getEdit())[0]->getClips().size(), 1);
                }

                {
                    ProjectSession session (engine, editFile);
                    expectEquals (getAudioTracks (session.getEdit())[0]->getClips().size(), 1);
                }
            }

            beginTest ("A clip inserted into an open project finds its file");
            {
                auto projectsDir = getTestDir().getChildFile ("LiveInsert");
                projectsDir.deleteRecursively();
                auto editFile = ProjectSession::createProjectFolder (projectsDir, "Live");
                ProjectSession session (engine, editFile);
                auto audio = session.getAudioFolder().getChildFile ("Imported").getChildFile ("live.wav");
                audio.getParentDirectory().createDirectory();
                writeSineFile ("live_src", 1.0, 0.3f).copyFileTo (audio);

                auto clip = getAudioTracks (session.getEdit())[0]->insertWaveClip ("live", audio, { { 0_tp, 1_td }, {} }, false);
                expect (clip != nullptr);
                expect (clip->getAudioFile().getFile() == audio, "Resolved to " + clip->getAudioFile().getFile().getFullPathName());
                expect (te::AudioFile (clip->getAudioFile()).isValid());
            }

            beginTest ("Project save and reload keeps clips with relative paths");
            {
                auto projectsDir = getTestDir().getChildFile ("Projects");
                projectsDir.deleteRecursively();
                projectsDir.createDirectory();

                auto editFile = ProjectSession::createProjectFolder (projectsDir, "Song");
                auto audio = editFile.getParentDirectory().getChildFile ("Audio").getChildFile ("riff.wav");

                {
                    ProjectSession session (engine, editFile);
                    expect (session.isValid());
                    audio.getParentDirectory().createDirectory();
                    writeSineFile ("riff_src", 1.0, 0.25f).copyFileTo (audio);

                    auto track = getAudioTracks (session.getEdit())[0];
                    track->insertWaveClip ("riff", audio, { { 1_tp, 1_td }, {} }, false);
                    expect (session.save());
                    expect (editFile.existsAsFile());
                }

                // Move the whole project folder: relative paths must still resolve.
                auto moved = projectsDir.getChildFile ("SongMoved");
                expect (editFile.getParentDirectory().moveFileTo (moved));
                auto movedEdit = moved.getChildFile (editFile.getFileName());

                {
                    ProjectSession session (engine, movedEdit);
                    expect (session.isValid());
                    auto clips = getAudioTracks (session.getEdit())[0]->getClips();
                    expectEquals (clips.size(), 1);

                    if (auto wc = dynamic_cast<WaveAudioClip*> (clips.getFirst()))
                    {
                        expect (wc->getSourceFileReference().getFile().existsAsFile(), "Source file not found after move");
                        expectWithinAbsoluteError (wc->getPosition().getStart().inSeconds(), 1.0, 0.001);
                    }
                }
            }
        }
    };

    static EngineTests engineTests;
}
