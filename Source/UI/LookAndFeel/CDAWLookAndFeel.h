#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

namespace cdaw
{
    /** Dark flat look used across CDAW. */
    class CDAWLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        CDAWLookAndFeel();

        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                                   bool isMouseOverButton, bool isButtonDown) override;

        void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                               float sliderPos, float minSliderPos, float maxSliderPos,
                               juce::Slider::SliderStyle, juce::Slider&) override;

        void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPosProportional,
                               float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;

        juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
        juce::Font getPopupMenuFont() override;
        juce::Font getMenuBarFont (juce::MenuBarComponent&, int itemIndex, const juce::String& itemText) override;

        void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;
    };
}
