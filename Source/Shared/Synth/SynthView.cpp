#include "SynthView.h"
#include "../../UI/LookAndFeel/Palette.h"

namespace cdaw
{
    using P = SynthParameters::ID;
    using Destination = SynthEngine::Destination;

    namespace
    {
        constexpr int headerHeight = 84, footerHeight = 150;

        /** Which knob shows a destination's modulation, and how its offset reads in the knob's units. */
        struct Ring
        {
            Destination destination;
            int parameter;
            float octaves;      // 0: the offset adds; else the offset is in units of this many octaves
        };

        const std::vector<Ring>& rings()
        {
            static const std::vector<Ring> list {
                { Destination::positionA, P::aPosition, 0.0f }, { Destination::positionB, P::bPosition, 0.0f },
                { Destination::levelA, P::aLevel, 0.0f }, { Destination::levelB, P::bLevel, 0.0f },
                { Destination::detune, P::aDetune, 0.0f }, { Destination::detune, P::bDetune, 0.0f },
                { Destination::spread, P::aSpread, 0.0f }, { Destination::spread, P::bSpread, 0.0f },
                { Destination::pan, P::aPan, 0.0f }, { Destination::pan, P::bPan, 0.0f },
                { Destination::cutoff, P::cutoff, SynthEngine::cutoffModOctaves }, { Destination::resonance, P::resonance, 0.0f },
                { Destination::noise, P::noiseLevel, 0.0f },
                { Destination::lfo1Rate, P::lfo1Rate, 3.0f }, { Destination::lfo2Rate, P::lfo2Rate, 3.0f } };
            return list;
        }
    }

    SynthView::SynthView (EffectModel& m, SynthEngine& e) : model (m), engine (e)
    {
        setOpaque (true);

        // Header: presets, macros, volume, pages.
        for (auto* b : { &previousButton, &nextButton })
            addAndMakeVisible (b);
        previousButton.setTooltip ("Previous preset");
        nextButton.setTooltip ("Next preset");
        previousButton.onClick = [this] { stepPreset (-1); };
        nextButton.onClick = [this] { stepPreset (1); };
        presetButton.setTooltip ("Choose a preset");
        presetButton.onClick = [this] { showPresetMenu(); };
        addAndMakeVisible (presetButton);
        saveButton.setTooltip ("Save these settings as a preset of your own");
        saveButton.onClick = [this] { savePreset(); };
        addAndMakeVisible (saveButton);

        for (int i = 0; i < SynthEngine::numMacros; ++i)
            addAndMakeVisible (macroKnobs.add (new ParamKnob (model.parameter (P::macro1 + i), MacroPanel::macroName (model, i), SynthColours::macro, 0.0f)));

        volumeKnob = std::make_unique<ParamKnob> (model.parameter (P::volume), "Volume", Palette::text, SynthParameters::get (P::volume).defaultValue);
        addAndMakeVisible (*volumeKnob);

        const char* names[] { "SYNTH", "MOD", "FX" };
        for (int i = 0; i < 3; ++i)
        {
            auto* tab = tabs.add (new juce::TextButton (names[i]));
            tab->setClickingTogglesState (true);
            tab->setRadioGroupId (4711);
            tab->setColour (juce::TextButton::buttonOnColourId, Palette::accentDim);
            tab->setConnectedEdges ((i > 0 ? juce::Button::ConnectedOnLeft : 0) | (i < 2 ? juce::Button::ConnectedOnRight : 0));
            tab->onClick = [this, i] { showPage (i); };
            addAndMakeVisible (tab);
        }

        // Pages.
        auto add = [this] (int p, SynthPanel* panel) { pages[p].addAndMakeVisible (panels[p].add (panel)); };
        add (synthPage, new OscillatorPanel (model, engine, 0));
        add (synthPage, new OscillatorPanel (model, engine, 1));
        add (synthPage, new LayersPanel (model, engine));
        add (synthPage, new FilterPanel (model, engine));
        add (synthPage, new EnvelopePanel (model, engine, true));
        add (synthPage, new EnvelopePanel (model, engine, false));
        add (synthPage, new LfoPanel (model, engine));
        add (modPage, new MatrixPanel (model, engine));
        add (modPage, new MovementPanel (model, engine));
        add (modPage, new MacroPanel (model, engine));
        add (modPage, new VoicePanel (model, engine));
        add (fxPage, new EffectPanel (model, engine, EffectPanel::Kind::chorus));
        add (fxPage, new EffectPanel (model, engine, EffectPanel::Kind::delay));
        add (fxPage, new EffectPanel (model, engine, EffectPanel::Kind::reverb));

        for (auto& p : pages)
            addChildComponent (p);

        voiceField = std::make_unique<VoiceField> (engine);
        stereo = std::make_unique<StereoField> (engine);
        spectrum = std::make_unique<OutputSpectrum> (engine);
        addAndMakeVisible (*voiceField);
        addAndMakeVisible (*stereo);
        addAndMakeVisible (*spectrum);

        presets = SynthPresets::everything();
        showPage (synthPage);
        updatePresetButton();
        setSize (defaultWidth, defaultHeight);
        startTimerHz (60);
    }

