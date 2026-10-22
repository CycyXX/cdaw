#pragma once

#include "../PCH.h"
#include "EditViewState.h"
#include "../Engine/MasterAnalyser.h"

namespace cdaw
{
    /** One open project: a folder holding <Name>.cdaw plus Audio/, Bounces/, MIDI/,
        Backups/ and Cache/. Owns the te::Edit and its SelectionManager.

        The .cdaw file is Tracktion's edit XML under CDAW's own extension. Projects saved by
        earlier CDAW builds as <Name>.tracktionedit are converted when opened: the project is
        saved as <Name>.cdaw and the old file is moved into Backups/. */
    class ProjectSession : private juce::Timer
    {
    public:
        static constexpr const char* projectFileSuffix = ".cdaw";
        static constexpr const char* legacyProjectFileSuffix = ".tracktionedit";

        /** True for .cdaw files and for .tracktionedit files from earlier builds. */
        static bool isProjectFile (const juce::File&);

        /** File-chooser pattern matching both. */
        static juce::String getProjectFilePattern()     { return "*.cdaw;*.tracktionedit"; }

        /** Opens an existing project file, or creates a new project if the file doesn't exist.
            If recoverFrom is given (an autosave), the project is loaded from it instead and marked unsaved. */
        ProjectSession (te::Engine&, const juce::File& editFile, const juce::File& recoverFrom = {});

        /** The autosave sibling (.tmp_<Name>) if it's newer than the project file. */
        static juce::File findNewerAutosave (const juce::File& editFile);

        /** Resolves a relative source path stored in the edit (both Tracktion styles). */
        static juce::File resolveRelativePath (const juce::File& editFile, const juce::String& path);
        void discardAutosave();
        ~ProjectSession();

        bool isValid() const                    { return edit != nullptr; }
        te::Edit& getEdit()                     { return *edit; }
        te::SelectionManager& getSelectionManager() { return selectionManager; }
        te::TransportControl& getTransport()    { return edit->getTransport(); }
        EditViewState& getViewState()           { return *viewState; }
        MasterAnalyser& getAnalyser()           { return *analyser; }

        juce::File getEditFile() const          { return editFile; }
        juce::File getProjectFolder() const     { return editFile.getParentDirectory(); }
        juce::File getAudioFolder() const;
        juce::File getBouncesFolder() const;
        juce::File getMidiFolder() const;
        juce::File getBackupsFolder() const;
        juce::String getName() const            { return editFile.getFileNameWithoutExtension(); }

        /** Audio files that had moved and were found again when the project opened. */
        int getReconnectedFileCount() const     { return reconnectedFiles; }

        bool hasChangedSinceSaved() const       { return edit != nullptr && edit->hasChangedSinceSaved(); }

        /** Stops a recording in progress and keeps the takes. Tracktion only adds recorded clips
            when the transport stops, so call this before asking whether to save (otherwise a
            take made while quitting or opening another project never reaches the file).
            Returns true if it was recording. */
        bool stopRecordingKeepingTakes();

        /** Whether a project file (or the autosave to recover from) can be read, without loading
            its plugins. A file that doesn't exist yet counts as readable (a new project). */
        /** The project file a "Save As" should write for what the user picked in the file dialog.
            An existing project file is overwritten in place (they confirmed that). A new name
            makes a new project folder next to the other projects: if the dialog was inside a
            project's own folder, beside that folder rather than nested in it. */
        static juce::File resolveSaveAsTarget (const juce::File& chosen);

        static bool isReadable (te::Engine&, const juce::File& editFile, const juce::File& recoverFrom = {});

        /** Saves to the edit file and writes a rotating backup. */
        bool save();

        /** Writes a copy of the project into a new folder (copying audio under the old
            project folder), then continues working in the new location. */
        bool saveAs (const juce::File& newEditFile);

        /** Quick background save to the .tmp_ sibling, used for crash recovery. */
        void autosave();

        /** Creates the folder layout for a new project named <name> inside <parent>. Returns the edit file. */
        static juce::File createProjectFolder (const juce::File& parent, const juce::String& name);

    private:
        te::Engine& engine;
        juce::File editFile;
        int reconnectedFiles = 0;

        std::unique_ptr<te::Edit> edit;
        te::SelectionManager selectionManager { engine };
        std::unique_ptr<EditViewState> viewState;
        std::unique_ptr<MasterAnalyser> analyser;

        void timerCallback() override           { autosave(); }

        void configureEdit (bool isNew);
        void writeBackup();
        void retireLegacyFile();
        void ensureFolders();
    };
}
