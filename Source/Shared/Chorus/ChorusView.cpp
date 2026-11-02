#include "ChorusView.h"
#include "../../UI/LookAndFeel/Palette.h"

namespace cdaw
{
    namespace
    {
        using P = ChorusParameters::ID;
        constexpr double twoPi = juce::MathConstants<double>::twoPi;

        const juce::Colour voiceColours[ChorusEngine::maxVoices] {
            juce::Colour (0xff4fd6c8), juce::Colour (0xffd77cff), juce::Colour (0xfff2b134), juce::Colour (0xff4fa3ff)
        };
        const juce::Colour approaching { 0xff5fb8ff }, receding { 0xffff6f61 };

        ChorusEngine::Settings settingsOf (EffectModel& m)
        {
            return ChorusParameters::toSettings ([&m] (int id) { return m.value (id); });
        }

        /** Each voice's LFO phase a while ago, given where it is now (the rate held steady). */
        double phaseAgo (const ChorusEngine::Settings& s, double now, int voice, double seconds)
        {
            return now - juce::jlimit (0.01, 20.0, (double) s.rateHz) * ChorusEngine::rateMultipliers[(size_t) voice] * seconds;
        }

        /** The longest delay the voices reach, rounded up for a scale. */
        float delayScale (const ChorusEngine::Settings& s)
        {
            const float longest = s.delayMs + s.depth * ChorusEngine::maxModulationMs;
            for (float step : { 2.0f, 4.0f, 6.0f, 10.0f, 15.0f, 20.0f, 30.0f, 40.0f })
                if (longest * 1.12f <= step)
                    return step;
            return 40.0f;
        }
    }

    //==============================================================================
    class ChorusView::VoiceTraces : public juce::Component
    {
    public:
        VoiceTraces (EffectModel& m, ChorusEngine& e) : model (m), engine (e) { setOpaque (true); }

        void paint (juce::Graphics& g) override
        {
            const auto s = settingsOf (model);
            g.fillAll (Palette::panelBackground);
            EffectUI::drawPanel (g, getLocalBounds().toFloat(), "VOICES");

            auto p = getLocalBounds().toFloat().withTrimmedLeft (44.0f).withTrimmedRight (26.0f).withTrimmedTop (28.0f).withTrimmedBottom (20.0f);
            g.setColour (EffectUI::plotBackground);
            g.fillRect (p);

            // The delay scale is zoomed to the sweep, with room around it.
            const float low = s.delayMs, high = s.delayMs + s.depth * ChorusEngine::maxModulationMs;
            const float margin = juce::jmax (0.5f, (high - low) * 0.45f);
            const float bottom = juce::jmax (0.0f, low - margin), top = high + margin;
            auto yFor = [&] (float ms) { return juce::jmap (juce::jlimit (bottom, top, ms), bottom, top, p.getBottom(), p.getY()); };

            g.setFont (juce::FontOptions (9.5f));
            const float span = top - bottom;
            const float step = span <= 2.0f ? 0.25f : span <= 4.0f ? 0.5f : span <= 10.0f ? 1.0f : span <= 20.0f ? 2.0f : 5.0f;
            for (float ms = std::ceil (bottom / step) * step; ms <= top + 0.01f; ms += step)
            {
                g.setColour (juce::Colours::white.withAlpha (0.05f));
                g.drawHorizontalLine ((int) yFor (ms), p.getX(), p.getRight());
                g.setColour (Palette::textDisabled);
                g.drawText (juce::String (ms, step < 0.5f ? 2 : step < 1.0f ? 1 : 0) + " ms", juce::Rectangle<float> (p.getX() - 42.0f, yFor (ms) - 6.0f, 38.0f, 12.0f),
                            juce::Justification::centredRight);
            }

            // The band the sweep covers.
            g.setGradientFill (juce::ColourGradient::vertical (Palette::tempo.withAlpha (0.10f), yFor (high), Palette::tempo.withAlpha (0.03f), yFor (low)));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (p.getX(), yFor (high), p.getRight(), yFor (low)));

            // A few cycles of each voice, now at the right edge.
            const double window = juce::jlimit (0.3, 8.0, 2.5 / juce::jlimit (0.03, 20.0, (double) s.rateHz));
            const int voices = juce::jlimit (1, ChorusEngine::maxVoices, s.voices);

