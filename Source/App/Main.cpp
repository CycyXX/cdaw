#include "../PCH.h"
#include "AppContext.h"
#include "Logging.h"
#include "CrashHandler.h"
#include "FileAssociation.h"
#include "DebugProfiler.h"
#include "MainWindow.h"
#include "MainComponent.h"
#include "../Engine/AudioDeviceSetup.h"
#include "../Model/ProjectSession.h"
#include "../Model/ClipOps.h"
#include "../Model/DawProject.h"
#include "../Model/TrackOps.h"
#include "../Plugins/BuiltInPlugins.h"
#include "../Plugins/EqPlugin.h"
#include "../Plugins/MultibandPlugin.h"
#include "../Plugins/ReverbPlugin.h"
#include "../UI/Plugins/PluginMenus.h"
#include "../UI/Common/StartupSplash.h"
#include "../Commands/CommandIDs.h"

namespace cdaw
{
    class CDAWApplication : public juce::JUCEApplication
    {
    public:
        const juce::String getApplicationName() override       { return "CDAW"; }
        const juce::String getApplicationVersion() override    { return JUCE_APPLICATION_VERSION_STRING; }

        // Must stay true: the out-of-process plugin scanner is this same exe.
        bool moreThanOneInstanceAllowed() override             { return true; }

        void initialise (const juce::String& commandLine) override
        {
            // Plugin scanner child process: scan and exit. No engine, no ASIO, no window.
            if (te::PluginManager::startChildProcessPluginScan (commandLine))
            {
                isScannerChild = true;
                return;
            }

            Logging::init();
            CrashHandler::install();
            const bool previousSessionCrashed = CrashHandler::beginSession();
            juce::Logger::writeToLog ("Starting CDAW " + getApplicationVersion() + "  args: " + commandLine);

            auto splash = std::make_unique<StartupSplash> (getApplicationVersion());

            splash->setStep ("Starting the audio engine", 0.05);
            context = std::make_unique<AppContext>();

            splash->setStep ("Opening the audio device", 0.30);
            auto status = AudioDeviceSetup::ensureAsioDefault (context->engine(), context->settings);
            context->deviceTracker = AudioDeviceSetup::trackPreferredDevice (context->engine(), context->settings);

            splash->setStep ("Scanning audio and MIDI devices", 0.50);
            settleDevices();

            splash->setStep ("Building the main window", 0.62);
            context->mainWindow = std::make_unique<MainWindow>();
            auto& main = context->mainWindow->getMainComponent();

            // Open the file given on the command line, else the last project, else a fresh one.
            auto argFile = juce::File::getCurrentWorkingDirectory().getChildFile (commandLine.trim().unquoted());
            auto last = juce::File (context->settings.getString (SettingKeys::lastProject));

            if (commandLine.isNotEmpty() && ProjectSession::isProjectFile (argFile) && argFile.existsAsFile())
            {
                splash->setStep ("Opening " + argFile.getFileNameWithoutExtension() + " and loading its plugins", 0.70);
                main.openProjectChecked (argFile);
            }
            else if (last.existsAsFile() && (context->settings.getBool (SettingKeys::reopenLastProject, true) || previousSessionCrashed))
            {
                splash->setStep ("Opening " + last.getFileNameWithoutExtension() + " and loading its plugins", 0.70);
                main.openProjectChecked (last);
            }
            else
            {
                splash->setStep ("Creating a new project", 0.70);
                auto folder = context->settings.getProjectsFolder();
                folder.createDirectory();
                main.openProject (ProjectSession::createProjectFolder (folder, "Untitled"));

                if (context->session != nullptr)
                    context->session->save();
            }

            FileAssociation::registerIfNeeded();

            splash->setStep ("Ready", 1.0);
            context->mainWindow->setVisible (true);

            // Developer override for measurements: CDAW_DEBUG_RENDERER=<index> (0 = software, 1 = Direct2D).
            if (auto r = juce::SystemStats::getEnvironmentVariable ("CDAW_DEBUG_RENDERER", {}); r.isNotEmpty())
                if (auto* peer = context->mainWindow->getPeer())
                {
                    peer->setCurrentRenderingEngine (r.getIntValue());
                    juce::Logger::writeToLog ("Renderer: " + peer->getAvailableRenderingEngines()[peer->getCurrentRenderingEngine()]);
                }

            context->mainWindow->toFront (true);
            splash.reset();

            if (context->postStatus)
                context->postStatus (status);

            context->mainWindow->getMainComponent().checkProjectHealth();

            runDebugHooks();

            if (previousSessionCrashed)
            {
                juce::Logger::writeToLog ("Previous session did not exit cleanly");
                CrashHandler::offerToDisableCrashedPlugins (context->engine());
            }
        }

