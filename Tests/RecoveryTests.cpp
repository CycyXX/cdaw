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

            beginTest ("An autosave still being written can't come back after it was discarded");
            {
                ProjectSession s (engine, editFile);
                auto tmp = te::EditFileOperations::getTempVersionOfEditFile (editFile);

                for (int i = 0; i < 5; ++i)
                {
                    TrackOps::addAudioTrack (s.getEdit(), nullptr, nullptr, "Track " + juce::String (i));
                    s.getEdit().markAsChanged();
                    s.autosave();
                    s.discardAutosave();
                    expect (! tmp.existsAsFile(), "The autosave came back");
                }

                juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
                expect (! tmp.existsAsFile(), "The autosave came back later");
            }

            beginTest ("Autosave and save keep working after saving as a new project");
            {
                auto movedFile = ProjectSession::createProjectFolder (dir, "SavedAs");

                {
                    ProjectSession s (engine, editFile);
                    expect (s.save());
                    expect (s.saveAs (movedFile));
                    expect (s.getEditFile() == movedFile);

                    juce::Thread::sleep (1100);   // file times have 1 s resolution on some systems
                    TrackOps::addAudioTrack (s.getEdit(), nullptr, nullptr, "After save as");
                    s.getEdit().markAsChanged();
                    s.autosave();
                }

                // The writer has finished by the time the session is gone.
                auto autosave = ProjectSession::findNewerAutosave (movedFile);
                expect (autosave.existsAsFile(), "The autosave didn't go beside the new project");
                expect (ProjectSession::findNewerAutosave (editFile) == juce::File(), "The autosave went beside the old project");

                ProjectSession reopened (engine, movedFile, autosave);
                bool found = false;
                for (auto t : te::getAudioTracks (reopened.getEdit()))
                    found = found || t->getName() == "After save as";

                expect (found, "The autosave is missing the new track");
                expect (reopened.save());
                expect (ProjectSession::findNewerAutosave (movedFile) == juce::File(), "Saving leaves no autosave");
            }

            beginTest ("A source file that goes away while the project is open is found to be missing");
            {
                auto file = ProjectSession::createProjectFolder (dir, "Vanishing");
                ProjectSession s (engine, file);
                auto& edit = s.getEdit();
                auto audio = s.getAudioFolder().getChildFile ("vanishing.wav");
                writeSineFile ("vanishing_source", 0.5, 0.5f).copyFileTo (audio);

                expect (te::SourceFileReference::findFileFromString (edit, "Audio/vanishing.wav") == audio);
                expect (te::SourceFileReference::findFileFromString (edit, "Audio/vanishing.wav").existsAsFile());

                audio.deleteFile();
                expect (! te::SourceFileReference::findFileFromString (edit, "Audio/vanishing.wav").existsAsFile());

                // Asked for before it exists, it's found once it does.
                auto later = s.getAudioFolder().getChildFile ("later.wav");
                expect (! te::SourceFileReference::findFileFromString (edit, "Audio/later.wav").existsAsFile());
                writeSineFile ("later_source", 0.5, 0.5f).copyFileTo (later);
                expect (te::SourceFileReference::findFileFromString (edit, "Audio/later.wav") == later);
            }
        }
    };

    static RecoveryTests recoveryTests;
}
