#include "ExportDialog.h"
#include "../LookAndFeel/Palette.h"
#include "../../Model/ProjectSession.h"
#include "../../Model/TrackOps.h"

namespace cdaw
{
    using namespace ExportService;

    ExportDialog::ExportDialog (ProjectSession& s, bool stems) : session (s)
    {
        auto& engine = session.getEdit().engine;
        folder = session.getBouncesFolder();

        whatBox.addItemList ({ "Mixdown (master)", "Stems: all tracks", "Stems: selected tracks" }, 1);
        whatBox.setSelectedId (stems ? 2 : 1);
        rangeBox.addItemList ({ "Whole song", "Loop range" }, 1);
        rangeBox.setSelectedId (session.getTransport().looping.get() ? 2 : 1);

        formatBox.addItem ("WAV", 1);
        formatBox.addItem ("FLAC", 2);
        formatBox.addItem ("OGG Vorbis", 3);
        formatBox.addItem (isFormatAvailable (engine, Format::mp3) ? "MP3" : "MP3 (put libmp3lame.dll next to CDAW.exe)", 4);
        formatBox.setItemEnabled (4, isFormatAvailable (engine, Format::mp3));
        formatBox.setSelectedId (1);

        depthBox.addItem ("16-bit", 16);
        depthBox.addItem ("24-bit", 24);
        depthBox.addItem ("32-bit float", 32);
        depthBox.setSelectedId (24);

        for (double r : { 44100.0, 48000.0, 88200.0, 96000.0 })
            rateBox.addItem (juce::String (r / 1000.0, 1) + " kHz", (int) r);

        auto deviceRate = (int) engine.getDeviceManager().getSampleRate();
        rateBox.setSelectedId (rateBox.indexOfItemId (deviceRate) >= 0 ? deviceRate : 48000);

        normaliseBox.addItemList ({ "No normalising", "Normalise peak (dBFS)", "Normalise loudness (LUFS)" }, 1);
        normaliseBox.setSelectedId (1);

        tailSlider.setRange (0.0, 10.0, 0.5);
        tailSlider.setValue (2.0);
        tailSlider.setTextValueSuffix (" s");
        tailSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 20);

