#pragma once

#include "../../PCH.h"
#include "../../Render/ExportService.h"

namespace cdaw
{
    class ProjectSession;

    /** Export mixdown or stems. */
    class ExportDialog : public juce::Component,
                         private juce::Timer
    {
    public:
        ExportDialog (ProjectSession&, bool stems);
        ~ExportDialog() override;

        static void show (ProjectSession&, bool stems);

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        void timerCallback() override;
        void startExport();
        void updateEnablement();
        ExportService::Options readOptions() const;
        juce::Array<te::Track*> getStemTracks() const;

        ProjectSession& session;
        juce::ComboBox whatBox, rangeBox, formatBox, depthBox, rateBox, qualityBox, normaliseBox;
        juce::Slider tailSlider, levelSlider;
        juce::ToggleButton ditherToggle { "Dither (16/24-bit)" };
        juce::TextEditor nameEditor;
        juce::TextButton folderButton, exportButton { "Export" }, cancelButton { "Close" };
        juce::Label statusLabel;
        double progressValue = 0.0;
        juce::ProgressBar progressBar { progressValue };
        juce::File folder;
        std::unique_ptr<ExportService::Job> job;
        std::unique_ptr<juce::FileChooser> chooser;

        struct Row { juce::String label; juce::Component* comp; };
        juce::Array<Row> rows;
    };
}
