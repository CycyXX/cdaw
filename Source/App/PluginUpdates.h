#pragma once

#include "../PCH.h"

/** Plugins > Check for Plugin Updates: whether cdaw-plugins has changes this CDAW doesn't have yet,
    and updating to them. CDAW compiles the plugins into itself, so updating means rebuilding CDAW
    from its source folder, with Update-Plugins.bat. A copy of CDAW without its source can't. */
namespace cdaw::PluginUpdates
{
    /** The CDAW source folder `exe` was built or published in: the nearest folder above it with
        Update-Plugins.bat and the plugins' submodule. None for a copy of CDAW without its source. */
    juce::File findSourceFolder (const juce::File& exe);

    /** The plugins' commit this CDAW was compiled with, recorded at build time. Empty if the build
        couldn't tell. */
    juce::String builtFrom();

    struct Status
    {
        enum class State
        {
            upToDate,       // this CDAW has main's plugins
            newer,          // main has changes this CDAW doesn't: `changes`
            notOnMain,      // this CDAW's plugins are at a commit main doesn't contain, such as a branch
            localChanges,   // the submodule has uncommitted changes, which an update would mix up
            failed          // `error` says why
        };

        State state = State::failed;
        juce::String current, latest;   // short commit IDs
        juce::StringArray changes;      // the newer commits' subjects, newest first
        int moreChanges = 0;            // newer commits beyond those in `changes`
        juce::String error;
    };

    /** Fetches cdaw-plugins' latest main into the submodule of `sourceFolder` and compares it with
        `compiledFrom`, the commit this CDAW was built with (the submodule's own commit if empty). Only
        git's records change, not the files. Runs git and waits for the network: call it off the
        message thread. */
    Status check (const juce::File& sourceFolder, const juce::String& compiledFrom);

    /** Opens a console running Update-Plugins.bat -Restart, which waits for CDAW to close, rebuilds it
        with the latest plugins and starts it again. False if it couldn't be started. */
    bool startUpdate (const juce::File& sourceFolder);

    struct ProcessResult
    {
        bool ok = false;        // started, and exited with 0
        bool timedOut = false;  // killed after `timeoutMs`
        juce::String output;    // stdout and stderr, trimmed
    };

    /** Runs a command and waits up to `timeoutMs` for it, then kills it. How check() runs git. */
    ProcessResult run (const juce::StringArray& command, int timeoutMs);
}
