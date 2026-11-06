#include "PluginUpdates.h"
#include "PluginsVersion.h"
#include <thread>

namespace cdaw::PluginUpdates
{
    namespace
    {
        constexpr int maxListedChanges = 15;
        constexpr int localTimeoutMs = 15000;
        constexpr int networkTimeoutMs = 120000;

        juce::File submoduleOf (const juce::File& sourceFolder)
        {
            return sourceFolder.getChildFile ("third_party").getChildFile ("cdaw-plugins");
        }

        ProcessResult git (const juce::File& repo, const juce::StringArray& args, int timeoutMs = localTimeoutMs)
        {
            // Nobody can answer a login prompt from here: fail instead of waiting for one.
            juce::StringArray command { "git", "-c", "credential.interactive=never", "-C", repo.getFullPathName() };
            command.addArray (args);

            auto result = run (command, timeoutMs);

            if (result.timedOut)
                result.output = "git didn't finish within " + juce::String (timeoutMs / 1000) + " seconds.";
            else if (! result.ok && result.output.isEmpty())
                result.output = "git " + args[0] + " failed.";

            return result;
        }

        Status failed (const juce::String& error)
        {
            Status s;
            s.state = Status::State::failed;
            s.error = error;
            return s;
        }
    }

    ProcessResult run (const juce::StringArray& command, int timeoutMs)
    {
        juce::ChildProcess process;
        if (! process.start (command, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
            return { false, false, command[0] + " isn't installed, or isn't on PATH." };

        // Reading the output blocks until the process ends, so a second thread enforces the deadline.
        juce::WaitableEvent finished;
        std::atomic<bool> timedOut { false };

        std::thread watchdog ([&]
        {
            if (! finished.wait (timeoutMs))
            {
                timedOut = true;
                process.kill();
            }
        });

        auto output = process.readAllProcessOutput().trim();
        finished.signal();
        watchdog.join();
        process.waitForProcessToFinish (5000);

        return { ! timedOut && ! process.isRunning() && process.getExitCode() == 0, timedOut, output };
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

    juce::String builtFrom()
    {
        return CDAW_PLUGINS_COMMIT;
    }

    Status check (const juce::File& sourceFolder, const juce::String& compiledFrom)
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

        // A stalled connection gives up after 20 seconds, well inside the deadline.
        auto fetched = git (plugins, { "-c", "http.lowSpeedLimit=1000", "-c", "http.lowSpeedTime=20",
                                       "fetch", "--quiet", "origin", "main" }, networkTimeoutMs);
        if (! fetched.ok)
            return failed ("Couldn't get the latest plugins from GitHub. " + fetched.output);

        const auto base = compiledFrom.isNotEmpty() ? compiledFrom : juce::String ("HEAD");
        auto current = git (plugins, { "rev-parse", "--verify", "--quiet", base + "^{commit}" });
        auto main = git (plugins, { "rev-parse", "FETCH_HEAD" });
        if (! main.ok)
            return failed (main.output);

        Status s;
        s.latest = main.output.substring (0, 7);
        s.current = current.ok ? current.output.substring (0, 7) : base.substring (0, 7);

        if (current.ok && current.output == main.output)
        {
            s.state = Status::State::upToDate;
            return s;
        }

        auto onMain = [&] { return current.ok && git (plugins, { "merge-base", "--is-ancestor", current.output, "FETCH_HEAD" }).ok; };

        // A shallow copy (as scripts\bootstrap.ps1 makes) can hold this CDAW's commit and main's as
        // unconnected pieces of history, or not hold this CDAW's commit at all: fetch the rest once.
        if (! onMain() && git (plugins, { "rev-parse", "--is-shallow-repository" }).output == "true")
        {
            auto deepened = git (plugins, { "fetch", "--quiet", "--unshallow", "origin", "main" }, networkTimeoutMs);
            if (! deepened.ok)
                return failed ("Couldn't get the plugins' history from GitHub. " + deepened.output);

            current = git (plugins, { "rev-parse", "--verify", "--quiet", base + "^{commit}" });
            main = git (plugins, { "rev-parse", "FETCH_HEAD" });
        }

        if (! onMain())
        {
            s.state = Status::State::notOnMain;
            return s;
        }

        auto log = git (plugins, { "log", "--format=%s", current.output + "..FETCH_HEAD" });
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
