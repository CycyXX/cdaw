#include "MainComponent.h"
#include "AppContext.h"
#include "../Commands/CommandIDs.h"
#include "../Engine/AudioDeviceSetup.h"
#include "../Model/ClipOps.h"
#include "../UI/Project/MissingFilesDialog.h"
#include "../Model/DawProject.h"
#include "../Model/ProjectSession.h"
#include "../Model/TrackOps.h"
#include "../UI/Arrange/ArrangeView.h"
#include "../UI/Inspector/InspectorPanel.h"
#include "../UI/LowerZone.h"
#include "../Render/ExportService.h"
#include "../UI/Plugins/PluginMenus.h"
#include "../UI/Common/StatusBar.h"
#include "../UI/LookAndFeel/Palette.h"
#include "../UI/Transport/TransportBar.h"

namespace cdaw
{
    namespace
    {
        struct NoProjectPlaceholder : public juce::Component
        {
            void paint (juce::Graphics& g) override
            {
                g.fillAll (Palette::laneBackground);
                g.setColour (Palette::textDim);
                g.setFont (juce::FontOptions (18.0f));
                g.drawText ("No project open. Use File > New Project or Open Project.", getLocalBounds(), juce::Justification::centred);
            }
        };
    }

    MainComponent::MainComponent()
    {
        auto& ctx = AppContext::get();

        statusBar = std::make_unique<StatusBar> (ctx.engine());
        statusBar->onDeviceClicked = [&ctx] { AudioDeviceSetup::showAudioSettings (ctx.engine()); };
        ctx.postStatus = [sb = juce::Component::SafePointer<StatusBar> (statusBar.get())] (const juce::String& m)
        {
            if (sb != nullptr)
                sb->showMessage (m);
        };

        addAndMakeVisible (menuBar);
        addAndMakeVisible (*statusBar);

        ctx.commandManager.registerAllCommandsForTarget (this);
        ctx.commandManager.setFirstCommandTarget (this);

        if (auto keys = ctx.settings.getXml (SettingKeys::keyMappings))
            ctx.commandManager.getKeyMappings()->restoreFromXml (*keys);

        ctx.commandManager.getKeyMappings()->addChangeListener (this);
        setWantsKeyboardFocus (true);
        lowerZoneHeight = ctx.settings.getInt (SettingKeys::lowerZoneHeight, 460);

        sessionChanged();
        setSize (1500, 900);
        startTimer (500);
    }

    MainComponent::~MainComponent()
    {
        auto& ctx = AppContext::get();
        ctx.commandManager.getKeyMappings()->removeChangeListener (this);
        ctx.commandManager.setFirstCommandTarget (nullptr);
        ctx.postStatus = nullptr;
        PluginMenus::closePluginManager();
        inspector.reset();
        lowerZone.reset();
        arrangeView.reset();
        transportBar.reset();
    }

    ProjectSession* MainComponent::getSession() const
    {
        auto& s = AppContext::get().session;
        return s != nullptr && s->isValid() ? s.get() : nullptr;
    }

    void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        auto& ctx = AppContext::get();

