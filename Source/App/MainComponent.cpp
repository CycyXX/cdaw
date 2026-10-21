#include "MainComponent.h"
#include "AppContext.h"
#include "../Commands/CommandIDs.h"
#include "../Engine/AudioDeviceSetup.h"
#include "../Model/ClipOps.h"
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
        lowerZoneHeight = ctx.settings.getInt (SettingKeys::lowerZoneHeight, 360);

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
            lowerZone->onResizeRequest = [this] (int h) { lowerZoneHeight = h; resized(); };
            lowerZone->onClose = [this] { setLowerZoneVisible (false, -1); };
            inspector->onDrumMapChanged = [this] (te::Track& t) { if (lowerZone) lowerZone->drumMapChanged (t); };
            addChildComponent (*lowerZone);

            addAndMakeVisible (*transportBar);
            addAndMakeVisible (*arrangeView);
            addAndMakeVisible (*inspector);
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
        inspector.reset();
        lowerZone.reset();
        arrangeView.reset();
        transportBar.reset();
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
            ctx.session = std::move (session);
            ctx.settings.addRecentProject (editFile);
            statusBar->showMessage ("Opened " + editFile.getFullPathName());
            juce::Logger::writeToLog ("Opened project " + editFile.getFullPathName());
        }

        sessionChanged();
    }

    void MainComponent::confirmDiscardOrSave (std::function<void (bool)> onDone)
    {
        auto* s = getSession();

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
                                                           "*" + juce::String (te::editFileSuffix));
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this] (const juce::FileChooser& fc)
                                  {
                                      if (fc.getResult().existsAsFile())
                                          openProjectChecked (fc.getResult());
                                  });
        });
    }

    void MainComponent::saveProject (std::function<void (bool)> onDone)
    {
        auto* s = getSession();
        bool ok = s != nullptr && s->save();
        statusBar->showMessage (ok ? "Saved " + s->getName() : juce::String ("Save failed - see the log"));
        updateTitle();

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
                                                       "*" + juce::String (te::editFileSuffix));
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this, onDone] (const juce::FileChooser& fc)
                              {
                                  auto result = fc.getResult();
                                  bool ok = false;

                                  if (result != juce::File())
                                      if (auto* sess = getSession())
                                      {
                                          auto file = ProjectSession::createProjectFolder (result.getParentDirectory(),
                                                                                           result.getFileNameWithoutExtension());
                                          ok = sess->saveAs (file);
                                          updateTitle();
                                          statusBar->showMessage (ok ? "Saved as " + file.getFullPathName() : juce::String ("Save As failed"));
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

        AppContext::get().settings.setInt (SettingKeys::lowerZoneHeight, lowerZoneHeight);
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
            add ({ 0, CommandIDs::saveProject, CommandIDs::saveProjectAs, CommandIDs::revealProjectFolder,
                   0, CommandIDs::importAudio, CommandIDs::importMidi,
                   0, CommandIDs::exportMixdown, CommandIDs::exportStems, CommandIDs::exportMidi,
                   0, CommandIDs::showAudioSettings, CommandIDs::showPreferences,
                   0, juce::StandardApplicationCommandIDs::quit });
        }
        else if (index == 1)
        {
            add ({ CommandIDs::undo, CommandIDs::redo, 0,
                   CommandIDs::deleteSelection, CommandIDs::duplicate, CommandIDs::splitAtCursor, CommandIDs::muteClips, 0,
                   CommandIDs::deleteRangeCloseGap, CommandIDs::nudgeLeft, CommandIDs::nudgeRight, 0,
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
                   CommandIDs::toggleClick, CommandIDs::toggleCountIn, CommandIDs::toggleFollowPlayhead });
        }
        else if (index == 3)
        {
            add ({ CommandIDs::addAudioTrack, CommandIDs::addInstrumentTrack, CommandIDs::addFxTrack,
                   CommandIDs::addGroupTrack, CommandIDs::addFolderTrack, 0,
                   CommandIDs::duplicateTrack, CommandIDs::deleteTrack, CommandIDs::moveTrackUp, CommandIDs::moveTrackDown, 0,
                   CommandIDs::toggleArm, CommandIDs::toggleMute, CommandIDs::toggleSolo });
        }
        else if (index == 4)
        {
            add ({ CommandIDs::toggleInspector, CommandIDs::toggleEditor, CommandIDs::toggleMixer, CommandIDs::toggleAutomation, 0,
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
