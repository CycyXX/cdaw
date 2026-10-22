#include "MissingFilesDialog.h"
#include "../LookAndFeel/Palette.h"
#include "../../Model/ProjectSession.h"

namespace cdaw
{
    namespace
    {
        constexpr int rowHeight = 44, margin = 14, headerHeight = 54, buttonHeight = 28;
    }

    MissingFilesDialog::MissingFilesDialog (ProjectSession& s) : session (s)
    {
        setSize (640, 420);

        list.setRowHeight (rowHeight);
        list.setColour (juce::ListBox::backgroundColourId, Palette::windowBackground);
        addAndMakeVisible (list);

        locateButton.setTooltip ("Pick the file for the selected entry. Other missing files in the same folder are found too.");
        searchButton.setTooltip ("Pick a folder; every missing file found in it (or below it) is reconnected.");
        locateButton.onClick = [this] { locateSelected(); };
        searchButton.onClick = [this] { searchFolder(); };
        doneButton.onClick = [this] { closeDialog(); };

        for (auto* b : { &locateButton, &searchButton, &doneButton })
            addAndMakeVisible (b);

        refresh();
    }

    MissingFilesDialog::~MissingFilesDialog() = default;

    void MissingFilesDialog::show (ProjectSession& session, bool evenIfNothing)
    {
        if (! evenIfNothing && MissingMedia::findMissingAudio (session.getEdit()).isEmpty()
             && MissingMedia::findMissingPlugins (session.getEdit()).isEmpty())
            return;

        juce::DialogWindow::LaunchOptions o;
        o.dialogTitle = "Missing files - " + session.getName();
        o.dialogBackgroundColour = Palette::panelBackground;
        o.content.setOwned (new MissingFilesDialog (session));
        o.escapeKeyTriggersCloseButton = true;
        o.useNativeTitleBar = true;
        o.resizable = true;
        o.launchAsync();
    }

    void MissingFilesDialog::refresh()
    {
        missing = MissingMedia::findMissingAudio (session.getEdit());
        missingPlugins = MissingMedia::findMissingPlugins (session.getEdit());

        list.updateContent();
        list.repaint();

        if (! missing.isEmpty() && list.getSelectedRow() < 0)
            list.selectRow (0);

        locateButton.setEnabled (! missing.isEmpty());
        searchButton.setEnabled (! missing.isEmpty());
        doneButton.setButtonText (missing.isEmpty() ? "Done" : "Close");
        resized();
        repaint();
    }

