#include "LeadView.h"
#include "../../UI/LookAndFeel/Palette.h"

namespace cdaw
{
    using P = LeadParameters::ID;
    using Destination = LeadEngine::Destination;
    using namespace LeadUI;

    namespace
    {
        constexpr int headerHeight = 84, footerHeight = 150;

        /** Which knob shows a destination's modulation, and how its offset reads in the knob's units. */
        struct Ring
        {
            Destination destination;
            int parameter;
            float octaves;      // the offset is in units of this many octaves (0: it adds)
            float scale;        // else, what one unit of offset adds
        };

        const std::vector<Ring>& rings()
        {
            static const std::vector<Ring> list {
                { Destination::levelA, P::aLevel, 0.0f, 1.0f }, { Destination::levelB, P::bLevel, 0.0f, 1.0f },
                { Destination::pulseWidth, P::aPulseWidth, 0.0f, 0.45f }, { Destination::pulseWidth, P::bPulseWidth, 0.0f, 0.45f },
                { Destination::syncPitch, P::bSemitones, 0.0f, LeadEngine::syncModSemitones },
                { Destination::fm, P::fm, 0.0f, 1.0f }, { Destination::ring, P::ring, 0.0f, 1.0f },
                { Destination::pan, P::aPan, 0.0f, 1.0f }, { Destination::pan, P::bPan, 0.0f, 1.0f },
                { Destination::drive, P::drive, 0.0f, 1.0f },
                { Destination::cutoff, P::cutoff, LeadEngine::cutoffModOctaves, 0.0f }, { Destination::resonance, P::resonance, 0.0f, 1.0f },
                { Destination::vibrato, P::vibratoDepth, 0.0f, 50.0f },
                { Destination::lfo1Rate, P::lfo1Rate, 3.0f, 0.0f }, { Destination::lfo2Rate, P::lfo2Rate, 3.0f, 0.0f } };
            return list;
        }
    }

    LeadView::LeadView (EffectModel& m, LeadEngine& e) : model (m), engine (e)
    {
        setOpaque (true);

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

        for (int i = 0; i < LeadEngine::numMacros; ++i)
            addAndMakeVisible (macroKnobs.add (new ParamKnob (model.parameter (P::macro1 + i), MacroPanel::macroName (model, i), LeadUI::Colours::macro, 0.0f)));

        volumeKnob = std::make_unique<ParamKnob> (model.parameter (P::volume), "Volume", Palette::text, LeadParameters::get (P::volume).defaultValue);
        addAndMakeVisible (*volumeKnob);

        const char* names[] { "PLAY", "MOD", "FX" };
        for (int i = 0; i < 3; ++i)
        {
            auto* tab = tabs.add (new juce::TextButton (names[i]));
            tab->setClickingTogglesState (true);
            tab->setRadioGroupId (4712);
            tab->setColour (juce::TextButton::buttonOnColourId, Palette::accentDim);
            tab->setConnectedEdges ((i > 0 ? juce::Button::ConnectedOnLeft : 0) | (i < 2 ? juce::Button::ConnectedOnRight : 0));
            tab->onClick = [this, i] { showPage (i); };
            addAndMakeVisible (tab);
        }

        auto add = [this] (int p, Panel* panel) { pages[p].addAndMakeVisible (panels[p].add (panel)); };
        add (playPage, new PitchPanel (model, engine));
        add (playPage, new OscillatorPanel (model, engine, 0));
        add (playPage, new OscillatorPanel (model, engine, 1));
        add (playPage, new ScopePanel (model, engine));
        add (playPage, new DrivePanel (model, engine));
        add (playPage, new FilterPanel (model, engine));
        add (playPage, new EnvelopePanel (model, engine, true));
        add (playPage, new EnvelopePanel (model, engine, false));
        add (modPage, new MatrixPanel (model, engine));
        add (modPage, new LfoPanel (model, engine));
        add (modPage, new MovementPanel (model, engine));
        add (modPage, new MacroPanel (model, engine));
        add (modPage, new PlayingPanel (model, engine));
        add (fxPage, new EffectPanel (model, engine, EffectPanel::Kind::dist));
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

        presets = LeadPresets::everything();
        showPage (playPage);
        updatePresetButton();
        setSize (defaultWidth, defaultHeight);
        startTimerHz (60);
    }

