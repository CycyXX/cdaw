#include "SynthPanels.h"
#include "../Filter/FilterIcons.h"
#include "../Filter/FilterParameters.h"
#include "../DSP/TempoSync.h"
#include "../../UI/LookAndFeel/Palette.h"

namespace cdaw
{
    using namespace SynthParameters;
    using P = SynthParameters::ID;

    namespace SynthColours
    {
        juce::Colour forCutoff (float hz)
        {
            static const juce::Colour stops[] { juce::Colour (0xff27609a), juce::Colour (0xff2fa7b8), juce::Colour (0xff8bd34f),
                                                juce::Colour (0xfff2b134), juce::Colour (0xfffff3d6) };
            constexpr int numStops = (int) std::size (stops);
            const float t = juce::jlimit (0.0f, 1.0f, (float) (std::log (juce::jmax (20.0f, hz) / 20.0) / std::log (1000.0)));
            const float p = t * (float) (numStops - 1);
            const int i = juce::jmin ((int) p, numStops - 2);
            return stops[i].interpolatedWith (stops[i + 1], p - (float) i);
        }

        juce::Colour forSlot (int slot)
        {
            static const juce::Colour colours[] { juce::Colour (0xff4fd6c8), juce::Colour (0xffd77cff), juce::Colour (0xfff2b134), juce::Colour (0xff4fa3ff),
                                                  juce::Colour (0xff3ccf6e), juce::Colour (0xffff6fae), juce::Colour (0xffff8a4f), juce::Colour (0xff9c8cff) };
            return colours[juce::jlimit (0, 7, slot)];
        }
    }

    //==============================================================================
    SynthPanel::SynthPanel (EffectModel& m, SynthEngine& e, const juce::String& t) : model (m), engine (e), title (t)
    {
        setOpaque (true);
    }

    void SynthPanel::paint (juce::Graphics& g)
    {
        g.fillAll (Palette::panelBackground);
        EffectUI::drawPanel (g, getLocalBounds().toFloat(), {});
        g.setColour (Palette::textDim);
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawText (title, titleRow().toFloat(), juce::Justification::centredLeft);
    }

    void SynthPanel::refresh()
    {
        for (auto* k : knobs)
            k->refresh();
    }

    ParamKnob* SynthPanel::knobFor (int id) const
    {
        for (size_t i = 0; i < knobIds.size(); ++i)
            if (knobIds[i] == id)
                return knobs[(int) i];
        return nullptr;
    }

    ParamKnob* SynthPanel::addKnob (int id, const juce::String& label, juce::Colour colour, float skewMidPoint)
    {
        auto* k = knobs.add (new ParamKnob (model.parameter (id), label, colour, SynthParameters::get (id).defaultValue, skewMidPoint));
        knobIds.push_back (id);
        addAndMakeVisible (k);
        return k;
    }

    void SynthPanel::layoutRow (juce::Rectangle<int> row, std::initializer_list<ParamKnob*> list)
    {
        const int w = row.getWidth() / juce::jmax (1, (int) list.size());
        for (auto* k : list)
            k->setBounds (row.removeFromLeft (w).reduced (2, 0));
    }

    SynthEngine::Settings SynthPanel::settings() const
    {
        return SynthParameters::toSettings ([this] (int id) { return model.value (id); });
    }

    void SynthPanel::drawPlot (juce::Graphics& g, juce::Rectangle<float> r)
    {
        g.setColour (EffectUI::plotBackground);
        g.fillRoundedRectangle (r, 3.0f);
    }