    SynthView::~SynthView()
    {
        stopTimer();
    }

    void SynthView::showPage (int p)
    {
        page = juce::jlimit (0, 2, p);
        for (int i = 0; i < 3; ++i)
        {
            pages[i].setVisible (i == page);
            tabs[i]->setToggleState (i == page, juce::dontSendNotification);
        }
        repaint();
    }

    ParamKnob* SynthView::knobFor (int id) const
    {
        for (auto& list : panels)
            for (auto* panel : list)
                if (auto* k = panel->knobFor (id))
                    return k;
        return nullptr;
    }

    //==============================================================================
    void SynthView::tick()
    {
        for (auto* panel : panels[page])
            panel->refresh();

        for (int i = 0; i < SynthEngine::numMacros; ++i)
        {
            macroKnobs[i]->setLabel (MacroPanel::macroName (model, i));
            macroKnobs[i]->refresh();
        }
        volumeKnob->refresh();

        voiceField->refresh();
        stereo->refresh();
        if ((frame & 1) == 0)
            spectrum->refresh();
        if (frame % 15 == 0)
            updatePresetButton();
        ++frame;

        updateModulationRings();

        for (int ch = 0; ch < 2; ++ch)
        {
            const float db = juce::Decibels::gainToDecibels (engine.outputPeak[ch].take(), -100.0f);
            outputLevel[ch] = db >= outputLevel[ch] ? db : juce::jmax (db, outputLevel[ch] - 0.8f);
        }
        repaint (meterArea);
        if (page == fxPage)
            repaint (flowArea);
    }

    void SynthView::updateModulationRings()
    {
        const auto s = SynthParameters::toSettings ([this] (int id) { return model.value (id); });
        const bool sounding = engine.activeVoices.load() > 0;

        for (auto& ring : rings())
        {
            auto* knob = knobFor (ring.parameter);
            if (knob == nullptr)
                continue;

            // An LFO rate knob shows its note when synced: no ring then.
            if ((ring.parameter == P::lfo1Rate && model.isOn (P::lfo1Sync)) || (ring.parameter == P::lfo2Rate && model.isOn (P::lfo2Sync)))
            {
                knob->setModulation (std::nullopt, std::nullopt);
                continue;
            }

            // How far the matrix can move it: each slot's amount over its source's range.
            float low = 0.0f, high = 0.0f;
            bool routed = false;
            for (auto& slot : s.slots)
                if (slot.destination == ring.destination && slot.source != SynthEngine::Source::none && std::abs (slot.amount) > 0.0001f)
                {
                    routed = true;
                    const bool bipolar = SynthEngine::isBipolar (slot.source);
                    low += bipolar ? -std::abs (slot.amount) : juce::jmin (0.0f, slot.amount);
                    high += bipolar ? std::abs (slot.amount) : juce::jmax (0.0f, slot.amount);
                }

            if (! routed)
            {
                knob->setModulation (std::nullopt, std::nullopt);
                continue;
            }

            const auto& spec = SynthParameters::get (ring.parameter);
            const float base = model.value (ring.parameter);
            auto apply = [&] (float offset)
            {
                const float v = ring.octaves > 0.0f ? base * std::pow (2.0f, ring.octaves * offset) : base + offset;
                return juce::jlimit (spec.minimum, spec.maximum, v);
            };

            std::optional<float> now;
            if (sounding)
                now = apply (engine.destinationValue[(size_t) ring.destination].load());

            knob->setModulation (juce::Range<float> (apply (low), apply (high)), now);
        }
    }

