#pragma once

#include "../PCH.h"
#include "EditViewState.h"

namespace cdaw
{
    /** One open project: a folder holding <Name>.tracktionedit plus Audio/, Bounces/, MIDI/,
        Backups/ and Cache/. Owns the te::Edit and its SelectionManager. */
    class ProjectSession : private juce::Timer
    {
    public:
        /** Opens an existing .tracktionedit, or creates a new project if the file doesn't exist.
            If recoverFrom is given (an autosave), the project is loaded from it instead and marked unsaved. */
        ProjectSession (te::Engine&, const juce::File& editFile, const juce::File& recoverFrom = {});

        /** The autosave sibling (.tmp_<Name>) if it's newer than the project file. */
        static juce::File findNewerAutosave (const juce::File& editFile);
        void discardAutosave();
        ~ProjectSession();

        bool isValid() const                    { return edit != nullptr; }
        te::Edit& getEdit()                     { return *edit; }
        te::SelectionManager& getSelectionManager() { return selectionManager; }
        te::TransportControl& getTransport()    { return edit->getTransport(); }
        EditViewState& getViewState()           { return *viewState; }

        juce::File getEditFile() const          { return editFile; }
        juce::File getProjectFolder() const     { return editFile.getParentDirectory(); }
        juce::File getAudioFolder() const;
        juce::File getBouncesFolder() const;
        juce::File getMidiFolder() const;
        juce::File getBackupsFolder() const;
        juce::String getName() const            { return editFile.getFileNameWithoutExtension(); }

        bool hasChangedSinceSaved() const       { return edit != nullptr && edit->hasChangedSinceSaved(); }

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
        std::unique_ptr<te::Edit> edit;
        te::SelectionManager selectionManager { engine };
        std::unique_ptr<EditViewState> viewState;

        void timerCallback() override           { autosave(); }

        void configureEdit (bool isNew);
        void writeBackup();
        void ensureFolders();
    };
}
