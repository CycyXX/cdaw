#include "LeadPanels.h"
#include "../Filter/FilterIcons.h"
#include "../Filter/FilterParameters.h"
#include "../DSP/TempoSync.h"
#include "../../UI/LookAndFeel/Palette.h"

namespace cdaw::LeadUI
{
    using namespace LeadParameters;
    using P = LeadParameters::ID;
    using Source = LeadEngine::Source;
    using Destination = LeadEngine::Destination;

    namespace
    {
        /** What an amount does to a destination, in its own units ("+-2.5 oct"). */
        juce::String describe (Destination d, float amount, bool bipolar)
        {
            const auto sign = juce::String (bipolar ? juce::String::fromUTF8 ("\xc2\xb1") : (amount < 0.0f ? "-" : "+"));
            const float a = std::abs (amount);
            switch (d)
            {
                case Destination::pitch:      return sign + juce::String (a * LeadEngine::pitchModSemitones, a < 0.1f ? 2 : 1) + " st";
                case Destination::syncPitch:  return sign + juce::String (a * LeadEngine::syncModSemitones, 1) + " st";
                case Destination::cutoff:     return sign + juce::String (a * LeadEngine::cutoffModOctaves, 1) + " oct";
                case Destination::pulseWidth: return sign + juce::String (juce::roundToInt (a * 45.0f)) + " % width";
                case Destination::vibrato:    return sign + juce::String (juce::roundToInt (a * 50.0f)) + " ct";
                case Destination::lfo1Rate:
                case Destination::lfo2Rate:   return juce::String::fromUTF8 ("\xc3\x97") + juce::String (std::pow (2.0f, 3.0f * a), 2);
                case Destination::amp:        return sign + juce::String (juce::roundToInt (a * 100.0f)) + " % level";
                case Destination::none:       return {};
                default:                      return sign + juce::String (juce::roundToInt (a * 100.0f)) + " %";
            }
        }
    }

    //==============================================================================
    LfoPanel::LfoPanel (EffectModel& m, LeadEngine& e) : Panel (m, e, {})
    {
        for (int l = 0; l < LeadEngine::numLfos; ++l)
        {
            const auto c = Colours::forLfo (l);
            auto* shape = shapes.add (new ChoiceButtons (m, lfo (l, P::lfo1Shape), FilterParameters::getShapeNames(), c, "LFO shape"));
            shape->drawIcon = FilterIcons::drawShape;
            addAndMakeVisible (shape);
            addAndMakeVisible (syncs.add (new ParamToggle (m, lfo (l, P::lfo1Sync), "Sync", Palette::tempo, "Rate in notes, following the tempo")));
            addAndMakeVisible (retriggers.add (new ParamToggle (m, lfo (l, P::lfo1Retrigger), "Note", c,
                                                                "Restart with each note (each voice its own); off: one LFO for all voices")));
            rateKnobs[l] = addKnob (lfo (l, P::lfo1Rate), "Rate", c, 1.0f);
            addKnob (lfo (l, P::lfo1Fade), "Fade", c, 1.5f);
            updateRateKnob (l);
        }
    }

    void LfoPanel::updateRateKnob (int l)
    {
        const bool note = model.isOn (lfo (l, P::lfo1Sync));
        if (note == showingNote[l] && ready[l])
            return;

        showingNote[l] = note;
        ready[l] = true;
        const int id = lfo (l, note ? P::lfo1Division : P::lfo1Rate);
        rateKnobs[l]->setParameter (model.parameter (id), LeadParameters::get (id).defaultValue, note ? 0.0f : 1.0f);
        rateKnobs[l]->setLabel (note ? "Note" : "Rate");
    }

    void LfoPanel::resized()
    {
        auto r = getLocalBounds().reduced (8, 4);
        const int h = r.getHeight() / 2;
        for (int l = 0; l < LeadEngine::numLfos; ++l)
        {
            auto section = r.removeFromTop (h);
            sections[l] = section;
            auto top = section.removeFromTop (titleHeight - 4).withTrimmedLeft (48);
            retriggers[l]->setBounds (top.removeFromRight (44).reduced (0, 1));
            top.removeFromRight (4);
            syncs[l]->setBounds (top.removeFromRight (44).reduced (0, 1));
            top.removeFromRight (6);
            shapes[l]->setBounds (top.removeFromRight (juce::jmin (top.getWidth(), 150)).reduced (0, 1));

            section.removeFromTop (4);
            auto knobArea = section.removeFromRight (2 * 62);
            knobs[l * 2]->setBounds (knobArea.removeFromLeft (62).withSizeKeepingCentre (62, juce::jmin (knobHeight, knobArea.getHeight())));
            knobs[l * 2 + 1]->setBounds (knobArea.withSizeKeepingCentre (62, juce::jmin (knobHeight, knobArea.getHeight())));
            displays[l] = section.withTrimmedRight (6).withTrimmedBottom (4);
        }
    }

    void LfoPanel::refresh()
    {
        for (int l = 0; l < LeadEngine::numLfos; ++l)
        {
            updateRateKnob (l);
            shapes[l]->refresh();
            syncs[l]->refresh();
            retriggers[l]->refresh();
        }

        Panel::refresh();
        repaint (displays[0]);
        repaint (displays[1]);
    }