    //==============================================================================
    OscillatorPanel::OscillatorPanel (EffectModel& m, SynthEngine& e, int i)
        : SynthPanel (m, e, i == 0 ? "OSC A" : "OSC B"), index (i),
          onButton (m, oscillator (i, aOn), "On", SynthColours::forOscillator (i), "Oscillator on / off"),
          bankButtons (m, oscillator (i, aBank), Wavetables::getBankNames(), SynthColours::forOscillator (i), "Wavetable")
    {
        addAndMakeVisible (onButton);
        addAndMakeVisible (bankButtons);
        bankButtons.setTooltip ("Wavetable: Analog (sine to square), Vowel (A E I O U), Glass, Organ, Air, Pulse (PWM)");

        const auto c = SynthColours::forOscillator (i);
        addKnob (oscillator (i, aPosition), "Position", c);
        addKnob (oscillator (i, aLevel), "Level", c);
        addKnob (oscillator (i, aPan), "Pan", c);
        addKnob (oscillator (i, aOctave), "Octave", Palette::textDim);
        addKnob (oscillator (i, aSemitones), "Semi", Palette::textDim);
        addKnob (oscillator (i, aFine), "Fine", Palette::textDim);
        addKnob (oscillator (i, aUnison), "Unison", Palette::tempo);
        addKnob (oscillator (i, aDetune), "Detune", Palette::tempo);
        addKnob (oscillator (i, aSpread), "Spread", Palette::tempo);
    }

    void OscillatorPanel::resized()
    {
        auto top = titleRow();
        onButton.setBounds (top.removeFromRight (44));
        top.removeFromRight (6);
        bankButtons.setBounds (top.removeFromRight (juce::jmin (330, top.getWidth() - 50)));

        auto r = content();
        auto second = r.removeFromBottom (knobHeight);
        auto first = r.removeFromBottom (knobHeight);
        layoutRow (first, { knobs[0], knobs[1], knobs[2], knobs[3], knobs[4] });
        layoutRow (second, { knobs[5], knobs[6], knobs[7], knobs[8] });
        display = r.withTrimmedBottom (4);
    }

    void OscillatorPanel::refresh()
    {
        SynthPanel::refresh();
        onButton.refresh();
        bankButtons.refresh();
        repaint (display);
    }

    void OscillatorPanel::paint (juce::Graphics& g)
    {
        SynthPanel::paint (g);
        const auto s = settings();
        auto r = display.toFloat();
        drawPlot (g, r);

        auto wave = r.removeFromLeft (r.getWidth() * 0.64f);
        r.removeFromLeft (4.0f);
        drawWavetable (g, wave, s);
        drawHarmonics (g, r, s);

        if (! s.osc[index].on)
        {
            g.setColour (EffectUI::plotBackground.withAlpha (0.65f));
            g.fillRoundedRectangle (display.toFloat(), 3.0f);
            g.setColour (Palette::textDim);
            g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            g.drawText ("OFF", display, juce::Justification::centred);
        }
    }