        if (auto xml = ctx.commandManager.getKeyMappings()->createXml (true))
            ctx.settings.setXml (SettingKeys::keyMappings, xml.get());
    }

    void MainComponent::timerCallback()
    {
        updateTitle();
        AppContext::get().commandManager.commandStatusChanged();
    }

    void MainComponent::paint (juce::Graphics& g)
    {
        g.fillAll (Palette::windowBackground);
    }

    void MainComponent::resized()
    {
        auto r = getLocalBounds();
        menuBar.setBounds (r.removeFromTop (24));
        statusBar->setBounds (r.removeFromBottom (24));

        if (transportBar != nullptr)
            transportBar->setBounds (r.removeFromTop (54));

        if (lowerZone != nullptr)
        {
            lowerZone->setVisible (showLowerZone);
            if (showLowerZone)
                lowerZone->setBounds (r.removeFromBottom (juce::jlimit (120, juce::jmax (120, r.getHeight() - 150), lowerZoneHeight)));
        }

        if (inspector != nullptr)
        {
            inspector->setVisible (showInspector);
            if (showInspector)
                inspector->setBounds (r.removeFromLeft (270));
        }

        if (arrangeView != nullptr)
            arrangeView->setBounds (r);

        if (placeholder != nullptr)
            placeholder->setBounds (r);
    }

    void MainComponent::sessionChanged()
    {
        inspector.reset();
        lowerZone.reset();
        arrangeView.reset();
        transportBar.reset();
        placeholder.reset();

        if (auto* s = getSession())
        {
            auto& ctx = AppContext::get();
            transportBar = std::make_unique<TransportBar> (s->getEdit(), s->getViewState(), ctx.commandManager);
            arrangeView = std::make_unique<ArrangeView> (*s, s->getViewState());
            arrangeView->onTrackCommand = [this] (int cmd) { performTrackCommand (cmd); };
            arrangeView->onOpenClipEditor = [this] (te::Clip& clip)
            {
                if (auto mc = dynamic_cast<te::MidiClip*> (&clip))
                {
                    lowerZone->editClip (*mc);
                    setLowerZoneVisible (true, (int) LowerZone::Page::editor);
                }
            };

            inspector = std::make_unique<InspectorPanel> (s->getViewState());
            lowerZone = std::make_unique<LowerZone> (s->getViewState());
            lowerZone->onResizeRequest = [this] (int h)
            {
                lowerZoneHeight = h;
                AppContext::get().settings.setInt (SettingKeys::lowerZoneHeight, lowerZoneHeight);
                resized();
            };
            lowerZone->onClose = [this] { setLowerZoneVisible (false, -1); };
            lowerZone->onTrackMenu = [this] (te::Track& t) { if (arrangeView) arrangeView->showTrackMenu (t); };
            inspector->onDrumMapChanged = [this] (te::Track& t) { if (lowerZone) lowerZone->drumMapChanged (t); };
            addChildComponent (*lowerZone);

            addAndMakeVisible (*transportBar);
            addAndMakeVisible (*arrangeView);
            addAndMakeVisible (*inspector);

            // Bottom panel: the mixer is open by default; remember the last choice.
            auto& settings = AppContext::get().settings;
            showLowerZone = settings.getBool (SettingKeys::lowerZoneVisible, true);
            lowerZone->showPage ((LowerZone::Page) settings.getInt (SettingKeys::lowerZonePage, (int) LowerZone::Page::mixer));
        }
        else
        {
            placeholder = std::make_unique<NoProjectPlaceholder>();
            addAndMakeVisible (*placeholder);
        }

        resized();
        updateTitle();
        menuItemsChanged();
        AppContext::get().commandManager.commandStatusChanged();
        grabKeyboardFocus();
    }

    void MainComponent::updateTitle()
    {
        if (auto* w = findParentComponentOfClass<juce::DocumentWindow>())
        {
            juce::String title = "CDAW";

            if (auto* s = getSession())
                title = s->getName() + (s->hasChangedSinceSaved() ? " *" : "") + " - CDAW";

            if (title != lastTitle)
            {
                lastTitle = title;
                w->setName (title);
            }
        }
    }

    //==============================================================================
    void MainComponent::openProjectChecked (const juce::File& editFile)
    {
        auto autosave = ProjectSession::findNewerAutosave (editFile);

        if (autosave == juce::File())
        {
            openProject (editFile);
            return;
        }

        auto options = juce::MessageBoxOptions::makeOptionsYesNo (juce::MessageBoxIconType::QuestionIcon,
            "Recover unsaved changes?",
            "\"" + editFile.getFileNameWithoutExtension() + "\" has an autosave from "
              + autosave.getLastModificationTime().toString (true, true, false)
              + " that is newer than the saved project (CDAW may have closed unexpectedly).\n\n"
                "Open the recovered version? You can then save it, or close without saving to go back.",
            "Recover", "Open last save", this);

        juce::AlertWindow::showAsync (options, [this, editFile, autosave] (int result)
        {
            if (result == 1)
            {
                openProject (editFile, autosave);
                statusBar->showMessage ("Recovered unsaved changes - save to keep them");
            }
            else
            {
                autosave.deleteFile();
                openProject (editFile);
            }
        });
    }

    void MainComponent::openProject (const juce::File& editFile, const juce::File& recoverFrom)
    {
        auto& ctx = AppContext::get();

        // Check the file can be read before closing the current project, so a damaged file
        // doesn't leave an empty window behind.
        if (! ProjectSession::isReadable (ctx.engine(), editFile, recoverFrom))
        {
            auto backups = editFile.getParentDirectory().getChildFile ("Backups");
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Open Project",
                                                    "Could not read " + editFile.getFullPathName() + "."
                                                      + (backups.isDirectory() ? "\n\nEarlier versions are in " + backups.getFullPathName() : juce::String()),
                                                    "OK", this);
            juce::Logger::writeToLog ("FAILED to read project " + editFile.getFullPathName());
            return;
        }

        inspector.reset();
        lowerZone.reset();
        arrangeView.reset();
        transportBar.reset();
        ClipOps::cancelRunningImport();
        ctx.session.reset();

        auto session = std::make_unique<ProjectSession> (ctx.engine(), editFile, recoverFrom);

        if (! session->isValid())
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Open Project",
                                                    "Could not open " + editFile.getFullPathName());
            ctx.settings.removeRecentProject (editFile);
            juce::Logger::writeToLog ("FAILED to open project " + editFile.getFullPathName());
        }
        else
        {
            // An old .tracktionedit continues as <Name>.cdaw, so remember the new file.
            if (session->getEditFile() != editFile)
                ctx.settings.removeRecentProject (editFile);

            ctx.session = std::move (session);
            ctx.settings.addRecentProject (ctx.session->getEditFile());
            statusBar->showMessage ("Opened " + editFile.getFullPathName());
            juce::Logger::writeToLog ("Opened project " + editFile.getFullPathName());

            // At startup the window isn't showing yet: Main checks once it is.
            if (isShowing())
                juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this)]
                                                 { if (safe != nullptr) safe->checkProjectHealth(); });
        }

        sessionChanged();
    }

    void MainComponent::confirmDiscardOrSave (std::function<void (bool)> onDone)
    {
        auto* s = getSession();

        // A take being recorded only becomes part of the project when recording stops.
        if (s != nullptr && s->stopRecordingKeepingTakes())
            statusBar->showMessage ("Recording stopped");

        if (s == nullptr || ! s->hasChangedSinceSaved())
        {
            onDone (true);
            return;
        }

        auto options = juce::MessageBoxOptions::makeOptionsYesNoCancel (juce::MessageBoxIconType::QuestionIcon,
                                                                        "Save changes?",
                                                                        "Save changes to \"" + s->getName() + "\" first?",
                                                                        "Save", "Don't save", "Cancel", this);

        juce::AlertWindow::showAsync (options, [this, onDone] (int result)
        {
            if (result == 1)       saveProject (onDone);
            else if (result == 2)  { if (auto* sess = getSession()) sess->discardAutosave(); onDone (true); }
            else                   onDone (false);
        });
    }

    void MainComponent::newProjectDialog()
    {
        confirmDiscardOrSave ([this] (bool ok)
        {
            if (! ok)
                return;

            auto* aw = new juce::AlertWindow ("New Project", "Project name:", juce::MessageBoxIconType::NoIcon, this);
            aw->addTextEditor ("name", "Untitled");
            aw->addButton ("Create", 1, juce::KeyPress (juce::KeyPress::returnKey));
            aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

            aw->enterModalState (true, juce::ModalCallbackFunction::create ([this, aw] (int result)
            {
                if (result == 1)
                {
                    auto& ctx = AppContext::get();
                    auto folder = ctx.settings.getProjectsFolder();
                    folder.createDirectory();
                    auto file = ProjectSession::createProjectFolder (folder, aw->getTextEditorContents ("name"));
                    openProject (file);

                    if (auto* s = getSession())
                        s->save();
                }
            }), true);
        });
    }

    void MainComponent::openProjectDialog()
    {
        confirmDiscardOrSave ([this] (bool ok)
        {
            if (! ok)
                return;

            auto& ctx = AppContext::get();
            chooser = std::make_unique<juce::FileChooser> ("Open Project", ctx.settings.getProjectsFolder(),
                                                           ProjectSession::getProjectFilePattern());
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this] (const juce::FileChooser& fc)
                                  {
                                      if (fc.getResult().existsAsFile())
                                          openProjectChecked (fc.getResult());
                                  });
        });
    }

    void MainComponent::showSaveFailed (const juce::File& file)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Save failed",
                                                "Could not write " + file.getFullPathName() + ".\n\n"
                                                "The disk may be full, the folder read-only, or the file locked by another program "
                                                "(e.g. a sync tool or virus scanner). Your changes are still open: try again, or use Save As.",
                                                "OK", this);
    }

    void MainComponent::checkProjectHealth()
    {
        auto* s = getSession();

        if (s == nullptr)
            return;

        if (s->getReconnectedFileCount() > 0)
            statusBar->showMessage ("Reconnected " + juce::String (s->getReconnectedFileCount()) + " audio file(s) that had moved");

        // Things the project needs that aren't there: say so now rather than leaving silent clips.
        MissingFilesDialog::show (*s);
    }

    void MainComponent::saveProject (std::function<void (bool)> onDone)
    {
        auto* s = getSession();
        bool ok = s != nullptr && s->save();
        statusBar->showMessage (ok ? "Saved " + s->getName() : juce::String ("Save failed - see the log"));
        updateTitle();

        if (! ok && s != nullptr)
            showSaveFailed (s->getEditFile());

        if (onDone)
            onDone (ok);
    }

    void MainComponent::saveProjectAsDialog (std::function<void (bool)> onDone)
    {
        auto* s = getSession();

        if (s == nullptr)
            return;

        auto& ctx = AppContext::get();
        chooser = std::make_unique<juce::FileChooser> ("Save Project As (choose a new project name)",
                                                       ctx.settings.getProjectsFolder().getChildFile (s->getName() + " copy"),
                                                       "*" + juce::String (ProjectSession::projectFileSuffix));
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this, onDone] (const juce::FileChooser& fc)
                              {
                                  auto result = fc.getResult();
                                  bool ok = false;

                                  if (result != juce::File())
                                      if (auto* sess = getSession())
                                      {
                                          auto file = ProjectSession::resolveSaveAsTarget (result);
                                          ok = file == sess->getEditFile() ? sess->save() : sess->saveAs (file);
                                          updateTitle();
                                          statusBar->showMessage (ok ? "Saved as " + file.getFullPathName() : juce::String ("Save As failed"));

                                          if (! ok)
                                              showSaveFailed (file);
                                      }

                                  if (onDone)
                                      onDone (ok);
                              });
    }

    void MainComponent::importAudioDialog()
    {
        auto* s = getSession();
        if (s == nullptr)
            return;

        chooser = std::make_unique<juce::FileChooser> ("Import Audio", juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                       "*.wav;*.wave;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                | juce::FileBrowserComponent::canSelectMultipleItems,
                              [this] (const juce::FileChooser& fc)
                              {
                                  auto* sess = getSession();
                                  if (sess == nullptr || fc.getResults().isEmpty())
                                      return;

                                  auto* track = arrangeView != nullptr ? dynamic_cast<te::AudioTrack*> (arrangeView->getFirstSelectedTrack()) : nullptr;
                                  auto copy = AppContext::get().settings.getBool (SettingKeys::copyImportedFiles, true);
                                  auto clips = ClipOps::importAudioFiles (*sess, fc.getResults(), track, sess->getTransport().getPosition(), copy);
                                  statusBar->showMessage ("Imported " + juce::String (clips.size()) + " file(s)");
                              });
    }

    void MainComponent::importMidiDialog()
    {
        if (getSession() == nullptr)
            return;

        chooser = std::make_unique<juce::FileChooser> ("Import MIDI", juce::File::getSpecialLocation (juce::File::userMusicDirectory), "*.mid;*.midi");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
        {
            auto* sess = getSession();
            auto file = fc.getResult();

            if (sess == nullptr || ! file.existsAsFile())
                return;

            auto& edit = sess->getEdit();
            edit.getUndoManager().beginNewTransaction ("Import MIDI");

            auto* target = arrangeView != nullptr ? dynamic_cast<te::AudioTrack*> (arrangeView->getFirstSelectedTrack()) : nullptr;
            if (target == nullptr || TrackOps::getKind (*target) != TrackKind::instrument)
                target = TrackOps::addInstrumentTrack (edit, &sess->getSelectionManager(), nullptr,
                                                       arrangeView != nullptr ? arrangeView->getFirstSelectedTrack() : nullptr).get();

            if (target != nullptr)
            {
                if (auto mc = te::createClipFromFile (file, *target, false))
                {
                    mc->setStart (sess->getTransport().getPosition(), false, true);
                    statusBar->showMessage ("Imported " + file.getFileName() + " onto " + target->getName());
                }
                else
                {
                    statusBar->showMessage ("Could not read " + file.getFileName());
                }
            }
        });
    }

    void MainComponent::exportMidiDialog()
    {
        auto* s = getSession();
        if (s == nullptr)
            return;

        chooser = std::make_unique<juce::FileChooser> ("Export MIDI", s->getMidiFolder().getChildFile (s->getName() + ".mid"), "*.mid");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this] (const juce::FileChooser& fc)
        {
            auto* sess = getSession();
            auto dest = fc.getResult();

            if (sess == nullptr || dest == juce::File())
                return;

            juce::Array<te::MidiClip*> clips;
            for (auto c : ClipOps::getSelectedClips (sess->getSelectionManager()))
                if (auto mc = dynamic_cast<te::MidiClip*> (c))
                    clips.add (mc);

            auto ok = ExportService::exportMidi (sess->getEdit(), clips, dest.withFileExtension (".mid"));
            statusBar->showMessage (ok ? "Exported " + dest.getFileName() + (clips.isEmpty() ? " (all MIDI clips)" : " (selected clips)")
                                       : juce::String ("No MIDI clips to export"));
        });
    }

    static void showDawProjectReport (juce::Component* parent, const juce::String& title, const DawProject::Report& report)
    {
        juce::AlertWindow::showMessageBoxAsync (report.notes.isEmpty() ? juce::MessageBoxIconType::InfoIcon : juce::MessageBoxIconType::WarningIcon,
                                                title, report.summary(), "OK", parent);
    }

    void MainComponent::importDawProjectDialog()
    {
        confirmDiscardOrSave ([this] (bool ok)
        {
            if (! ok)
                return;

            chooser = std::make_unique<juce::FileChooser> ("Import DAWproject", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
                                                           "*" + juce::String (DawProject::fileExtension));
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this] (const juce::FileChooser& fc)
            {
                auto source = fc.getResult();
                if (! source.existsAsFile())
                    return;

                // A new CDAW project next to the others, named after the file.
                auto& ctx = AppContext::get();
                ctx.settings.getProjectsFolder().createDirectory();
                auto file = ProjectSession::createProjectFolder (ctx.settings.getProjectsFolder(), source.getFileNameWithoutExtension());
                openProject (file);

                auto* sess = getSession();
                if (sess == nullptr)
                    return;

                DawProject::Report report;
                auto result = DawProject::importInto (sess->getEdit(), source, sess->getAudioFolder().getChildFile ("Imported"), report);
                sess->getEdit().getUndoManager().clearUndoHistory();
                sess->save();
                updateTitle();

                if (result.failed())
                {
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Import DAWproject", result.getErrorMessage(), "OK", this);
                    return;
                }

                statusBar->showMessage ("Imported " + source.getFileName() + " as " + file.getFullPathName());
                showDawProjectReport (this, "Imported " + source.getFileName(), report);
            });
        });
    }

    void MainComponent::exportDawProjectDialog()
    {
        auto* s = getSession();
        if (s == nullptr)
            return;

        chooser = std::make_unique<juce::FileChooser> ("Export DAWproject", s->getProjectFolder().getChildFile (s->getName() + DawProject::fileExtension),
                                                       "*" + juce::String (DawProject::fileExtension));
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this] (const juce::FileChooser& fc)
        {
            auto* sess = getSession();
            auto dest = fc.getResult();

            if (sess == nullptr || dest == juce::File())
                return;

            dest = dest.withFileExtension (DawProject::fileExtension);
            DawProject::Report report;
            auto result = DawProject::exportTo (sess->getEdit(), dest, report);

            if (result.failed())
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Export DAWproject", result.getErrorMessage(), "OK", this);
                return;
            }

            statusBar->showMessage ("Exported " + dest.getFullPathName());
            showDawProjectReport (this, "Exported " + dest.getFileName(), report);
        });
    }

    void MainComponent::openPluginFromFileDialog()
    {
        if (getSession() == nullptr)
            return;

        chooser = std::make_unique<juce::FileChooser> ("Open VST3 plugin", juce::File ("C:\\Program Files\\Common Files\\VST3"), "*.vst3");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                | juce::FileBrowserComponent::canSelectDirectories,
                              [this] (const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            auto* s = getSession();

            if (file == juce::File() || s == nullptr)
                return;

            juce::OwnedArray<juce::PluginDescription> types;

            for (auto* format : AppContext::get().engine().getPluginManager().pluginFormatManager.getFormats())
                if (format->getName() == "VST3")
                    format->findAllTypesForFile (types, file.getFullPathName());

            if (types.isEmpty())
            {
                statusBar->showMessage ("No VST3 plugin found in " + file.getFileName());
                return;
            }

            // Tracktion only instantiates plugins present in the known-plugin list.
            AppContext::get().engine().getPluginManager().knownPluginList.addType (*types[0]);

            auto& edit = s->getEdit();
            auto plugin = edit.getPluginCache().createNewPlugin (te::ExternalPlugin::xmlTypeName, *types[0]);

            if (plugin == nullptr)
            {
                statusBar->showMessage ("Could not load " + types[0]->name);
                return;
            }

            auto* track = arrangeView != nullptr ? arrangeView->getFirstSelectedTrack() : nullptr;
            if (track == nullptr)
                track = te::getFirstAudioTrack (edit);

            if (track != nullptr)
            {
                edit.getUndoManager().beginNewTransaction ("Insert plugin");
                track->pluginList.insertPlugin (plugin, 0, nullptr);
            }

            plugin->showWindowExplicitly();
            statusBar->showMessage ("Loaded " + types[0]->name + (track != nullptr ? " on " + track->getName() : juce::String()));
        });
    }

    void MainComponent::addInstrumentTrackMenu()
    {
        auto* s = getSession();
        if (s == nullptr)
            return;

        auto& pm = AppContext::get().engine().getPluginManager();
        juce::PopupMenu m;

        juce::Array<juce::PluginDescription> instruments;
        for (auto& d : pm.knownPluginList.getTypes())
            if (d.isInstrument)
                instruments.add (d);

        std::sort (instruments.begin(), instruments.end(), [] (const auto& a, const auto& b) { return a.name.compareIgnoreCase (b.name) < 0; });

        auto addTrackWith = [this] (juce::PluginDescription desc)
        {
            if (auto* sess = getSession())
            {
                auto& edit = sess->getEdit();
                edit.getUndoManager().beginNewTransaction ("Add instrument track");
                auto after = arrangeView != nullptr ? arrangeView->getFirstSelectedTrack() : nullptr;
                auto track = TrackOps::addInstrumentTrack (edit, &sess->getSelectionManager(), &desc, after);

                if (track != nullptr)
                    if (auto p = track->pluginList.getPlugins().getFirst())
                        if (dynamic_cast<te::ExternalPlugin*> (p.get()) != nullptr)
                            p->showWindowExplicitly();
            }
        };

        m.addSectionHeader ("VST3 instruments");

        if (instruments.isEmpty())
            m.addItem ("(none found - use Plugins > Scan for Plugins)", false, false, [] {});

        for (auto& d : instruments)
            m.addItem (d.name + "   (" + d.manufacturerName + ")", [addTrackWith, d] { addTrackWith (d); });

        m.addSectionHeader ("Built-in");
        m.addItem ("4OSC Synth", [addTrackWith] { addTrackWith (te::PluginManager::createBuiltInPluginDescription<te::FourOscPlugin> (true)); });
        m.addItem ("Sampler", [addTrackWith] { addTrackWith (te::PluginManager::createBuiltInPluginDescription<te::SamplerPlugin> (true)); });
        m.addSeparator();
        m.addItem ("Empty instrument track", [this]
        {
            if (auto* sess = getSession())
            {
                sess->getEdit().getUndoManager().beginNewTransaction ("Add instrument track");
                TrackOps::addInstrumentTrack (sess->getEdit(), &sess->getSelectionManager(), nullptr,
                                              arrangeView != nullptr ? arrangeView->getFirstSelectedTrack() : nullptr);
            }
        });

        m.showMenuAsync ({});
    }

    void MainComponent::setLowerZoneVisible (bool visible, int page)
    {
        showLowerZone = visible && lowerZone != nullptr;

        if (showLowerZone && page >= 0)
            lowerZone->showPage ((LowerZone::Page) page);

        auto& settings = AppContext::get().settings;
        settings.setInt (SettingKeys::lowerZoneHeight, lowerZoneHeight);
        settings.setBool (SettingKeys::lowerZoneVisible, showLowerZone);
        if (lowerZone != nullptr)
            settings.setInt (SettingKeys::lowerZonePage, (int) lowerZone->getPage());
        resized();
    }

    void MainComponent::performTrackCommand (int commandID)
    {
        AppContext::get().commandManager.invokeDirectly (commandID, true);
    }

    //==============================================================================
    juce::StringArray MainComponent::getMenuBarNames()
    {
        return { "File", "Edit", "Transport", "Track", "View", "Plugins" };
    }

    juce::PopupMenu MainComponent::getMenuForIndex (int index, const juce::String&)
    {
        auto& ctx = AppContext::get();
        auto* cm = &ctx.commandManager;
        juce::PopupMenu m;

        auto add = [&m, cm] (std::initializer_list<juce::CommandID> ids)
        {
            for (auto id : ids)
            {
                if (id == 0) m.addSeparator();
                else         m.addCommandItem (cm, id);
            }
        };

        if (index == 0)
        {
            add ({ CommandIDs::newProject, CommandIDs::openProject });

            juce::PopupMenu recent;
            for (auto& path : ctx.settings.getRecentProjects())
                recent.addItem (path, [this, path] { confirmDiscardOrSave ([this, path] (bool ok) { if (ok) openProjectChecked (juce::File (path)); }); });

            m.addSubMenu ("Open Recent", recent, recent.getNumItems() > 0);
            add ({ 0, CommandIDs::saveProject, CommandIDs::saveProjectAs, CommandIDs::revealProjectFolder, CommandIDs::locateMissingFiles,
                   0, CommandIDs::importAudio, CommandIDs::importMidi, CommandIDs::importDawProject,
                   0, CommandIDs::exportMixdown, CommandIDs::exportStems, CommandIDs::exportMidi, CommandIDs::exportDawProject,
                   0, CommandIDs::showAudioSettings, CommandIDs::showPreferences,
                   0, juce::StandardApplicationCommandIDs::quit });
        }
        else if (index == 1)
        {
            add ({ CommandIDs::undo, CommandIDs::redo, 0,
                   CommandIDs::deleteSelection, CommandIDs::duplicate, CommandIDs::splitAtCursor, CommandIDs::muteClips, 0,
                   CommandIDs::deleteRangeCloseGap, CommandIDs::nudgeLeft, CommandIDs::nudgeRight, 0,
                   CommandIDs::crossfadeClips, CommandIDs::toggleClipLoop, CommandIDs::resetClipGain, 0,
                   CommandIDs::addSection, 0,
                   CommandIDs::selectAll, CommandIDs::deselectAll, 0,
                   CommandIDs::toggleSnap, CommandIDs::gridFiner, CommandIDs::gridCoarser });

            juce::PopupMenu tools;
            for (auto id : { CommandIDs::toolSelect, CommandIDs::toolRange, CommandIDs::toolSplit,
                             CommandIDs::toolDraw, CommandIDs::toolErase, CommandIDs::toolMute })
                tools.addCommandItem (cm, id);
            m.addSubMenu ("Tools", tools);
        }
        else if (index == 2)
        {
            add ({ CommandIDs::playStop, CommandIDs::stop, CommandIDs::record, CommandIDs::abortRecording, 0,
                   CommandIDs::returnToStart, CommandIDs::gotoEnd, 0,
                   CommandIDs::addMarker, CommandIDs::prevMarker, CommandIDs::nextMarker, 0,
                   CommandIDs::toggleLoop, CommandIDs::setLoopToSelection, CommandIDs::togglePunch, 0,
                   CommandIDs::toggleClick, CommandIDs::metronomeSettings, CommandIDs::toggleCountIn, CommandIDs::toggleFollowPlayhead, CommandIDs::toggleReturnOnStop });
        }
        else if (index == 3)
        {
            add ({ CommandIDs::addAudioTrack, CommandIDs::addStereoAudioTrack, CommandIDs::addInstrumentTrack, CommandIDs::addFxTrack,
                   CommandIDs::addGroupTrack, CommandIDs::addFolderTrack, 0,
                   CommandIDs::duplicateTrack, CommandIDs::deleteTrack, CommandIDs::moveTrackUp, CommandIDs::moveTrackDown, 0,
                   CommandIDs::toggleArm, CommandIDs::toggleMute, CommandIDs::toggleSolo });
        }
        else if (index == 4)
        {
            add ({ CommandIDs::toggleInspector, CommandIDs::toggleEditor, CommandIDs::toggleMixer, CommandIDs::toggleAutomation,
                   CommandIDs::toggleArranger, CommandIDs::toggleSpectrum, 0,
                   CommandIDs::zoomIn, CommandIDs::zoomOut, CommandIDs::zoomToFit, 0,
                   CommandIDs::zoomInVertical, CommandIDs::zoomOutVertical });
        }
        else if (index == 5)
        {
            add ({ CommandIDs::showPluginManager, 0, CommandIDs::openPluginFromFile });
        }

        return m;
    }
}
