#pragma once

#include "../../PCH.h"
#include "../../Model/EditViewState.h"
#include "../Common/LevelMeter.h"

namespace cdaw
{
    /** One audio input of the interface in the mixer: its name, level meter, peak readout and
        the tracks it feeds. Right-click pairs it with its neighbour or splits a pair. */
    class InputStrip : public juce::Component,
                       public juce::SettableTooltipClient,
                       private te::SelectableListener,
                       private juce::Timer
    {
    public:
        InputStrip (te::Edit&, te::WaveInputDevice&);
        ~InputStrip() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;

        /** The engine rebuilt its inputs and this one is gone (set by the panel). */
        std::function<void()> onDeviceDeleted;

        static constexpr int width = 62;

    private:
        void timerCallback() override;
        void selectableObjectChanged (te::Selectable*) override {}
        void selectableObjectAboutToBeDeleted (te::Selectable*) override;
        void showMenu();
        juce::String describeTargets() const;

        te::Edit& edit;
        te::WaveInputDevice* device;
        LevelMeter meter;
        te::LevelMeasurer::Client peakClient;
        float peakDb = -100.0f;
        juce::String targets;
        int firstChannel = 0, numChannels = 1;
    };

    /** The "Inputs" section at the left of the mixer: one strip per enabled audio input. It
        folds to a narrow tab. */
    class InputsPanel : public juce::Component,
                        private juce::ChangeListener,
                        private juce::AsyncUpdater
    {
    public:
        explicit InputsPanel (EditViewState&);
        ~InputsPanel() override;

        /** Width this panel wants: the strips when open, the tab when folded. */
        int getIdealWidth() const;

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseUp (const juce::MouseEvent&) override;

        /** Called when the panel opens or folds, so the mixer can lay out again. */
        std::function<void()> onWidthChanged;

        static constexpr int headerH = 22, foldedWidth = 20;

    private:
        bool isOpen() const;
        void setOpen (bool);
        void rebuild();
        void changeListenerCallback (juce::ChangeBroadcaster*) override   { triggerAsyncUpdate(); }
        void handleAsyncUpdate() override                                  { rebuild(); }

        EditViewState& viewState;
        juce::Viewport viewport;
        juce::Component holder;
        juce::OwnedArray<InputStrip> strips;
        juce::TextButton foldButton;
    };
}
