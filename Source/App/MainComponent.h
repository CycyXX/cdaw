#pragma once

#include "../PCH.h"

namespace cdaw
{
    class StatusBar;
    class TransportBar;
    class ArrangeView;
    class ProjectSession;
    class InspectorPanel;
    class LowerZone;

    /** Root component: menu bar, transport, arrangement, status bar.
        Root ApplicationCommandTarget (commands are implemented in MainCommands.cpp). */
    class MainComponent : public juce::Component,
                          public juce::ApplicationCommandTarget,
                          public juce::MenuBarModel,
                          private juce::ChangeListener,
                          private juce::Timer
    {
    public:
        MainComponent();
        ~MainComponent() override;

        void resized() override;
        void paint (juce::Graphics&) override;

        /** Called after a project is opened/closed so the workspace rebuilds. */
        void sessionChanged();

        /** Asks to save unsaved changes, then calls onDone(true) if it's OK to continue. */
        void confirmDiscardOrSave (std::function<void (bool)> onDone);

        void openProject (const juce::File& editFile, const juce::File& recoverFrom = {});
        void openProjectChecked (const juce::File& editFile);   // offers autosave recovery first
        void newProjectDialog();
        void openProjectDialog();

        // ApplicationCommandTarget
        juce::ApplicationCommandTarget* getNextCommandTarget() override  { return nullptr; }
        void getAllCommands (juce::Array<juce::CommandID>&) override;
        void getCommandInfo (juce::CommandID, juce::ApplicationCommandInfo&) override;
        bool perform (const InvocationInfo&) override;

        // MenuBarModel
        juce::StringArray getMenuBarNames() override;
        juce::PopupMenu getMenuForIndex (int topLevelMenuIndex, const juce::String&) override;
        void menuItemSelected (int, int) override {}

    private:
        juce::MenuBarComponent menuBar { this };
        std::unique_ptr<StatusBar> statusBar;
        std::unique_ptr<TransportBar> transportBar;
        std::unique_ptr<ArrangeView> arrangeView;
        std::unique_ptr<InspectorPanel> inspector;
        bool showInspector = true;
        std::unique_ptr<LowerZone> lowerZone;
        bool showLowerZone = false;
        int lowerZoneHeight = 360;
        void setLowerZoneVisible (bool, int page);
        std::unique_ptr<juce::Component> placeholder;
        juce::TooltipWindow tooltipWindow { this, 600 };

        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void timerCallback() override;
        void updateTitle();
        void saveProject (std::function<void (bool)> onDone = nullptr);
        void saveProjectAsDialog (std::function<void (bool)> onDone = nullptr);
        void openPluginFromFileDialog();
        void importAudioDialog();
        void importMidiDialog();
        void exportMidiDialog();
        void addInstrumentTrackMenu();
        void performTrackCommand (int commandID);
        void deleteSelection();

        ProjectSession* getSession() const;

        std::unique_ptr<juce::FileChooser> chooser;
        juce::String lastTitle;
    };
}
