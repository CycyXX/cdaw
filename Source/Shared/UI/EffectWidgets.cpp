#include "EffectWidgets.h"
#include "../../UI/LookAndFeel/Palette.h"

namespace cdaw
{
    namespace EffectUI
    {
        void drawPanel (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title)
        {
            g.setColour (Palette::windowBackground);
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (Palette::panelOutline.withAlpha (0.7f));
            g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);

            if (title.isNotEmpty())
            {
                g.setColour (Palette::textDim);
                g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
                g.drawText (title, r.reduced (9.0f, 5.0f).removeFromTop (13.0f), juce::Justification::centredLeft);
            }
        }

        void drawHeader (juce::Graphics& g, juce::Rectangle<int> header, const juce::String& title, const juce::String& summary)
        {
            header = header.reduced (12, 0);
            g.setColour (Palette::text);
            g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
            g.drawText (title, header, juce::Justification::centredLeft);

            const auto titleWidth = (int) juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), title) + 26;
            g.setColour (Palette::textDim);
            g.setFont (juce::FontOptions (12.0f));
            g.drawText (summary, header.withTrimmedLeft (titleWidth), juce::Justification::centredLeft, true);
        }

        void layoutKnobGroups (std::vector<KnobGroup>& groups, const juce::OwnedArray<ParamKnob>& knobs, juce::Rectangle<int> area)
        {
            const int gap = 8;
            int total = 0;
            for (auto& group : groups)
                total += group.count;

            const int knobWidth = (area.getWidth() - gap * ((int) groups.size() - 1)) / juce::jmax (1, total);

            for (auto& group : groups)
            {
                group.area = area.removeFromLeft (knobWidth * group.count);
                area.removeFromLeft (gap);

                auto inner = group.area.withTrimmedTop (18).reduced (0, 2);
                for (int i = 0; i < group.count; ++i)
                    knobs[group.first + i]->setBounds (inner.removeFromLeft (knobWidth).reduced (2, 0));
            }
        }

        void paintKnobGroups (juce::Graphics& g, const std::vector<KnobGroup>& groups)
        {
            for (auto& group : groups)
            {
                auto r = group.area.toFloat();
                g.setColour (Palette::panelRaised.withAlpha (0.45f));
                g.fillRoundedRectangle (r, 5.0f);
                g.setColour (Palette::textDim);
                g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
                g.drawText (group.title, r.removeFromTop (18.0f).reduced (8.0f, 0.0f), juce::Justification::centredLeft);
            }
        }

        juce::String formatHz (double hz)
        {
            return hz >= 1000.0 ? juce::String (hz / 1000.0, hz >= 10000.0 ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (hz)) + " Hz";
        }

        void drawFrequencyGrid (juce::Graphics& g, juce::Rectangle<float> plot, const FrequencyAxis& axis, bool labels)
        {
            g.setFont (juce::FontOptions (9.5f));

            for (double decade = 10.0; decade <= axis.highHz; decade *= 10.0)
                for (int m = 1; m <= 9; ++m)
                {
                    const double hz = decade * m;
                    if (hz < axis.lowHz || hz > axis.highHz)
                        continue;

                    const bool major = m == 1 || m == 2 || m == 5;
                    const auto x = axis.toX (hz, plot);
                    g.setColour (Palette::panelOutline.withAlpha (major ? 0.5f : 0.2f));
                    g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());

                    if (labels && major)
                    {
                        g.setColour (Palette::textDisabled);
                        auto text = hz >= 1000.0 ? juce::String ((int) (hz / 1000.0)) + "k" : juce::String ((int) hz);
                        g.drawText (text, juce::Rectangle<float> (x - 20.0f, plot.getBottom() + 3.0f, 40.0f, 12.0f), juce::Justification::centred);
                    }
                }
        }

        void drawGlow (juce::Graphics& g, juce::Point<float> centre, float radius, juce::Colour colour, float strength)
        {
            strength = juce::jlimit (0.0f, 1.0f, strength);
            if (strength <= 0.01f)
                return;

            juce::ColourGradient glow (colour.withAlpha (0.55f * strength), centre.x, centre.y,
                                       colour.withAlpha (0.0f), centre.x + radius, centre.y, true);
            g.setGradientFill (glow);
            g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre));
        }

        void drawHandle (juce::Graphics& g, juce::Point<float> p, juce::Colour c, bool active, const juce::String& letter)
        {
            const auto radius = active ? 8.0f : 6.5f;
            g.setColour (c.withAlpha (active ? 0.35f : 0.2f));
            g.fillEllipse (juce::Rectangle<float> (radius * 3.0f, radius * 3.0f).withCentre (p));
            g.setColour (c);
            g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (p));
            g.setColour (Palette::windowBackground);
            g.drawEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (p), 1.2f);

            if (letter.isNotEmpty())
            {
                g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
                g.drawText (letter, juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (p), juce::Justification::centred);
            }
        }
    }

    //==============================================================================
    ChoiceButtons::ChoiceButtons (EffectModel& m, int parameterId, juce::StringArray l, juce::Colour c, const juce::String& undo)
        : model (m), id (parameterId), labels (std::move (l)), colour (c), undoName (undo)
    {
        refresh();
    }

    float ChoiceButtons::valueOf (int segment) const
    {
        return segment < (int) values.size() ? values[(size_t) segment] : (float) segment;
    }

    int ChoiceButtons::current() const
    {
        const auto v = model.value (id);
        int best = 0;
        for (int i = 1; i < labels.size(); ++i)
            if (std::abs (valueOf (i) - v) < std::abs (valueOf (best) - v))
                best = i;
        return best;
    }

    void ChoiceButtons::refresh()
    {
        const auto now = current();
        if (now != shown)
        {
            shown = now;
            repaint();
        }
    }

    int ChoiceButtons::segmentAt (juce::Point<float> p) const
    {
        if (labels.isEmpty() || ! getLocalBounds().toFloat().contains (p))
            return -1;

        return juce::jlimit (0, labels.size() - 1, (int) (p.x / ((float) getWidth() / (float) labels.size())));
    }

    void ChoiceButtons::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (Palette::windowBackground);
        g.fillRoundedRectangle (r, 4.0f);

        const float w = r.getWidth() / (float) juce::jmax (1, labels.size());
        const bool enabled = isEnabled();

        for (int i = 0; i < labels.size(); ++i)
        {
            auto segment = juce::Rectangle<float> (r.getX() + w * (float) i, r.getY(), w, r.getHeight()).reduced (1.5f);
            const bool on = i == shown;

            if (on)
            {
                g.setColour (enabled ? colour.withAlpha (0.85f) : colour.withSaturation (0.0f).withAlpha (0.35f));
                g.fillRoundedRectangle (segment, 3.0f);
            }
            else if (i == hover && enabled)
            {
                g.setColour (Palette::panelRaised.brighter (0.1f));
                g.fillRoundedRectangle (segment, 3.0f);
            }

            const auto ink = on ? juce::Colours::black.withAlpha (0.85f) : (enabled ? Palette::textDim : Palette::textDisabled);

            if (drawIcon)
            {
                drawIcon (g, segment.reduced (4.0f, 3.0f), i, ink);
            }
            else
            {
                g.setColour (ink);
                g.setFont (juce::FontOptions (11.5f, on ? juce::Font::bold : juce::Font::plain));
                g.drawText (labels[i], segment, juce::Justification::centred, true);
            }
        }

        g.setColour (Palette::panelOutline.withAlpha (0.8f));
        g.drawRoundedRectangle (r, 4.0f, 1.0f);
    }

    void ChoiceButtons::mouseDown (const juce::MouseEvent& e)
    {
        auto segment = segmentAt (e.position);
        if (segment < 0 || segment == shown)
            return;

        model.setValue (id, valueOf (segment), undoName);
        refresh();
    }

    void ChoiceButtons::mouseMove (const juce::MouseEvent& e)
    {
        auto segment = segmentAt (e.position);
        if (segment != hover)
        {
            hover = segment;
            setTooltip (segment >= 0 ? labels[segment] : juce::String());
            repaint();
        }
    }

    void ChoiceButtons::mouseExit (const juce::MouseEvent&)
    {
        hover = -1;
        repaint();
    }

    //==============================================================================
    ParamCombo::ParamCombo (EffectModel& m, int parameterId, const juce::StringArray& names, const juce::String& undo)
        : model (m), id (parameterId), undoName (undo)
    {
        addItemList (names, 1);
        onChange = [this]
        {
            if (! updating && getSelectedId() > 0)
                model.setValue (id, (float) (getSelectedId() - 1), undoName);
        };
        refresh();
    }

    void ParamCombo::refresh()
    {
        const juce::ScopedValueSetter<bool> svs (updating, true);
        const int wanted = juce::roundToInt (model.value (id)) + 1;
        if (getSelectedId() != wanted)
            setSelectedId (wanted, juce::dontSendNotification);
    }

    //==============================================================================
    ParamToggle::ParamToggle (EffectModel& m, int parameterId, const juce::String& text, juce::Colour onColour, const juce::String& tooltip)
        : juce::TextButton (text), model (m), id (parameterId)
    {
        setClickingTogglesState (false);
        setTooltip (tooltip);
        setColour (juce::TextButton::buttonOnColourId, onColour);
        setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        onClick = [this] { model.setValue (id, model.isOn (id) ? 0.0f : 1.0f, getButtonText() + (model.isOn (id) ? " off" : " on")); refresh(); };
        refresh();
    }

    void ParamToggle::refresh()
    {
        setToggleState (model.isOn (id), juce::dontSendNotification);
    }
}