        void shutdown() override
        {
            if (isScannerChild)
                return;

            if (context != nullptr)
            {
                if (context->session != nullptr)
                    context->session->getTransport().stop (false, true);

                context->mainWindow.reset();
                ClipOps::cancelRunningImport();
                context->session.reset();
                context.reset();
            }

            CrashHandler::endSession();
            juce::Logger::writeToLog ("CDAW shut down cleanly");
            Logging::shutdown();
        }

        void systemRequestedQuit() override
        {
            if (isScannerChild || context == nullptr || context->mainWindow == nullptr)
            {
                quit();
                return;
            }

            context->mainWindow->getMainComponent().confirmDiscardOrSave ([] (bool ok)
            {
                if (ok)
                    juce::JUCEApplication::quit();
            });
        }

        void anotherInstanceStarted (const juce::String&) override {}

    private:
        /** The engine rebuilds its wave and MIDI device lists asynchronously after a device opens.
            Each rebuild restarts the audio graph, which stops playback that has already started
            (it resumes a few seconds later). Finish all of that before a project is opened. */
        void settleDevices()
        {
            auto& dm = context->engine().getDeviceManager();
            dm.rescanWaveDeviceList();
            dm.rescanMidiDeviceList();
            dm.dispatchPendingUpdates();

            // Let queued device-change messages arrive and be handled now rather than mid-playback.
            for (int i = 0; i < 3; ++i)
            {
                juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
                dm.dispatchPendingUpdates();
            }
        }

