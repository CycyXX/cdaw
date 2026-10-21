#include "CDAWLookAndFeel.h"
#include "Palette.h"

namespace cdaw
{
    CDAWLookAndFeel::CDAWLookAndFeel()
    {
        using namespace juce;

        auto scheme = LookAndFeel_V4::ColourScheme {
            Palette::windowBackground,  // windowBackground
            Palette::panelBackground,   // widgetBackground
            Palette::panelRaised,       // menuBackground
            Palette::panelOutline,      // outline
            Palette::text,              // defaultText
            Palette::panelRaised,       // defaultFill
            Palette::text,              // highlightedText
            Palette::accentDim,         // highlightedFill
            Palette::text               // menuText
        };
        setColourScheme (scheme);

        setColour (ResizableWindow::backgroundColourId, Palette::windowBackground);
        setColour (DocumentWindow::backgroundColourId, Palette::windowBackground);
        setColour (TextButton::buttonColourId, Palette::panelRaised);
        setColour (TextButton::buttonOnColourId, Palette::accentDim);
        setColour (TextButton::textColourOffId, Palette::text);
        setColour (TextButton::textColourOnId, Palette::text);
        setColour (ComboBox::backgroundColourId, Palette::panelRaised);
        setColour (ComboBox::outlineColourId, Palette::panelOutline);
        setColour (ComboBox::textColourId, Palette::text);
        setColour (ComboBox::arrowColourId, Palette::textDim);
        setColour (Label::textColourId, Palette::text);
        setColour (TextEditor::backgroundColourId, Palette::laneBackground);
        setColour (TextEditor::outlineColourId, Palette::panelOutline);
        setColour (TextEditor::focusedOutlineColourId, Palette::accent);
        setColour (TextEditor::textColourId, Palette::text);
        setColour (TextEditor::highlightColourId, Palette::selection);
        setColour (Slider::thumbColourId, Palette::text);
        setColour (Slider::trackColourId, Palette::accent);
        setColour (Slider::backgroundColourId, Palette::laneBackground);
        setColour (Slider::rotarySliderFillColourId, Palette::accent);
        setColour (Slider::rotarySliderOutlineColourId, Palette::panelOutline);
        setColour (Slider::textBoxTextColourId, Palette::text);
        setColour (Slider::textBoxBackgroundColourId, Palette::laneBackground);
        setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
        setColour (PopupMenu::backgroundColourId, Palette::panelRaised);
        setColour (PopupMenu::highlightedBackgroundColourId, Palette::accentDim);
        setColour (PopupMenu::textColourId, Palette::text);
        setColour (PopupMenu::highlightedTextColourId, Palette::text);
        setColour (ListBox::backgroundColourId, Palette::laneBackground);
        setColour (ListBox::outlineColourId, Palette::panelOutline);
        setColour (ScrollBar::thumbColourId, Palette::panelOutline);
        setColour (TreeView::backgroundColourId, Palette::laneBackground);
        setColour (TreeView::selectedItemBackgroundColourId, Palette::accentDim);
        setColour (TooltipWindow::backgroundColourId, Palette::panelRaised);
        setColour (TooltipWindow::textColourId, Palette::text);
        setColour (TooltipWindow::outlineColourId, Palette::panelOutline);
        setColour (ToggleButton::textColourId, Palette::text);
        setColour (ToggleButton::tickColourId, Palette::accent);
        setColour (ToggleButton::tickDisabledColourId, Palette::textDisabled);
        setColour (TabbedButtonBar::tabTextColourId, Palette::textDim);
        setColour (TabbedButtonBar::frontTextColourId, Palette::text);
        setColour (TabbedComponent::backgroundColourId, Palette::panelBackground);
        setColour (TabbedComponent::outlineColourId, Palette::panelOutline);
        setColour (AlertWindow::backgroundColourId, Palette::panelBackground);
        setColour (AlertWindow::textColourId, Palette::text);
        setColour (AlertWindow::outlineColourId, Palette::panelOutline);
        setColour (ProgressBar::backgroundColourId, Palette::laneBackground);
        setColour (ProgressBar::foregroundColourId, Palette::accent);
        setColour (CaretComponent::caretColourId, Palette::accent);
        setColour (HyperlinkButton::textColourId, Palette::accent);
        setColour (PropertyComponent::backgroundColourId, Palette::panelBackground);
        setColour (PropertyComponent::labelTextColourId, Palette::text);
        setColour (KeyMappingEditorComponent::backgroundColourId, Palette::panelBackground);
        setColour (KeyMappingEditorComponent::textColourId, Palette::text);
    }

