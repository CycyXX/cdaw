#include "ProjectSession.h"

namespace cdaw
{
    ProjectSession::ProjectSession (te::Engine& e, const juce::File& file, const juce::File& recoverFrom)
        : engine (e), editFile (file)
    {
        ensureFolders();
        const bool recovering = recoverFrom.existsAsFile();
        const bool isNew = ! editFile.existsAsFile() && ! recovering;

        juce::ValueTree state;

        if (recovering)
            state = te::loadEditFromFile (engine, recoverFrom, te::ProjectItemID::createNewID ({}));
        else if (isNew)
            state = te::createEmptyEdit (engine);
        else
            state = te::loadEditFromFile (engine, editFile, te::ProjectItemID::createNewID ({}));

        if (! state.isValid())
        {
            juce::Logger::writeToLog ("Could not read project: " + editFile.getFullPathName());
            return;
        }

        auto id = te::ProjectItemID::fromProperty (state, te::IDs::projectID);
        if (! id.isValid())
            id = te::ProjectItemID::createNewID ({});

        te::Edit::Options options { engine, state, te::ProjectItemRef (id) };
        options.role = te::Edit::forEditing;
        options.numUndoLevelsToStore = 200;
        options.editFileRetriever = [this] { return editFile; };

        // Relative source paths are stored relative to the edit file (see
        // SourceFileReference::findPathFromFile); resolve them the same way. Without
        // this, Tracktion would look them up through its ProjectManager, which CDAW
        // doesn't use.
        options.filePathResolver = [this] (const juce::String& path) -> juce::File
        {
            if (juce::File::isAbsolutePath (path))
                return juce::File (path);

            return editFile.getChildFile (path);
        };
        options.numAudioTracks = isNew ? 1 : 0;

        edit = te::Edit::createEdit (std::move (options));

        if (edit != nullptr)
        {
            configureEdit (isNew);
            viewState = std::make_unique<EditViewState> (*edit, selectionManager);
            startTimer (60 * 1000);   // autosave

            if (recovering)
            {
                edit->markAsChanged();
                juce::Logger::writeToLog ("Recovered project from " + recoverFrom.getFullPathName());
            }
        }
    }

    ProjectSession::~ProjectSession()
    {
        stopTimer();
        viewState.reset();

        if (edit != nullptr)
        {
            edit->getTransport().stop (false, true);
            selectionManager.deselectAll();
        }

        edit.reset();
    }

    void ProjectSession::configureEdit (bool isNew)
    {
        edit->alwaysUseRelativePaths = true;
        edit->playInStopEnabled = true;
        edit->setTempDirectory (getProjectFolder().getChildFile ("Cache"));

        if (isNew)
        {
            edit->tempoSequence.getTempo (0)->setBpm (120.0);
            edit->clickTrackEmphasiseBars = true;

            if (auto first = te::getFirstAudioTrack (*edit))
                first->setName ("Audio 1");

            edit->resetChangedStatus();
        }

        edit->getTransport().ensureContextAllocated();
    }

    void ProjectSession::ensureFolders()
    {
        getProjectFolder().createDirectory();
        getAudioFolder().createDirectory();
        getBouncesFolder().createDirectory();
        getMidiFolder().createDirectory();
        getBackupsFolder().createDirectory();
    }

    juce::File ProjectSession::getAudioFolder() const   { return getProjectFolder().getChildFile ("Audio"); }
    juce::File ProjectSession::getBouncesFolder() const { return getProjectFolder().getChildFile ("Bounces"); }
    juce::File ProjectSession::getMidiFolder() const    { return getProjectFolder().getChildFile ("MIDI"); }
    juce::File ProjectSession::getBackupsFolder() const { return getProjectFolder().getChildFile ("Backups"); }

    bool ProjectSession::save()
    {
        if (edit == nullptr)
            return false;

        ensureFolders();
        writeBackup();

        te::EditFileOperations ops (*edit);
        bool ok = ops.writeToFile (editFile, false);

        if (ok)
        {
            ops.deleteTempVersion();
            edit->resetChangedStatus();
            engine.getEngineBehaviour().editHasBeenSaved (*edit, editFile);
        }

        juce::Logger::writeToLog ((ok ? "Saved " : "FAILED to save ") + editFile.getFullPathName());
        return ok;
    }

    bool ProjectSession::saveAs (const juce::File& newEditFile)
    {
        if (edit == nullptr)
            return false;

        // Make every source path absolute, move the edit, then make paths relative to
        // the new location. Existing audio stays where it is; new recordings go to the
        // new project's Audio folder.
        te::convertAllSourcePaths (*edit, false);
        editFile = newEditFile.withFileExtension (te::editFileSuffix);
        ensureFolders();
        edit->setTempDirectory (getProjectFolder().getChildFile ("Cache"));
        te::convertAllSourcePaths (*edit, true);

        return save();
    }

    void ProjectSession::autosave()
    {
        if (edit == nullptr || edit->getTransport().isRecording())
            return;

        if (edit->hasChangedSinceSaved())
            te::EditFileOperations (*edit).saveTempVersion (false);
    }

    void ProjectSession::writeBackup()
    {
        if (! editFile.existsAsFile())
            return;

        auto folder = getBackupsFolder();
        auto name = getName() + "_" + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S");
        editFile.copyFileTo (folder.getNonexistentChildFile (name, te::editFileSuffix, false));

        auto backups = folder.findChildFiles (juce::File::findFiles, false, "*" + juce::String (te::editFileSuffix));
        std::sort (backups.begin(), backups.end(), [] (const juce::File& a, const juce::File& b)
                   { return a.getLastModificationTime() > b.getLastModificationTime(); });

        for (int i = 10; i < backups.size(); ++i)
            backups.getReference (i).deleteFile();
    }

    juce::File ProjectSession::findNewerAutosave (const juce::File& file)
    {
        auto tmp = te::EditFileOperations::getTempVersionOfEditFile (file);

        if (tmp.existsAsFile() && tmp.getSize() > 0
             && (! file.existsAsFile() || tmp.getLastModificationTime() > file.getLastModificationTime()))
            return tmp;

        return {};
    }

    void ProjectSession::discardAutosave()
    {
        if (edit != nullptr)
            te::EditFileOperations (*edit).deleteTempVersion();
        else
            te::EditFileOperations::getTempVersionOfEditFile (editFile).deleteFile();
    }

    juce::File ProjectSession::createProjectFolder (const juce::File& parent, const juce::String& name)
    {
        auto legal = juce::File::createLegalFileName (name.trim().isEmpty() ? juce::String ("Untitled") : name.trim());
        auto folder = parent.getNonexistentChildFile (legal, {}, false);
        folder.createDirectory();
        return folder.getChildFile (folder.getFileName() + te::editFileSuffix);
    }
}
