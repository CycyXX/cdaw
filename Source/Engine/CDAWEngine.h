#pragma once

#include "../PCH.h"

namespace cdaw
{
    class AppSettings;

    /** Engine-wide behaviour overrides (limits, recording paths, plugin scanning). */
    class CDAWEngineBehaviour : public te::EngineBehaviour
    {
    public:
        explicit CDAWEngineBehaviour (AppSettings&);

        bool canScanPluginsOutOfProcess() override                      { return true; }
        // CDAW registers its own (filtered) device types and initialises the devices itself.
        bool addSystemAudioIODeviceTypes() override                     { return false; }
        bool autoInitialiseDeviceManager() override                     { return false; }
        te::EditLimits getEditLimits() override;
        int getMiddleCOctave() override;
        void setMiddleCOctave (int) override;
        juce::File getFileForNewAudioRecording (te::Track&, const juce::String& fileExtension) override;
        bool isPluginDisabled (const juce::String& pluginID) override;
        void setPluginDisabled (const juce::String& pluginID, bool shouldBeDisabled) override;
        bool autoAddClipEdgeFades() override                            { return true; }
        int getNumberOfCPUsToUseForAudio() override;

        /** Raises the process to high priority while playing or recording (Tracktion calls this).
            Never the Windows "realtime" class, which can starve input and the system. */
        void setProcessPriority (int level) override
        {
            juce::Process::setPriority (level > 0 ? juce::Process::HighPriority : juce::Process::NormalPriority);
        }
        void newClipCreated (te::Clip&, bool fromRecording) override;
        void editHasBeenSaved (te::Edit&, juce::File) override;
        ControlSurfaces getDesiredControlSurfaces() override;

    private:
        AppSettings& settings;
    };

    /** UI hooks the engine calls back into: plugin windows, alerts, focused edit. */
    class CDAWUIBehaviour : public te::UIBehaviour
    {
    public:
        std::function<te::Edit*()> getEdit;
        std::function<te::SelectionManager*()> getSelectionManager;
        std::function<void (const juce::String&)> showMessage;
        juce::ApplicationCommandManager* commandManager = nullptr;

        te::Edit* getCurrentlyFocusedEdit() override                    { return getEdit ? getEdit() : nullptr; }
        te::Edit* getLastFocusedEdit() override                         { return getCurrentlyFocusedEdit(); }
        juce::Array<te::Edit*> getAllOpenEdits() override;
        te::SelectionManager* getCurrentlyFocusedSelectionManager() override { return getSelectionManager ? getSelectionManager() : nullptr; }
        juce::ApplicationCommandManager* getApplicationCommandManager() override { return commandManager; }

        std::unique_ptr<juce::Component> createPluginWindow (te::PluginWindowState&) override;
        void recreatePluginWindowContentAsync (te::Plugin&) override;

        void showInfoMessage (const juce::String&) override;
        void showWarningMessage (const juce::String&) override;
        void runTaskWithProgressBar (te::ThreadPoolJobWithProgress&) override;
    };

    /** Owns the te::Engine and wires up the behaviours. */
    class CDAWEngine : private juce::ChangeListener
    {
    public:
        explicit CDAWEngine (AppSettings&);
        ~CDAWEngine() override;

        te::Engine& get()                       { return *engine; }
        CDAWUIBehaviour& getUI()                { return *uiBehaviour; }

        /** Applies CDAW's recording defaults to every audio input: WAV at the configured
            bit depth, "replace" merge mode (Tracktion would otherwise overlay new
            recordings on old clips, and overlapping clips play summed), manual offset. */
        void applyRecordingDefaults();

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override  { applyRecordingDefaults(); }

        AppSettings& settings;
        CDAWUIBehaviour* uiBehaviour = nullptr;  // owned by the engine
        std::unique_ptr<te::Engine> engine;
    };
}
