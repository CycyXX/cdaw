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