        /** Developer aids, only active when the environment variables are set:
            CDAW_DEBUG_IMPORT=<file>  imports the file at bar 1 the same way a drag-and-drop does.
            CDAW_DEBUG_AUTOPLAY=1     presses play 0.5 s after start and logs the transport state for 6 s.
            CDAW_DEBUG_PLAYAT=<s>     with AUTOPLAY: starts playing from that position (seconds).
            CDAW_DEBUG_SPECTRUM=1     opens the mixer with the spectrum analyser.
            CDAW_DEBUG_CRASH=1        crashes on purpose after 3 s (tests the crash report).
            CDAW_DEBUG_XRUNS=1        logs the audio device's dropout count and engine load every 5 s.
            CDAW_DEBUG_RENDERER=<n>   0 = software renderer, 1 = Direct2D (for measurements).
            CDAW_DEBUG_PROFILE=<s>    after 12 s, samples the message thread for <s> seconds and logs the hot spots.
            CDAW_DEBUG_DAWPROJECT=<plugin name>  adds the VST3 on a new track (CDAW_DEBUG_DAWPROJECT_EDIT=find|replace
                                      edits its state first), exports a .dawproject to %TEMP%, re-imports it
                                      into the open project and logs whether the plugin's preset came back
                                      identical. Use a throwaway project.
            CDAW_DEBUG_PLUGINS=1      after 3 s, inserts CDAW's EQ (a few bands set), compressor, multiband and reverb on the first
                                      track with clips (or the first audio track) and opens both windows.
            CDAW_DEBUG_RECORD=<s>     after 2 s, adds a mono track on the first input, records <s> seconds and logs the
                                      clip it made. Use a throwaway project. */
        void runDebugHooks()
        {
            // CDAW_DEBUG_CRASH=1: crash on purpose after 3 s, to check the crash report (and its symbols).
            if (juce::SystemStats::getEnvironmentVariable ("CDAW_DEBUG_CRASH", {}) == "1")
                juce::Timer::callAfterDelay (3000, []
                {
                    juce::Logger::writeToLog ("DEBUG crashing on purpose");
                    volatile int* nowhere = nullptr;
                    *nowhere = 1;
                });

            // CDAW_DEBUG_XRUNS=1: log the device, its dropout (xrun) count and the engine's CPU load every 5 s.
            if (juce::SystemStats::getEnvironmentVariable ("CDAW_DEBUG_XRUNS", {}) == "1")
                for (int i = 1; i <= 24; ++i)
                    juce::Timer::callAfterDelay (i * 5000, []
                    {
                        auto& dm = AppContext::get().engine().getDeviceManager();
                        if (auto* dev = dm.deviceManager.getCurrentAudioDevice())
                            juce::Logger::writeToLog ("DEBUG device=" + dev->getName() + " sr=" + juce::String (dev->getCurrentSampleRate())
                                                      + " buffer=" + juce::String (dev->getCurrentBufferSizeSamples())
                                                      + " xruns=" + juce::String (dev->getXRunCount())
                                                      + " cpu=" + juce::String (dm.getCpuUsage() * 100.0f, 1) + "%");
                    });

            if (auto secs = juce::SystemStats::getEnvironmentVariable ("CDAW_DEBUG_PROFILE", {}).getDoubleValue(); secs > 0.0)
                juce::Timer::callAfterDelay (12000, [secs]
                {
                    DebugProfiler::start (secs, juce::SystemStats::getEnvironmentVariable ("CDAW_DEBUG_PROFILE_HOT", {}) == "1");
                });

            if (juce::SystemStats::getEnvironmentVariable ("CDAW_DEBUG_SPECTRUM", {}) == "1")
                juce::Timer::callAfterDelay (300, []
                {
                    if (auto& s = AppContext::get().session; s != nullptr && s->isValid())
                        if (! (bool) s->getViewState().state.getProperty (ViewIDs::showSpectrum, false))
                            AppContext::get().commandManager.invokeDirectly (CommandIDs::toggleSpectrum, false);
                });

            auto importPath = juce::SystemStats::getEnvironmentVariable ("CDAW_DEBUG_IMPORT", {});

            if (importPath.isNotEmpty())
                juce::Timer::callAfterDelay (1500, [importPath]
                {
                    if (auto& s = AppContext::get().session; s != nullptr && s->isValid())
                    {
                        auto clips = ClipOps::importAudioFiles (*s, { juce::File (importPath) }, nullptr, te::TimePosition(), true);
                        juce::Logger::writeToLog ("DEBUG import: " + juce::String (clips.size()) + " clip(s)");
                    }
                });

            // CDAW_DEBUG_DAWPROJECT=<plugin>: export / re-import round trip of a VST3's settings in the running app.
            auto dawPlugin = juce::SystemStats::getEnvironmentVariable ("CDAW_DEBUG_DAWPROJECT", {});

            if (dawPlugin.isNotEmpty())
            {
                // The preset the plugin should come back with (shared between the two steps).
                auto expected = std::make_shared<juce::MemoryBlock>();

                juce::Timer::callAfterDelay (2000, [dawPlugin, expected]
                {
                    auto& s = AppContext::get().session;
                    if (s == nullptr || ! s->isValid())
                        return;

                    auto& edit = s->getEdit();
                    for (auto& desc : edit.engine.getPluginManager().knownPluginList.getTypes())
                    {
                        if (! desc.name.equalsIgnoreCase (dawPlugin) || desc.pluginFormatName != "VST3")
                            continue;

                        auto track = TrackOps::addAudioTrack (edit, nullptr, nullptr, "DAWproject test");
                        auto plugin = edit.getPluginCache().createNewPlugin (te::ExternalPlugin::xmlTypeName, desc);
                        track->pluginList.insertPlugin (plugin, 0, nullptr);
                        auto* instance = dynamic_cast<te::ExternalPlugin*> (plugin.get())->getAudioPluginInstance();
                        auto* vst3 = instance != nullptr ? instance->getVST3Client() : nullptr;

                        if (vst3 == nullptr)
                            break;

                        // CDAW_DEBUG_DAWPROJECT_EDIT=find|replace changes the plugin's own state first (same-length
                        // text keeps the preset valid), so the test uses settings the plugin really stores.
                        auto change = juce::StringArray::fromTokens (juce::SystemStats::getEnvironmentVariable ("CDAW_DEBUG_DAWPROJECT_EDIT", {}), "|", "");
                        auto preset = vst3->getPreset();

                        if (change.size() == 2 && change[0].length() == change[1].length())
                        {
                            auto bytes = juce::MemoryBlock (preset);
                            auto* data = static_cast<char*> (bytes.getData());
                            auto find = change[0].toStdString(), replace = change[1].toStdString();
                            auto at = std::search (data, data + bytes.getSize(), find.begin(), find.end());

                            if (at != data + bytes.getSize())
                            {
                                std::copy (replace.begin(), replace.end(), at);
                                vst3->setPreset (bytes);
                                juce::MemoryBlock state;
                                instance->getStateInformation (state);
                                instance->setStateInformation (state.getData(), (int) state.getSize());
                            }

                            juce::Logger::writeToLog ("DEBUG dawproject: state edit " + juce::String (at != data + bytes.getSize() ? "applied" : "not found"));
                        }

                        juce::MemoryBlock flush;
                        instance->getStateInformation (flush);
                        *expected = vst3->getPreset();
                        juce::Logger::writeToLog ("DEBUG dawproject: " + desc.name + " preset " + juce::String ((int) expected->getSize()) + " bytes");
                        break;
                    }
                });

                juce::Timer::callAfterDelay (4500, [expected]
                {
                    auto& s = AppContext::get().session;
                    if (s == nullptr || ! s->isValid())
                        return;

                    auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("cdaw_debug.dawproject");
                    DawProject::Report out, in;
                    auto exported = DawProject::exportTo (s->getEdit(), file, out);
                    juce::Logger::writeToLog ("DEBUG dawproject export: " + (exported.wasOk() ? out.summary() : exported.getErrorMessage()));

                    auto imported = DawProject::importInto (s->getEdit(), file, s->getAudioFolder().getChildFile ("Imported"), in);
                    juce::Logger::writeToLog ("DEBUG dawproject import: " + (imported.wasOk() ? in.summary() : imported.getErrorMessage()));

                    juce::Timer::callAfterDelay (1500, [expected]
                    {
                        auto& session = AppContext::get().session;
                        if (session == nullptr || ! session->isValid())
                            return;

                        for (auto t : te::getAudioTracks (session->getEdit()))
                            for (auto p : t->pluginList.getPlugins())
                                if (auto ext = dynamic_cast<te::ExternalPlugin*> (p))
                                    if (auto* instance = ext->getAudioPluginInstance(); instance != nullptr && instance->getVST3Client() != nullptr)
                                    {
                                        juce::MemoryBlock flush;
                                        instance->getStateInformation (flush);
                                        auto now = instance->getVST3Client()->getPreset();
                                        // Byte equality is too strict for some plugins (Nolly X stamps a new presetUid on load),
                                        // so also report whether the edited setting is there.
                                        auto edited = juce::StringArray::fromTokens (juce::SystemStats::getEnvironmentVariable ("CDAW_DEBUG_DAWPROJECT_EDIT", {}), "|", "");
                                        auto restored = [&] (const juce::String& setting)
                                        {
                                            auto needle = setting.toStdString();
                                            auto* begin = static_cast<const char*> (now.getData());
                                            return std::search (begin, begin + now.getSize(), needle.begin(), needle.end()) != begin + now.getSize();
                                        };
                                        juce::Logger::writeToLog ("DEBUG dawproject: after import " + ext->getName() + " preset "
                                                                  + juce::String (now == *expected ? "identical" : "differs") + " to the original"
                                                                  + (edited.size() == 2 ? ", edited setting " + edited[1] + (restored (edited[1]) ? " restored" : " MISSING")
                                                                                        : juce::String()));
                                    }
                    });
                });
            }

            // CDAW_DEBUG_PLUGINS=1: CDAW's EQ and compressor on the first track with clips, windows open.
            if (juce::SystemStats::getEnvironmentVariable ("CDAW_DEBUG_PLUGINS", {}) == "1")
                juce::Timer::callAfterDelay (3000, []
                {
                    auto& s = AppContext::get().session;
                    if (s == nullptr || ! s->isValid())
                        return;

                    // The first track with clips, else the first audio track.
                    auto tracks = te::getAudioTracks (s->getEdit());
                    std::stable_partition (tracks.begin(), tracks.end(), [] (auto* t) { return ! t->getClips().isEmpty(); });

                    for (auto t : tracks)
                    {
                        auto eqPlugin = PluginMenus::createPlugin (s->getEdit(), BuiltInPlugins::getEqDescription());
                        auto compPlugin = PluginMenus::createPlugin (s->getEdit(), BuiltInPlugins::getCompressorDescription());
                        auto multibandPlugin = PluginMenus::createPlugin (s->getEdit(), BuiltInPlugins::getMultibandDescription());
                        t->pluginList.insertPlugin (multibandPlugin, PluginMenus::getPreFaderInsertIndex (*t), nullptr);
                        auto reverbPlugin = PluginMenus::createPlugin (s->getEdit(), BuiltInPlugins::getReverbDescription());
                        t->pluginList.insertPlugin (reverbPlugin, PluginMenus::getPreFaderInsertIndex (*t), nullptr);
                        if (auto rv = dynamic_cast<ReverbPlugin*> (reverbPlugin.get()))
                            rv->mix.setParameter (0.4f, juce::sendNotification);
                        t->pluginList.insertPlugin (eqPlugin, PluginMenus::getPreFaderInsertIndex (*t), nullptr);
                        t->pluginList.insertPlugin (compPlugin, PluginMenus::getPreFaderInsertIndex (*t), nullptr);

                        if (auto eq = dynamic_cast<EqPlugin*> (eqPlugin.get()))
                        {
                            eq->setType (0, EqMath::BandType::lowCut);
                            eq->getBand (0).freq.setParameter (50.0f, juce::sendNotification);
                            eq->setOn (0, true);
                            eq->getBand (3).freq.setParameter (400.0f, juce::sendNotification);
                            eq->getBand (3).gain.setParameter (-3.0f, juce::sendNotification);
                            eq->setOn (3, true);
                            eq->getBand (6).gain.setParameter (2.0f, juce::sendNotification);
                            eq->setOn (6, true);
                        }

                        if (auto comp = dynamic_cast<te::CompressorPlugin*> (compPlugin.get()))
                        {
                            comp->thresholdGain.setParameter (juce::Decibels::decibelsToGain (-20.0f), juce::sendNotification);
                            comp->ratio.setParameter (0.25f, juce::sendNotification);
                        }

                        if (auto mb = dynamic_cast<MultibandPlugin*> (multibandPlugin.get()))
                            for (int b = 0; b < MultibandPlugin::numBands; ++b)
                                mb->getBand (b).on.setParameter (1.0f, juce::sendNotification);

                        eqPlugin->showWindowExplicitly();
                        compPlugin->showWindowExplicitly();
                        multibandPlugin->showWindowExplicitly();
                        reverbPlugin->showWindowExplicitly();
                        juce::Logger::writeToLog ("DEBUG plugins: EQ, compressor, multiband and reverb on " + t->getName());
                        break;
                    }
                });

            if (auto secs = juce::SystemStats::getEnvironmentVariable ("CDAW_DEBUG_RECORD", {}).getDoubleValue(); secs > 0.0)
                juce::Timer::callAfterDelay (2000, [secs]
                {
                    auto& s = AppContext::get().session;
                    if (s == nullptr || ! s->isValid())
                        return;

                    auto& edit = s->getEdit();
                    edit.getUndoManager().beginNewTransaction ("Add audio track");
                    auto track = TrackOps::addAudioTrack (edit, &s->getSelectionManager(), nullptr, "Debug Recording");
                    TrackOps::assignAudioInput (*track, 0, 1);
                    TrackOps::setArmed (*track, true);

                    auto in = TrackOps::getAssignedInput (*track);
                    juce::Logger::writeToLog ("DEBUG record: input=" + (in != nullptr ? in->getInputDevice().getName() : juce::String ("none"))
                                              + " armed=" + juce::String ((int) TrackOps::isArmed (*track)));
                    s->getTransport().record (false);

                    juce::Timer::callAfterDelay ((int) (secs * 1000.0), [track]
                    {
                        if (auto& s2 = AppContext::get().session; s2 != nullptr && s2->isValid())
                        {
                            s2->getTransport().stop (false, false);
                            juce::Timer::callAfterDelay (1000, [track]
                            {
                                auto clips = track->getClips();
                                juce::Logger::writeToLog ("DEBUG record: " + juce::String (clips.size()) + " clip(s)"
                                                          + (clips.isEmpty() ? juce::String() : ", " + juce::String (clips[0]->getPosition().getLength().inSeconds(), 2) + " s"));
                            });
                        }
                    });
                });

            if (juce::SystemStats::getEnvironmentVariable ("CDAW_DEBUG_AUTOPLAY", {}) == "1")
            {
                juce::Timer::callAfterDelay (500, []
                {
                    if (auto& s = AppContext::get().session; s != nullptr && s->isValid())
                    {
                        // CDAW_DEBUG_PLAYAT=<seconds> starts from there (for screenshots of a given section).
                        if (auto from = juce::SystemStats::getEnvironmentVariable ("CDAW_DEBUG_PLAYAT", {}).getDoubleValue(); from > 0.0)
                            s->getTransport().setPosition (te::TimePosition::fromSeconds (from));

                        juce::Logger::writeToLog ("DEBUG autoplay: play");
                        s->getTransport().play (false);
                    }
                });

                for (int i = 1; i <= 48; ++i)
                    juce::Timer::callAfterDelay (500 + i * 250, [i]
                    {
                        if (auto& s = AppContext::get().session; s != nullptr && s->isValid())
                            juce::Logger::writeToLog ("DEBUG t+" + juce::String (i * 0.25, 2) + "s playing=" + juce::String ((int) s->getTransport().isPlaying())
                                                      + " pos=" + juce::String (s->getTransport().getPosition().inSeconds(), 3)
                                                      + " loop=" + juce::String ((int) s->getTransport().looping.get())
                                                      + " len=" + juce::String (s->getEdit().getLength().inSeconds(), 2));
                    });
            }
        }

        std::unique_ptr<AppContext> context;
        bool isScannerChild = false;
    };
}

START_JUCE_APPLICATION (cdaw::CDAWApplication)
