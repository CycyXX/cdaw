#include "TestEngine.h"
#include "Model/ProjectSession.h"
#include "Model/TrackOps.h"

namespace cdaw::test
{
    struct RecoveryTests : public juce::UnitTest
    {
        RecoveryTests() : juce::UnitTest ("Autosave and recovery", "CDAW") {}

        void runTest() override
        {
            auto& engine = getEngine();
            auto dir = getTestDir().getChildFile ("RecoveryProjects");
            dir.deleteRecursively();
            dir.createDirectory();
            auto editFile = ProjectSession::createProjectFolder (dir, "Crashy");

            beginTest ("Autosave is found and restores unsaved changes");
            {
                {
                    ProjectSession s (engine, editFile);
                    expect (s.save());
                    expect (ProjectSession::findNewerAutosave (editFile) == juce::File(), "No autosave right after saving");

                    // Unsaved change, then autosave (as the 60 s timer would).
                    juce::Thread::sleep (1100);   // file times have 1 s resolution on some systems
                    TrackOps::addAudioTrack (s.getEdit(), nullptr, nullptr, "Unsaved track");
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (300);   // change flag is set asynchronously
                    expect (s.hasChangedSinceSaved());
                    s.autosave();

                    // The quick save is written on a background thread.
                    auto tmp = te::EditFileOperations::getTempVersionOfEditFile (editFile);
                    for (int i = 0; i < 50 && ! tmp.existsAsFile(); ++i)
                        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);

                    expect (tmp.existsAsFile(), "Autosave file not written");
                    // Session is destroyed without saving: simulates a crash.
                }

                auto autosave = ProjectSession::findNewerAutosave (editFile);
                expect (autosave.existsAsFile());

                ProjectSession recovered (engine, editFile, autosave);
                expect (recovered.isValid());
                expect (recovered.hasChangedSinceSaved());

                bool found = false;
                for (auto t : te::getAudioTracks (recovered.getEdit()))
                    found = found || t->getName() == "Unsaved track";

                expect (found, "Recovered project is missing the unsaved track");
                recovered.discardAutosave();
                expect (ProjectSession::findNewerAutosave (editFile) == juce::File());
            }
        }
    };

    static RecoveryTests recoveryTests;
}
