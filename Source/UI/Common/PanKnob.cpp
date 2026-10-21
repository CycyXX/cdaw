#include "PanKnob.h"
#include "../LookAndFeel/Palette.h"

namespace cdaw
{
    PanKnob::PanKnob()
    {
        setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        setRange (-1.0, 1.0, 0.01);
        setDoubleClickReturnValue (true, 0.0);
        setPopupDisplayEnabled (true, false, nullptr);
    }

    juce::String PanKnob::describe (double pan)
    {
        auto percent = juce::roundToInt (std::abs (pan) * 100.0);

        if (percent == 0)
            return "C";

        return (pan < 0 ? "L " : "R ") + juce::String (percent);
    }

    juce::String PanKnob::getTextFromValue (double v)
    {
        return describe (v);
    }

    double PanKnob::getValueFromText (const juce::String& text)
    {
        auto t = text.trim().toUpperCase();

        if (t.isEmpty() || t.startsWith ("C"))
            return 0.0;

        auto amount = juce::jlimit (0.0, 100.0, std::abs (t.retainCharacters ("0123456789.").getDoubleValue())) / 100.0;
        return (t.startsWith ("L") || t.startsWith ("-")) ? -amount : amount;
    }

    juce::String PanKnob::getTooltip()
    {
        return "Pan: " + describe (getValue()) + "\nDrag to change, double-click to centre";
    }

    void PanKnob::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds();
        auto letterW = (r.getWidth() - r.getHeight()) / 2;

        // The dial sits in the middle; the letters use the space on either side, level with the arc ends.
        if (letterW >= 5)
        {
            auto& lf = getLookAndFeel();
            auto dial = r.withSizeKeepingCentre (r.getHeight(), r.getHeight());
            auto rotary = getRotaryParameters();
            lf.drawRotarySlider (g, dial.getX(), dial.getY(), dial.getWidth(), dial.getHeight(),
                                 (float) valueToProportionOfLength (getValue()),
                                 rotary.startAngleRadians, rotary.endAngleRadians, *this);

            const auto v = getValue();
            const float fontH = juce::jlimit (8.0f, 11.0f, (float) r.getHeight() * 0.38f);
            g.setFont (juce::FontOptions (fontH, juce::Font::bold));
            auto letters = r.withTrimmedTop (r.getHeight() / 3);

            // drawText drops glyphs that don't fit, so give each letter room around its column centre.
            auto leftBox = letters.removeFromLeft (letterW).expanded (6, 0);
            auto rightBox = letters.removeFromRight (letterW).expanded (6, 0);

            g.setColour (v < -0.005 ? Palette::text : Palette::textDim);
            g.drawText ("L", leftBox, juce::Justification::centred, false);
            g.setColour (v > 0.005 ? Palette::text : Palette::textDim);
            g.drawText ("R", rightBox, juce::Justification::centred, false);
            return;
        }

        juce::Slider::paint (g);
    }
}