    void OscillatorPanel::drawWavetable (juce::Graphics& g, juce::Rectangle<float> area, const SynthEngine::Settings& s)
    {
        const auto& tables = Wavetables::get();
        const auto& osc = s.osc[index];
        const int bank = juce::jlimit (0, Wavetables::numBanks - 1, osc.bank);
        const auto colour = SynthColours::forOscillator (index);
        constexpr int points = 96;

        auto r = area.reduced (10.0f, 10.0f).withTrimmedBottom (10.0f);
        const float depthX = juce::jmin (r.getWidth() * 0.24f, 80.0f), depthY = r.getHeight() * 0.42f;
        const float waveW = r.getWidth() - depthX - 12.0f, waveH = r.getHeight() - depthY;

        // The frames recede into depth: the first in front (bottom left), the last at the back.
        auto origin = [&] (float depth) { return juce::Point<float> (r.getX() + depth * depthX, r.getBottom() - waveH * 0.5f - depth * depthY); };

        auto wavePath = [&] (float depth, std::function<float (double)> sampleAt)
        {
            juce::Path p;
            const auto o = origin (depth);
            for (int i = 0; i <= points; ++i)
            {
                const auto x = o.x + waveW * (float) i / points;
                const auto y = o.y - sampleAt ((double) i / points) * waveH * 0.45f;
                if (i == 0) p.startNewSubPath (x, y);
                else        p.lineTo (x, y);
            }
            return p;
        };

        const float position = osc.position;
        for (int f = Wavetables::numFrames - 1; f >= 0; --f)
        {
            const float depth = (float) f / (Wavetables::numFrames - 1);
            const float near = juce::jmax (0.0f, 1.0f - std::abs (depth - position) * (Wavetables::numFrames - 1) * 0.5f);
            const float* frame = tables.frame (bank, f, 0);
            auto p = wavePath (depth, [frame] (double t) { return frame[juce::jmin (Wavetables::tableSize - 1, (int) (t * Wavetables::tableSize))]; });
            g.setColour (colour.withAlpha (0.10f + 0.35f * near));
            g.strokePath (p, juce::PathStrokeType (1.0f));
        }

        // The frame playing now (between two), glowing.
        auto current = wavePath (position, [&] (double t) { return tables.read (bank, position, 0, juce::jmin (0.9999, t)); });
        g.setColour (colour.withAlpha (0.25f));
        g.strokePath (current, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (colour.brighter (0.3f));
        g.strokePath (current, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // A rail along the depth with each sounding voice's modulated position.
        const auto railStart = origin (0.0f).translated (waveW + 8.0f, 0.0f), railEnd = origin (1.0f).translated (waveW + 8.0f, 0.0f);
        g.setColour (Palette::panelOutline);
        g.drawLine ({ railStart, railEnd }, 1.0f);
        g.setColour (colour);
        g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (railStart + (railEnd - railStart) * position));

        for (auto& v : engine.voiceViews)
            if (v.note.load() >= 0)
            {
                const float p = index == 0 ? v.positionA.load() : v.positionB.load();
                const auto at = railStart + (railEnd - railStart) * p;
                EffectUI::drawGlow (g, at, 9.0f, colour, v.amp.load());
                g.setColour (Palette::text.withAlpha (0.4f + 0.6f * v.amp.load()));
                g.fillEllipse (juce::Rectangle<float> (4.0f, 4.0f).withCentre (at));
            }

        g.setFont (juce::FontOptions (10.0f));
        g.setColour (Palette::textDim);
        g.drawText (Wavetables::getBankNames()[bank] + "  -  frame " + juce::String (1.0f + position * (Wavetables::numFrames - 1), 1) + " / "
                        + juce::String (Wavetables::numFrames) + "  -  " + juce::String (juce::jlimit (1, SynthEngine::maxUnison, osc.unison))
                        + (osc.unison > 1 ? " voices" : " voice"),
                    area.reduced (8.0f, 4.0f).removeFromBottom (12.0f), juce::Justification::centredLeft);
    }

    void OscillatorPanel::drawHarmonics (juce::Graphics& g, juce::Rectangle<float> area, const SynthEngine::Settings& s)
    {
        const auto& tables = Wavetables::get();
        const auto& osc = s.osc[index];
        const int bank = juce::jlimit (0, Wavetables::numBanks - 1, osc.bank);
        const float f = osc.position * (Wavetables::numFrames - 1);
        const int f0 = juce::jmin ((int) f, Wavetables::numFrames - 2);
        const float blend = f - (float) f0;
        const auto& a = tables.harmonics (bank, f0);
        const auto& b = tables.harmonics (bank, f0 + 1);
        const auto colour = SynthColours::forOscillator (index);

        auto r = area.reduced (8.0f, 8.0f);
        g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
        g.setColour (Palette::textDim);
        g.drawText ("HARMONICS", r.removeFromTop (12.0f), juce::Justification::centredLeft);
        r.removeFromTop (4.0f);

        constexpr int shown = 32;
        const float w = r.getWidth() / shown;
        for (int h = 0; h < shown; ++h)
        {
            const float amplitude = a[(size_t) h] + (b[(size_t) h] - a[(size_t) h]) * blend;
            const float db = juce::Decibels::gainToDecibels (amplitude, -60.0f);
            const float height = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f) * r.getHeight();
            if (height <= 0.5f)
                continue;

            auto bar = juce::Rectangle<float> (r.getX() + w * (float) h, r.getBottom() - height, juce::jmax (1.0f, w - 1.5f), height);
            g.setGradientFill (juce::ColourGradient::vertical (colour.brighter (0.2f), bar.getY(), colour.withAlpha (0.25f), r.getBottom()));
            g.fillRect (bar);
        }

        g.setColour (Palette::textDisabled);
        g.setFont (juce::FontOptions (9.0f));
        g.drawText ("1", juce::Rectangle<float> (r.getX(), r.getBottom() - 11.0f, 14.0f, 11.0f), juce::Justification::centredLeft);
        g.drawText ("32", juce::Rectangle<float> (r.getRight() - 16.0f, r.getBottom() - 11.0f, 16.0f, 11.0f), juce::Justification::centredRight);
    }