    void CDAWLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                                const juce::Colour& backgroundColour,
                                                bool isMouseOverButton, bool isButtonDown)
    {
        auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
        auto base = backgroundColour.withMultipliedSaturation (button.hasKeyboardFocus (true) ? 1.3f : 0.9f)
                                    .withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.5f);

        if (isButtonDown || isMouseOverButton)
            base = base.contrasting (isButtonDown ? 0.2f : 0.08f);

        g.setColour (base);
        g.fillRoundedRectangle (bounds, 3.0f);
        g.setColour (Palette::panelOutline);
        g.drawRoundedRectangle (bounds, 3.0f, 1.0f);
    }

    void CDAWLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                            float sliderPos, float minSliderPos, float maxSliderPos,
                                            juce::Slider::SliderStyle style, juce::Slider& slider)
    {
        if (style != juce::Slider::LinearVertical)
        {
            LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos,
                                              style, slider);
            return;
        }

        // Mixer-style fader: thin groove plus a flat cap.
        auto area = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
        auto groove = area.withSizeKeepingCentre (4.0f, area.getHeight());

        g.setColour (Palette::laneBackground);
        g.fillRoundedRectangle (groove, 2.0f);
        g.setColour (slider.findColour (juce::Slider::trackColourId).withAlpha (0.6f));
        g.fillRoundedRectangle (groove.withTop (sliderPos), 2.0f);

        auto cap = juce::Rectangle<float> (juce::jmin (28.0f, area.getWidth() - 2.0f), 14.0f)
                       .withCentre ({ area.getCentreX(), sliderPos });
        g.setColour (Palette::panelRaised.brighter (0.25f));
        g.fillRoundedRectangle (cap, 2.0f);
        g.setColour (Palette::panelOutline.brighter (0.3f));
        g.drawRoundedRectangle (cap, 2.0f, 1.0f);
        g.setColour (Palette::text);
        g.drawHorizontalLine ((int) cap.getCentreY(), cap.getX() + 3.0f, cap.getRight() - 3.0f);
    }

    void CDAWLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                            float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                                            juce::Slider& slider)
    {
        auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (2.0f);
        auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
        auto centre = bounds.getCentre();
        auto lineW = juce::jmax (2.0f, radius * 0.18f);
        auto arcRadius = radius - lineW * 0.5f;
        auto toAngle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

        juce::Path background;
        background.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
        g.setColour (Palette::panelOutline);
        g.strokePath (background, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        if (slider.isEnabled())
        {
            // Bipolar parameters (pan) fill from the centre.
            const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
            auto fromAngle = bipolar ? (rotaryStartAngle + rotaryEndAngle) * 0.5f : rotaryStartAngle;

            juce::Path valueArc;
            valueArc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                                    juce::jmin (fromAngle, toAngle), juce::jmax (fromAngle, toAngle), true);
            g.setColour (slider.findColour (juce::Slider::rotarySliderFillColourId));
            g.strokePath (valueArc, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        juce::Point<float> thumb (centre.x + (arcRadius - lineW) * std::cos (toAngle - juce::MathConstants<float>::halfPi),
                                  centre.y + (arcRadius - lineW) * std::sin (toAngle - juce::MathConstants<float>::halfPi));
        g.setColour (Palette::text);
        g.drawLine ({ centre, thumb }, 1.5f);
    }

    juce::Font CDAWLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
    {
        return juce::Font (juce::FontOptions (juce::jmin (14.0f, (float) buttonHeight * 0.6f)));
    }

    juce::Font CDAWLookAndFeel::getPopupMenuFont()
    {
        return juce::Font (juce::FontOptions (14.0f));
    }

    juce::Font CDAWLookAndFeel::getMenuBarFont (juce::MenuBarComponent& menuBar, int, const juce::String&)
    {
        return juce::Font (juce::FontOptions ((float) menuBar.getHeight() * 0.6f));
    }

    void CDAWLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
    {
        g.fillAll (Palette::panelRaised);
        g.setColour (Palette::panelOutline);
        g.drawRect (0, 0, width, height);
        g.setColour (Palette::text);
        g.setFont (juce::FontOptions (13.0f));
        g.drawFittedText (text, juce::Rectangle<int> (width, height).reduced (4, 2), juce::Justification::centredLeft, 4);
    }
}