    void LfoPanel::paint (juce::Graphics& g)
    {
        Panel::paint (g);
        const auto s = settings();

        for (int l = 0; l < LeadEngine::numLfos; ++l)
        {
            const auto c = Colours::forLfo (l);
            g.setColour (Palette::textDim);
            g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
            g.drawText ("LFO " + juce::String (l + 1), sections[l].withHeight (titleHeight - 4), juce::Justification::centredLeft);

            auto r = displays[l].toFloat();
            drawPlot (g, r);
            auto p = r.reduced (8.0f, 8.0f);
            g.setColour (Palette::panelOutline.withAlpha (0.6f));
            g.drawHorizontalLine ((int) p.getCentreY(), p.getX(), p.getRight());

            const auto shape = s.lfo[l].shape;
            juce::Path path;
            for (int i = 0; i <= 64; ++i)
            {
                const double phase = i / 64.0;
                const float held = std::sin ((float) std::floor (phase * 6.0) * 2.3f + (float) l);
                const auto v = FilterEngine::lfoValue (shape, juce::jmin (0.9999, phase), held);
                const auto x = p.getX() + p.getWidth() * (float) phase, y = p.getCentreY() - v * p.getHeight() * 0.45f;
                if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
            }
            g.setColour (c.withAlpha (0.8f));
            g.strokePath (path, juce::PathStrokeType (1.5f));

            const float phase = engine.lfoPhase[(size_t) l].load(), value = engine.lfoValue[(size_t) l].load();
            const juce::Point<float> dot { p.getX() + p.getWidth() * phase, p.getCentreY() - value * p.getHeight() * 0.45f };
            g.setColour (c.withAlpha (0.25f));
            g.drawVerticalLine ((int) dot.x, p.getY(), p.getBottom());
            EffectUI::drawGlow (g, dot, 12.0f, c, 0.8f);
            g.setColour (Palette::text);
            g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (dot));

            g.setFont (juce::FontOptions (9.5f));
            g.setColour (Palette::textDim);
            g.drawText (s.lfo[l].sync ? TempoSync::get (s.lfo[l].division).name : model.parameter (lfo (l, P::lfo1Rate))->getCurrentText(),
                        r.reduced (6.0f, 3.0f).removeFromTop (11.0f), juce::Justification::centredRight);
        }
    }

    //==============================================================================
    MatrixPanel::MatrixPanel (EffectModel& m, LeadEngine& e) : Panel (m, e, "MODULATION MATRIX")
    {
        for (int k = 0; k < LeadEngine::numSlots; ++k)
        {
            addAndMakeVisible (sources.add (new ParamCombo (m, slot (k, 0), getSourceNames(), "Modulation source")));
            addAndMakeVisible (destinations.add (new ParamCombo (m, slot (k, 1), getDestinationNames(), "Modulation destination")));
            amounts[k] = addKnob (slot (k, 2), {}, Colours::forSlot (k));
        }
    }

    void MatrixPanel::resized()
    {
        auto r = content();
        const int h = r.getHeight() / LeadEngine::numSlots;

        for (int k = 0; k < LeadEngine::numSlots; ++k)
        {
            auto row = r.removeFromTop (h);
            rows[k] = row;
            row.removeFromLeft (30);
            sources[k]->setBounds (row.removeFromLeft (150).withSizeKeepingCentre (150, 24));
            row.removeFromLeft (28);
            destinations[k]->setBounds (row.removeFromLeft (150).withSizeKeepingCentre (150, 24));
            row.removeFromLeft (8);
            amounts[k]->setBounds (row.removeFromLeft (70).withSizeKeepingCentre (70, juce::jmin (h, knobHeight)));
            row.removeFromLeft (10);
            bars[k] = row.withSizeKeepingCentre (row.getWidth(), juce::jmin (32, h - 8));
        }
    }

    void MatrixPanel::refresh()
    {
        Panel::refresh();
        for (int k = 0; k < LeadEngine::numSlots; ++k)
        {
            sources[k]->refresh();
            destinations[k]->refresh();
            repaint (bars[k]);
        }
    }

    void MatrixPanel::paint (juce::Graphics& g)
    {
        Panel::paint (g);
        const auto s = settings();

        for (int k = 0; k < LeadEngine::numSlots; ++k)
        {
            const auto& slot = s.slots[k];
            const bool active = slot.source != Source::none && slot.destination != Destination::none && std::abs (slot.amount) > 0.0001f;
            const auto colour = Colours::forSlot (k);
            auto row = rows[k].toFloat();

            auto number = juce::Rectangle<float> (row.getX(), row.getCentreY() - 10.0f, 20.0f, 20.0f);
            g.setColour (active ? colour : Palette::panelRaised);
            g.fillEllipse (number);
            g.setColour (active ? juce::Colours::black : Palette::textDisabled);
            g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
            g.drawText (juce::String (k + 1), number, juce::Justification::centred);

            const float arrowX = (float) sources[k]->getRight() + 6.0f, y = row.getCentreY();
            juce::Path arrow;
            arrow.addArrow ({ arrowX, y, arrowX + 16.0f, y }, 1.5f, 7.0f, 6.0f);
            g.setColour (active ? colour.withAlpha (0.8f) : Palette::textDisabled);
            g.fillPath (arrow);

            auto bar = bars[k].toFloat();
            drawPlot (g, bar);
            auto track = bar.reduced (6.0f, bar.getHeight() * 0.3f);
            const float centre = track.getCentreX();
            const bool bipolar = LeadEngine::isBipolar (slot.source);
            const float half = track.getWidth() * 0.5f;

            if (active)
            {
                const float lo = bipolar ? -std::abs (slot.amount) : juce::jmin (0.0f, slot.amount);
                const float hi = bipolar ? std::abs (slot.amount) : juce::jmax (0.0f, slot.amount);
                g.setColour (colour.withAlpha (0.18f));
                g.fillRect (juce::Rectangle<float>::leftTopRightBottom (centre + lo * half, track.getY(), centre + hi * half, track.getBottom()));

                const float now = juce::jlimit (-1.0f, 1.0f, engine.slotValue[(size_t) k].load());
                g.setColour (colour);
                g.fillRect (juce::Rectangle<float>::leftTopRightBottom (juce::jmin (centre, centre + now * half), track.getY() + 2.0f,
                                                                        juce::jmax (centre, centre + now * half), track.getBottom() - 2.0f));

                g.setFont (juce::FontOptions (10.0f));
                g.setColour (Palette::textDim);
                g.drawText (describe (slot.destination, slot.amount, bipolar), bar.reduced (8.0f, 0.0f), juce::Justification::centredRight);
            }

            g.setColour (Palette::panelOutline);
            g.drawVerticalLine ((int) centre, track.getY() - 3.0f, track.getBottom() + 3.0f);
        }
    }

    //==============================================================================
    MovementPanel::MovementPanel (EffectModel& m, LeadEngine& e) : Panel (m, e, "MOVEMENT")
    {
        history.assign ((size_t) length, {});
    }

    void MovementPanel::refresh()
    {
        auto& frame = history[(size_t) write];
        for (int k = 0; k < LeadEngine::numSlots; ++k)
            frame[(size_t) k] = engine.slotValue[(size_t) k].load();
        write = (write + 1) % length;
        repaint();
    }

    void MovementPanel::paint (juce::Graphics& g)
    {
        Panel::paint (g);
        const auto s = settings();
        auto r = content().toFloat();
        drawPlot (g, r);
        auto p = r.reduced (8.0f, 10.0f);

        g.setColour (Palette::panelOutline.withAlpha (0.7f));
        g.drawHorizontalLine ((int) p.getCentreY(), p.getX(), p.getRight());

        int legendX = 0;
        for (int k = 0; k < LeadEngine::numSlots; ++k)
        {
            const auto& slot = s.slots[k];
            if (slot.source == Source::none || slot.destination == Destination::none || std::abs (slot.amount) < 0.0001f)
                continue;

            juce::Path line;
            for (int i = 0; i < length; ++i)
            {
                const float v = juce::jlimit (-1.0f, 1.0f, history[(size_t) ((write + i) % length)][(size_t) k]);
                const auto x = p.getX() + p.getWidth() * (float) i / (length - 1), y = p.getCentreY() - v * p.getHeight() * 0.5f;
                if (i == 0) line.startNewSubPath (x, y); else line.lineTo (x, y);
            }

            g.setColour (Colours::forSlot (k));
            g.strokePath (line, juce::PathStrokeType (1.5f));

            auto legend = juce::Rectangle<float> (p.getX() + 4.0f + (float) legendX * 26.0f, p.getY(), 22.0f, 12.0f);
            g.fillRoundedRectangle (legend.withWidth (8.0f).withSizeKeepingCentre (8.0f, 4.0f), 2.0f);
            g.setFont (juce::FontOptions (9.5f));
            g.drawText (juce::String (k + 1), legend.withTrimmedLeft (10.0f), juce::Justification::centredLeft);
            ++legendX;
        }

        g.setFont (juce::FontOptions (9.5f));
        g.setColour (Palette::textDisabled);
        g.drawText ("-5 s", p.removeFromBottom (11.0f), juce::Justification::centredLeft);
    }

    //==============================================================================
    juce::String MacroPanel::macroName (EffectModel& m, int macro)
    {
        auto name = m.getText ("macroName" + juce::String (macro + 1));
        return name.isNotEmpty() ? name : getDefaultMacroNames()[macro];
    }

    MacroPanel::MacroPanel (EffectModel& m, LeadEngine& e) : Panel (m, e, "MACROS")
    {
        for (int i = 0; i < LeadEngine::numMacros; ++i)
        {
            auto* label = names.add (new juce::Label());
            label->setEditable (false, true, false);
            label->setJustificationType (juce::Justification::centred);
            label->setColour (juce::Label::textColourId, Colours::macro);
            label->setFont (juce::FontOptions (12.0f, juce::Font::bold));
            label->setTooltip ("Double-click to rename");
            label->onTextChange = [this, i, label]
            {
                model.beginUndoTransaction ("Rename macro");
                model.setText ("macroName" + juce::String (i + 1), label->getText().trim());
            };
            addAndMakeVisible (label);
            addKnob (P::macro1 + i, {}, Colours::macro);
        }

        refresh();
    }

    void MacroPanel::resized()
    {
        auto r = content();
        const int w = r.getWidth() / LeadEngine::numMacros;
        for (int i = 0; i < LeadEngine::numMacros; ++i)
        {
            auto column = r.removeFromLeft (w).reduced (3, 0);
            columns[i] = column;
            names[i]->setBounds (column.removeFromTop (20));
            knobs[i]->setBounds (column.removeFromTop (juce::jmin (knobHeight, column.getHeight() - 14)));
        }
    }

    void MacroPanel::refresh()
    {
        Panel::refresh();
        for (int i = 0; i < LeadEngine::numMacros; ++i)
            if (! names[i]->isBeingEdited())
                names[i]->setText (macroName (model, i), juce::dontSendNotification);
    }

    void MacroPanel::paint (juce::Graphics& g)
    {
        Panel::paint (g);
        const auto s = settings();
        const auto destinationNames = getDestinationNames();

        for (int i = 0; i < LeadEngine::numMacros; ++i)
        {
            auto list = columns[i].withTrimmedTop (20 + knobs[i]->getHeight() + 2).toFloat();
            g.setFont (juce::FontOptions (9.5f));
            int lines = 0;
            for (auto& slot : s.slots)
                if (slot.source == (Source) ((int) Source::macro1 + i) && slot.destination != Destination::none && lines < 2)
                {
                    g.setColour (Palette::textDim);
                    g.drawText (juce::String::fromUTF8 ("\xe2\x86\x92 ") + destinationNames[(int) slot.destination] + " " + describe (slot.destination, slot.amount, false),
                                list.removeFromTop (12.0f), juce::Justification::centred, true);
                    ++lines;
                }

            if (lines == 0)
            {
                g.setColour (Palette::textDisabled);
                g.drawText ("not routed", list.removeFromTop (12.0f), juce::Justification::centred);
            }
        }
    }

    //==============================================================================
    PlayingPanel::PlayingPanel (EffectModel& m, LeadEngine& e)
        : Panel (m, e, "PLAYING"), priorityButtons (m, P::priority, { "Last", "High", "Low" }, Colours::pitch, "Note priority")
    {
        priorityButtons.setTooltip ("Mono and legato: which held key plays. Last: the newest. High / Low: the highest / lowest held");
        addAndMakeVisible (priorityButtons);
        addKnob (P::polyphony, "Voices", Colours::pitch);
        addKnob (P::bendUp, "Bend Up", Palette::textDim);
        addKnob (P::bendDown, "Bend Down", Palette::textDim);
        addKnob (P::vibratoWheel, "Wheel Vib", Colours::lfo1);
        addKnob (P::vibratoAftertouch, "AT Vib", Colours::lfo1);
    }

    void PlayingPanel::resized()
    {
        auto top = titleRow();
        top.removeFromLeft (64);
        priorityButtons.setBounds (top.removeFromRight (juce::jmin (160, top.getWidth())));
        auto r = content();
        layoutRow (r.removeFromTop (juce::jmin (r.getHeight(), knobHeight)), { knobs[0], knobs[1], knobs[2], knobs[3], knobs[4] });
    }

    void PlayingPanel::refresh()
    {
        Panel::refresh();
        priorityButtons.refresh();
    }

    //==============================================================================
    EffectPanel::EffectPanel (EffectModel& m, LeadEngine& e, Kind k)
        : Panel (m, e, k == Kind::chorus ? "CHORUS" : k == Kind::delay ? "DELAY" : k == Kind::reverb ? "REVERB" : "DISTORTION"), kind (k),
          onButton (m, k == Kind::chorus ? P::chorusOn : k == Kind::delay ? P::delayOn : k == Kind::reverb ? P::reverbOn : P::distOn,
                    "On", k == Kind::dist ? Colours::drive : Colours::effects, "Effect on / off")
    {
        addAndMakeVisible (onButton);
        const auto c = Colours::effects;

        if (kind == Kind::dist)
        {
            voicingButtons = std::make_unique<ChoiceButtons> (m, P::distVoicing, getDistVoicingNames(), Colours::drive, "Distortion voicing");
            voicingButtons->setTooltip ("C-Dist's circuits: Crunch (British, uneven), Lead (mid-humped, singing), Modern (tight, high gain), Fuzz");
            addAndMakeVisible (*voicingButtons);
            addKnob (P::distDrive, "Drive", Colours::drive);
            addKnob (P::distTone, "Tone", Colours::drive);
            addKnob (P::distMix, "Mix", Colours::drive);
        }
        else if (kind == Kind::chorus)
        {
            addKnob (P::chorusDepth, "Depth", c);
            addKnob (P::chorusMix, "Mix", c);
        }
        else if (kind == Kind::delay)
        {
            addKnob (P::delayDivision, "Note", c);
            addKnob (P::delayFeedback, "Feedback", c);
            addKnob (P::delayMix, "Mix", c);
        }
        else
        {
            addKnob (P::reverbSize, "Size", c);
            addKnob (P::reverbDecay, "Decay", c, 3.0f);
            addKnob (P::reverbMix, "Mix", c);
        }
    }

    void EffectPanel::resized()
    {
        auto top = titleRow();
        onButton.setBounds (top.removeFromRight (44));
        if (voicingButtons != nullptr)
        {
            top.removeFromRight (6);
            top.removeFromLeft (80);
            voicingButtons->setBounds (top.removeFromRight (juce::jmin (230, top.getWidth())));
        }
        auto r = content();
        auto row = r.removeFromBottom (knobHeight);
        if (knobs.size() == 2)
            layoutRow (row.withSizeKeepingCentre (row.getWidth() * 2 / 3, row.getHeight()), { knobs[0], knobs[1] });
        else
            layoutRow (row, { knobs[0], knobs[1], knobs[2] });
        display = r.withTrimmedBottom (4);
    }

    void EffectPanel::refresh()
    {
        Panel::refresh();
        onButton.refresh();
        if (voicingButtons != nullptr)
            voicingButtons->refresh();
        repaint (display);
    }

    void EffectPanel::paint (juce::Graphics& g)
    {
        Panel::paint (g);
        const auto s = settings();
        auto r = display.toFloat();
        drawPlot (g, r);

        if (kind == Kind::chorus)      drawChorus (g, r.reduced (10.0f), s);
        else if (kind == Kind::delay)  drawDelay (g, r.reduced (10.0f), s);
        else if (kind == Kind::dist)   drawDist (g, r.reduced (10.0f), s);
        else                           drawReverb (g, r.reduced (10.0f), s);

        const bool on = kind == Kind::chorus ? s.chorusOn : kind == Kind::delay ? s.delayOn : kind == Kind::dist ? s.distOn : s.reverbOn;
        if (! on)
            drawOff (g, r);
    }

    void EffectPanel::drawDist (juce::Graphics& g, juce::Rectangle<float> r, const LeadEngine::Settings& s)
    {
        // C-Dist's curve as it bends the lead right now, the input on a square-root scale (so the
        // knee shows at high gain), with the last samples through it glowing on it.
        const auto d = LeadEngine::distSettings (s);
        const auto& dist = engine.getDist();
        const auto& p = DistEngine::profile (d.voicing);
        const float level = juce::jmax (0.002f, dist.inputLevel.load());

        auto square = r.withSizeKeepingCentre (juce::jmin (r.getWidth(), r.getHeight()), juce::jmin (r.getWidth(), r.getHeight()));
        float range = 0.01f;
        const int write = dist.pairWrite.load();
        for (int k = 1; k <= 1024; ++k)
            range = juce::jmax (range, std::abs (dist.pairIn[(size_t) ((write - k + DistEngine::pairLength) % DistEngine::pairLength)].load()));
        range *= 1.2f;

        auto xFor = [&] (float x) { return square.getCentreX() + std::copysign (std::sqrt (juce::jmin (1.0f, std::abs (x) / range)), x) * square.getWidth() * 0.5f; };
        auto yFor = [&] (float y) { return square.getCentreY() - juce::jlimit (-1.15f, 1.15f, y) / 1.15f * square.getHeight() * 0.5f; };

        g.setColour (Palette::panelOutline.withAlpha (0.6f));
        g.drawLine (square.getX(), square.getCentreY(), square.getRight(), square.getCentreY(), 1.0f);
        g.drawLine (square.getCentreX(), square.getY(), square.getCentreX(), square.getBottom(), 1.0f);

        for (int k = 1; k <= 768; k += 2)
        {
            const int i = (write - k + DistEngine::pairLength) % DistEngine::pairLength;
            g.setColour (Colours::drive.withAlpha (0.12f));
            g.fillEllipse (juce::Rectangle<float> (3.0f, 3.0f).withCentre ({ xFor (dist.pairIn[(size_t) i].load()), yFor (dist.pairOut[(size_t) i].load()) }));
        }

        float heat = 0.0f;
        for (auto& h : dist.stageHeat)
            heat = juce::jmax (heat, h.load());

        juce::Path curve;
        for (int k = 0; k <= 120; ++k)
        {
            const float t = -1.0f + 2.0f * (float) k / 120.0f, x = std::copysign (t * t, t) * range;
            const juce::Point<float> at { xFor (x), yFor (DistEngine::transferCurve (d, x, level)) };
            if (k == 0) curve.startNewSubPath (at); else curve.lineTo (at);
        }
        g.setColour (Colours::drive.withAlpha (0.2f + 0.3f * heat));
        g.strokePath (curve, juce::PathStrokeType (6.0f));
        g.setColour (Colours::drive.interpolatedWith (juce::Colour (0xfffff1c1), heat * 0.6f));
        g.strokePath (curve, juce::PathStrokeType (1.8f));

        g.setFont (juce::FontOptions (9.5f));
        g.setColour (Palette::textDim);
        const float gainDb = p.minGainDb + d.drive * (p.maxGainDb - p.minGainDb);
        g.drawText (juce::String (p.name) + "  +" + juce::String (juce::roundToInt (gainDb)) + " dB  -  clipping " + juce::String (juce::roundToInt (heat * 100.0f)) + " %",
                    r.removeFromTop (11.0f), juce::Justification::centredRight);
    }

    void EffectPanel::drawChorus (juce::Graphics& g, juce::Rectangle<float> r, const LeadEngine::Settings& s)
    {
        // Its voices' delays sweeping over a few seconds (now at the right).
        const auto c = LeadEngine::chorusSettings (s);
        const float low = c.delayMs, high = c.delayMs + c.depth * ChorusEngine::maxModulationMs + 0.01f;
        const double window = 2.5 / c.rateHz;
        static const juce::Colour colours[] { Colours::oscA, Colours::oscB, Colours::pitch };

        for (int v = 0; v < c.voices; ++v)
        {
            const double now = engine.getChorus().voicePhase[(size_t) v].load();
            juce::Path trace;
            for (float x = r.getX(); x <= r.getRight(); x += 2.0f)
            {
                const double ago = (r.getRight() - x) / r.getWidth() * window;
                const double phase = now - c.rateHz * ChorusEngine::rateMultipliers[(size_t) v] * ago;
                const auto y = juce::jmap (ChorusEngine::getVoiceDelayMs (c, phase, 0), low - 0.5f, high + 0.5f, r.getBottom(), r.getY());
                if (x == r.getX()) trace.startNewSubPath (x, y); else trace.lineTo (x, y);
            }
            g.setColour (colours[v % 3].withAlpha (0.85f));
            g.strokePath (trace, juce::PathStrokeType (1.6f));
        }

        g.setFont (juce::FontOptions (9.5f));
        g.setColour (Palette::textDim);
        g.drawText (juce::String (c.voices) + " voices, " + juce::String (low, 0) + "-" + juce::String (high, 1) + " ms", r.removeFromTop (11.0f),
                    juce::Justification::centredRight);
    }

    void EffectPanel::drawDelay (juce::Graphics& g, juce::Rectangle<float> r, const LeadEngine::Settings& s)
    {
        // The echoes of a click: left above the line, right below, as loud as they play.
        const auto d = LeadEngine::delaySettings (s);
        const auto echoes = DelayEngine::getEchoes (d, model.getBpm(), engine.getSampleRate(), -48.0f, 24);
        const double range = echoes.empty() ? 1.0 : echoes.back().seconds * 1.1;
        const float centre = r.getCentreY(), half = r.getHeight() * 0.5f - 4.0f;

        g.setColour (Palette::panelOutline);
        g.drawHorizontalLine ((int) centre, r.getX(), r.getRight());

        for (auto& e : echoes)
        {
            const float x = r.getX() + (float) (e.seconds / range) * r.getWidth();
            const float h = juce::jlimit (0.0f, 1.0f, (e.gainDb + 48.0f) / 48.0f) * half;
            const auto colour = Colours::effects.interpolatedWith (Colours::filter, juce::jmin (1.0f, e.passes / 8.0f));
            g.setColour (colour);
            if (e.channel != 1) g.fillRoundedRectangle (x - 2.0f, centre - h, 4.0f, h, 2.0f);
            if (e.channel != 0) g.fillRoundedRectangle (x - 2.0f, centre, 4.0f, h, 2.0f);
        }

        g.setFont (juce::FontOptions (9.5f));
        g.setColour (Palette::textDim);
        g.drawText (TempoSync::get (d.division).name + juce::String ("  ping-pong, ducked  -  ") + juce::String ((int) echoes.size()) + " echoes",
                    r.removeFromTop (11.0f), juce::Justification::centredRight);
    }

    void EffectPanel::drawReverb (juce::Graphics& g, juce::Rectangle<float> r, const LeadEngine::Settings& s)
    {
        // How long each frequency rings (low frequencies left).
        const auto rv = LeadEngine::reverbSettings (s);
        EffectUI::FrequencyAxis axis;
        const double longest = juce::jmax (0.5, ReverbEngine::getDecayAt (rv, 100.0) * 1.15);
        const double rate = engine.getSampleRate();

        juce::Path shape;
        shape.startNewSubPath (r.getX(), r.getBottom());
        for (float x = r.getX(); x <= r.getRight(); x += 2.0f)
        {
            const double hz = axis.toHz (x, r);
            const double ring = juce::jmax (0.0, ReverbEngine::getDecayAt (rv, hz) * (1.0 + ReverbEngine::getCutsDb (rv, hz, true, rate) / 60.0));
            shape.lineTo (x, r.getBottom() - (float) (ring / longest) * r.getHeight());
        }
        shape.lineTo (r.getRight(), r.getBottom());
        shape.closeSubPath();

        g.setGradientFill (juce::ColourGradient::horizontal (Colours::pitch.withAlpha (0.55f), r.getX(), Colours::oscA.withAlpha (0.55f), r.getRight()));
        g.fillPath (shape);

        g.setFont (juce::FontOptions (9.5f));
        g.setColour (Palette::textDim);
        g.drawText ("RT60 " + juce::String (ReverbEngine::getDecayAt (rv, 1000.0), 1) + " s at 1 kHz", r.removeFromTop (11.0f), juce::Justification::centredRight);
    }

    //==============================================================================
    VoiceField::VoiceField (LeadEngine& e) : engine (e)
    {
        setOpaque (true);
    }

    bool VoiceField::isBlack (int key)
    {
        const int n = key % 12;
        return n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
    }

    float VoiceField::keyX (int key, juce::Rectangle<float> keys) const
    {
        int whites = 0;
        for (int k = lowestKey; k <= highestKey; ++k)
            if (! isBlack (k))
                ++whites;

        int index = 0;
        for (int k = lowestKey; k < key; ++k)
            if (! isBlack (k))
                ++index;

        const float w = keys.getWidth() / (float) whites;
        return keys.getX() + (isBlack (key) ? (float) index * w : ((float) index + 0.5f) * w);
    }

    float VoiceField::pitchX (float pitch, juce::Rectangle<float> keys) const
    {
        const float clamped = juce::jlimit ((float) lowestKey, (float) highestKey, pitch);
        const int below = juce::jmin ((int) std::floor (clamped), highestKey - 1);
        const float t = clamped - (float) below;
        return keyX (below, keys) + (keyX (below + 1, keys) - keyX (below, keys)) * t;
    }

    void VoiceField::refresh()
    {
        auto bounds = getLocalBounds().toFloat().reduced (8.0f, 6.0f);
        auto keys = bounds.removeFromBottom (30.0f);
        auto field = bounds.withTrimmedTop (14.0f);

        for (int i = 0; i < LeadEngine::maxVoices; ++i)
        {
            auto& v = engine.voiceViews[(size_t) i];
            auto& trail = trails[(size_t) i];
            const int note = v.note.load();

            // A mono voice keeps its trail across notes: the glide between them is the point.
            if (note < 0 && trail.note >= 0)
                trail.points.clear();
            trail.note = note;

            if (note >= 0)
            {
                const juce::Point<float> at { pitchX (v.pitch.load(), keys), field.getBottom() - 8.0f - v.amp.load() * (field.getHeight() - 16.0f) };
                trail.points.push_front (at);
                while (trail.points.size() > 24)
                    trail.points.pop_back();
            }
        }

        repaint();
    }

    void VoiceField::paint (juce::Graphics& g)
    {
        g.fillAll (Palette::panelBackground);
        EffectUI::drawPanel (g, getLocalBounds().toFloat(), "VOICES");

        auto bounds = getLocalBounds().toFloat().reduced (8.0f, 6.0f);
        auto keys = bounds.removeFromBottom (30.0f);
        auto field = bounds.withTrimmedTop (14.0f);

        g.setColour (EffectUI::plotBackground);
        g.fillRoundedRectangle (field, 3.0f);

        // Which keys are down: the playing one in its voice's colour, others held (mono) dimmer.
        std::array<juce::Colour, 128> pressed {};
        std::array<bool, 128> down {};
        for (int i = 0; i < engine.numHeldKeys.load(); ++i)
        {
            const int k = juce::jlimit (0, 127, engine.heldKeys[(size_t) i].load());
            down[(size_t) k] = true;
            pressed[(size_t) k] = Colours::pitch.withAlpha (0.45f).withMultipliedBrightness (0.8f);
        }

        int sounding = 0, held = 0;
        for (auto& v : engine.voiceViews)
            if (const int note = v.note.load(); note >= 0)
            {
                ++sounding;
                if (v.held.load())
                {
                    ++held;
                    down[(size_t) juce::jlimit (0, 127, note)] = true;
                    pressed[(size_t) juce::jlimit (0, 127, note)] = Colours::forCutoff (v.cutoff.load());
                }
            }

        int whites = 0;
        for (int k = lowestKey; k <= highestKey; ++k)
            if (! isBlack (k))
                ++whites;
        const float whiteW = keys.getWidth() / (float) whites;

        for (int k = lowestKey; k <= highestKey; ++k)
            if (! isBlack (k))
            {
                auto key = juce::Rectangle<float> (keyX (k, keys) - whiteW * 0.5f, keys.getY(), whiteW - 1.0f, keys.getHeight());
                g.setColour (down[(size_t) k] ? pressed[(size_t) k].withAlpha (1.0f) : Palette::text.withAlpha (0.85f));
                g.fillRoundedRectangle (key, 1.5f);
                if (k % 12 == 0)
                {
                    g.setColour (Palette::textDisabled);
                    g.setFont (juce::FontOptions (8.5f));
                    g.drawText ("C" + juce::String (k / 12 - 1), key.removeFromBottom (10.0f), juce::Justification::centred);
                }
            }

        for (int k = lowestKey; k <= highestKey; ++k)
            if (isBlack (k))
            {
                auto key = juce::Rectangle<float> (keyX (k, keys) - whiteW * 0.32f, keys.getY(), whiteW * 0.64f, keys.getHeight() * 0.6f);
                g.setColour (down[(size_t) k] ? pressed[(size_t) k].withAlpha (1.0f).darker (0.2f) : Palette::windowBackground);
                g.fillRoundedRectangle (key, 1.5f);
            }

        for (int i = 0; i < LeadEngine::maxVoices; ++i)
        {
            auto& v = engine.voiceViews[(size_t) i];
            auto& trail = trails[(size_t) i];
            if (v.note.load() < 0 || trail.points.empty())
                continue;

            const auto colour = Colours::forCutoff (v.cutoff.load());
            const float amp = v.amp.load();
            const auto at = trail.points.front();

            for (size_t t = trail.points.size(); --t > 0;)
            {
                g.setColour (colour.withAlpha (0.3f * (1.0f - (float) t / (float) trail.points.size())));
                g.fillEllipse (juce::Rectangle<float> (4.0f, 4.0f).withCentre (trail.points[t]));
            }

            g.setColour (colour.withAlpha (0.25f));
            g.drawLine (at.x, at.y, at.x, field.getBottom(), 1.0f);

            const float radius = 4.0f + 7.0f * amp;
            EffectUI::drawGlow (g, at, radius * 2.6f, colour, 0.3f + 0.7f * amp);
            if (v.held.load())
            {
                g.setColour (colour);
                g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (at));
            }
            else
            {
                g.setColour (colour.withAlpha (0.4f + 0.6f * amp));
                g.drawEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (at), 1.5f);
            }
        }

        juce::String text = juce::String (sounding) + (sounding == 1 ? " voice" : " voices");
        if (sounding > held)
            text << "  -  " << juce::String (sounding - held) << " releasing";
        if (engine.numHeldKeys.load() > 1)
            text << "  -  " << juce::String (engine.numHeldKeys.load()) << " keys held";

        g.setFont (juce::FontOptions (10.0f));
        g.setColour (Palette::textDim);
        g.drawText (text, getLocalBounds().toFloat().reduced (10.0f, 5.0f).removeFromTop (13.0f), juce::Justification::centredRight);
    }

    //==============================================================================
    StereoField::StereoField (LeadEngine& e) : engine (e)
    {
        setOpaque (true);
    }

    StereoField::~StereoField()
    {
        engine.stereo.active = false;
    }

    void StereoField::visibilityChanged()
    {
        engine.stereo.active = isShowing();
    }

    void StereoField::add (const float* l, const float* r, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            left[(size_t) write] = l[i];
            right[(size_t) write] = r[i];
            write = (write + 1) % length;
        }
    }

    void StereoField::refresh()
    {
        if (isShowing())
            engine.stereo.active = true;

        float l[1024], r[1024];
        for (int got; (got = engine.stereo.pull (l, r, 1024)) > 0;)
            add (l, r, got);

        float peak = 0.0f;
        double lr = 0.0, ll = 0.0, rr = 0.0;
        for (int i = 0; i < length; ++i)
        {
            const float a = left[(size_t) i], b = right[(size_t) i];
            peak = juce::jmax (peak, std::abs (a), std::abs (b));
            lr += a * b;
            ll += a * a;
            rr += b * b;
        }

        const float target = peak > 1.0e-4f ? 0.9f / peak : scale;
        scale = target < scale ? target : scale + (target - scale) * 0.05f;
        correlation = (ll > 1.0e-12 && rr > 1.0e-12) ? (float) (lr / std::sqrt (ll * rr)) : 1.0f;
        repaint();
    }

    void StereoField::paint (juce::Graphics& g)
    {
        g.fillAll (Palette::panelBackground);
        EffectUI::drawPanel (g, getLocalBounds().toFloat(), "STEREO");

        auto r = getLocalBounds().toFloat().reduced (8.0f, 6.0f).withTrimmedTop (14.0f);
        auto meter = r.removeFromBottom (14.0f);
        auto square = r.withSizeKeepingCentre (juce::jmin (r.getWidth(), r.getHeight()), juce::jmin (r.getWidth(), r.getHeight()));
        g.setColour (EffectUI::plotBackground);
        g.fillRoundedRectangle (square, 3.0f);

        const auto c = square.getCentre();
        const float radius = square.getWidth() * 0.5f - 4.0f;
        g.setColour (Palette::panelOutline.withAlpha (0.6f));
        g.drawLine (c.x, square.getY() + 4.0f, c.x, square.getBottom() - 4.0f, 1.0f);
        g.drawLine (c.x - radius * 0.7f, c.y - radius * 0.7f, c.x + radius * 0.7f, c.y + radius * 0.7f, 1.0f);
        g.drawLine (c.x + radius * 0.7f, c.y - radius * 0.7f, c.x - radius * 0.7f, c.y + radius * 0.7f, 1.0f);
        g.setFont (juce::FontOptions (8.5f));
        g.setColour (Palette::textDisabled);
        g.drawText ("L", juce::Rectangle<float> (square.getX() + 4.0f, square.getY() + 2.0f, 10.0f, 10.0f), juce::Justification::centred);
        g.drawText ("R", juce::Rectangle<float> (square.getRight() - 14.0f, square.getY() + 2.0f, 10.0f, 10.0f), juce::Justification::centred);

        g.setColour (Colours::oscA.withAlpha (0.35f));
        for (int i = 0; i < length; i += 2)
        {
            const float l = left[(size_t) i] * scale, rr = right[(size_t) i] * scale;
            const float x = c.x + (rr - l) * 0.7071f * radius, y = c.y - (l + rr) * 0.7071f * radius;
            g.fillRect (x, y, 1.5f, 1.5f);
        }

        g.setColour (EffectUI::plotBackground);
        g.fillRoundedRectangle (meter.reduced (0.0f, 3.0f), 2.0f);
        const float x = meter.getCentreX() + correlation * meter.getWidth() * 0.5f;
        g.setColour (correlation < -0.3f ? Palette::meterRed : Colours::oscA);
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (juce::jmin (x, meter.getCentreX()), meter.getY() + 3.0f,
                                                                juce::jmax (x, meter.getCentreX()), meter.getBottom() - 3.0f));
    }

    //==============================================================================
    OutputSpectrum::OutputSpectrum (LeadEngine& e) : engine (e), analyser (e.outputFifo)
    {
        setOpaque (true);
    }

    OutputSpectrum::~OutputSpectrum()
    {
        engine.outputFifo->active = false;
    }

    void OutputSpectrum::visibilityChanged()
    {
        engine.outputFifo->active = isShowing();
    }

    void OutputSpectrum::resized()
    {
        auto p = getLocalBounds().toFloat().reduced (8.0f, 6.0f).withTrimmedTop (14.0f);
        columnHz.resize ((size_t) juce::jmax (1, (int) p.getWidth()));
        for (size_t i = 0; i < columnHz.size(); ++i)
            columnHz[i] = axis.toHz (p.getX() + (float) i + 0.5f, p);
    }

    void OutputSpectrum::refresh()
    {
        if (isShowing())
            engine.outputFifo->active = true;

        if (analyser.update (columnHz, engine.getSampleRate()))
            repaint();
    }

    void OutputSpectrum::paint (juce::Graphics& g)
    {
        g.fillAll (Palette::panelBackground);
        EffectUI::drawPanel (g, getLocalBounds().toFloat(), "OUTPUT");

        auto p = getLocalBounds().toFloat().reduced (8.0f, 6.0f).withTrimmedTop (14.0f);
        g.setColour (EffectUI::plotBackground);
        g.fillRoundedRectangle (p, 3.0f);
        EffectUI::drawFrequencyGrid (g, p, axis, false);

        if (analyser.levels.empty())
            return;

        juce::Path fill;
        fill.startNewSubPath (p.getX(), p.getBottom());
        for (size_t i = 0; i < analyser.levels.size(); ++i)
            fill.lineTo (p.getX() + (float) i, juce::jmap (juce::jlimit (-90.0f, -10.0f, analyser.levels[i]), -90.0f, -10.0f, p.getBottom(), p.getY()));
        fill.lineTo (p.getRight(), p.getBottom());
        fill.closeSubPath();
        g.setGradientFill (juce::ColourGradient::horizontal (Colours::pitch.withAlpha (0.6f), p.getX(), Colours::oscB.withAlpha (0.6f), p.getRight()));
        g.fillPath (fill);
    }
}
