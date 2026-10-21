#include "ParamKnob.h"
#include "../../UI/LookAndFeel/Palette.h"

namespace cdaw
{
    struct ParamKnob::Dial : public juce::Slider
    {
        explicit Dial (ParamKnob& o) : owner (o)
        {
            setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
            setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
            setMouseDragSensitivity (220);
            setVelocityBasedMode (false);
        }

        juce::String getTextFromValue (double v) override
        {
            return owner.param != nullptr ? owner.param->getText ((float) owner.toParam (v)) : juce::String (v, 2);
        }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat().reduced (3.0f);
            auto size = juce::jmin (r.getWidth(), r.getHeight());
            auto centre = r.getCentre();
            auto radius = size * 0.5f;
            auto params = getRotaryParameters();
            auto toAngle = [&] (double value)
            {
                auto prop = (float) valueToProportionOfLength (juce::jlimit (getMinimum(), getMaximum(), value));
                return params.startAngleRadians + prop * (params.endAngleRadians - params.startAngleRadians);
            };

            // Track, then the value arc from the reference point (default for bipolar ranges).
            const float thickness = juce::jmax (2.5f, radius * 0.16f);
            juce::Path track;
            track.addCentredArc (centre.x, centre.y, radius - thickness, radius - thickness, 0.0f,
                                 params.startAngleRadians, params.endAngleRadians, true);
            g.setColour (Palette::windowBackground);
            g.strokePath (track, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            const bool bipolar = getMinimum() < 0.0 && getMaximum() > 0.0;
            auto from = toAngle (bipolar ? 0.0 : getMinimum());
            auto to = toAngle (getValue());
            juce::Path arc;
            arc.addCentredArc (centre.x, centre.y, radius - thickness, radius - thickness, 0.0f,
                               juce::jmin (from, to), juce::jmax (from, to), true);
            g.setColour (isEnabled() ? owner.colour : owner.colour.withSaturation (0.0f).withAlpha (0.4f));
            g.strokePath (arc, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            // Knob body and pointer.
            auto body = radius - thickness * 2.2f;
            g.setGradientFill (juce::ColourGradient (Palette::panelRaised.brighter (0.25f), centre.x, centre.y - body,
                                                     Palette::panelRaised.darker (0.3f), centre.x, centre.y + body, false));
            g.fillEllipse (centre.x - body, centre.y - body, body * 2.0f, body * 2.0f);
            g.setColour (Palette::panelOutline.brighter (0.2f));
            g.drawEllipse (centre.x - body, centre.y - body, body * 2.0f, body * 2.0f, 1.0f);

            juce::Path pointer;
            pointer.addRoundedRectangle (-1.5f, -body + 2.0f, 3.0f, body * 0.55f, 1.5f);
            g.setColour (isEnabled() ? Palette::text : Palette::textDisabled);
            g.fillPath (pointer, juce::AffineTransform::rotation (to).translated (centre));
        }

        ParamKnob& owner;
    };

    ParamKnob::ParamKnob (std::shared_ptr<KnobParameter> p, const juce::String& l, juce::Colour c, float defaultValue, float skewMidPoint, bool invert)
        : label (l), colour (c), dial (std::make_unique<Dial> (*this))
    {
        addAndMakeVisible (*dial);

        dial->onDragStart = [this] { if (param != nullptr) param->beginGesture(); };
        dial->onDragEnd   = [this] { if (param != nullptr) param->endGesture(); };
        dial->onValueChange = [this]
        {
            if (param != nullptr)
            {
                param->setValue ((float) toParam (dial->getValue()));
                refresh();
            }
        };

        setParameter (std::move (p), defaultValue, skewMidPoint, invert);
    }

    ParamKnob::~ParamKnob() = default;

    double ParamKnob::toDial (double v) const
    {
        if (! inverted || param == nullptr)
            return v;

        auto range = param->getRange();
        return range.getStart() + range.getEnd() - v;
    }

    double ParamKnob::toParam (double d) const
    {
        return toDial (d);      // the mapping is its own inverse
    }

    void ParamKnob::setParameter (std::shared_ptr<KnobParameter> p, float defaultValue, float skewMidPoint, bool invert)
    {
        param = std::move (p);
        inverted = invert;
        auto range = param->getRange();
        dial->setRange (range.getStart(), range.getEnd(), 0.0);

        auto mid = toDial (skewMidPoint);
        if (skewMidPoint != 0.0f && mid > range.getStart() && mid < range.getEnd())
            dial->setSkewFactorFromMidPoint (mid);
        else
            dial->setSkewFactor (1.0);

        dial->setDoubleClickReturnValue (true, toDial (defaultValue));
        dial->setValue (toDial (param->getValue()), juce::dontSendNotification);
        shownValue = {};
        refresh();
        dial->repaint();
    }

    void ParamKnob::setColour (juce::Colour c)
    {
        colour = c;
        dial->repaint();
    }

    void ParamKnob::setLabel (const juce::String& l)
    {
        if (label != l)
        {
            label = l;
            repaint();
        }
    }

    void ParamKnob::refresh()
    {
        if (param == nullptr)
            return;

        if (! dial->isMouseButtonDown() && std::abs (dial->getValue() - toDial (param->getValue())) > 1.0e-6)
            dial->setValue (toDial (param->getValue()), juce::dontSendNotification);

        auto text = param->getCurrentText();
        if (text != shownValue)
        {
            shownValue = text;
            dial->setTooltip (label + ": " + text);
            repaint();
        }
    }

    void ParamKnob::resized()
    {
        auto r = getLocalBounds();
        r.removeFromTop (14);
        r.removeFromBottom (15);
        dial->setBounds (r);
    }

    void ParamKnob::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds();
        g.setFont (juce::FontOptions (11.0f));
        g.setColour (isEnabled() ? Palette::textDim : Palette::textDisabled);
        g.drawText (label, r.removeFromTop (14), juce::Justification::centred, true);

        g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
        g.setColour (isEnabled() ? Palette::text : Palette::textDisabled);
        g.drawText (shownValue, r.removeFromBottom (15), juce::Justification::centred, true);
    }
}