            g.saveState();
            g.reduceClipRegion (p.toNearestInt());

            for (int v = voices; --v >= 0;)
            {
                const double now = engine.voicePhase[(size_t) v].load();

                for (int ch = 1; ch >= 0; --ch)
                {
                    juce::Path trace;
                    for (float x = p.getX(); x <= p.getRight() + 1.0f; x += 2.0f)
                    {
                        const double ago = (double) (p.getRight() - x) / p.getWidth() * window;
                        const auto y = yFor (ChorusEngine::getVoiceDelayMs (s, phaseAgo (s, now, v, ago), ch));
                        if (x == p.getX()) trace.startNewSubPath (x, y);
                        else               trace.lineTo (x, y);
                    }

                    if (ch == 1)
                    {
                        const float dashes[] { 4.0f, 3.0f };
                        juce::PathStrokeType (1.2f).createDashedStroke (trace, trace, dashes, 2);
                        g.setColour (voiceColours[v].withAlpha (0.45f));
                        g.fillPath (trace);
                    }
                    else
                    {
                        g.setColour (voiceColours[v].withAlpha (0.18f));
                        g.strokePath (trace, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                        g.setColour (voiceColours[v]);
                        g.strokePath (trace, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                    }
                }
            }

            g.restoreState();

            // Where each voice is now.
            for (int v = 0; v < voices; ++v)
            {
                const double now = engine.voicePhase[(size_t) v].load();
                const juce::Point<float> dot { p.getRight(), yFor (ChorusEngine::getVoiceDelayMs (s, now, 0)) };
                EffectUI::drawGlow (g, dot, 18.0f, voiceColours[v], 0.5f + level);
                g.setColour (voiceColours[v]);
                g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre (dot));
                g.setColour (Palette::windowBackground);
                g.drawEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre (dot), 1.2f);
            }

            g.setFont (juce::FontOptions (9.5f));
            g.setColour (Palette::textDisabled);
            g.drawText ("now", juce::Rectangle<float> (p.getRight() - 40.0f, p.getBottom() + 3.0f, 40.0f, 12.0f), juce::Justification::centredRight);
            g.drawText ("-" + juce::String (window, window < 1.0 ? 2 : 1) + " s", juce::Rectangle<float> (p.getX(), p.getBottom() + 3.0f, 60.0f, 12.0f),
                        juce::Justification::centredLeft);

            auto legend = getLocalBounds().toFloat().reduced (10.0f, 6.0f).removeFromTop (14.0f);
            g.setFont (juce::FontOptions (10.5f));
            g.setColour (Palette::textDim);
            g.drawText (juce::String::fromUTF8 ("\xe2\x80\x94 left    - - right"), legend, juce::Justification::centredRight);
        }

