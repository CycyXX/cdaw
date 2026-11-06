#include "PluginUpdates.h"

namespace cdaw::PluginUpdates
{
    namespace
    {
        constexpr int maxListedChanges = 15;

        juce::File submoduleOf (const juce::File& sourceFolder)
        {
            return sourceFolder.getChildFile ("third_party").getChildFile ("cdaw-plugins");
        }

        struct GitResult
        {
            bool ok = false;
            juce::String output;   // stdout and stderr, trimmed
        };

        GitResult git (const juce::File& repo, const juce::StringArray& args)
        {
            juce::StringArray command { "git", "-C", repo.getFullPathName() };
            command.addArray (args);

            juce::ChildProcess process;
            if (! process.start (command, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
                return { false, "git isn't installed, or isn't on PATH" };

            auto output = process.readAllProcessOutput().trim();   // returns once git has ended its output
            process.waitForProcessToFinish (10000);
            return { process.getExitCode() == 0, output };
        }

        Status failed (const juce::String& error)
        {
            Status s;
            s.state = Status::State::failed;
            s.error = error;
            return s;
        }
    }

    juce::File findSourceFolder (const juce::File& exe)
    {
        for (auto folder = exe.getParentDirectory(); ; folder = folder.getParentDirectory())
        {
            if (folder.getChildFile ("Update-Plugins.bat").existsAsFile() && submoduleOf (folder).isDirectory())
                return folder;

            if (folder.isRoot() || folder == folder.getParentDirectory())
                return {};
        }
    }

    Status check (const juce::File& sourceFolder)
    {
        auto plugins = submoduleOf (sourceFolder);
        if (! plugins.getChildFile (".git").exists())
            return failed ("CDAW's copy of the plugins is missing (" + plugins.getFullPathName() + "). Run scripts\\bootstrap.ps1.");

        auto changed = git (plugins, { "status", "--porcelain", "--ignore-submodules" });
        if (! changed.ok)
            return failed (changed.output);

        if (changed.output.isNotEmpty())
        {
            Status s;
            s.state = Status::State::localChanges;
            return s;
        }

        // A stalled connection gives up after 20 seconds instead of waiting forever.
        auto fetched = git (plugins, { "-c", "http.lowSpeedLimit=1000", "-c", "http.lowSpeedTime=20",
                                       "fetch", "--quiet", "origin", "main" });
        if (! fetched.ok)
            return failed ("Couldn't get the latest plugins from GitHub. " + fetched.output);

        auto head = git (plugins, { "rev-parse", "HEAD" });
        auto main = git (plugins, { "rev-parse", "FETCH_HEAD" });
        if (! head.ok || ! main.ok)
            return failed (head.ok ? main.output : head.output);

        Status s;
        s.current = head.output.substring (0, 7);
        s.latest = main.output.substring (0, 7);

        if (head.output == main.output)
        {
            s.state = Status::State::upToDate;
            return s;
        }

        if (! git (plugins, { "merge-base", "--is-ancestor", "HEAD", "FETCH_HEAD" }).ok)
        {
            s.state = Status::State::notOnMain;
            return s;
        }

        auto log = git (plugins, { "log", "--format=%s", "HEAD..FETCH_HEAD" });
        if (! log.ok)
            return failed (log.output);

        auto subjects = juce::StringArray::fromLines (log.output);
        subjects.removeEmptyStrings();

        s.state = Status::State::newer;
        s.moreChanges = juce::jmax (0, subjects.size() - maxListedChanges);
        subjects.removeRange (maxListedChanges, s.moreChanges);
        s.changes = subjects;
        return s;
    }

    bool startUpdate (const juce::File& sourceFolder)
    {
       #if JUCE_WINDOWS
        auto script = sourceFolder.getChildFile ("Update-Plugins.bat");
        return script.existsAsFile() && script.startAsProcess ("-Restart");
       #else
        juce::ignoreUnused (sourceFolder);
        return false;
       #endif
    }
}