    void MissingFilesDialog::locateSelected()
    {
        auto row = list.getSelectedRow();

        if (! juce::isPositiveAndBelow (row, missing.size()))
            return;

        auto entry = missing[row];
        auto start = entry.expected.getParentDirectory().isDirectory() ? entry.expected.getParentDirectory() : session.getAudioFolder();

        chooser = std::make_unique<juce::FileChooser> ("Locate " + entry.expected.getFileName(), start,
                                                       "*" + entry.expected.getFileExtension());
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this, entry] (const juce::FileChooser& fc)
                              {
                                  auto file = fc.getResult();

                                  if (! file.existsAsFile())
                                      return;

                                  MissingMedia::relink (entry, file);

                                  // Others that went missing together are usually beside it.
                                  auto others = MissingMedia::relinkFromFolders (session.getEdit(), { file.getParentDirectory() });
                                  session.getEdit().markAsChanged();
                                  message = "Reconnected " + file.getFileName()
                                              + (others > 0 ? " and " + juce::String (others) + " more from the same folder" : juce::String());
                                  refresh();
                              });
    }

    void MissingFilesDialog::searchFolder()
    {
        chooser = std::make_unique<juce::FileChooser> ("Search this folder (and its subfolders) for the missing files",
                                                       session.getProjectFolder());
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [this] (const juce::FileChooser& fc)
                              {
                                  auto folder = fc.getResult();

                                  if (! folder.isDirectory())
                                      return;

                                  auto found = MissingMedia::relinkFromFolders (session.getEdit(), { folder });

                                  if (found > 0)
                                      session.getEdit().markAsChanged();

                                  message = found > 0 ? "Reconnected " + juce::String (found) + " file(s) from " + folder.getFileName()
                                                      : "None of the missing files are in " + folder.getFileName();
                                  refresh();
                              });
    }

    void MissingFilesDialog::closeDialog()
    {
        if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
            window->exitModalState (0);
    }

    void MissingFilesDialog::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
    {
        if (! juce::isPositiveAndBelow (row, missing.size()))
            return;

        auto& m = missing.getReference (row);
        g.fillAll (selected ? Palette::accentDim : (row % 2 ? Palette::laneAlternate : Palette::laneBackground));

        auto r = juce::Rectangle<int> (0, 0, width, height).reduced (10, 4);
        g.setColour (Palette::text);
        g.setFont (juce::FontOptions (13.5f, juce::Font::bold));
        g.drawText (m.expected.getFileName(), r.removeFromTop (18), juce::Justification::centredLeft, true);

        g.setColour (selected ? Palette::text.withAlpha (0.8f) : Palette::textDim);
        g.setFont (juce::FontOptions (11.0f));
        auto clips = m.clipNames.size() == 1 ? "clip " + m.clipNames[0] : juce::String (m.clipNames.size()) + " clips";
        g.drawText ("Expected in " + m.expected.getParentDirectory().getFullPathName() + "   -   " + clips, r,
                    juce::Justification::centredLeft, true);
    }

    void MissingFilesDialog::resized()
    {
        auto r = getLocalBounds().reduced (margin);
        r.removeFromTop (headerHeight);

        auto buttons = r.removeFromBottom (buttonHeight);
        r.removeFromBottom (8);

        if (! missingPlugins.isEmpty())
            r.removeFromBottom (juce::jmin (missingPlugins.size(), 4) * 16 + 28);

        doneButton.setBounds (buttons.removeFromRight (90));
        buttons.removeFromRight (8);
        searchButton.setBounds (buttons.removeFromRight (130));
        buttons.removeFromRight (8);
        locateButton.setBounds (buttons.removeFromRight (120));

        list.setBounds (r);
    }

    void MissingFilesDialog::paint (juce::Graphics& g)
    {
        g.fillAll (Palette::panelBackground);
        auto r = getLocalBounds().reduced (margin);

        g.setColour (Palette::text);
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));

        if (missing.isEmpty())
            g.drawText (missingPlugins.isEmpty() ? "All audio files were found" : "Audio files are all there",
                        r.removeFromTop (22), juce::Justification::centredLeft);
        else
            g.drawText (juce::String (missing.size()) + (missing.size() == 1 ? " audio file can't be found" : " audio files can't be found"),
                        r.removeFromTop (22), juce::Justification::centredLeft);

        g.setColour (Palette::textDim);
        g.setFont (juce::FontOptions (12.0f));
        g.drawFittedText (message.isNotEmpty() ? message
                                                : juce::String ("They are not where the project expects them. Point CDAW at a file, or at a folder to search; "
                                                                "nothing is changed until you do."),
                          r.removeFromTop (headerHeight - 22), juce::Justification::topLeft, 2);

        if (! missingPlugins.isEmpty())
        {
            auto p = getLocalBounds().reduced (margin);
            p.removeFromBottom (buttonHeight + 8);
            p = p.removeFromBottom (juce::jmin (missingPlugins.size(), 4) * 16 + 28);

            g.setColour (Palette::meterYellow);
            g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
            g.drawText ("Plugins that could not be loaded (reinstall them, or rescan plugins):", p.removeFromTop (20), juce::Justification::centredLeft);

            g.setColour (Palette::textDim);
            g.setFont (juce::FontOptions (12.0f));

            for (int i = 0; i < juce::jmin (missingPlugins.size(), 4); ++i)
                g.drawText (missingPlugins[i] + (i == 3 && missingPlugins.size() > 4 ? "  ... and " + juce::String (missingPlugins.size() - 4) + " more" : juce::String()),
                            p.removeFromTop (16), juce::Justification::centredLeft, true);
        }
    }
}
