#include "TestEngine.h"
#include "Model/ProjectSession.h"
#include "Model/MissingMedia.h"
#include "Plugins/CompressorPlugin.h"
#include "Shared/DSP/ReverbEngine.h"

namespace cdaw::test
{
    struct RobustnessTests : public juce::UnitTest
    {
        RobustnessTests() : juce::UnitTest ("Robustness", "CDAW") {}

        void runTest() override
        {
            auto& engine = getEngine();

            beginTest ("The compressor clears only its own block of the sidechain channel");
            {
                auto edit = te::test_utilities::createTestEdit (engine, 1);
                auto plugin = edit->getPluginCache().createNewPlugin (CompressorPlugin::xmlTypeName, {});
                auto comp = dynamic_cast<CompressorPlugin*> (plugin.get());
                expect (comp != nullptr);
                comp->initialise ({ te::TimePosition(), 48000.0, 512 });

                // Three channels: stereo plus a sidechain; process samples 128..255 only.
                juce::AudioBuffer<float> buffer (3, 512);
                buffer.clear();
                juce::FloatVectorOperations::fill (buffer.getWritePointer (2), 1.0f, 512);

                te::PluginRenderContext rc (&buffer, 128, 128, nullptr, 0.0, {}, true, false, false, false);
                comp->applyToBuffer (rc);

                expectEquals (buffer.getSample (2, 100), 1.0f, "before the block: untouched");
                expectEquals (buffer.getSample (2, 200), 0.0f, "in the block: cleared");
                expectEquals (buffer.getSample (2, 300), 1.0f, "after the block: untouched");
            }

            beginTest ("The reverb processes blocks longer than it was prepared for");
            {
                ReverbEngine reverb;
                ReverbEngine::Settings s;
                s.mix = 1.0f;
                s.preDelay = 0.0f;
                reverb.prepare (48000.0, 256, s);

                juce::AudioBuffer<float> buffer (2, 4096);
                buffer.clear();
                buffer.setSample (0, 0, 1.0f);
                buffer.setSample (1, 0, 1.0f);
                reverb.process (buffer, 0, buffer.getNumSamples(), s);

                // Fully wet: the tail fills the later part of the block instead of silence.
                expectGreaterThan (buffer.getMagnitude (0, 2048, 2048), 1.0e-4f);
            }

            beginTest ("Save As onto an existing project overwrites it instead of nesting a folder");
            {
                auto root = getTestDir().getChildFile ("SaveAs");
                root.deleteRecursively();
                root.createDirectory();

                auto existing = ProjectSession::createProjectFolder (root, "Song");
                existing.replaceWithText ("<EDIT/>");

                expect (ProjectSession::resolveSaveAsTarget (existing) == existing, "Same file, no subfolder");
                expect (! existing.getParentDirectory().getChildFile ("Song_2").exists());
                expect (ProjectSession::resolveSaveAsTarget (existing.withFileExtension (".txt")) == existing,
                        "The extension is normalised");

                // A new name typed inside Song's folder goes beside it, not inside it.
                auto fresh = ProjectSession::resolveSaveAsTarget (existing.getSiblingFile ("Remix"));
                expect (fresh == root.getChildFile ("Remix").getChildFile ("Remix.cdaw"), fresh.getFullPathName());

                // A new name in a plain folder makes its own project folder there.
                auto plain = ProjectSession::resolveSaveAsTarget (root.getChildFile ("Other"));
                expect (plain == root.getChildFile ("Other").getChildFile ("Other.cdaw"), plain.getFullPathName());
            }

            beginTest ("Audio that moved is found again in the project's Audio folder when it opens");
            {
                auto root = getTestDir().getChildFile ("Relink");
                root.deleteRecursively();
                root.createDirectory();

                auto projectFile = ProjectSession::createProjectFolder (root, "Song");
                {
                    ProjectSession session (engine, projectFile);
                    auto track = te::getFirstAudioTrack (session.getEdit());
                    auto wav = writeSineFile ("relink_src", 1.0, 0.5f).copyFileTo (session.getAudioFolder().getChildFile ("take.wav"))
                                 ? session.getAudioFolder().getChildFile ("take.wav") : juce::File();
                    expect (wav.existsAsFile());
                    expect (track->insertWaveClip ("take", wav, { { te::TimePosition(), te::TimeDuration::fromSeconds (1.0) }, {} }, false) != nullptr);
                    expect (session.save());
                    expect (MissingMedia::findMissingAudio (session.getEdit()).isEmpty());
                }

                // What a nested Save As followed by moving the file back left behind: one ".." too many.
                auto xml = projectFile.loadFileAsString();
                expect (xml.contains ("take.wav"));
                auto stale = xml.replace ("..\\Audio\\take.wav", "..\\..\\Audio\\take.wav").replace ("../Audio/take.wav", "../../Audio/take.wav");
                expect (stale != xml, "the saved path was relative, so the test can break it");
                projectFile.replaceWithText (stale);

                ProjectSession reopened (engine, projectFile);
                expectEquals (reopened.getReconnectedFileCount(), 1);
                expect (MissingMedia::findMissingAudio (reopened.getEdit()).isEmpty());

                auto clip = dynamic_cast<te::AudioClipBase*> (te::getFirstAudioTrack (reopened.getEdit())->getClips()[0]);
                expect (clip != nullptr && clip->getSourceFileReference().getFile() == reopened.getAudioFolder().getChildFile ("take.wav"));
                expect (clip != nullptr && clip->getAudioFile().isValid(), "the clip plays again");
                expect (reopened.hasChangedSinceSaved(), "the repaired path is saved on the next save");
            }

            beginTest ("Missing audio outside the project can be found by folder or pointed at");
            {
                auto root = getTestDir().getChildFile ("Relink2");
                root.deleteRecursively();
                root.createDirectory();

                auto projectFile = ProjectSession::createProjectFolder (root, "Song");
                ProjectSession session (engine, projectFile);
                auto track = te::getFirstAudioTrack (session.getEdit());

                // Two files in a folder that is later removed from where the project expects them.
                auto original = root.getChildFile ("Samples");
                original.createDirectory();
                auto a = writeSineFile ("relink_a", 0.5, 0.5f).copyFileTo (original.getChildFile ("a.wav")) ? original.getChildFile ("a.wav") : juce::File();
                auto b = writeSineFile ("relink_b", 0.5, 0.5f).copyFileTo (original.getChildFile ("b.wav")) ? original.getChildFile ("b.wav") : juce::File();
                track->insertWaveClip ("a", a, { { te::TimePosition(), te::TimeDuration::fromSeconds (0.5) }, {} }, false);
                track->insertWaveClip ("b", b, { { te::TimePosition::fromSeconds (1.0), te::TimeDuration::fromSeconds (0.5) }, {} }, false);
                expect (MissingMedia::findMissingAudio (session.getEdit()).isEmpty());

                auto moved = root.getChildFile ("Elsewhere").getChildFile ("Deeper");
                moved.createDirectory();
                expect (original.getChildFile ("a.wav").moveFileTo (moved.getChildFile ("A.WAV")), "case differs on purpose");
                expect (original.getChildFile ("b.wav").moveFileTo (moved.getChildFile ("b.wav")));

                auto missing = MissingMedia::findMissingAudio (session.getEdit());
                expectEquals (missing.size(), 2);
                expect (MissingMedia::findMissingPlugins (session.getEdit()).isEmpty());

                // Not found in the project's own folders...
                expectEquals (MissingMedia::relinkFromFolders (session.getEdit(), { session.getAudioFolder() }), 0);

                // ...pointing at one file reconnects it, and searching the folder above finds the rest.
                expectEquals (MissingMedia::relink (missing[0], moved.getChildFile (missing[0].expected.getFileName() == "a.wav" ? "A.WAV" : "b.wav")), 1);
                expectEquals (MissingMedia::findMissingAudio (session.getEdit()).size(), 1);
                expectEquals (MissingMedia::relinkFromFolders (session.getEdit(), { root.getChildFile ("Elsewhere") }), 1);
                expect (MissingMedia::findMissingAudio (session.getEdit()).isEmpty());
            }

            beginTest ("Unreadable project files are recognised before anything is closed");
            {
                auto dir = getTestDir().getChildFile ("Readable");
                dir.deleteRecursively();
                dir.createDirectory();

                auto missing = dir.getChildFile ("New.cdaw");
                expect (ProjectSession::isReadable (engine, missing), "A new project is fine");

                auto broken = dir.getChildFile ("Broken.cdaw");
                broken.replaceWithText ("this is not a project");
                expect (! ProjectSession::isReadable (engine, broken));

                auto good = ProjectSession::createProjectFolder (dir, "Good");
                {
                    ProjectSession session (engine, good);
                    expect (session.save());
                }
                expect (ProjectSession::isReadable (engine, good));
            }
        }
    };

    static RobustnessTests robustnessTests;
}
