#include "SynthPanels.h"
#include "../../UI/LookAndFeel/Palette.h"

namespace cdaw
{
    using namespace SynthParameters;
    using P = SynthParameters::ID;
    using Source = SynthEngine::Source;
    using Destination = SynthEngine::Destination;

    namespace
    {
        /** What an amount does to a destination, in its own units ("+-2.5 oct"). */
        juce::String describe (Destination d, float amount, bool bipolar)
        {
            const auto sign = juce::String (bipolar ? juce::String::fromUTF8 ("\xc2\xb1") : (amount < 0.0f ? "-" : "+"));
            const float a = std::abs (amount);
            switch (d)
            {
                case Destination::pitch:     return sign + juce::String (a * SynthEngine::pitchModSemitones, a < 0.1f ? 2 : 1) + " st";
                case Destination::cutoff:    return sign + juce::String (a * SynthEngine::cutoffModOctaves, 1) + " oct";
                case Destination::lfo1Rate:
                case Destination::lfo2Rate:  return juce::String::fromUTF8 ("\xc3\x97") + juce::String (std::pow (2.0f, 3.0f * a), 2);
                case Destination::amp:       return sign + juce::String (juce::roundToInt (a * 100.0f)) + " % level";
                case Destination::none:      return {};
                default:                     return sign + juce::String (juce::roundToInt (a * 100.0f)) + " %";
            }
        }
    }

    //==============================================================================
    MatrixPanel::MatrixPanel (EffectModel& m, SynthEngine& e) : SynthPanel (m, e, "MODULATION MATRIX")
    {
        for (int k = 0; k < SynthEngine::numSlots; ++k)
        {
            addAndMakeVisible (sources.add (new ParamCombo (m, slot (k, 0), getSourceNames(), "Modulation source")));
            addAndMakeVisible (destinations.add (new ParamCombo (m, slot (k, 1), getDestinationNames(), "Modulation destination")));
            amounts[k] = addKnob (slot (k, 2), {}, SynthColours::forSlot (k));
        }
    }

    void MatrixPanel::resized()
    {
        auto r = content();
        const int h = r.getHeight() / SynthEngine::numSlots;

        for (int k = 0; k < SynthEngine::numSlots; ++k)
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
        SynthPanel::refresh();
        for (int k = 0; k < SynthEngine::numSlots; ++k)
        {
            sources[k]->refresh();
            destinations[k]->refresh();
            repaint (bars[k]);
        }
    }