    //==============================================================================
    LayersPanel::LayersPanel (EffectModel& m, SynthEngine& e)
        : SynthPanel (m, e, "LAYERS"), subShape (m, P::subShape, { "Sine", "Tri" }, SynthColours::layers, "Sub shape")
    {
        addAndMakeVisible (subShape);
        addKnob (P::subLevel, "Sub", SynthColours::layers);
        addKnob (P::subOctave, "Sub Oct", SynthColours::layers);
        addKnob (P::noiseLevel, "Noise", Palette::textDim);
        addKnob (P::noiseColour, "Colour", Palette::textDim);
        addKnob (P::drift, "Drift", Palette::tempo);
    }

    void LayersPanel::resized()
    {
        auto top = titleRow();
        subShape.setBounds (top.removeFromRight (92));

        auto r = content();
        layoutRow (r.removeFromBottom (knobHeight), { knobs[0], knobs[1], knobs[2], knobs[3], knobs[4] });
        display = r.withTrimmedBottom (4);
    }

    void LayersPanel::refresh()
    {
        SynthPanel::refresh();
        subShape.refresh();

        // The drift picture wanders faster while voices play.
        wander += (engine.activeVoices.load() > 0 ? 1.0 : 0.25) / 60.0;
        repaint (display);
    }

    void LayersPanel::paint (juce::Graphics& g)
    {
        SynthPanel::paint (g);
        const auto s = settings();
        auto r = display.toFloat();
        const float w = (r.getWidth() - 8.0f) / 3.0f;

        auto box = [&] (const char* name)
        {
            auto b = r.removeFromLeft (w);
            r.removeFromLeft (4.0f);
            drawPlot (g, b);
            g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
            g.setColour (Palette::textDim);
            g.drawText (name, b.reduced (6.0f, 4.0f).removeFromTop (12.0f), juce::Justification::centredLeft);
            return b.reduced (8.0f, 6.0f).withTrimmedTop (14.0f);
        };

        // Sub: one cycle, as loud as it's set.
        {
            auto b = box ("SUB");
            juce::Path p;
            for (int i = 0; i <= 48; ++i)
            {
                const double t = i / 48.0;
                const float v = s.subTriangle ? (float) (1.0 - 4.0 * std::abs (t - 0.5)) : (float) std::sin (juce::MathConstants<double>::twoPi * t);
                const auto x = b.getX() + b.getWidth() * (float) t, y = b.getCentreY() - v * s.subLevel * b.getHeight() * 0.45f;
                if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            g.setColour (SynthColours::layers.withAlpha (0.3f + 0.7f * juce::jmin (1.0f, s.subLevel * 2.0f)));
            g.strokePath (p, juce::PathStrokeType (1.6f));
        }

        // Noise: its spectrum, tilted by the colour.
        {
            auto b = box ("NOISE");
            const float c = s.noiseColour;
            const double cornerHz = c < 0.0f ? 20000.0 * std::pow (2.0, 6.0 * c) : 40.0 * std::pow (2.0, 7.3 * c);
            EffectUI::FrequencyAxis axis;
            juce::Path p;
            p.startNewSubPath (b.getX(), b.getBottom());
            for (float x = b.getX(); x <= b.getRight(); x += 2.0f)
            {
                const double ratio = axis.toHz (x, b) / cornerHz;
                const double gain = c < 0.0f ? 1.0 / std::sqrt (1.0 + ratio * ratio) : (c > 0.0f ? ratio / std::sqrt (1.0 + ratio * ratio) : 1.0);
                const float db = (float) (20.0 * std::log10 (juce::jmax (1.0e-4, gain)));
                p.lineTo (x, b.getBottom() - juce::jlimit (0.0f, 1.0f, (db + 36.0f) / 36.0f) * b.getHeight() * (0.25f + 0.75f * juce::jmin (1.0f, s.noiseLevel * 3.0f)));
            }
            p.lineTo (b.getRight(), b.getBottom());
            p.closeSubPath();
            g.setGradientFill (juce::ColourGradient::vertical (Palette::textDim.withAlpha (0.5f), b.getY(), Palette::textDim.withAlpha (0.05f), b.getBottom()));
            g.fillPath (p);
        }

        // Drift: a few voices' pitch wandering slowly.
        {
            auto b = box ("DRIFT");
            for (int k = 0; k < 3; ++k)
            {
                juce::Path p;
                for (int i = 0; i <= 48; ++i)
                {
                    const double t = wander + i / 48.0 * 3.0 + k * 1.7;
                    const float v = (float) (0.6 * std::sin (1.3 * t + k) + 0.4 * std::sin (0.47 * t * (k + 1) + 2.0));
                    const auto x = b.getX() + b.getWidth() * (float) i / 48.0f, y = b.getCentreY() - v * s.drift * b.getHeight() * 0.45f;
                    if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
                }
                g.setColour (Palette::tempo.withAlpha (0.25f + 0.5f * s.drift));
                g.strokePath (p, juce::PathStrokeType (1.2f));
            }
        }
    }

    //==============================================================================
    FilterPanel::FilterPanel (EffectModel& m, SynthEngine& e)
        : SynthPanel (m, e, "FILTER"),
          typeButtons (m, P::filterType, FilterParameters::getTypeNames(), SynthColours::filter, "Filter type"),
          slopeButtons (m, P::filterSlope, { "12", "24" }, SynthColours::filter, "Filter slope (dB/oct)")
    {
        typeButtons.drawIcon = FilterIcons::drawType;
        addAndMakeVisible (typeButtons);
        addAndMakeVisible (slopeButtons);

        addKnob (P::cutoff, "Cutoff", SynthColours::filter, 1000.0f);
        addKnob (P::resonance, "Resonance", SynthColours::filter);
        addKnob (P::filterDrive, "Drive", SynthColours::layers);
        addKnob (P::keyTrack, "Key Track", Palette::textDim);
        addKnob (P::filterEnv, "Env", SynthColours::modEnv);
    }

    void FilterPanel::resized()
    {
        auto top = titleRow();
        slopeButtons.setBounds (top.removeFromRight (64));
        top.removeFromRight (6);
        typeButtons.setBounds (top.removeFromRight (140));

        auto r = content();
        layoutRow (r.removeFromBottom (knobHeight), { knobs[0], knobs[1], knobs[2], knobs[3], knobs[4] });
        display = r.withTrimmedBottom (4);
    }

    void FilterPanel::refresh()
    {
        SynthPanel::refresh();
        typeButtons.refresh();
        slopeButtons.refresh();
        repaint (display);
    }

    void FilterPanel::paint (juce::Graphics& g)
    {
        SynthPanel::paint (g);
        const auto s = settings();
        const auto filter = SynthEngine::filterSettings (s);
        const double rate = engine.getSampleRate();

        auto area = display.toFloat();
        drawPlot (g, area);
        auto p = area.reduced (6.0f, 6.0f).withTrimmedBottom (12.0f);
        EffectUI::drawFrequencyGrid (g, p, axis, true);

        constexpr float minDb = -36.0f, maxDb = 24.0f;
        auto yFor = [&] (double db) { return juce::jmap (juce::jlimit (minDb - 6.0f, maxDb + 6.0f, (float) db), minDb, maxDb, p.getBottom(), p.getY()); };
        g.setColour (Palette::panelOutline.withAlpha (0.7f));
        g.drawHorizontalLine ((int) yFor (0.0), p.getX(), p.getRight());

        auto curve = [&] (double cutoffHz)
        {
            juce::Path path;
            for (float x = p.getX(); x <= p.getRight(); x += 2.0f)
            {
                const auto y = yFor (FilterEngine::getResponseDb (filter, cutoffHz, axis.toHz (x, p), rate));
                if (x == p.getX()) path.startNewSubPath (x, y); else path.lineTo (x, y);
            }
            return path;
        };

        g.saveState();
        g.reduceClipRegion (p.toNearestInt());

        // Each sounding voice where its modulation has taken the filter, the loudest filled.
        const SynthEngine::VoiceView* loudest = nullptr;
        for (auto& v : engine.voiceViews)
            if (v.note.load() >= 0 && (loudest == nullptr || v.amp.load() > loudest->amp.load()))
                loudest = &v;

        if (loudest != nullptr)
        {
            auto fill = curve (loudest->cutoff.load());
            fill.lineTo (p.getRight(), p.getBottom() + 10.0f);
            fill.lineTo (p.getX(), p.getBottom() + 10.0f);
            fill.closeSubPath();
            const auto c = SynthColours::forCutoff (loudest->cutoff.load());
            g.setGradientFill (juce::ColourGradient::vertical (c.withAlpha (0.25f), p.getY(), c.withAlpha (0.02f), p.getBottom()));
            g.fillPath (fill);
        }

        for (auto& v : engine.voiceViews)
            if (v.note.load() >= 0)
            {
                const auto hz = v.cutoff.load();
                g.setColour (SynthColours::forCutoff (hz).withAlpha (0.25f + 0.65f * v.amp.load()));
                g.strokePath (curve (hz), juce::PathStrokeType (1.4f));
                g.fillRect (axis.toX (hz, p) - 1.0f, p.getBottom() - 6.0f, 2.0f, 6.0f);
            }

        // The set curve.
        auto set = curve (s.cutoff);
        const float dashes[] { 4.0f, 3.0f };
        juce::PathStrokeType (1.2f).createDashedStroke (set, set, dashes, 2);
        g.setColour (Palette::text.withAlpha (0.6f));
        g.fillPath (set);
        g.restoreState();

        g.setFont (juce::FontOptions (10.0f));
        g.setColour (Palette::textDim);
        g.drawText (model.parameter (P::cutoff)->getCurrentText() + "  -  " + FilterParameters::getTypeNames()[(int) s.filterType]
                        + (s.filterSteep ? " 24" : " 12"),
                    p.reduced (6.0f, 2.0f).removeFromTop (12.0f), juce::Justification::centredRight);
    }

    //==============================================================================
    EnvelopePanel::EnvelopePanel (EffectModel& m, SynthEngine& e, bool amp)
        : SynthPanel (m, e, amp ? "AMP ENVELOPE" : "MOD ENVELOPE"), isAmp (amp), colour (amp ? SynthColours::ampEnv : SynthColours::modEnv)
    {
        addKnob (first() + 0, "Attack", colour, 1.0f);
        addKnob (first() + 1, "Decay", colour, 1.5f);
        addKnob (first() + 2, "Sustain", colour);
        addKnob (first() + 3, "Release", colour, 2.0f);
        if (isAmp)
            addKnob (P::ampVelocity, "Velocity", Palette::textDim);
    }

    void EnvelopePanel::resized()
    {
        auto r = content();
        auto row = r.removeFromBottom (knobHeight);
        if (isAmp)
            layoutRow (row, { knobs[0], knobs[1], knobs[2], knobs[3], knobs[4] });
        else
            layoutRow (row, { knobs[0], knobs[1], knobs[2], knobs[3] });
        display = r.withTrimmedBottom (4);
    }

    void EnvelopePanel::refresh()
    {
        SynthPanel::refresh();
        repaint (display);
    }

    EnvelopePanel::Layout EnvelopePanel::layout() const
    {
        Layout l;
        l.plot = display.toFloat().reduced (10.0f, 10.0f);
        const float a = std::sqrt (juce::jmax (0.001f, model.value (first() + 0)));
        const float d = std::sqrt (juce::jmax (0.001f, model.value (first() + 1)));
        const float r = std::sqrt (juce::jmax (0.005f, model.value (first() + 3)));

        // Times on a square-root scale, so a short attack still shows next to a long release.
        const float plateau = l.plot.getWidth() * 0.16f;
        const float scale = (l.plot.getWidth() - plateau) / (a + d + r);
        l.attackEnd = l.plot.getX() + a * scale;
        l.decayEnd = l.attackEnd + d * scale;
        l.sustainEnd = l.decayEnd + plateau;
        l.releaseEnd = l.sustainEnd + r * scale;
        return l;
    }

    juce::Point<float> EnvelopePanel::handle (int which) const
    {
        const auto l = layout();
        const float sustainY = l.plot.getBottom() - model.value (first() + 2) * l.plot.getHeight();
        switch (which)
        {
            case 0:  return { l.attackEnd, l.plot.getY() };
            case 1:  return { l.decayEnd, sustainY };
            default: return { l.releaseEnd, l.plot.getBottom() };
        }
    }

    int EnvelopePanel::handleAt (juce::Point<float> p) const
    {
        for (int i = 0; i < 3; ++i)
            if (p.getDistanceFrom (handle (i)) < 11.0f)
                return i;
        return -1;
    }

    void EnvelopePanel::paint (juce::Graphics& g)
    {
        SynthPanel::paint (g);
        drawPlot (g, display.toFloat());

        const auto l = layout();
        const float s = model.value (first() + 2);
        auto yFor = [&] (float level) { return l.plot.getBottom() - level * l.plot.getHeight(); };

        juce::Path shape;
        shape.startNewSubPath (l.plot.getX(), l.plot.getBottom());
        shape.lineTo (l.attackEnd, l.plot.getY());
        for (float x = l.attackEnd; x <= l.decayEnd; x += 2.0f)
            shape.lineTo (x, yFor (s + (1.0f - s) * std::exp (-4.6f * (x - l.attackEnd) / juce::jmax (1.0f, l.decayEnd - l.attackEnd))));
        shape.lineTo (l.decayEnd, yFor (s));
        shape.lineTo (l.sustainEnd, yFor (s));
        for (float x = l.sustainEnd; x <= l.releaseEnd; x += 2.0f)
            shape.lineTo (x, yFor (s * std::exp (-6.9f * (x - l.sustainEnd) / juce::jmax (1.0f, l.releaseEnd - l.sustainEnd))));
        shape.lineTo (l.releaseEnd, l.plot.getBottom());

        juce::Path fill (shape);
        fill.closeSubPath();
        g.setGradientFill (juce::ColourGradient::vertical (colour.withAlpha (0.28f), l.plot.getY(), colour.withAlpha (0.03f), l.plot.getBottom()));
        g.fillPath (fill);
        g.setColour (colour);
        g.strokePath (shape, juce::PathStrokeType (1.8f));

        // Stage labels along the bottom.
        g.setFont (juce::FontOptions (9.0f));
        g.setColour (Palette::textDisabled);
        g.drawText ("A", juce::Rectangle<float> (l.plot.getX(), l.plot.getBottom() - 11.0f, l.attackEnd - l.plot.getX(), 11.0f), juce::Justification::centred);
        g.drawText ("D", juce::Rectangle<float> (l.attackEnd, l.plot.getBottom() - 11.0f, l.decayEnd - l.attackEnd, 11.0f), juce::Justification::centred);
        g.drawText ("S", juce::Rectangle<float> (l.decayEnd, l.plot.getBottom() - 11.0f, l.sustainEnd - l.decayEnd, 11.0f), juce::Justification::centred);
        g.drawText ("R", juce::Rectangle<float> (l.sustainEnd, l.plot.getBottom() - 11.0f, l.releaseEnd - l.sustainEnd, 11.0f), juce::Justification::centred);

        // Each sounding voice where it is on the shape.
        const float a = juce::jmax (0.001f, model.value (first() + 0)), d = juce::jmax (0.001f, model.value (first() + 1));
        const float r = juce::jmax (0.005f, model.value (first() + 3));
        for (auto& v : engine.voiceViews)
        {
            if (v.note.load() < 0)
                continue;

            const float t = v.seconds.load(), released = v.releasedAt.load();
            const float level = isAmp ? v.amp.load() : v.mod.load();
            float x;
            if (released >= 0.0f)  x = l.sustainEnd + juce::jlimit (0.0f, 1.0f, (t - released) / r) * (l.releaseEnd - l.sustainEnd);
            else if (t < a)        x = l.plot.getX() + t / a * (l.attackEnd - l.plot.getX());
            else if (t < a + d)    x = l.attackEnd + (t - a) / d * (l.decayEnd - l.attackEnd);
            else                   x = l.decayEnd + juce::jmin (1.0f, (t - a - d) / 2.0f) * (l.sustainEnd - l.decayEnd);

            const juce::Point<float> dot { x, yFor (level) };
            EffectUI::drawGlow (g, dot, 12.0f, colour, 0.5f + 0.5f * level);
            g.setColour (Palette::text);
            g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (dot));
        }

        for (int i = 0; i < 3; ++i)
            EffectUI::drawHandle (g, handle (i), colour, hover == i || dragging == i);
    }