    //==============================================================================
    juce::String SynthView::getPresetName() const
    {
        auto name = model.getText ("presetName");
        return name.isNotEmpty() ? name : SynthPresets::factory()[1].name;     // the defaults are Warm Horizon
    }

    void SynthView::updatePresetButton()
    {
        const auto name = getPresetName();
        bool edited = false;

        for (auto& p : presets)
            if (p.name == name)
            {
                const auto values = SynthPresets::valuesOf (p);
                for (int i = 0; i < P::count && ! edited; ++i)
                {
                    const auto& spec = SynthParameters::get (i);
                    edited = std::abs (model.value (i) - values[(size_t) i]) > (spec.maximum - spec.minimum) * 0.0005f;
                }

                const auto text = p.category + " / " + name + (edited ? " *" : "");
                if (presetButton.getButtonText() != text)
                    presetButton.setButtonText (text);
                return;
            }

        presetButton.setButtonText (name);
    }

    void SynthView::applyPreset (const SynthPresets::Preset& preset)
    {
        model.beginUndoTransaction ("Preset: " + preset.name);
        const auto values = SynthPresets::valuesOf (preset);

        for (int i = 0; i < P::count; ++i)
        {
            auto p = model.parameter (i);
            if (std::abs (p->getValue() - values[(size_t) i]) > 1.0e-6f)
            {
                p->beginGesture();
                p->setValue (values[(size_t) i]);
                p->endGesture();
            }
        }

        model.setText ("presetName", preset.name);
        for (int m = 0; m < SynthEngine::numMacros; ++m)
            model.setText ("macroName" + juce::String (m + 1), preset.macroNames[m]);

        updatePresetButton();
    }

    void SynthView::stepPreset (int delta)
    {
        presets = SynthPresets::everything();
        if (presets.empty())
            return;

        int current = 0;
        for (int i = 0; i < (int) presets.size(); ++i)
            if (presets[(size_t) i].name == getPresetName())
                current = i;

        const int n = (int) presets.size();
        applyPreset (presets[(size_t) (((current + delta) % n + n) % n)]);
    }

