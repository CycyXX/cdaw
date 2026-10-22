#pragma once

#include "../../PCH.h"
#include "../../Model/MissingMedia.h"

namespace cdaw
{
    class ProjectSession;

    /** Lists the audio files (and plugins) a project can't find and lets the user reconnect them:
        point at one file, or at a folder to search. */
    class MissingFilesDialog : public juce::Component,
                               private juce::ListBoxModel
    {
    public:
        explicit MissingFilesDialog (ProjectSession&);
        ~MissingFilesDialog() override;

        /** Opens the dialog (modal). Does nothing if there is nothing missing and `evenIfNothing` is false. */
        static void show (ProjectSession&, bool evenIfNothing = false);

        void resized() override;
        void paint (juce::Graphics&) override;

    private:
        int getNumRows() override                       { return missing.size(); }
        void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;

        void refresh();
        void locateSelected();
        void searchFolder();
        void closeDialog();

        ProjectSession& session;
        juce::Array<MissingMedia::MissingFile> missing;
        juce::StringArray missingPlugins;
        juce::String message;

        juce::ListBox list { {}, this };
        juce::TextButton locateButton { "Locate File..." }, searchButton { "Search Folder..." }, doneButton { "Done" };
        std::unique_ptr<juce::FileChooser> chooser;
    };
}
