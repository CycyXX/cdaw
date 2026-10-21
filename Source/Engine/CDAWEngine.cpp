#include "CDAWEngine.h"
#include "PluginWindow.h"
#include "SafeAsioType.h"
#include "../Plugins/BuiltInPlugins.h"
#include "../App/AppSettings.h"

namespace cdaw
{
    //==============================================================================
    CDAWEngineBehaviour::CDAWEngineBehaviour (AppSettings& s) : settings (s) {}

    te::EditLimits CDAWEngineBehaviour::getEditLimits()
    {
        te::EditLimits limits;
        limits.maxNumTracks = 512;
        limits.maxClipsInTrack = 5000;
        limits.maxPluginsOnClip = 8;
        limits.maxPluginsOnTrack = 40;
        limits.maxNumMasterPlugins = 16;
        return limits;
    }

    int CDAWEngineBehaviour::getMiddleCOctave()
    {
        // Cubase / Yamaha convention: middle C (MIDI 60) is C3.
        return settings.getInt (SettingKeys::middleCOctave, 3);
    }

    void CDAWEngineBehaviour::setMiddleCOctave (int octave)
    {
        settings.setInt (SettingKeys::middleCOctave, octave);
    }

    juce::File CDAWEngineBehaviour::getFileForNewAudioRecording (te::Track& track, const juce::String& ext)
    {
        auto editFile = track.edit.editFileRetriever ? track.edit.editFileRetriever() : juce::File();
        auto folder = editFile != juce::File() ? editFile.getParentDirectory().getChildFile ("Audio")
                                               : settings.getProjectsFolder().getChildFile ("Unsaved Recordings");
        folder.createDirectory();

        auto trackName = juce::File::createLegalFileName (track.getName()).replaceCharacter (' ', '_');
        auto stamp = juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S");
        return folder.getNonexistentChildFile (trackName + "_" + stamp, ext.startsWithChar ('.') ? ext : "." + ext, false);
    }

    static juce::StringArray getDisabledList (AppSettings& s)
    {
        juce::StringArray list;
        list.addLines (s.getString (SettingKeys::disabledPlugins));
        list.removeEmptyStrings();
        return list;
    }

    bool CDAWEngineBehaviour::isPluginDisabled (const juce::String& pluginID)
    {
        return getDisabledList (settings).contains (pluginID);
    }

    void CDAWEngineBehaviour::setPluginDisabled (const juce::String& pluginID, bool shouldBeDisabled)
    {
        auto list = getDisabledList (settings);
        list.removeString (pluginID);

        if (shouldBeDisabled)
            list.add (pluginID);

        settings.setString (SettingKeys::disabledPlugins, list.joinIntoString ("\n"));
    }

    int CDAWEngineBehaviour::getNumberOfCPUsToUseForAudio()
    {
        // Developer override for measurements (CDAW_DEBUG_AUDIO_THREADS=<n>).
        if (auto n = juce::SystemStats::getEnvironmentVariable ("CDAW_DEBUG_AUDIO_THREADS", {}).getIntValue(); n > 0)
            return n;

        return juce::jmax (1, juce::SystemStats::getNumCpus() - 1);
    }

    void CDAWEngineBehaviour::newClipCreated (te::Clip& clip, bool)
    {
        if (auto track = clip.getTrack())
            clip.setColour (track->getColour());
    }

    void CDAWEngineBehaviour::editHasBeenSaved (te::Edit&, juce::File file)
    {
        settings.addRecentProject (file);
    }

    te::EngineBehaviour::ControlSurfaces CDAWEngineBehaviour::getDesiredControlSurfaces()
    {
        ControlSurfaces cs;
        cs.mackieMCU = cs.mackieC4 = cs.iconProG2 = cs.tranzport = false;
        cs.alphaTrack = cs.remoteSL = cs.remoteSLCompact = cs.automap = false;
        return cs;
    }

    //==============================================================================
    juce::Array<te::Edit*> CDAWUIBehaviour::getAllOpenEdits()
    {
        juce::Array<te::Edit*> edits;

        if (auto e = getCurrentlyFocusedEdit())
            edits.add (e);

        return edits;
    }

    std::unique_ptr<juce::Component> CDAWUIBehaviour::createPluginWindow (te::PluginWindowState& pws)
    {
        if (auto ws = dynamic_cast<te::Plugin::WindowState*> (&pws))
            return PluginWindow::create (ws->plugin);

        return {};
    }

    void CDAWUIBehaviour::recreatePluginWindowContentAsync (te::Plugin& p)
    {
        if (auto w = dynamic_cast<PluginWindow*> (p.windowState->pluginWindow.get()))
            return w->recreateEditorAsync();

        UIBehaviour::recreatePluginWindowContentAsync (p);
    }

    void CDAWUIBehaviour::showInfoMessage (const juce::String& message)
    {
        juce::Logger::writeToLog ("Info: " + message);

        if (showMessage)
            showMessage (message);
    }

