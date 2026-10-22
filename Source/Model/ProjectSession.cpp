#include "ProjectSession.h"
#include "../Plugins/BuiltInPlugins.h"
#include "MissingMedia.h"

namespace cdaw
{
    juce::File ProjectSession::resolveRelativePath (const juce::File& editFile, const juce::String& path)
    {
        if (juce::File::isAbsolutePath (path))
            return juce::File (path);

        // Tracktion writes relative paths in two styles: relative to the project folder
        // ("Audio/x.wav") and relative to the edit file itself ("../Audio/x.wav").
        auto fromFolder = editFile.getParentDirectory().getChildFile (path);
        auto fromFile = editFile.getChildFile (path);

        if (fromFolder.exists())    return fromFolder;
        if (fromFile.exists())      return fromFile;

        return path.startsWith ("..") ? fromFile : fromFolder;
    }

    bool ProjectSession::isProjectFile (const juce::File& f)
    {
        return f.hasFileExtension (projectFileSuffix) || f.hasFileExtension (legacyProjectFileSuffix);
    }

    ProjectSession::ProjectSession (te::Engine& e, const juce::File& file, const juce::File& recoverFrom)
        : engine (e), editFile (file.withFileExtension (projectFileSuffix))
    {
        // An old .tracktionedit is read once and continues life as <Name>.cdaw. If both exist,
        // the .cdaw is the current one.
        auto sourceFile = editFile.existsAsFile() ? editFile : file;
        const bool migrating = sourceFile != editFile && sourceFile.existsAsFile();

        ensureFolders();
        const bool recovering = recoverFrom.existsAsFile();
        const bool isNew = ! sourceFile.existsAsFile() && ! recovering;

        juce::ValueTree state;

        if (recovering)
            state = te::loadEditFromFile (engine, recoverFrom, te::ProjectItemID::createNewID ({}));
        else if (isNew)
            state = te::createEmptyEdit (engine);
        else
            state = te::loadEditFromFile (engine, sourceFile, te::ProjectItemID::createNewID ({}));

        if (! state.isValid())
        {
            juce::Logger::writeToLog ("Could not read project: " + editFile.getFullPathName());
            return;
        }

        // Plugins from earlier builds become CDAW's own compressor and EQ.
        if (auto converted = BuiltInPlugins::migrateEditState (state); converted > 0)
            juce::Logger::writeToLog ("Converted " + juce::String (converted) + " built-in plugin(s) to CDAW's versions");

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
            return resolveRelativePath (editFile, path);
        };
        options.numAudioTracks = isNew ? 1 : 0;

        edit = te::Edit::createEdit (std::move (options));

        if (edit != nullptr)
        {
            // Audio that moved (a moved or renamed project folder, an old Save As): look for it by
            // name in the project's Audio folder first, then the rest of the project folder.
            if (! isNew)
            {
                reconnectedFiles = MissingMedia::relinkFromFolders (*edit, { getAudioFolder(), getProjectFolder() });

                if (reconnectedFiles > 0)
                {
                    edit->markAsChanged();
                    juce::Logger::writeToLog ("Reconnected " + juce::String (reconnectedFiles) + " audio file(s) that had moved");
                }
            }

            configureEdit (isNew);
            viewState = std::make_unique<EditViewState> (*edit, selectionManager);
            analyser = std::make_unique<MasterAnalyser> (*edit);
            viewState->analyser = analyser.get();
            startTimer (60 * 1000);   // autosave

            if (recovering)
            {
                edit->markAsChanged();
                juce::Logger::writeToLog ("Recovered project from " + recoverFrom.getFullPathName());
            }
            else if (migrating)
            {
                // Write the .cdaw straight away so the project is in the new format even if
                // nothing is changed. A recovered project waits for the user to save instead.
                juce::Logger::writeToLog ("Converting " + sourceFile.getFullPathName() + " to " + projectFileSuffix);
                save();
            }
        }
    }

    ProjectSession::~ProjectSession()
    {
        stopTimer();

        if (viewState != nullptr)
            viewState->analyser = nullptr;

        analyser.reset();
        viewState.reset();

        if (edit != nullptr)
        {
            edit->getTransport().stop (false, true);
            selectionManager.deselectAll();
        }

        edit.reset();
    }

    bool ProjectSession::stopRecordingKeepingTakes()
    {
        if (edit == nullptr || ! edit->getTransport().isRecording())
            return false;

        edit->getTransport().stop (false, false);
        juce::Logger::writeToLog ("Stopped recording (takes kept)");
        return true;
    }

    bool ProjectSession::isReadable (te::Engine& engine, const juce::File& file, const juce::File& recoverFrom)
    {
        auto cdawFile = file.withFileExtension (projectFileSuffix);
        auto source = recoverFrom.existsAsFile() ? recoverFrom
                    : cdawFile.existsAsFile()    ? cdawFile
                                                 : file;

        if (! source.existsAsFile())
            return true;

        return te::loadEditFromFile (engine, source, te::ProjectItemID::createNewID ({})).isValid();
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
            retireLegacyFile();
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
        editFile = newEditFile.withFileExtension (projectFileSuffix);
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
        editFile.copyFileTo (folder.getNonexistentChildFile (name, projectFileSuffix, false));

        auto backups = folder.findChildFiles (juce::File::findFiles, false, "*" + juce::String (projectFileSuffix));
        std::sort (backups.begin(), backups.end(), [] (const juce::File& a, const juce::File& b)
                   { return a.getLastModificationTime() > b.getLastModificationTime(); });

        for (int i = 10; i < backups.size(); ++i)
            backups.getReference (i).deleteFile();
    }

    void ProjectSession::retireLegacyFile()
    {
        auto legacy = editFile.withFileExtension (legacyProjectFileSuffix);

        if (! legacy.existsAsFile())
            return;

        auto dest = getBackupsFolder().getNonexistentChildFile (legacy.getFileNameWithoutExtension() + "_before_cdaw",
                                                                legacyProjectFileSuffix, false);

        if (legacy.moveFileTo (dest))
            juce::Logger::writeToLog ("Moved the old project file to " + dest.getFullPathName());
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

    juce::File ProjectSession::resolveSaveAsTarget (const juce::File& chosen)
    {
        auto file = chosen.withFileExtension (projectFileSuffix);

        if (file.existsAsFile())
            return file;

        auto parent = file.getParentDirectory();

        // Inside a project's own folder (it holds <FolderName>.cdaw): a new project goes beside it.
        if (parent.getChildFile (parent.getFileName() + projectFileSuffix).existsAsFile())
            parent = parent.getParentDirectory();

        return createProjectFolder (parent, file.getFileNameWithoutExtension());
    }

    juce::File ProjectSession::createProjectFolder (const juce::File& parent, const juce::String& name)
    {
        auto legal = juce::File::createLegalFileName (name.trim().isEmpty() ? juce::String ("Untitled") : name.trim());
        auto folder = parent.getNonexistentChildFile (legal, {}, false);
        folder.createDirectory();
        return folder.getChildFile (folder.getFileName() + projectFileSuffix);
    }
}