        levelSlider.setRange (-30.0, 0.0, 0.1);
        levelSlider.setValue (-1.0);
        levelSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 20);

        ditherToggle.setToggleState (true, juce::dontSendNotification);

        nameEditor.setText (session.getName() + (stems ? "" : " mix"));
        folderButton.setButtonText (folder.getFullPathName());
        folderButton.onClick = [this]
        {
            chooser = std::make_unique<juce::FileChooser> ("Export folder", folder);
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                  [this] (const juce::FileChooser& fc)
                                  {
                                      if (fc.getResult().isDirectory())
                                      {
                                          folder = fc.getResult();
                                          folderButton.setButtonText (folder.getFullPathName());
                                      }
                                  });
        };

        auto onChange = [this] { updateEnablement(); };
        for (auto* box : { &whatBox, &formatBox, &normaliseBox })
            box->onChange = onChange;

        const std::pair<const char*, juce::Component*> layout[] = {
            { "Export", &whatBox }, { "Range", &rangeBox }, { "Format", &formatBox }, { "Bit depth", &depthBox },
            { "Quality", &qualityBox }, { "Sample rate", &rateBox }, { "Tail", &tailSlider },
            { "Normalise", &normaliseBox }, { "Level", &levelSlider }, { "", &ditherToggle },
            { "File name", &nameEditor }, { "Folder", &folderButton }
        };

        for (auto& [label, comp] : layout)
            rows.add (Row { label, comp });

        for (auto& r : rows)
            addAndMakeVisible (r.comp);

        exportButton.onClick = [this] { startExport(); };
        cancelButton.onClick = [this]
        {
            if (job != nullptr && ! job->isFinished())
                job->cancel();
            else if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
                dw->exitModalState (0);
        };

        statusLabel.setColour (juce::Label::textColourId, Palette::textDim);
        addAndMakeVisible (exportButton);
        addAndMakeVisible (cancelButton);
        addAndMakeVisible (statusLabel);
        addChildComponent (progressBar);

        updateEnablement();
        setSize (560, 520);
    }

    ExportDialog::~ExportDialog()
    {
        job.reset();
    }

    void ExportDialog::show (ProjectSession& s, bool stems)
    {
        juce::DialogWindow::LaunchOptions o;
        o.dialogTitle = stems ? "Export Stems" : "Export Mixdown";
        o.dialogBackgroundColour = Palette::panelBackground;
        o.content.setOwned (new ExportDialog (s, stems));
        o.useNativeTitleBar = true;
        o.resizable = false;
        o.launchAsync();
    }

    void ExportDialog::updateEnablement()
    {
        auto format = formatBox.getSelectedId();
        depthBox.setEnabled (format == 1 || format == 2);
        ditherToggle.setEnabled (format == 1 || format == 2);

        qualityBox.clear (juce::dontSendNotification);
        auto fmt = (Format) (format - 1);
        auto& afm = session.getEdit().engine.getAudioFileFormatManager();
        auto* af = fmt == Format::ogg ? afm.getOggFormat() : fmt == Format::mp3 ? afm.getLameFormat() : nullptr;

        if (af != nullptr)
        {
            auto q = af->getQualityOptions();
            for (int i = 0; i < q.size(); ++i)
                qualityBox.addItem (q[i], i + 1);
            qualityBox.setSelectedItemIndex (juce::jmax (0, q.size() - 1));
        }

        qualityBox.setEnabled (af != nullptr);

        auto norm = normaliseBox.getSelectedId();
        levelSlider.setEnabled (norm > 1);
        levelSlider.setTextValueSuffix (norm == 3 ? " LUFS" : " dBFS");
        if (norm == 3 && levelSlider.getValue() > -6.0)  levelSlider.setValue (-14.0);
        if (norm == 2 && levelSlider.getValue() < -6.0)  levelSlider.setValue (-1.0);

        nameEditor.setEnabled (whatBox.getSelectedId() == 1);
    }

    ExportService::Options ExportDialog::readOptions() const
    {
        Options o;
        o.format = (Format) (formatBox.getSelectedId() - 1);
        o.bitDepth = depthBox.getSelectedId();
        o.sampleRate = (double) rateBox.getSelectedId();
        o.range = rangeBox.getSelectedId() == 2 ? Range::loop : Range::song;
        o.tailSeconds = tailSlider.getValue();
        o.normalise = normaliseBox.getSelectedId() == 2 ? Normalise::peak : normaliseBox.getSelectedId() == 3 ? Normalise::lufs : Normalise::none;
        o.normaliseLevel = (float) levelSlider.getValue();
        o.dither = ditherToggle.getToggleState();
        o.mp3QualityIndex = qualityBox.getSelectedItemIndex();
        return o;
    }

    juce::Array<te::Track*> ExportDialog::getStemTracks() const
    {
        juce::Array<te::Track*> tracks;
        auto& edit = session.getEdit();
        const bool selectedOnly = whatBox.getSelectedId() == 3;

        for (auto t : te::getAudioTracks (edit))
        {
            if (selectedOnly && ! session.getSelectionManager().isSelected (t))
                continue;

            if (t->isMuted (true))
                continue;

            tracks.add (t);
        }

        return tracks;
    }

    void ExportDialog::startExport()
    {
        auto& edit = session.getEdit();
        auto options = readOptions();
        auto ext = getExtension (options.format);

        if (getRenderRange (edit, options).isEmpty())
        {
            statusLabel.setText ("Nothing to export: the project is empty (or the loop range is empty).", juce::dontSendNotification);
            return;
        }

        std::vector<Job::Item> items;

        if (whatBox.getSelectedId() == 1)
        {
            auto name = juce::File::createLegalFileName (nameEditor.getText().trim());
            if (name.isEmpty()) name = session.getName();
            auto dest = folder.getChildFile (name + ext);
            items.push_back ({ makeParameters (edit, options, dest, {}), "Mixdown" });
        }
        else
        {
            auto tracks = getStemTracks();

            if (tracks.isEmpty())
            {
                statusLabel.setText ("No tracks to export (select tracks first, or choose all tracks).", juce::dontSendNotification);
                return;
            }

            auto stemFolder = folder.getChildFile (juce::File::createLegalFileName (session.getName() + " stems"));
            int i = 1;

            for (auto t : tracks)
            {
                auto name = juce::String (i++).paddedLeft ('0', 2) + " " + juce::File::createLegalFileName (t->getName());
                items.push_back ({ makeParameters (edit, options, stemFolder.getChildFile (name + ext), { t }), t->getName() });
            }
        }

        for (auto& r : rows)
            r.comp->setEnabled (false);

        exportButton.setEnabled (false);
        cancelButton.setButtonText ("Cancel");
        progressBar.setVisible (true);
        statusLabel.setText ("Rendering...", juce::dontSendNotification);

        job = std::make_unique<Job> (std::move (items), [this] (bool ok, juce::StringArray results)
        {
            stopTimer();
            progressValue = 1.0;
            cancelButton.setButtonText ("Close");

            for (auto& r : rows)
                r.comp->setEnabled (true);

            exportButton.setEnabled (true);
            updateEnablement();

            if (ok)
            {
                statusLabel.setText ("Done: " + juce::String (results.size()) + " file(s) written.", juce::dontSendNotification);
                juce::File (results[0]).revealToUser();
            }
            else
            {
                statusLabel.setText (results.isEmpty() ? juce::String ("Cancelled.") : results.joinIntoString ("\n").getLastCharacters (200),
                                     juce::dontSendNotification);
            }

            juce::Logger::writeToLog ("Export " + juce::String (ok ? "finished" : "failed/cancelled") + ":\n" + results.joinIntoString ("\n"));
        });

        startTimerHz (10);
    }

    void ExportDialog::timerCallback()
    {
        if (job != nullptr)
        {
            progressValue = job->getProgress();
            statusLabel.setText ("Rendering " + job->getCurrentDescription() + "...", juce::dontSendNotification);
        }
    }

    void ExportDialog::paint (juce::Graphics& g)
    {
        g.fillAll (Palette::panelBackground);
        g.setColour (Palette::textDim);
        g.setFont (juce::FontOptions (13.0f));

        for (auto& r : rows)
            if (r.label.isNotEmpty())
                g.drawText (r.label, r.comp->getBounds().withX (16).withWidth (110), juce::Justification::centredLeft);
    }

    void ExportDialog::resized()
    {
        auto r = getLocalBounds().reduced (16);
        auto buttons = r.removeFromBottom (30);
        cancelButton.setBounds (buttons.removeFromRight (100));
        buttons.removeFromRight (8);
        exportButton.setBounds (buttons.removeFromRight (120));

        r.removeFromBottom (8);
        statusLabel.setBounds (r.removeFromBottom (36));
        progressBar.setBounds (r.removeFromBottom (18));
        r.removeFromBottom (8);

        for (auto& row : rows)
        {
            auto line = r.removeFromTop (28);
            row.comp->setBounds (line.withTrimmedLeft (120).reduced (0, 2));
            r.removeFromTop (3);
        }
    }
}