    void CDAWUIBehaviour::showWarningMessage (const juce::String& message)
    {
        juce::Logger::writeToLog ("Warning: " + message);

        if (showMessage)
            showMessage (message);
    }

    void CDAWUIBehaviour::runTaskWithProgressBar (te::ThreadPoolJobWithProgress& job)
    {
        struct Runner : public juce::ThreadWithProgressWindow
        {
            explicit Runner (te::ThreadPoolJobWithProgress& j)
                : ThreadWithProgressWindow (j.getJobName(), true, j.canCancel()), job (j) {}

            void run() override
            {
                while (! threadShouldExit())
                {
                    setProgress (job.getCurrentTaskProgress());

                    if (job.runJob() == juce::ThreadPoolJob::jobHasFinished)
                        break;
                }
            }

            te::ThreadPoolJobWithProgress& job;
        };

        Runner runner (job);
        runner.runThread();
    }

    //==============================================================================
    CDAWEngine::CDAWEngine (AppSettings& s) : settings (s)
    {
        auto ui = std::make_unique<CDAWUIBehaviour>();
        uiBehaviour = ui.get();

        // Developer override for measurements (CDAW_DEBUG_POOL=<tracktion::graph::ThreadPoolStrategy>).
        if (auto pool = juce::SystemStats::getEnvironmentVariable ("CDAW_DEBUG_POOL", {}); pool.isNotEmpty())
            te::EditPlaybackContext::setThreadPoolStrategy (pool.getIntValue());

        engine = std::make_unique<te::Engine> (std::make_unique<te::PropertyStorage> ("CDAW"),
                                               std::move (ui),
                                               std::make_unique<CDAWEngineBehaviour> (settings));

        // Register filtered device types, then open the saved device (falling back safely).
        registerAudioDeviceTypes (engine->getDeviceManager().deviceManager);
        engine->getDeviceManager().initialise (te::DeviceManager::defaultNumChannelsToOpen,
                                               te::DeviceManager::defaultNumChannelsToOpen);

        auto& pm = engine->getPluginManager();
        pm.setUsesSeparateProcessForScanning (true);
        pm.createBuiltInType<te::ImpulseResponsePlugin>();
        pm.createBuiltInType<te::ToneGeneratorPlugin>();
        BuiltInPlugins::registerTypes (*engine);

        engine->getDeviceManager().addChangeListener (this);
        applyRecordingDefaults();

        // MP3 export: LAME is loaded at runtime if libmp3lame.dll sits next to CDAW.exe.
       #if TRACKTION_ENABLE_LIBLAME
        {
            auto dll = juce::File::getSpecialLocation (juce::File::currentExecutableFile)
                           .getSiblingFile (te::LibLameEncoderAudioFormat::getDefaultLibraryName());

            if (dll.existsAsFile())
            {
                auto forArray = std::make_unique<te::LibLameEncoderAudioFormat> (dll.getFullPathName());
                auto forAccess = std::make_unique<te::LibLameEncoderAudioFormat> (dll.getFullPathName());

                if (forArray->isLibraryLoaded() && forAccess->isLibraryLoaded())
                {
                    engine->getAudioFileFormatManager().addLameFormat (std::move (forArray), std::move (forAccess));
                    juce::Logger::writeToLog ("MP3 export enabled: " + dll.getFullPathName());
                }
            }
        }
       #endif
    }

    CDAWEngine::~CDAWEngine()
    {
        engine->getDeviceManager().removeChangeListener (this);
        engine.reset();
    }

    void CDAWEngine::applyRecordingDefaults()
    {
        auto& dm = engine->getDeviceManager();
        auto wav = engine->getAudioFileFormatManager().getWavFormat()->getFormatName();
        auto replaceMode = te::WaveInputDevice::getMergeModes()[1];
        auto overlayMode = te::WaveInputDevice::getMergeModes()[0];
        auto depth = settings.getInt (SettingKeys::recordBitDepth, 24);
        auto offsetMs = settings.getDouble (SettingKeys::recordOffsetMs, 0.0);

        for (auto* in : dm.getWaveInputDevices())
        {
            if (in->isTrackDevice())
                continue;

            if (in->getOutputFormat() != wav)
                in->setOutputFormat (wav);

            if (in->getBitDepth() != depth && in->getAvailableBitDepths().contains (depth))
                in->setBitDepth (depth);

            // Keep "don't record" if the user chose it; otherwise always replace.
            if (in->getMergeMode() == overlayMode)
                in->setMergeMode (replaceMode);

            if (std::abs (in->getRecordAdjustmentMs() - offsetMs) > 0.001)
                in->setRecordAdjustmentMs (offsetMs);
        }

        // MIDI: 0 = overdub into the existing clip, 1 = replace, 2 = new clips.
        auto midiMode = settings.getInt (SettingKeys::midiRecordMode, 0);

        for (auto& in : dm.getMidiInDevices())
        {
            in->mergeRecordings = midiMode == 0;
            in->replaceExistingClips = midiMode == 1;
        }
    }
}