    void MatrixPanel::paint (juce::Graphics& g)
    {
        SynthPanel::paint (g);
        const auto s = settings();

        for (int k = 0; k < SynthEngine::numSlots; ++k)
        {
            const auto& slot = s.slots[k];
            const bool active = slot.source != Source::none && slot.destination != Destination::none && std::abs (slot.amount) > 0.0001f;
            const auto colour = SynthColours::forSlot (k);
            auto row = rows[k].toFloat();

            // Slot number and the arrow from source to destination.
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

            // What it's adding now, against how far it could go.
            auto bar = bars[k].toFloat();
            drawPlot (g, bar);
            auto track = bar.reduced (6.0f, bar.getHeight() * 0.3f);
            const float centre = track.getCentreX();
            const bool bipolar = SynthEngine::isBipolar (slot.source);
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
    MovementPanel::MovementPanel (EffectModel& m, SynthEngine& e) : SynthPanel (m, e, "MOVEMENT")
    {
        history.assign ((size_t) length, {});
    }

    void MovementPanel::refresh()
    {
        auto& frame = history[(size_t) write];
        for (int k = 0; k < SynthEngine::numSlots; ++k)
            frame[(size_t) k] = engine.slotValue[(size_t) k].load();
        write = (write + 1) % length;
        repaint();
    }

    void MovementPanel::paint (juce::Graphics& g)
    {
        SynthPanel::paint (g);
        const auto s = settings();
        auto r = content().toFloat();
        drawPlot (g, r);
        auto p = r.reduced (8.0f, 10.0f);

        g.setColour (Palette::panelOutline.withAlpha (0.7f));
        g.drawHorizontalLine ((int) p.getCentreY(), p.getX(), p.getRight());

        // Each active slot's contribution over the last five seconds, now at the right.
        int legendX = 0;
        for (int k = 0; k < SynthEngine::numSlots; ++k)
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

            g.setColour (SynthColours::forSlot (k));
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

    MacroPanel::MacroPanel (EffectModel& m, SynthEngine& e) : SynthPanel (m, e, "MACROS")
    {
        for (int i = 0; i < SynthEngine::numMacros; ++i)
        {
            auto* label = names.add (new juce::Label());
            label->setEditable (false, true, false);
            label->setJustificationType (juce::Justification::centred);
            label->setColour (juce::Label::textColourId, SynthColours::macro);
            label->setFont (juce::FontOptions (12.0f, juce::Font::bold));
            label->setTooltip ("Double-click to rename");
            label->onTextChange = [this, i, label]
            {
                model.beginUndoTransaction ("Rename macro");
                model.setText ("macroName" + juce::String (i + 1), label->getText().trim());
            };
            addAndMakeVisible (label);
            addKnob (P::macro1 + i, {}, SynthColours::macro);
        }

        refresh();
    }

    void MacroPanel::resized()
    {
        auto r = content();
        const int w = r.getWidth() / SynthEngine::numMacros;
        for (int i = 0; i < SynthEngine::numMacros; ++i)
        {
            auto column = r.removeFromLeft (w).reduced (3, 0);
            columns[i] = column;
            names[i]->setBounds (column.removeFromTop (20));
            knobs[i]->setBounds (column.removeFromTop (knobHeight));
        }
    }

    void MacroPanel::refresh()
    {
        SynthPanel::refresh();
        for (int i = 0; i < SynthEngine::numMacros; ++i)
            if (! names[i]->isBeingEdited())
                names[i]->setText (macroName (model, i), juce::dontSendNotification);
    }

    void MacroPanel::paint (juce::Graphics& g)
    {
        SynthPanel::paint (g);
        const auto s = settings();
        const auto destinationNames = getDestinationNames();

        // Where each macro goes, under it.
        for (int i = 0; i < SynthEngine::numMacros; ++i)
        {
            auto list = columns[i].withTrimmedTop (20 + knobHeight + 2).toFloat();
            g.setFont (juce::FontOptions (9.5f));
            int lines = 0;
            for (auto& slot : s.slots)
                if (slot.source == (Source) ((int) Source::macro1 + i) && slot.destination != Destination::none && lines < 3)
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
    VoicePanel::VoicePanel (EffectModel& m, SynthEngine& e)
        : SynthPanel (m, e, "VOICE"), modeButtons (m, P::voiceMode, { "Poly", "Mono", "Legato" }, Palette::accent, "Voice mode")
    {
        modeButtons.setTooltip ("Poly: a voice per key. Mono: one voice, each key restarts it. Legato: one voice gliding between held keys");
        addAndMakeVisible (modeButtons);
        addKnob (P::polyphony, "Voices", Palette::accent);
        addKnob (P::glide, "Glide", Palette::accent, 0.5f);
        addKnob (P::bendRange, "Bend", Palette::textDim);
    }

    void VoicePanel::resized()
    {
        auto top = titleRow();
        modeButtons.setBounds (top.removeFromRight (180));
        auto r = content();
        layoutRow (r.removeFromTop (juce::jmin (r.getHeight(), knobHeight)), { knobs[0], knobs[1], knobs[2] });
    }

    void VoicePanel::refresh()
    {
        SynthPanel::refresh();
        modeButtons.refresh();
    }

    //==============================================================================
    EffectPanel::EffectPanel (EffectModel& m, SynthEngine& e, Kind k)
        : SynthPanel (m, e, k == Kind::chorus ? "CHORUS" : k == Kind::delay ? "DELAY" : "REVERB"), kind (k),
          onButton (m, k == Kind::chorus ? P::chorusOn : k == Kind::delay ? P::delayOn : P::reverbOn, "On", SynthColours::effects, "Effect on / off")
    {
        addAndMakeVisible (onButton);
        const auto c = SynthColours::effects;

        if (kind == Kind::chorus)
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
        onButton.setBounds (titleRow().removeFromRight (44));
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
        SynthPanel::refresh();
        onButton.refresh();
        repaint (display);
    }

    void EffectPanel::paint (juce::Graphics& g)
    {
        SynthPanel::paint (g);
        const auto s = settings();
        auto r = display.toFloat();
        drawPlot (g, r);

        if (kind == Kind::chorus)      drawChorus (g, r.reduced (10.0f), s);
        else if (kind == Kind::delay)  drawDelay (g, r.reduced (10.0f), s);
        else                           drawReverb (g, r.reduced (10.0f), s);

        const bool on = kind == Kind::chorus ? s.chorusOn : kind == Kind::delay ? s.delayOn : s.reverbOn;
        if (! on)
        {
            g.setColour (EffectUI::plotBackground.withAlpha (0.65f));
            g.fillRoundedRectangle (r, 3.0f);
            g.setColour (Palette::textDim);
            g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            g.drawText ("OFF", r, juce::Justification::centred);
        }
    }

    void EffectPanel::drawChorus (juce::Graphics& g, juce::Rectangle<float> r, const SynthEngine::Settings& s)
    {
        // Its three voices' delays sweeping over a few seconds (now at the right).
        const auto c = SynthEngine::chorusSettings (s);
        const float low = c.delayMs, high = c.delayMs + c.depth * ChorusEngine::maxModulationMs + 0.01f;
        const double window = 2.5 / c.rateHz;
        static const juce::Colour colours[] { SynthColours::oscA, SynthColours::oscB, SynthColours::layers };

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
            g.setColour (colours[v].withAlpha (0.85f));
            g.strokePath (trace, juce::PathStrokeType (1.6f));
        }

        g.setFont (juce::FontOptions (9.5f));
        g.setColour (Palette::textDim);
        g.drawText (juce::String (c.voices) + " voices, " + juce::String (low, 0) + "-" + juce::String (high, 1) + " ms", r.removeFromTop (11.0f),
                    juce::Justification::centredRight);
    }

    void EffectPanel::drawDelay (juce::Graphics& g, juce::Rectangle<float> r, const SynthEngine::Settings& s)
    {
        // The echoes of a click: left above the line, right below, as loud as they play.
        const auto d = SynthEngine::delaySettings (s);
        const auto echoes = DelayEngine::getEchoes (d, model.getBpm(), engine.getSampleRate(), -48.0f, 24);
        const double range = echoes.empty() ? 1.0 : echoes.back().seconds * 1.1;
        const float centre = r.getCentreY(), half = r.getHeight() * 0.5f - 4.0f;

        g.setColour (Palette::panelOutline);
        g.drawHorizontalLine ((int) centre, r.getX(), r.getRight());

        for (auto& e : echoes)
        {
            const float x = r.getX() + (float) (e.seconds / range) * r.getWidth();
            const float h = juce::jlimit (0.0f, 1.0f, (e.gainDb + 48.0f) / 48.0f) * half;
            const auto colour = SynthColours::effects.interpolatedWith (SynthColours::filter, juce::jmin (1.0f, e.passes / 8.0f));
            g.setColour (colour);
            if (e.channel != 1) g.fillRoundedRectangle (x - 2.0f, centre - h, 4.0f, h, 2.0f);
            if (e.channel != 0) g.fillRoundedRectangle (x - 2.0f, centre, 4.0f, h, 2.0f);
        }

        g.setFont (juce::FontOptions (9.5f));
        g.setColour (Palette::textDim);
        g.drawText (TempoSync::get (d.division).name + juce::String ("  ping-pong  -  ") + juce::String ((int) echoes.size()) + " echoes",
                    r.removeFromTop (11.0f), juce::Justification::centredRight);
    }

    void EffectPanel::drawReverb (juce::Graphics& g, juce::Rectangle<float> r, const SynthEngine::Settings& s)
    {
        // How long each frequency rings (low frequencies left).
        const auto rv = SynthEngine::reverbSettings (s);
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

        g.setGradientFill (juce::ColourGradient::horizontal (SynthColours::layers.withAlpha (0.55f), r.getX(), SynthColours::oscA.withAlpha (0.55f), r.getRight()));
        g.fillPath (shape);

        g.setFont (juce::FontOptions (9.5f));
        g.setColour (Palette::textDim);
        g.drawText ("RT60 " + juce::String (ReverbEngine::getDecayAt (rv, 1000.0), 1) + " s at 1 kHz", r.removeFromTop (11.0f), juce::Justification::centredRight);
    }

    //==============================================================================
    VoiceField::VoiceField (SynthEngine& e) : engine (e)
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

    void VoiceField::refresh()
    {
        auto bounds = getLocalBounds().toFloat().reduced (8.0f, 6.0f);
        auto keys = bounds.removeFromBottom (30.0f);
        auto field = bounds.withTrimmedTop (14.0f);

        for (int i = 0; i < SynthEngine::maxVoices; ++i)
        {
            auto& v = engine.voiceViews[(size_t) i];
            auto& trail = trails[(size_t) i];
            const int note = v.note.load();

            if (note != trail.note)
            {
                trail.note = note;
                trail.points.clear();
            }

            if (note >= 0)
            {
                const juce::Point<float> at { keyX (juce::jlimit (lowestKey, highestKey, note), keys) + v.pan.load() * 6.0f,
                                              field.getBottom() - 8.0f - v.amp.load() * (field.getHeight() - 16.0f) };
                trail.points.push_front (at);
                while (trail.points.size() > 16)
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

        // Which keys are down, and the colour of their voice.
        std::array<juce::Colour, 128> pressed {};
        std::array<bool, 128> down {};
        int sounding = 0, held = 0;
        for (auto& v : engine.voiceViews)
            if (const int note = v.note.load(); note >= 0)
            {
                ++sounding;
                if (v.held.load())
                {
                    ++held;
                    down[(size_t) juce::jlimit (0, 127, note)] = true;
                    pressed[(size_t) juce::jlimit (0, 127, note)] = SynthColours::forCutoff (v.cutoff.load());
                }
            }

        // The keyboard: white keys, then black ones over them.
        int whites = 0;
        for (int k = lowestKey; k <= highestKey; ++k)
            if (! isBlack (k))
                ++whites;
        const float whiteW = keys.getWidth() / (float) whites;

        for (int k = lowestKey; k <= highestKey; ++k)
            if (! isBlack (k))
            {
                auto key = juce::Rectangle<float> (keyX (k, keys) - whiteW * 0.5f, keys.getY(), whiteW - 1.0f, keys.getHeight());
                g.setColour (down[(size_t) k] ? pressed[(size_t) k] : Palette::text.withAlpha (0.85f));
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
                g.setColour (down[(size_t) k] ? pressed[(size_t) k].darker (0.2f) : Palette::windowBackground);
                g.fillRoundedRectangle (key, 1.5f);
            }

        // Each voice: a trail, a thread down to its key, an orb.
        for (int i = 0; i < SynthEngine::maxVoices; ++i)
        {
            auto& v = engine.voiceViews[(size_t) i];
            auto& trail = trails[(size_t) i];
            if (v.note.load() < 0 || trail.points.empty())
                continue;

            const auto colour = SynthColours::forCutoff (v.cutoff.load());
            const float amp = v.amp.load();
            const auto at = trail.points.front();

            for (size_t t = trail.points.size(); --t > 0;)
            {
                g.setColour (colour.withAlpha (0.25f * (1.0f - (float) t / (float) trail.points.size())));
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

        g.setFont (juce::FontOptions (10.0f));
        g.setColour (Palette::textDim);
        g.drawText (juce::String (sounding) + (sounding == 1 ? " voice" : " voices") + (sounding > held ? "  -  " + juce::String (sounding - held) + " releasing" : juce::String()),
                    getLocalBounds().toFloat().reduced (10.0f, 5.0f).removeFromTop (13.0f), juce::Justification::centredRight);
    }

    //==============================================================================
    StereoField::StereoField (SynthEngine& e) : engine (e)
    {
        setOpaque (true);
    }

    StereoField::~StereoField()
    {
        engine.scope.active = false;
    }

    void StereoField::visibilityChanged()
    {
        engine.scope.active = isShowing();
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
            engine.scope.active = true;

        float l[1024], r[1024];
        for (int got; (got = engine.scope.pull (l, r, 1024)) > 0;)
            add (l, r, got);

        // Scale to the recent peak (slowly), and measure how alike the two sides are.
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

        // Mid up, side across: a mono sound is a vertical line, a wide one a cloud.
        g.setColour (SynthColours::oscA.withAlpha (0.35f));
        for (int i = 0; i < length; i += 2)
        {
            const float l = left[(size_t) i] * scale, rr = right[(size_t) i] * scale;
            const float x = c.x + (rr - l) * 0.7071f * radius, y = c.y - (l + rr) * 0.7071f * radius;
            g.fillRect (x, y, 1.5f, 1.5f);
        }

        // Correlation: +1 mono, 0 wide, below 0 out of phase.
        g.setColour (EffectUI::plotBackground);
        g.fillRoundedRectangle (meter.reduced (0.0f, 3.0f), 2.0f);
        const float x = meter.getCentreX() + correlation * meter.getWidth() * 0.5f;
        g.setColour (correlation < -0.3f ? Palette::meterRed : SynthColours::oscA);
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (juce::jmin (x, meter.getCentreX()), meter.getY() + 3.0f,
                                                                juce::jmax (x, meter.getCentreX()), meter.getBottom() - 3.0f));
    }

    //==============================================================================
    OutputSpectrum::OutputSpectrum (SynthEngine& e) : engine (e), analyser (e.outputFifo)
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
        g.setGradientFill (juce::ColourGradient::horizontal (SynthColours::layers.withAlpha (0.6f), p.getX(), SynthColours::oscB.withAlpha (0.6f), p.getRight()));
        g.fillPath (fill);
    }
}
