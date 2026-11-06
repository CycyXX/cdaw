#include "App/PluginUpdates.h"

namespace cdaw::test
{
    /** Plugins > Check for Plugin Updates, against throwaway git repositories standing in for GitHub's
        cdaw-plugins and CDAW's copy of it. Needs git; without it, only the folder search runs. */
    struct PluginUpdatesTests : public juce::UnitTest
    {
        PluginUpdatesTests() : juce::UnitTest ("Plugin updates", "CDAW") {}

        using State = PluginUpdates::Status::State;

        static bool git (const juce::File& repo, const juce::StringArray& args)
        {
            juce::StringArray command { "git", "-c", "user.name=CDAW Tests", "-c", "user.email=tests@cdaw.invalid",
                                        "-c", "commit.gpgsign=false", "-C", repo.getFullPathName() };
            command.addArray (args);

            juce::ChildProcess process;
            if (! process.start (command, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
                return false;

            process.readAllProcessOutput();
            process.waitForProcessToFinish (10000);
            return process.getExitCode() == 0;
        }

        static bool commit (const juce::File& repo, const juce::String& file, const juce::String& subject)
        {
            repo.getChildFile (file).replaceWithText (subject);
            return git (repo, { "add", file }) && git (repo, { "commit", "--quiet", "-m", subject });
        }

        /** git makes its object files read-only, which stops a plain delete on Windows. */
        static bool remove (const juce::File& folder)
        {
            folder.setReadOnly (false, true);
            return folder.deleteRecursively() && ! folder.exists();
        }

        static juce::String stateName (State s)
        {
            switch (s)
            {
                case State::upToDate:     return "up to date";
                case State::newer:        return "newer";
                case State::notOnMain:    return "not on main";
                case State::localChanges: return "local changes";
                case State::failed:       return "failed";
            }
            return {};
        }

        void expectState (const PluginUpdates::Status& status, State expected)
        {
            expectEquals (stateName (status.state), stateName (expected), status.error);
        }

        void runTest() override
        {
            juce::TemporaryFile temp;
            auto root = temp.getFile();
            root.createDirectory();

            auto source = root.getChildFile ("CDAW");
            auto plugins = source.getChildFile ("third_party").getChildFile ("cdaw-plugins");

            beginTest ("Finds the source folder from the published or the built exe, and nothing elsewhere");
            {
                plugins.createDirectory();
                source.getChildFile ("Update-Plugins.bat").replaceWithText ("rem");

                auto published = source.getChildFile ("dist/CDAW/CDAW.exe");
                auto built = source.getChildFile ("build/ninja-msvc-relwithdebinfo/Source/CDAW_artefacts/RelWithDebInfo/CDAW.exe");
                auto installedElsewhere = root.getChildFile ("Programs/CDAW/CDAW.exe");

                expectEquals (PluginUpdates::findSourceFolder (published).getFullPathName(), source.getFullPathName());
                expectEquals (PluginUpdates::findSourceFolder (built).getFullPathName(), source.getFullPathName());
                expect (PluginUpdates::findSourceFolder (installedElsewhere) == juce::File(), "a copy without its source has none");

                expect (remove (plugins));
            }

            auto github = root.getChildFile ("github-cdaw-plugins");
            github.createDirectory();

            if (! git (github, { "init", "--quiet", "-b", "main" }))
            {
                logMessage ("git isn't available: skipping the checks against repositories");
                remove (root);
                return;
            }

            expect (commit (github, "Delay.cpp", "C-Delay: the first version"));
            expect (git (source, { "clone", "--quiet", github.getFullPathName(), plugins.getFullPathName() }));

            beginTest ("The same commit as main is up to date");
            {
                auto status = PluginUpdates::check (source);
                expectState (status, State::upToDate);
                expectEquals (status.current.length(), 7);
                expectEquals (status.current, status.latest);
            }

            beginTest ("Newer commits on main are listed, newest first, without changing CDAW's copy");
            {
                expect (commit (github, "Delay.cpp", "C-Delay: shorter echoes"));
                expect (commit (github, "Verb.cpp", "C-Verb: a darker room"));

                auto before = plugins.getChildFile ("Delay.cpp").loadFileAsString();
                auto status = PluginUpdates::check (source);

                expectState (status, State::newer);
                expectEquals (status.changes.joinIntoString ("|"), juce::String ("C-Verb: a darker room|C-Delay: shorter echoes"));
                expectEquals (status.moreChanges, 0);
                expect (status.current != status.latest);
                expectEquals (plugins.getChildFile ("Delay.cpp").loadFileAsString(), before, "the files stay as they were");
            }

            beginTest ("A long list stops at 15 and counts the rest");
            {
                for (int i = 1; i <= 17; ++i)
                    expect (commit (github, "Filter.cpp", "C-Filter: step " + juce::String (i)));

                auto status = PluginUpdates::check (source);
                expectState (status, State::newer);
                expectEquals (status.changes.size(), 15);
                expectEquals (status.changes[0], juce::String ("C-Filter: step 17"));
                expectEquals (status.moreChanges, 4);
            }

            beginTest ("Uncommitted changes in CDAW's copy are reported, not fetched over");
            {
                plugins.getChildFile ("Delay.cpp").replaceWithText ("work in progress");
                expectState (PluginUpdates::check (source), State::localChanges);
                expect (git (plugins, { "checkout", "--quiet", "--", "Delay.cpp" }));
            }

            beginTest ("A commit that main doesn't contain, as on a branch, isn't called older or newer");
            {
                expect (commit (plugins, "Phaser.cpp", "C-Phaser: an experiment"));
                expectState (PluginUpdates::check (source), State::notOnMain);
            }

            beginTest ("Without GitHub, the check fails and says so");
            {
                expect (remove (github));
                auto status = PluginUpdates::check (source);
                expectState (status, State::failed);
                expect (status.error.startsWith ("Couldn't get the latest plugins"), status.error);
            }

            beginTest ("Without CDAW's copy of the plugins, the check fails and says so");
            {
                expect (remove (plugins));
                auto status = PluginUpdates::check (source);
                expectState (status, State::failed);
                expect (status.error.contains ("bootstrap"), status.error);
            }

            expect (remove (root));
        }
    };

    static PluginUpdatesTests pluginUpdatesTests;
}