    void EnvelopePanel::mouseMove (const juce::MouseEvent& e)
    {
        const auto h = handleAt (e.position);
        if (h != hover)
        {
            hover = h;
            setMouseCursor (h < 0 ? juce::MouseCursor::NormalCursor : h == 1 ? juce::MouseCursor::UpDownLeftRightResizeCursor
                                                                             : juce::MouseCursor::LeftRightResizeCursor);
            repaint (display);
        }
    }

    void EnvelopePanel::mouseDown (const juce::MouseEvent& e)
    {
        dragging = handleAt (e.position);
        if (dragging < 0)
            return;

        dragStart = e.position;
        for (int i = 0; i < 4; ++i)
            startValues[i] = model.value (first() + i);

        model.beginUndoTransaction (isAmp ? "Amp envelope" : "Mod envelope");
        for (int i = 0; i < 4; ++i)
            model.parameter (first() + i)->beginGesture();
    }

    void EnvelopePanel::mouseDrag (const juce::MouseEvent& e)
    {
        if (dragging < 0)
            return;

        // Sideways scales a time (doubling every 40 px), up and down sets the sustain.
        const auto delta = e.position - dragStart;
        auto setTime = [&] (int part)
        {
            auto& spec = SynthParameters::get (first() + part);
            model.parameter (first() + part)->setValue (juce::jlimit (spec.minimum, spec.maximum, startValues[part] * std::pow (2.0f, delta.x / 40.0f)));
        };

        if (dragging == 0)
            setTime (0);
        else if (dragging == 2)
            setTime (3);
        else
        {
            setTime (1);
            model.parameter (first() + 2)->setValue (juce::jlimit (0.0f, 1.0f, startValues[2] - delta.y / juce::jmax (1.0f, layout().plot.getHeight())));
        }

        repaint (display);
    }