    void SynthView::showPresetMenu()
    {
        presets = SynthPresets::everything();
        const auto current = getPresetName();
        juce::PopupMenu menu;
        juce::StringArray categories;
        for (auto& p : presets)
            if (! p.isUser())
                categories.addIfNotAlreadyThere (p.category);

        juce::Component::SafePointer<SynthView> safe (this);
        auto choose = [safe] (SynthPresets::Preset p) { if (safe != nullptr) safe->applyPreset (p); };

        for (auto& category : categories)
        {
            juce::PopupMenu sub;
            for (auto& p : presets)
                if (! p.isUser() && p.category == category)
                    sub.addItem (p.name, true, p.name == current, [choose, p] { choose (p); });
            menu.addSubMenu (category, sub);
        }

        juce::PopupMenu user;
        for (auto& p : presets)
            if (p.isUser())
                user.addItem (p.name, true, p.name == current, [choose, p] { choose (p); });
        if (user.getNumItems() == 0)
            user.addItem ("(none yet: use Save)", false, false, [] {});
        menu.addSubMenu ("User", user);

        menu.addSeparator();
        menu.addItem ("Save as preset...", [safe] { if (safe != nullptr) safe->savePreset(); });
        menu.addItem ("Show presets folder", [] { SynthPresets::getUserFolder().createDirectory(); SynthPresets::getUserFolder().revealToUser(); });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetButton));
    }

    void SynthView::savePreset()
    {
        auto* window = new juce::AlertWindow ("Save preset", "Name of the preset (saved in " + SynthPresets::getUserFolder().getFullPathName() + "):",
                                              juce::MessageBoxIconType::NoIcon, this);
        window->addTextEditor ("name", getPresetName());
        window->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

        juce::Component::SafePointer<SynthView> safe (this);
        window->enterModalState (true, juce::ModalCallbackFunction::create ([safe, window] (int result)
        {
            if (result != 1 || safe == nullptr)
                return;

            const auto name = window->getTextEditorContents ("name").trim();
            juce::StringArray macros;
            for (int m = 0; m < SynthEngine::numMacros; ++m)
                macros.add (MacroPanel::macroName (safe->model, m));

            auto& model = safe->model;
            const auto saved = SynthPresets::saveUser (name, [&model] (int id) { return model.value (id); }, macros);
            if (saved.failed())
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Preset not saved", saved.getErrorMessage());
                return;
            }

            model.beginUndoTransaction ("Save preset");
            model.setText ("presetName", name);
            safe->presets = SynthPresets::everything();
            safe->updatePresetButton();
        }), true);
    }

    //==============================================================================
    void SynthView::paint (juce::Graphics& g)
    {
        g.fillAll (Palette::panelBackground);

        // Header: the name, then a bar under the presets.
        auto header = headerArea;
        g.setColour (Palette::windowBackground);
        g.fillRect (header);
        g.setColour (Palette::panelOutline);
        g.drawHorizontalLine (header.getBottom() - 1, 0.0f, (float) getWidth());

        g.setColour (Palette::text);
        g.setFont (juce::FontOptions (19.0f, juce::Font::bold));
        g.drawText ("C-SYNTH", header.withTrimmedLeft (14).withWidth (110).withTrimmedBottom (16), juce::Justification::centredLeft);
        g.setFont (juce::FontOptions (10.0f));
        g.setColour (Palette::textDim);
        g.drawText ("wavetable pads", header.withTrimmedLeft (14).withWidth (110).withTrimmedTop (34), juce::Justification::centredLeft);

        // Output level.
        auto m = meterArea.toFloat();
        g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
        g.setColour (Palette::textDim);
        g.drawText ("OUT", m.removeFromTop (12.0f), juce::Justification::centred);
        const float w = (m.getWidth() - 4.0f) * 0.5f;
        for (int ch = 0; ch < 2; ++ch)
        {
            auto bar = juce::Rectangle<float> (m.getX() + (float) ch * (w + 4.0f), m.getY(), w, m.getHeight());
            g.setColour (EffectUI::plotBackground);
            g.fillRect (bar);
            const float level = juce::jlimit (0.0f, 1.0f, (outputLevel[ch] + 48.0f) / 48.0f);
            auto fill = bar.withTop (bar.getBottom() - level * bar.getHeight());
            g.setGradientFill (juce::ColourGradient::vertical (Palette::meterRed, bar.getY(), Palette::meterGreen, bar.getY() + bar.getHeight() * 0.5f));
            g.fillRect (fill);
        }

        // On the FX page: where the sound goes.
        if (page == fxPage)
        {
            const auto s = SynthParameters::toSettings ([this] (int id) { return model.value (id); });
            const std::pair<const char*, bool> stages[] { { "VOICES", true }, { "CHORUS", s.chorusOn }, { "DELAY", s.delayOn },
                                                          { "REVERB", s.reverbOn }, { "VOLUME", true }, { "OUT", true } };
            auto r = flowArea.toFloat().reduced (20.0f, 18.0f);
            const float boxW = 110.0f, gap = (r.getWidth() - boxW * 6.0f) / 5.0f;
            const float level = juce::jlimit (0.0f, 1.0f, (juce::jmax (outputLevel[0], outputLevel[1]) + 48.0f) / 48.0f);

            for (int i = 0; i < 6; ++i)
            {
                auto box = juce::Rectangle<float> (r.getX() + (float) i * (boxW + gap), r.getCentreY() - 16.0f, boxW, 32.0f);
                const bool on = stages[i].second;
                g.setColour (on ? SynthColours::effects.withAlpha (0.15f + 0.3f * level) : Palette::panelRaised);
                g.fillRoundedRectangle (box, 5.0f);
                g.setColour (on ? SynthColours::effects : Palette::panelOutline);
                g.drawRoundedRectangle (box, 5.0f, 1.2f);
                g.setColour (on ? Palette::text : Palette::textDisabled);
                g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
                g.drawText (on || i == 0 || i >= 4 ? juce::String (stages[i].first) : juce::String (stages[i].first) + " (off)", box, juce::Justification::centred);

                if (i < 5)
                {
                    juce::Path arrow;
                    arrow.addArrow ({ box.getRight() + 6.0f, box.getCentreY(), box.getRight() + gap - 6.0f, box.getCentreY() }, 1.5f, 8.0f, 7.0f);
                    g.setColour (Palette::textDim);
                    g.fillPath (arrow);
                }
            }
        }
    }

    void SynthView::resized()
    {
        auto r = getLocalBounds();
        headerArea = r.removeFromTop (headerHeight);

        // Header, left to right: name, presets, macros, then volume, level and pages at the right.
        auto h = headerArea.reduced (8, 6);
        h.removeFromLeft (120);
        auto presetArea = h.removeFromLeft (380).withSizeKeepingCentre (380, 28);
        previousButton.setBounds (presetArea.removeFromLeft (28));
        presetArea.removeFromLeft (4);
        saveButton.setBounds (presetArea.removeFromRight (56));
        presetArea.removeFromRight (4);
        nextButton.setBounds (presetArea.removeFromRight (28));
        presetArea.removeFromRight (4);
        presetButton.setBounds (presetArea);

        auto tabArea = h.removeFromRight (210).withSizeKeepingCentre (210, 28);
        for (auto* tab : tabs)
            tab->setBounds (tabArea.removeFromLeft (70));
        h.removeFromRight (12);
        meterArea = h.removeFromRight (34).reduced (0, 4);
        h.removeFromRight (8);
        volumeKnob->setBounds (h.removeFromRight (76));
        h.removeFromRight (12);
        h.removeFromLeft (16);
        const int macroW = juce::jmin (90, h.getWidth() / SynthEngine::numMacros);
        for (auto* k : macroKnobs)
            k->setBounds (h.removeFromLeft (macroW));

        // Footer: voices, stereo, spectrum.
        auto footer = r.removeFromBottom (footerHeight).reduced (8, 6);
        spectrum->setBounds (footer.removeFromRight (260));
        footer.removeFromRight (6);
        stereo->setBounds (footer.removeFromRight (150));
        footer.removeFromRight (6);
        voiceField->setBounds (footer);

        auto body = r.reduced (8, 6);
        for (auto& p : pages)
            p.setBounds (body);

        const auto local = body.withZeroOrigin();
        const int gap = 6;
        auto place = [] (juce::Component* c, juce::Rectangle<int> b) { c->setBounds (b); };

        // SYNTH: oscillators and layers above; filter, envelopes, LFOs below.
        {
            auto& list = panels[synthPage];
            auto area = local;
            auto top = area.removeFromTop (area.getHeight() / 2 - gap / 2);
            area.removeFromTop (gap);
            const int w = top.getWidth();
            place (list[0], top.removeFromLeft (w * 37 / 100));
            top.removeFromLeft (gap);
            place (list[1], top.removeFromLeft (w * 37 / 100));
            top.removeFromLeft (gap);
            place (list[2], top);

            place (list[3], area.removeFromLeft (w * 32 / 100));
            area.removeFromLeft (gap);
            place (list[4], area.removeFromLeft (w * 22 / 100));
            area.removeFromLeft (gap);
            place (list[5], area.removeFromLeft (w * 20 / 100));
            area.removeFromLeft (gap);
            place (list[6], area);
        }

        // MOD: the matrix, and beside it movement, macros and voice.
        {
            auto& list = panels[modPage];
            auto area = local;
            place (list[0], area.removeFromLeft (area.getWidth() * 58 / 100));
            area.removeFromLeft (gap);
            place (list[1], area.removeFromTop (area.getHeight() * 40 / 100));
            area.removeFromTop (gap);
            place (list[2], area.removeFromTop (area.getHeight() * 58 / 100));
            area.removeFromTop (gap);
            place (list[3], area);
        }

        // FX: the three effects, the signal path under them.
        {
            auto& list = panels[fxPage];
            auto area = local;
            auto top = area.removeFromTop (area.getHeight() - 90);
            const int w = (top.getWidth() - 2 * gap) / 3;
            for (int i = 0; i < 3; ++i)
            {
                place (list[i], top.removeFromLeft (w));
                top.removeFromLeft (gap);
            }
            flowArea = area.withTrimmedTop (gap) + body.getPosition();
        }
    }
}