        EffectModel& model;
        ChorusEngine& engine;
        float level = 0.0f;
    };

    //==============================================================================
    class ChorusView::SpaceView : public juce::Component
    {
    public:
        SpaceView (EffectModel& m, ChorusEngine& e) : model (m), engine (e) { setOpaque (true); }

        void paint (juce::Graphics& g) override
        {
            const auto s = settingsOf (model);
            g.fillAll (Palette::panelBackground);
            EffectUI::drawPanel (g, getLocalBounds().toFloat(), "SPACE");

            auto area = getLocalBounds().toFloat().reduced (10.0f).withTrimmedTop (18.0f);
            g.setColour (EffectUI::plotBackground);
            g.fillRoundedRectangle (area, 3.0f);

            const juce::Point<float> listener { area.getCentreX(), area.getBottom() - 16.0f };
            const float radius = juce::jmin (area.getHeight() - 30.0f, area.getWidth() * 0.5f - 12.0f);
            const float scale = delayScale (s);
            auto distance = [&] (float ms) { return juce::jlimit (0.0f, 1.0f, ms / scale) * radius; };
            constexpr float spreadAngle = 1.0f;     // radians either side at full spread

            // Distance rings, in ms and metres (sound travels about 34 cm per ms).
            g.setFont (juce::FontOptions (9.0f));
            for (int i = 1; i <= 4; ++i)
            {
                const float ms = scale * (float) i / 4.0f, r = distance (ms);
                juce::Path ring;
                ring.addCentredArc (listener.x, listener.y, r, r, 0.0f, -spreadAngle - 0.35f, spreadAngle + 0.35f, true);
                g.setColour (juce::Colours::white.withAlpha (0.06f));
                g.strokePath (ring, juce::PathStrokeType (1.0f));
                g.setColour (Palette::textDisabled);
                g.drawText (juce::String (ms, ms < 10.0f ? 1 : 0) + " ms  " + juce::String (ms * 0.343f, 1) + " m",
                            juce::Rectangle<float> (listener.x + 4.0f, listener.y - r - 12.0f, 90.0f, 11.0f), juce::Justification::centredLeft);
            }

            // The listener.
            g.setColour (Palette::textDim);
            g.fillEllipse (juce::Rectangle<float> (14.0f, 14.0f).withCentre (listener));
            g.setColour (Palette::textDisabled);
            g.fillEllipse (juce::Rectangle<float> (5.0f, 8.0f).withCentre (listener.translated (-8.5f, 0.0f)));
            g.fillEllipse (juce::Rectangle<float> (5.0f, 8.0f).withCentre (listener.translated (8.5f, 0.0f)));

            const int voices = juce::jlimit (1, ChorusEngine::maxVoices, s.voices);
            const double window = juce::jlimit (0.1, 1.5, 0.6 / juce::jlimit (0.03, 20.0, (double) s.rateHz));

            auto positionAt = [&] (int v, double phase)
            {
                // Distance from the average of the voice's two sides; direction from its pan.
                const float ms = 0.5f * (ChorusEngine::getVoiceDelayMs (s, phase, 0) + ChorusEngine::getVoiceDelayMs (s, phase, 1));
                const float angle = ChorusEngine::getVoicePan (s, v) * spreadAngle;
                const float r = distance (ms);
                return juce::Point<float> (listener.x + r * std::sin (angle), listener.y - r * std::cos (angle));
            };

            for (int v = 0; v < voices; ++v)
            {
                const double now = engine.voicePhase[(size_t) v].load();
                const auto here = positionAt (v, now);

                // A fading trail of where it has just been.
                for (int i = 12; i >= 1; --i)
                {
                    const auto past = positionAt (v, phaseAgo (s, now, v, window * i / 12.0));
                    g.setColour (voiceColours[v].withAlpha (0.25f * (1.0f - (float) i / 13.0f)));
                    g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (past));
                }

                // The sound's path to the listener, tinted by the Doppler shift.
                const float before = ChorusEngine::getVoiceDelayMs (s, phaseAgo (s, now, v, 0.01), 0);
                const float speed = (ChorusEngine::getVoiceDelayMs (s, now, 0) - before) / 10.0f;     // ms of delay per ms
                const auto tint = speed < 0.0f ? approaching : receding;
                const float amount = juce::jlimit (0.0f, 1.0f, std::abs (speed) * 60.0f);

                g.setColour (tint.withAlpha (0.12f + 0.3f * amount));
                g.drawLine ({ here, listener }, 1.0f);

                EffectUI::drawGlow (g, here, 22.0f, tint, amount * (0.4f + level));
                g.setColour (voiceColours[v]);
                g.fillEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre (here));
                g.setColour (tint.withAlpha (amount));
                g.drawEllipse (juce::Rectangle<float> (16.0f, 16.0f).withCentre (here), 1.5f);
            }

            auto legend = getLocalBounds().toFloat().reduced (10.0f, 6.0f).removeFromTop (14.0f);
            g.setFont (juce::FontOptions (10.5f));
            g.setColour (receding);
            g.drawText ("moving away", legend.removeFromRight (80.0f), juce::Justification::centredRight);
            g.setColour (approaching);
            g.drawText ("closer", legend.removeFromRight (50.0f), juce::Justification::centredRight);
        }

        EffectModel& model;
        ChorusEngine& engine;
        float level = 0.0f;
    };

    //==============================================================================
    class ChorusView::ResponseView : public juce::Component
    {
    public:
        ResponseView (EffectModel& m, ChorusEngine& e) : model (m), engine (e) { setOpaque (true); }

        void paint (juce::Graphics& g) override
        {
            const auto s = settingsOf (model);
            const double rate = engine.getSampleRate();
            g.fillAll (Palette::panelBackground);
            EffectUI::drawPanel (g, getLocalBounds().toFloat(), "RESPONSE");

            auto p = getLocalBounds().toFloat().withTrimmedLeft (34.0f).withTrimmedRight (10.0f).withTrimmedTop (26.0f).withTrimmedBottom (18.0f);
            g.setColour (EffectUI::plotBackground);
            g.fillRect (p);
            EffectUI::drawFrequencyGrid (g, p, axis, true);

            auto yFor = [&] (double db) { return juce::jmap ((float) juce::jlimit (-24.0, 12.0, db), -24.0f, 12.0f, p.getBottom(), p.getY()); };

            g.setFont (juce::FontOptions (9.0f));
            for (float db : { 12.0f, 0.0f, -12.0f, -24.0f })
            {
                g.setColour (Palette::panelOutline.withAlpha (db == 0.0f ? 0.8f : 0.35f));
                g.drawHorizontalLine ((int) yFor (db), p.getX(), p.getRight());
                g.setColour (Palette::textDisabled);
                g.drawText ((db > 0 ? "+" : "") + juce::String ((int) db), juce::Rectangle<float> (p.getX() - 32.0f, yFor (db) - 6.0f, 28.0f, 12.0f),
                            juce::Justification::centredRight);
            }

            float delays[ChorusEngine::maxVoices] {};
            for (int v = 0; v < ChorusEngine::maxVoices; ++v)
                delays[v] = ChorusEngine::getVoiceDelayMs (s, engine.voicePhase[(size_t) v].load(), 0);

            // Up high the comb's teeth are closer together than the pixels: show each column's
            // average (in power) rather than whichever tooth it happens to land on.
            juce::Path curve;
            constexpr int samplesPerColumn = 8;
            for (float x = p.getX(); x <= p.getRight(); x += 2.0f)
            {
                double power = 0.0;
                for (int i = 0; i < samplesPerColumn; ++i)
                {
                    const auto db = ChorusEngine::getResponseDb (s, delays, axis.toHz (x - 1.0f + 2.0f * ((float) i + 0.5f) / samplesPerColumn, p), rate);
                    power += std::pow (10.0, db / 10.0);
                }

                const auto y = yFor (10.0 * std::log10 (juce::jmax (1.0e-12, power / samplesPerColumn)));
                if (x == p.getX()) curve.startNewSubPath (x, y);
                else               curve.lineTo (x, y);
            }

            g.saveState();
            g.reduceClipRegion (p.toNearestInt());
            juce::Path fill (curve);
            fill.lineTo (p.getRight(), p.getBottom());
            fill.lineTo (p.getX(), p.getBottom());
            fill.closeSubPath();
            const auto colour = std::abs (s.feedback) > 0.3f ? juce::Colour (0xffd77cff) : Palette::tempo;
            g.setGradientFill (juce::ColourGradient::vertical (colour.withAlpha (0.35f), p.getY(), colour.withAlpha (0.03f), p.getBottom()));
            g.fillPath (fill);
            g.setColour (colour);
            g.strokePath (curve, juce::PathStrokeType (1.6f));
            g.restoreState();
        }

        EffectModel& model;
        ChorusEngine& engine;
        EffectUI::FrequencyAxis axis;
    };

    //==============================================================================
    ChorusView::ChorusView (EffectModel& m, ChorusEngine& e) : model (m), engine (e)
    {
        setOpaque (true);

        traces = std::make_unique<VoiceTraces> (model, engine);
        space = std::make_unique<SpaceView> (model, engine);
        response = std::make_unique<ResponseView> (model, engine);
        addAndMakeVisible (*traces);
        addAndMakeVisible (*space);
        addAndMakeVisible (*response);

        auto add = [this] (P id, const char* label, juce::Colour c, float mid)
        {
            auto* k = knobs.add (new ParamKnob (model.parameter (id), label, c, ChorusParameters::get (id).defaultValue, mid));
            addAndMakeVisible (k);
        };

        add (P::voices, "Voices", Palette::tempo, 0.0f);
        add (P::delay, "Delay", Palette::tempo, 8.0f);
        add (P::spread, "Spread", Palette::tempo, 0.0f);
        add (P::rate, "Rate", Palette::automation, 1.0f);
        add (P::depth, "Depth", Palette::automation, 0.0f);
        add (P::feedback, "Feedback", Palette::marker, 0.0f);
        add (P::tone, "Tone", Palette::accent, 6000.0f);
        add (P::width, "Width", Palette::marker, 0.0f);
        add (P::mix, "Mix", Palette::marker, 0.0f);

        groups = { { "VOICES", 0, 3, {} }, { "MOTION", 3, 2, {} }, { "COLOUR", 5, 2, {} }, { "OUTPUT", 7, 2, {} } };

        const auto& characters = ChorusParameters::getCharacters();
        for (int i = 0; i < (int) characters.size(); ++i)
        {
            auto* b = characterButtons.add (new juce::TextButton (characters[(size_t) i].name));
            b->setTooltip (characters[(size_t) i].tooltip);
            b->setClickingTogglesState (false);
            b->setColour (juce::TextButton::buttonOnColourId, Palette::accentDim);
            b->setConnectedEdges ((i > 0 ? juce::Button::ConnectedOnLeft : 0) | (i < (int) characters.size() - 1 ? juce::Button::ConnectedOnRight : 0));
            b->onClick = [this, i] { applyCharacter (i); };
            addAndMakeVisible (b);
        }

        updateCharacterButtons();
        setSize (defaultWidth, defaultHeight);
        startTimerHz (60);
    }

    ChorusView::~ChorusView()
    {
        stopTimer();
    }

    void ChorusView::applyCharacter (int index)
    {
        auto& character = ChorusParameters::getCharacters()[(size_t) index];
        model.beginUndoTransaction (juce::String ("Chorus: ") + character.name);

        for (auto& [id, value] : character.values)
        {
            auto p = model.parameter (id);
            p->beginGesture();
            p->setValue (value);
            p->endGesture();
        }

        updateCharacterButtons();
    }

    void ChorusView::updateCharacterButtons()
    {
        auto& characters = ChorusParameters::getCharacters();
        for (int i = 0; i < characterButtons.size(); ++i)
        {
            bool matches = true;
            for (auto& [id, value] : characters[(size_t) i].values)
            {
                auto range = ChorusParameters::get (id).getRange().getRange();
                matches = matches && std::abs (model.value (id) - value) <= range.getLength() * 0.01f;
            }

            characterButtons[i]->setToggleState (matches, juce::dontSendNotification);
        }
    }

    void ChorusView::tick (float input, float wet)
    {
        juce::ignoreUnused (input);
        const auto level = juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (wet, -100.0f) + 48.0f) / 48.0f);
        wetLevel = level > wetLevel ? level : wetLevel * 0.92f;
        traces->level = space->level = wetLevel;

        traces->repaint();
        space->repaint();
        response->repaint();
    }

    void ChorusView::timerCallback()
    {
        const float in = engine.inputPeak.take(), wet = engine.wetPeak.take();
        const bool empty = in == 0.0f && wet == 0.0f;
        emptyTicks = empty ? emptyTicks + 1 : 0;
        if (! empty || emptyTicks > 3)
            tick (in, wet);

        for (auto* k : knobs)
            k->refresh();

        updateCharacterButtons();
        repaint (getLocalBounds().removeFromTop (EffectUI::headerHeight));
    }

    void ChorusView::paint (juce::Graphics& g)
    {
        g.fillAll (Palette::panelBackground);

        auto text = [this] (P id) { return model.parameter (id)->getCurrentText(); };
        auto summary = text (P::voices) + "  -  " + text (P::rate) + "  -  depth " + text (P::depth) + "  -  mix " + text (P::mix);
        EffectUI::drawHeader (g, getLocalBounds().removeFromTop (EffectUI::headerHeight).withTrimmedRight (360), "C-CHORUS", summary);
        EffectUI::paintKnobGroups (g, groups);
    }

    void ChorusView::resized()
    {
        auto r = getLocalBounds();
        auto header = r.removeFromTop (EffectUI::headerHeight).reduced (10, 5);
        for (int i = characterButtons.size(); --i >= 0;)
            characterButtons[i]->setBounds (header.removeFromRight (84));

        EffectUI::layoutKnobGroups (groups, knobs, r.removeFromBottom (EffectUI::knobRowHeight).reduced (8, 6));

        r.reduce (8, 0);
        auto right = r.removeFromRight (juce::jlimit (300, 420, r.getWidth() * 2 / 5));
        r.removeFromRight (8);
        traces->setBounds (r);
        space->setBounds (right.removeFromTop (right.getHeight() * 11 / 20));
        right.removeFromTop (8);
        response->setBounds (right);
    }
}