    void EnvelopePanel::mouseUp (const juce::MouseEvent&)
    {
        if (dragging >= 0)
            for (int i = 0; i < 4; ++i)
                model.parameter (first() + i)->endGesture();
        dragging = -1;
    }

    //==============================================================================
    LfoPanel::LfoPanel (EffectModel& m, SynthEngine& e) : SynthPanel (m, e, {})
    {
        for (int l = 0; l < SynthEngine::numLfos; ++l)
        {
            const auto c = SynthColours::forLfo (l);
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
        rateKnobs[l]->setParameter (model.parameter (id), SynthParameters::get (id).defaultValue, note ? 0.0f : 1.0f);
        rateKnobs[l]->setLabel (note ? "Note" : "Rate");
    }

    void LfoPanel::resized()
    {
        auto r = getLocalBounds().reduced (8, 4);
        const int h = r.getHeight() / 2;
        for (int l = 0; l < SynthEngine::numLfos; ++l)
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
        for (int l = 0; l < SynthEngine::numLfos; ++l)
        {
            updateRateKnob (l);
            shapes[l]->refresh();
            syncs[l]->refresh();
            retriggers[l]->refresh();
        }

        SynthPanel::refresh();
        repaint (displays[0]);
        repaint (displays[1]);
    }

    void LfoPanel::paint (juce::Graphics& g)
    {
        SynthPanel::paint (g);
        const auto s = settings();

        for (int l = 0; l < SynthEngine::numLfos; ++l)
        {
            const auto c = SynthColours::forLfo (l);
            g.setColour (Palette::textDim);
            g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
            g.drawText ("LFO " + juce::String (l + 1), sections[l].withHeight (titleHeight - 4), juce::Justification::centredLeft);

            auto r = displays[l].toFloat();
            drawPlot (g, r);
            auto p = r.reduced (8.0f, 8.0f);
            g.setColour (Palette::panelOutline.withAlpha (0.6f));
            g.drawHorizontalLine ((int) p.getCentreY(), p.getX(), p.getRight());

            // One cycle of the shape (the random one's steps drawn as a sample).
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

            // Where it is now.
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
}