    LeadView::~LeadView()
    {
        stopTimer();
    }

    void LeadView::showPage (int p)
    {
        page = juce::jlimit (0, 2, p);
        for (int i = 0; i < 3; ++i)
        {
            pages[i].setVisible (i == page);
            tabs[i]->setToggleState (i == page, juce::dontSendNotification);
        }
        repaint();
    }

    ParamKnob* LeadView::knobFor (int id) const
    {
        for (auto& list : panels)
            for (auto* panel : list)
                if (auto* k = panel->knobFor (id))
                    return k;
        return nullptr;
    }

    //==============================================================================
    void LeadView::tick()
    {
        for (auto* panel : panels[page])
            panel->refresh();

        for (int i = 0; i < LeadEngine::numMacros; ++i)
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

    void LeadView::updateModulationRings()
    {
        const auto s = LeadParameters::toSettings ([this] (int id) { return model.value (id); });
        const bool sounding = engine.activeVoices.load() > 0;

        for (auto& ring : rings())
        {
            auto* knob = knobFor (ring.parameter);
            if (knob == nullptr)
                continue;

            if ((ring.parameter == P::lfo1Rate && model.isOn (P::lfo1Sync)) || (ring.parameter == P::lfo2Rate && model.isOn (P::lfo2Sync)))
            {
                knob->setModulation (std::nullopt, std::nullopt);
                continue;
            }

            float low = 0.0f, high = 0.0f;
            bool routed = false;
            for (auto& slot : s.slots)
                if (slot.destination == ring.destination && slot.source != LeadEngine::Source::none && std::abs (slot.amount) > 0.0001f)
                {
                    routed = true;
                    const bool bipolar = LeadEngine::isBipolar (slot.source);
                    low += bipolar ? -std::abs (slot.amount) : juce::jmin (0.0f, slot.amount);
                    high += bipolar ? std::abs (slot.amount) : juce::jmax (0.0f, slot.amount);
                }

            if (! routed)
            {
                knob->setModulation (std::nullopt, std::nullopt);
                continue;
            }

            const auto& spec = LeadParameters::get (ring.parameter);
            const float base = model.value (ring.parameter);
            auto apply = [&] (float offset)
            {
                const float v = ring.octaves > 0.0f ? base * std::pow (2.0f, ring.octaves * offset) : base + offset * ring.scale;
                return juce::jlimit (spec.minimum, spec.maximum, v);
            };

            std::optional<float> now;
            if (sounding)
                now = apply (engine.destinationValue[(size_t) ring.destination].load());

            knob->setModulation (juce::Range<float> (apply (low), apply (high)), now);
        }
    }

    //==============================================================================
    juce::String LeadView::getPresetName() const
    {
        auto name = model.getText ("presetName");
        return name.isNotEmpty() ? name : LeadPresets::factory()[1].name;     // the defaults are the Classic Saw Lead
    }

    void LeadView::updatePresetButton()
    {
        const auto name = getPresetName();
        bool edited = false;

        for (auto& p : presets)
            if (p.name == name)
            {
                const auto values = LeadPresets::valuesOf (p);
                for (int i = 0; i < P::count && ! edited; ++i)
                {
                    const auto& spec = LeadParameters::get (i);
                    edited = std::abs (model.value (i) - values[(size_t) i]) > (spec.maximum - spec.minimum) * 0.0005f;
                }

                const auto text = p.category + " / " + name + (edited ? " *" : "");
                if (presetButton.getButtonText() != text)
                    presetButton.setButtonText (text);
                return;
            }

        presetButton.setButtonText (name);
    }

    void LeadView::applyPreset (const LeadPresets::Preset& preset)
    {
        model.beginUndoTransaction ("Preset: " + preset.name);
        const auto values = LeadPresets::valuesOf (preset);

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
        for (int m = 0; m < LeadEngine::numMacros; ++m)
            model.setText ("macroName" + juce::String (m + 1), preset.macroNames[m]);

        updatePresetButton();
    }

    void LeadView::stepPreset (int delta)
    {
        presets = LeadPresets::everything();
        if (presets.empty())
            return;

        int current = 0;
        for (int i = 0; i < (int) presets.size(); ++i)
            if (presets[(size_t) i].name == getPresetName())
                current = i;

        const int n = (int) presets.size();
        applyPreset (presets[(size_t) (((current + delta) % n + n) % n)]);
    }

    void LeadView::showPresetMenu()
    {
        presets = LeadPresets::everything();
        const auto current = getPresetName();
        juce::PopupMenu menu;
        juce::StringArray categories;
        for (auto& p : presets)
            if (! p.isUser())
                categories.addIfNotAlreadyThere (p.category);

        juce::Component::SafePointer<LeadView> safe (this);
        auto choose = [safe] (LeadPresets::Preset p) { if (safe != nullptr) safe->applyPreset (p); };

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
        menu.addItem ("Show presets folder", [] { LeadPresets::getUserFolder().createDirectory(); LeadPresets::getUserFolder().revealToUser(); });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetButton));
    }

    void LeadView::savePreset()
    {
        auto* window = new juce::AlertWindow ("Save preset", "Name of the preset (saved in " + LeadPresets::getUserFolder().getFullPathName() + "):",
                                              juce::MessageBoxIconType::NoIcon, this);
        window->addTextEditor ("name", getPresetName());
        window->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

        juce::Component::SafePointer<LeadView> safe (this);
        window->enterModalState (true, juce::ModalCallbackFunction::create ([safe, window] (int result)
        {
            if (result != 1 || safe == nullptr)
                return;

            const auto name = window->getTextEditorContents ("name").trim();
            juce::StringArray macros;
            for (int m = 0; m < LeadEngine::numMacros; ++m)
                macros.add (MacroPanel::macroName (safe->model, m));

            auto& model = safe->model;
            const auto saved = LeadPresets::saveUser (name, [&model] (int id) { return model.value (id); }, macros);
            if (saved.failed())
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Preset not saved", saved.getErrorMessage());
                return;
            }

            model.beginUndoTransaction ("Save preset");
            model.setText ("presetName", name);
            safe->presets = LeadPresets::everything();
            safe->updatePresetButton();
        }), true);
    }

    //==============================================================================
    void LeadView::paint (juce::Graphics& g)
    {
        g.fillAll (Palette::panelBackground);

        auto header = headerArea;
        g.setColour (Palette::windowBackground);
        g.fillRect (header);
        g.setColour (Palette::panelOutline);
        g.drawHorizontalLine (header.getBottom() - 1, 0.0f, (float) getWidth());

        g.setColour (Palette::text);
        g.setFont (juce::FontOptions (19.0f, juce::Font::bold));
        g.drawText ("C-LEAD", header.withTrimmedLeft (14).withWidth (120).withTrimmedBottom (16), juce::Justification::centredLeft);
        g.setFont (juce::FontOptions (10.0f));
        g.setColour (Palette::textDim);
        g.drawText ("virtual analog leads", header.withTrimmedLeft (14).withWidth (120).withTrimmedTop (34), juce::Justification::centredLeft);

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

        if (page == fxPage)
        {
            const auto s = LeadParameters::toSettings ([this] (int id) { return model.value (id); });
            const std::pair<const char*, bool> stages[] { { "VOICES", true }, { "DISTORTION", s.distOn }, { "CHORUS", s.chorusOn }, { "DELAY", s.delayOn },
                                                          { "REVERB", s.reverbOn }, { "VOLUME", true }, { "OUT", true } };
            constexpr int numStages = (int) std::size (stages);
            auto r = flowArea.toFloat().reduced (20.0f, 18.0f);
            const float boxW = 120.0f, gap = (r.getWidth() - boxW * (float) numStages) / (float) (numStages - 1);
            const float level = juce::jlimit (0.0f, 1.0f, (juce::jmax (outputLevel[0], outputLevel[1]) + 48.0f) / 48.0f);

            for (int i = 0; i < numStages; ++i)
            {
                auto box = juce::Rectangle<float> (r.getX() + (float) i * (boxW + gap), r.getCentreY() - 16.0f, boxW, 32.0f);
                const bool on = stages[i].second;
                g.setColour (on ? LeadUI::Colours::effects.withAlpha (0.15f + 0.3f * level) : Palette::panelRaised);
                g.fillRoundedRectangle (box, 5.0f);
                g.setColour (on ? LeadUI::Colours::effects : Palette::panelOutline);
                g.drawRoundedRectangle (box, 5.0f, 1.2f);
                g.setColour (on ? Palette::text : Palette::textDisabled);
                g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
                g.drawText (on || i == 0 || i >= numStages - 2 ? juce::String (stages[i].first) : juce::String (stages[i].first) + " (off)", box, juce::Justification::centred);

                if (i < numStages - 1)
                {
                    juce::Path arrow;
                    arrow.addArrow ({ box.getRight() + 6.0f, box.getCentreY(), box.getRight() + gap - 6.0f, box.getCentreY() }, 1.5f, 8.0f, 7.0f);
                    g.setColour (Palette::textDim);
                    g.fillPath (arrow);
                }
            }
        }
    }

    void LeadView::resized()
    {
        auto r = getLocalBounds();
        headerArea = r.removeFromTop (headerHeight);

        auto h = headerArea.reduced (8, 6);
        h.removeFromLeft (130);
        auto presetArea = h.removeFromLeft (390).withSizeKeepingCentre (390, 28);
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
        const int macroW = juce::jmin (90, h.getWidth() / LeadEngine::numMacros);
        for (auto* k : macroKnobs)
            k->setBounds (h.removeFromLeft (macroW));

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

        // PLAY: pitch and the oscillators above; scope, drive, filter and envelopes below.
        {
            auto& list = panels[playPage];
            auto area = local;
            auto top = area.removeFromTop (area.getHeight() / 2 - gap / 2);
            area.removeFromTop (gap);
            const int w = top.getWidth();
            place (list[0], top.removeFromLeft (w * 36 / 100));
            top.removeFromLeft (gap);
            place (list[1], top.removeFromLeft ((top.getWidth() - gap) / 2));
            top.removeFromLeft (gap);
            place (list[2], top);

            place (list[3], area.removeFromLeft (w * 17 / 100));
            area.removeFromLeft (gap);
            place (list[4], area.removeFromLeft (w * 17 / 100));
            area.removeFromLeft (gap);
            place (list[5], area.removeFromLeft (w * 26 / 100));
            area.removeFromLeft (gap);
            place (list[6], area.removeFromLeft (w * 21 / 100));
            area.removeFromLeft (gap);
            place (list[7], area);
        }

        // MOD: the matrix, and beside it the LFOs, movement, macros and playing.
        {
            auto& list = panels[modPage];
            auto area = local;
            place (list[0], area.removeFromLeft (area.getWidth() * 56 / 100));
            area.removeFromLeft (gap);
            const int height = area.getHeight();
            place (list[1], area.removeFromTop (height * 33 / 100));
            area.removeFromTop (gap);
            place (list[2], area.removeFromTop (height * 18 / 100));
            area.removeFromTop (gap);
            place (list[3], area.removeFromTop (height * 27 / 100));
            area.removeFromTop (gap);
            place (list[4], area);
        }

        // FX: the four effects, the signal path under them.
        {
            auto& list = panels[fxPage];
            auto area = local;
            auto top = area.removeFromTop (area.getHeight() - 90);
            const int w = (top.getWidth() - 3 * gap) / 4;
            for (int i = 0; i < 4; ++i)
            {
                place (list[i], top.removeFromLeft (w));
                top.removeFromLeft (gap);
            }
            flowArea = area.withTrimmedTop (gap) + body.getPosition();
        }
    }
}
