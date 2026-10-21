#pragma once

#include "../../PCH.h"

namespace cdaw
{
    /** Metronome settings: volume, sound, accent, output. Shown in a call-out box. */
    class MetronomePanel : public juce::Component
    {
    public:
        explicit MetronomePanel (te::Edit&);

        /** Opens the panel as a call-out next to `target`. */
        static void show (te::Edit&, juce::Component& target);

        void paint (juce::Graphics&) override;
        void resized() override;

        /** Built-in sound names; "Classic" is the engine's own click. */
        static juce::StringArray getSoundNames();
        static void applySound (te::Engine&, const juce::String& soundName);
        static juce::String getCurrentSoundName (te::Engine&);

    private:
        void pickCustomFile (bool accent);
        void refresh();

        te::Edit& edit;
        juce::Slider volume;
        juce::ComboBox sound, output;
        juce::ToggleButton accent { "Accent the first beat of each bar" },
                           recordOnly { "Only while recording" };
        juce::TextButton customAccent { "Accent sample..." }, customBeat { "Beat sample..." };
        juce::Label volumeLabel, soundLabel, outputLabel;
        std::unique_ptr<juce::FileChooser> chooser;
    };
}
