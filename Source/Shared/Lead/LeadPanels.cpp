#include "LeadPanels.h"
#include "../../UI/LookAndFeel/Palette.h"
#include <complex>

namespace cdaw::LeadUI
{
    using namespace LeadParameters;
    using P = LeadParameters::ID;
    using Destination = LeadEngine::Destination;

    namespace
    {
        double noteHz (double note)     { return 440.0 * std::pow (2.0, (note - 69.0) / 12.0); }

        void drawWaveIcon (juce::Graphics& g, juce::Rectangle<float> area, int wave, juce::Colour colour)
        {
            auto r = area.withSizeKeepingCentre (juce::jmin (area.getWidth() - 6.0f, 22.0f), juce::jmin (area.getHeight() - 6.0f, 12.0f));
            juce::Path p;
            switch ((LeadEngine::Wave) wave)
            {
                case LeadEngine::Wave::saw:
                    p.startNewSubPath (r.getX(), r.getBottom());
                    p.lineTo (r.getCentreX(), r.getY());
                    p.lineTo (r.getCentreX(), r.getBottom());
                    p.lineTo (r.getRight(), r.getY());
                    break;
                case LeadEngine::Wave::pulse:
                    p.startNewSubPath (r.getX(), r.getBottom());
                    p.lineTo (r.getX(), r.getY());
                    p.lineTo (r.getX() + r.getWidth() * 0.35f, r.getY());
                    p.lineTo (r.getX() + r.getWidth() * 0.35f, r.getBottom());
                    p.lineTo (r.getRight(), r.getBottom());
                    p.lineTo (r.getRight(), r.getY());
                    break;
                case LeadEngine::Wave::triangle:
                    p.startNewSubPath (r.getX(), r.getBottom());
                    p.lineTo (r.getX() + r.getWidth() * 0.25f, r.getY());
                    p.lineTo (r.getX() + r.getWidth() * 0.75f, r.getBottom());
                    p.lineTo (r.getRight(), r.getY());
                    break;
                case LeadEngine::Wave::sine:
                case LeadEngine::Wave::numWaves:
                default:
                    for (int i = 0; i <= 24; ++i)
                    {
                        const float x = r.getX() + r.getWidth() * (float) i / 24.0f;
                        const float y = r.getCentreY() - std::sin (juce::MathConstants<float>::twoPi * (float) i / 24.0f) * r.getHeight() * 0.5f;
                        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
                    }
                    break;
            }
            g.setColour (colour);
            g.strokePath (p, juce::PathStrokeType (1.5f, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
        }
    }

    namespace Colours
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

    juce::String noteName (int note)
    {
        static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return names[((note % 12) + 12) % 12] + juce::String (note / 12 - 1);
    }

    //==============================================================================
    Panel::Panel (EffectModel& m, LeadEngine& e, const juce::String& t) : model (m), engine (e), title (t)
    {
        setOpaque (true);
    }

    void Panel::paint (juce::Graphics& g)
    {
        g.fillAll (Palette::panelBackground);
        EffectUI::drawPanel (g, getLocalBounds().toFloat(), {});
        g.setColour (Palette::textDim);
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawText (title, titleRow().toFloat(), juce::Justification::centredLeft);
    }

    void Panel::refresh()
    {
        for (auto* k : knobs)
            k->refresh();
    }

    ParamKnob* Panel::knobFor (int id) const
    {
        for (size_t i = 0; i < knobIds.size(); ++i)
            if (knobIds[i] == id)
                return knobs[(int) i];
        return nullptr;
    }

    ParamKnob* Panel::addKnob (int id, const juce::String& label, juce::Colour colour, float skewMidPoint)
    {
        auto* k = knobs.add (new ParamKnob (model.parameter (id), label, colour, LeadParameters::get (id).defaultValue, skewMidPoint));
        knobIds.push_back (id);
        addAndMakeVisible (k);
        return k;
    }

    void Panel::layoutRow (juce::Rectangle<int> row, std::initializer_list<ParamKnob*> list)
    {
        const int w = row.getWidth() / juce::jmax (1, (int) list.size());
        for (auto* k : list)
            k->setBounds (row.removeFromLeft (w).reduced (2, 0));
    }

    LeadEngine::Settings Panel::settings() const
    {
        return LeadParameters::toSettings ([this] (int id) { return model.value (id); });
    }

    void Panel::drawPlot (juce::Graphics& g, juce::Rectangle<float> r)
    {
        g.setColour (EffectUI::plotBackground);
        g.fillRoundedRectangle (r, 3.0f);
    }

    void Panel::drawOff (juce::Graphics& g, juce::Rectangle<float> r)
    {
        g.setColour (EffectUI::plotBackground.withAlpha (0.65f));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (Palette::textDim);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText ("OFF", r, juce::Justification::centred);
    }

    //==============================================================================
    PitchPanel::PitchPanel (EffectModel& m, LeadEngine& e)
        : Panel (m, e, "PITCH"),
          modeButtons (m, P::voiceMode, { "Mono", "Legato", "Poly" }, Colours::pitch, "Voice mode"),
          glideModeButtons (m, P::glideMode, { "Glide always", "Glide legato" }, Colours::pitch, "Glide mode")
    {
        modeButtons.setTooltip ("Mono: one voice, each key restarts its envelopes. Legato: one voice, keys played over held ones "
                                "move it without a new attack. Poly: a voice per key");
        glideModeButtons.setTooltip ("Glide always, or only from a key still held (legato playing)");
        addAndMakeVisible (modeButtons);
        addAndMakeVisible (glideModeButtons);

        addKnob (P::glide, "Glide", Colours::pitch, 0.3f);
        addKnob (P::vibratoRate, "Vib Rate", Colours::lfo1, 5.0f);
        addKnob (P::vibratoDepth, "Vib Depth", Colours::lfo1);
        addKnob (P::vibratoDelay, "Vib Delay", Colours::lfo1, 0.6f);
        addKnob (P::vibratoFade, "Vib Fade", Colours::lfo1, 0.6f);

        Frame empty {};
        for (int v = 0; v < LeadEngine::maxVoices; ++v)
        {
            empty.pitch[v] = -1.0f;
            empty.note[v] = -1;
        }
        history.assign ((size_t) length, empty);
    }

    void PitchPanel::resized()
    {
        auto top = titleRow();
        top.removeFromLeft (52);
        modeButtons.setBounds (top.removeFromRight (juce::jmin (180, top.getWidth() / 2)));
        top.removeFromRight (8);
        glideModeButtons.setBounds (top.removeFromRight (juce::jmin (190, top.getWidth())));

        auto r = content();
        layoutRow (r.removeFromBottom (knobHeight), { knobs[0], knobs[1], knobs[2], knobs[3], knobs[4] });
        display = r.withTrimmedBottom (4);
    }

    void PitchPanel::refresh()
    {
        Panel::refresh();
        modeButtons.refresh();
        glideModeButtons.refresh();

        auto& frame = history[(size_t) write];
        for (int v = 0; v < LeadEngine::maxVoices; ++v)
        {
            const auto& view = engine.voiceViews[(size_t) v];
            frame.note[v] = view.note.load();
            frame.pitch[v] = frame.note[v] >= 0 ? view.pitch.load() : -1.0f;
            frame.amp[v] = view.amp.load();
        }
        write = (write + 1) % length;

        // The range eases towards what has played lately and the keys held, at least an octave and a bit.
        float lo = 1000.0f, hi = -1000.0f;
        for (auto& f : history)
            for (int v = 0; v < LeadEngine::maxVoices; ++v)
                if (f.pitch[v] >= 0.0f)
                {
                    lo = juce::jmin (lo, f.pitch[v]);
                    hi = juce::jmax (hi, f.pitch[v]);
                }
        for (int i = 0; i < engine.numHeldKeys.load(); ++i)
        {
            lo = juce::jmin (lo, (float) engine.heldKeys[(size_t) i].load());
            hi = juce::jmax (hi, (float) engine.heldKeys[(size_t) i].load());
        }

        if (hi >= lo)
        {
            const float centre = 0.5f * (lo + hi), span = juce::jmax (15.0f, hi - lo + 6.0f);
            low += (centre - span * 0.5f - low) * 0.08f;
            high += (centre + span * 0.5f - high) * 0.08f;
        }

        repaint (display);
    }

    void PitchPanel::paint (juce::Graphics& g)
    {
        Panel::paint (g);
        auto area = display.toFloat();
        drawPlot (g, area);

        auto r = area.reduced (6.0f, 6.0f);
        auto keys = r.removeFromLeft (30.0f);
        r.removeFromLeft (4.0f);
        auto yFor = [&] (float pitch) { return juce::jmap (pitch, low, high, r.getBottom(), r.getY()); };
        const float semitone = r.getHeight() / juce::jmax (1.0f, high - low);

        auto isBlack = [] (int key) { const int n = ((key % 12) + 12) % 12; return n == 1 || n == 3 || n == 6 || n == 8 || n == 10; };

        // Which keys are held now.
        std::array<bool, 128> held {};
        for (int i = 0; i < engine.numHeldKeys.load(); ++i)
            held[(size_t) juce::jlimit (0, 127, engine.heldKeys[(size_t) i].load())] = true;

        int target = -1;
        float loudest = -1.0f;
        for (auto& v : engine.voiceViews)
            if (v.note.load() >= 0)
            {
                if (v.held.load())
                    held[(size_t) juce::jlimit (0, 127, v.note.load())] = true;
                if (v.amp.load() > loudest)
                {
                    loudest = v.amp.load();
                    target = v.note.load();
                }
            }

        // The keyboard on its side, and a lane per semitone across the plot.
        g.saveState();
        g.reduceClipRegion (area.toNearestInt());
        for (int k = (int) std::floor (low) - 1; k <= (int) std::ceil (high) + 1; ++k)
        {
            if (k < 0 || k > 127)
                continue;

            const float y = yFor ((float) k);
            auto key = juce::Rectangle<float> (keys.getX(), y - semitone * 0.5f, keys.getWidth(), semitone);
            g.setColour (held[(size_t) k] ? Colours::pitch : (isBlack (k) ? Palette::windowBackground : Palette::text.withAlpha (0.8f)));
            g.fillRect (isBlack (k) ? key.withWidth (keys.getWidth() * 0.62f).reduced (0.0f, 0.5f) : key.reduced (0.0f, 0.5f));

            if (held[(size_t) k])
            {
                g.setColour (Colours::pitch.withAlpha (0.10f));
                g.fillRect (juce::Rectangle<float> (r.getX(), y - semitone * 0.5f, r.getWidth(), semitone));
            }

            g.setColour (Palette::panelOutline.withAlpha (k % 12 == 0 ? 0.7f : 0.25f));
            g.drawHorizontalLine ((int) y, r.getX(), r.getRight());

            if (k % 12 == 0)
            {
                g.setColour (Palette::textDisabled);
                g.setFont (juce::FontOptions (9.0f));
                g.drawText (noteName (k), juce::Rectangle<float> (r.getX() + 3.0f, y - 11.0f, 30.0f, 10.0f), juce::Justification::centredLeft);
            }
        }

        // The note the loudest voice is heading for.
        if (target >= 0)
        {
            juce::Path line;
            line.startNewSubPath (r.getX(), yFor ((float) target));
            line.lineTo (r.getRight(), yFor ((float) target));
            const float dashes[] { 5.0f, 4.0f };
            juce::PathStrokeType (1.0f).createDashedStroke (line, line, dashes, 2);
            g.setColour (Colours::pitch.withAlpha (0.55f));
            g.fillPath (line);
        }

        // Each voice's pitch over the last four seconds, now at the right.
        for (int v = 0; v < LeadEngine::maxVoices; ++v)
        {
            juce::Point<float> previous;
            bool drawing = false;
            int previousNote = -1;

            for (int i = 0; i < length; ++i)
            {
                const auto& f = history[(size_t) ((write + i) % length)];
                const float x = r.getX() + r.getWidth() * (float) i / (float) (length - 1);
                if (f.pitch[v] < 0.0f)
                {
                    drawing = false;
                    continue;
                }

                const juce::Point<float> at { x, yFor (f.pitch[v]) };
                if (drawing)
                {
                    const float strength = 0.2f + 0.8f * juce::jlimit (0.0f, 1.0f, f.amp[v]);
                    g.setColour (Colours::pitch.withAlpha (0.18f * strength));
                    g.drawLine ({ previous, at }, 5.0f);
                    g.setColour (Colours::pitch.withAlpha (strength));
                    g.drawLine ({ previous, at }, 1.8f);
                }
                else if (previousNote >= 0)
                {
                    // A new note on the same voice: a small tick where it starts.
                    g.setColour (Colours::pitch.withAlpha (0.5f));
                    g.fillEllipse (juce::Rectangle<float> (3.0f, 3.0f).withCentre (at));
                }

                previous = at;
                previousNote = f.note[v];
                drawing = true;
            }

            const auto& now = history[(size_t) ((write + length - 1) % length)];
            if (now.pitch[v] >= 0.0f)
            {
                const juce::Point<float> at { r.getRight(), yFor (now.pitch[v]) };
                EffectUI::drawGlow (g, at, 14.0f, Colours::pitch, 0.4f + 0.6f * now.amp[v]);
                g.setColour (Palette::text);
                g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (at));
            }
        }
        g.restoreState();

        // What's playing, in words.
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        const float pitch = engine.currentPitch.load();
        juce::String text;
        if (pitch >= 0.0f)
        {
            const int nearest = juce::roundToInt (pitch);
            const int cents = juce::roundToInt ((pitch - (float) nearest) * 100.0f);
            text = noteName (nearest) + (cents != 0 ? juce::String ("  ") + (cents > 0 ? "+" : "") + juce::String (cents) + " ct" : juce::String());
            const float vibrato = std::abs (engine.vibratoAmount.load());
            if (vibrato > 0.02f)
                text << "   vibrato " << juce::String (juce::roundToInt (vibrato * 100.0f)) << " ct";
        }
        else
        {
            text = "-";
        }
        g.setColour (Palette::text);
        g.drawText (text, r.reduced (6.0f, 2.0f).removeFromTop (14.0f), juce::Justification::centredRight);

        g.setFont (juce::FontOptions (9.5f));
        g.setColour (Palette::textDisabled);
        g.drawText ("-4 s", r.removeFromBottom (11.0f).translated (4.0f, 0.0f), juce::Justification::centredLeft);
    }

    //==============================================================================
    ScopePanel::ScopePanel (EffectModel& m, LeadEngine& e) : Panel (m, e, "SCOPE") {}

    void ScopePanel::refresh()
    {
        Panel::refresh();
        const float pitch = engine.currentPitch.load();
        if (pitch < 0.0f)
        {
            repaint();
            return;
        }

        // The period to lock to: A's (B follows it when synced), or B's when A is silent.
        const auto s = settings();
        const auto& a = s.osc[0];
        const auto& b = s.osc[1];
        const bool useA = s.sync || (a.on && a.level > 0.01f) || ! b.on;
        const auto& o = useA ? a : b;
        hz = (float) noteHz (pitch + o.octave * 12 + o.semitones + o.fine * 0.01);

        const double rate = engine.getSampleRate();
        const double period = rate / juce::jmax (20.0f, hz);
        const double span = juce::jmin (2.0 * period, (double) LeadEngine::scopeLength / 2.5);
        const int end = engine.scopeWrite.load();
        auto at = [this, end] (double back)
        {
            // `back` samples before the newest, interpolated.
            const double position = (double) end - back;
            const int i = (int) std::floor (position);
            const float t = (float) (position - i);
            const float x0 = engine.scope[(size_t) (((i % LeadEngine::scopeLength) + LeadEngine::scopeLength) % LeadEngine::scopeLength)].load();
            const float x1 = engine.scope[(size_t) ((((i + 1) % LeadEngine::scopeLength) + LeadEngine::scopeLength) % LeadEngine::scopeLength)].load();
            return x0 + (x1 - x0) * t;
        };

        // A rising zero crossing about one period before the shown stretch, the steepest one
        // in a period's search, so the picture holds still.
        const double searchFrom = span + 2.0 * period, searchTo = span + period;
        double trigger = span + period;
        float steepest = 0.0f;
        for (double back = searchFrom; back > searchTo; back -= 1.0)
        {
            const float x0 = at (back), x1 = at (back - 1.0);
            if (x0 < 0.0f && x1 >= 0.0f && x1 - x0 > steepest)
            {
                steepest = x1 - x0;
                trigger = back - (-x0 / juce::jmax (1.0e-9f, x1 - x0));
            }
        }

        newest = (newest + 1) % ghosts;
        filled = juce::jmin (ghosts, filled + 1);
        float peak = 0.0f;
        auto& frame = frames[(size_t) newest];
        for (int i = 0; i < points; ++i)
        {
            frame[(size_t) i] = at (trigger - span * (double) i / (points - 1));
            peak = juce::jmax (peak, std::abs (frame[(size_t) i]));
        }

        const float target = peak > 1.0e-4f ? 0.85f / peak : scale;
        scale = target < scale ? target : scale + (target - scale) * 0.05f;
        repaint();
    }

    void ScopePanel::paint (juce::Graphics& g)
    {
        Panel::paint (g);
        auto area = content().toFloat();
        drawPlot (g, area);
        auto r = area.reduced (8.0f, 10.0f);

        g.setColour (Palette::panelOutline.withAlpha (0.7f));
        g.drawHorizontalLine ((int) r.getCentreY(), r.getX(), r.getRight());
        g.setColour (Palette::panelOutline.withAlpha (0.35f));
        g.drawVerticalLine ((int) r.getCentreX(), r.getY(), r.getBottom());

        const bool playing = engine.currentPitch.load() >= 0.0f;
        const auto colour = Colours::oscA.interpolatedWith (Palette::text, 0.3f);

        for (int k = filled - 1; k >= 0; --k)
        {
            const auto& frame = frames[(size_t) ((newest - k + ghosts) % ghosts)];
            juce::Path p;
            for (int i = 0; i < points; ++i)
            {
                const float x = r.getX() + r.getWidth() * (float) i / (points - 1);
                const float y = r.getCentreY() - juce::jlimit (-1.1f, 1.1f, frame[(size_t) i] * scale) * r.getHeight() * 0.5f;
                if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }

            const float fade = (playing ? 1.0f : 0.35f) * (k == 0 ? 1.0f : 0.35f / (float) k);
            if (k == 0)
            {
                g.setColour (colour.withAlpha (0.2f * fade));
                g.strokePath (p, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            g.setColour (colour.withAlpha (fade));
            g.strokePath (p, juce::PathStrokeType (k == 0 ? 1.6f : 1.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        g.setFont (juce::FontOptions (9.5f));
        g.setColour (Palette::textDim);
        g.drawText (playing ? "2 cycles at " + (hz < 100.0f ? juce::String (hz, 1) : juce::String (juce::roundToInt (hz))) + " Hz" : juce::String ("waiting for a note"),
                    area.reduced (6.0f, 3.0f).removeFromTop (11.0f), juce::Justification::centredRight);
    }

    //==============================================================================
    OscillatorPanel::OscillatorPanel (EffectModel& m, LeadEngine& e, int i)
        : Panel (m, e, i == 0 ? "OSC A" : "OSC B"), index (i),
          onButton (m, oscillator (i, aOn), "On", Colours::forOscillator (i), "Oscillator on / off"),
          waveButtons (m, oscillator (i, aWave), getWaveNames(), Colours::forOscillator (i), "Wave")
    {
        addAndMakeVisible (onButton);
        waveButtons.drawIcon = drawWaveIcon;
        waveButtons.setTooltip ("Wave: saw, pulse (with its width), triangle, sine");
        addAndMakeVisible (waveButtons);

        if (index == 1)
        {
            syncButton = std::make_unique<ParamToggle> (m, P::sync, "Sync", Colours::oscB, "Hard sync: B restarts with every cycle of A. Tune B up for the classic sync sweep");
            addAndMakeVisible (*syncButton);
        }

        const auto c = Colours::forOscillator (i);
        addKnob (oscillator (i, aLevel), "Level", c);
        addKnob (oscillator (i, aPulseWidth), "Width", c);
        addKnob (oscillator (i, aPan), "Pan", c);
        if (index == 1)
        {
            addKnob (P::fm, "FM", c);
            addKnob (P::ring, "Ring", c);
        }
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
        top.removeFromLeft (50);
        onButton.setBounds (top.removeFromRight (44));
        top.removeFromRight (6);
        if (syncButton != nullptr)
        {
            syncButton->setBounds (top.removeFromRight (50));
            top.removeFromRight (6);
        }
        waveButtons.setBounds (top.removeFromRight (juce::jmin (150, top.getWidth())));

        auto r = content();
        auto second = r.removeFromBottom (knobHeight);
        auto first = r.removeFromBottom (knobHeight);
        const int shift = index == 1 ? 2 : 0;
        if (index == 1)
            layoutRow (first, { knobs[0], knobs[1], knobs[2], knobs[3], knobs[4] });
        else
            layoutRow (first.withSizeKeepingCentre (first.getWidth() * 3 / 5, first.getHeight()), { knobs[0], knobs[1], knobs[2] });
        layoutRow (second, { knobs[3 + shift], knobs[4 + shift], knobs[5 + shift], knobs[6 + shift], knobs[7 + shift], knobs[8 + shift] });
        display = r.withTrimmedBottom (4);
    }

    void OscillatorPanel::refresh()
    {
        Panel::refresh();
        onButton.refresh();
        waveButtons.refresh();
        if (syncButton != nullptr)
            syncButton->refresh();
        repaint (display);
    }

    LeadEngine::Settings OscillatorPanel::playing() const
    {
        auto s = settings();
        if (engine.activeVoices.load() == 0)
            return s;

        auto now = [this] (Destination d) { return engine.destinationValue[(size_t) d].load(); };
        for (auto& osc : s.osc)
            osc.pulseWidth = juce::jlimit (0.05f, 0.95f, osc.pulseWidth + now (Destination::pulseWidth) * 0.45f);
        s.osc[1].fine += now (Destination::syncPitch) * LeadEngine::syncModSemitones * 100.0f;
        s.fm = juce::jlimit (0.0f, 1.0f, s.fm + now (Destination::fm));
        s.ring = juce::jlimit (0.0f, 1.0f, s.ring + now (Destination::ring));
        return s;
    }

    void OscillatorPanel::paint (juce::Graphics& g)
    {
        Panel::paint (g);
        const auto s = playing();
        auto r = display.toFloat();
        drawPlot (g, r);

        auto wave = r.removeFromLeft (r.getWidth() * 0.62f);
        r.removeFromLeft (4.0f);
        drawWave (g, wave, s);
        drawHarmonics (g, r, s);

        if (! s.osc[index].on)
            drawOff (g, display.toFloat());
    }

    void OscillatorPanel::drawWave (juce::Graphics& g, juce::Rectangle<float> area, const LeadEngine::Settings& s)
    {
        const auto colour = Colours::forOscillator (index);
        const auto& osc = s.osc[index];
        auto r = area.reduced (10.0f, 12.0f).withTrimmedBottom (8.0f);
        constexpr int points = 200;

        g.setColour (Palette::panelOutline.withAlpha (0.6f));
        g.drawHorizontalLine ((int) r.getCentreY(), r.getX(), r.getRight());

        // B over one cycle of A shows what sync, FM and ring do to it; A (or B alone) over two of its own.
        const bool overA = index == 1 && (s.sync || s.fm > 0.0f || s.ring > 0.0f);
        const double cycles = overA ? 1.0 : 2.0;
        juce::Path p;
        for (int i = 0; i <= points; ++i)
        {
            const double phase = cycles * i / points;
            float v;
            if (overA)
            {
                v = LeadEngine::waveAt (s, 1, phase);
            }
            else
            {
                auto single = s;
                single.osc[0] = osc;
                v = LeadEngine::waveAt (single, 0, phase);
            }

            const float x = r.getX() + r.getWidth() * (float) i / points, y = r.getCentreY() - v * r.getHeight() * 0.45f;
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }

        // Unison: the copies, faintly, a little apart.
        const int unison = juce::jlimit (1, LeadEngine::maxUnison, osc.unison);
        if (unison > 1)
            for (int u = 1; u < juce::jmin (unison, 4); ++u)
            {
                g.setColour (colour.withAlpha (0.12f));
                g.strokePath (p, juce::PathStrokeType (1.0f), juce::AffineTransform::translation ((float) u * 3.0f * osc.detune * 4.0f, 0.0f));
            }

        g.setColour (colour.withAlpha (0.22f));
        g.strokePath (p, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (colour.brighter (0.3f));
        g.strokePath (p, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        juce::String text = getWaveNames()[(int) osc.wave];
        if (osc.wave == LeadEngine::Wave::pulse)
            text << " " << juce::String (juce::roundToInt (osc.pulseWidth * 100.0f)) << " %";
        if (unison > 1)
            text << "  -  " << juce::String (unison) << " voices " << juce::String::fromUTF8 ("\xc2\xb1")
                 << juce::String (osc.detune * LeadEngine::maxDetuneCents, 1) << " ct";
        if (index == 1 && overA)
            text << (s.sync ? "  -  synced to A" : "  -  over a cycle of A");

        g.setFont (juce::FontOptions (10.0f));
        g.setColour (Palette::textDim);
        g.drawText (text, area.reduced (8.0f, 4.0f).removeFromBottom (12.0f), juce::Justification::centredLeft);
    }

    void OscillatorPanel::drawHarmonics (juce::Graphics& g, juce::Rectangle<float> area, const LeadEngine::Settings& s)
    {
        const auto colour = Colours::forOscillator (index);
        const bool overA = index == 1 && (s.sync || s.fm > 0.0f || s.ring > 0.0f);

        // One cycle, then its first 32 harmonics.
        constexpr int n = 256, shown = 32;
        std::array<float, n> cycle {};
        auto single = s;
        single.osc[0] = s.osc[index];
        for (int i = 0; i < n; ++i)
            cycle[(size_t) i] = overA ? LeadEngine::waveAt (s, 1, (double) i / n) : LeadEngine::waveAt (single, 0, (double) i / n);

        std::array<float, shown> amplitude {};
        float largest = 1.0e-6f;
        for (int h = 1; h <= shown; ++h)
        {
            std::complex<double> sum;
            for (int i = 0; i < n; ++i)
                sum += (double) cycle[(size_t) i] * std::polar (1.0, -juce::MathConstants<double>::twoPi * h * i / n);
            amplitude[(size_t) h - 1] = (float) std::abs (sum);
            largest = juce::jmax (largest, amplitude[(size_t) h - 1]);
        }

        auto r = area.reduced (8.0f, 8.0f);
        g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
        g.setColour (Palette::textDim);
        g.drawText (overA ? "HARMONICS OF A" : "HARMONICS", r.removeFromTop (12.0f), juce::Justification::centredLeft);
        r.removeFromTop (4.0f);

        const float w = r.getWidth() / shown;
        for (int h = 0; h < shown; ++h)
        {
            const float db = juce::Decibels::gainToDecibels (amplitude[(size_t) h] / largest, -60.0f);
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
    DrivePanel::DrivePanel (EffectModel& m, LeadEngine& e)
        : Panel (m, e, "DRIVE"),
          typeButtons (m, P::driveType, getDriveNames(), Colours::drive, "Drive type"),
          subShape (m, P::subShape, { "Sine", "Square" }, Colours::oscA, "Sub shape")
    {
        typeButtons.setTooltip ("Soft: smooth clipping. Tube: uneven halves, adding even harmonics. Fold: the wave folds back on itself");
        addAndMakeVisible (typeButtons);
        addAndMakeVisible (subShape);
        addKnob (P::drive, "Drive", Colours::drive);
        addKnob (P::subLevel, "Sub", Colours::oscA);
        addKnob (P::subOctave, "Sub Oct", Colours::oscA);
        addKnob (P::noiseLevel, "Noise", Palette::textDim);
    }

    void DrivePanel::resized()
    {
        auto top = titleRow();
        top.removeFromLeft (46);
        typeButtons.setBounds (top.removeFromRight (juce::jmin (156, top.getWidth())));

        auto r = content();
        layoutRow (r.removeFromBottom (knobHeight), { knobs[0], knobs[1], knobs[2], knobs[3] });
        auto strip = r.removeFromBottom (22);
        subShape.setBounds (strip.withSizeKeepingCentre (juce::jmin (strip.getWidth(), 120), 20));
        display = r.withTrimmedBottom (4);
    }

    void DrivePanel::refresh()
    {
        Panel::refresh();
        typeButtons.refresh();
        subShape.refresh();
        repaint (display);
    }

    void DrivePanel::paint (juce::Graphics& g)
    {
        Panel::paint (g);
        const auto s = settings();
        float drive = s.drive;
        if (engine.activeVoices.load() > 0)
            drive = juce::jlimit (0.0f, 1.0f, drive + engine.destinationValue[(size_t) Destination::drive].load());

        auto area = display.toFloat();
        drawPlot (g, area);
        auto r = area.reduced (8.0f, 8.0f).withTrimmedTop (12.0f);

        // Left: the curve (input across, output up). Right: a sine through it.
        const float side = juce::jmin (r.getHeight(), r.getWidth() * 0.48f);
        auto curve = r.removeFromLeft (side).withSizeKeepingCentre (side, side);
        r.removeFromLeft (8.0f);

        g.setColour (Palette::panelOutline.withAlpha (0.6f));
        g.drawLine (curve.getX(), curve.getCentreY(), curve.getRight(), curve.getCentreY(), 1.0f);
        g.drawLine (curve.getCentreX(), curve.getY(), curve.getCentreX(), curve.getBottom(), 1.0f);
        g.drawLine (curve.getX(), curve.getBottom(), curve.getRight(), curve.getY(), 1.0f);

        juce::Path shape, through;
        for (int i = 0; i <= 64; ++i)
        {
            const float x = -1.0f + 2.0f * (float) i / 64.0f;
            const float y = LeadEngine::driveCurve (s.driveType, drive, x);
            const juce::Point<float> at { curve.getX() + curve.getWidth() * (float) i / 64.0f, curve.getCentreY() - juce::jlimit (-1.1f, 1.1f, y) * curve.getHeight() * 0.5f };
            if (i == 0) shape.startNewSubPath (at); else shape.lineTo (at);
        }
        g.setColour (Colours::drive.withAlpha (0.25f));
        g.strokePath (shape, juce::PathStrokeType (5.0f));
        g.setColour (Colours::drive);
        g.strokePath (shape, juce::PathStrokeType (1.8f));

        juce::Path sine;
        for (int i = 0; i <= 96; ++i)
        {
            const float x = 0.8f * std::sin (juce::MathConstants<float>::twoPi * (float) i / 96.0f);
            const float px = r.getX() + r.getWidth() * (float) i / 96.0f;
            const float y = LeadEngine::driveCurve (s.driveType, drive, x);
            if (i == 0) { sine.startNewSubPath (px, r.getCentreY() - x * r.getHeight() * 0.45f); through.startNewSubPath (px, r.getCentreY() - y * r.getHeight() * 0.45f); }
            else        { sine.lineTo (px, r.getCentreY() - x * r.getHeight() * 0.45f); through.lineTo (px, r.getCentreY() - y * r.getHeight() * 0.45f); }
        }
        g.setColour (Palette::textDim.withAlpha (0.5f));
        g.strokePath (sine, juce::PathStrokeType (1.0f));
        g.setColour (Colours::drive.brighter (0.2f));
        g.strokePath (through, juce::PathStrokeType (1.6f));

        g.setFont (juce::FontOptions (9.5f));
        g.setColour (Palette::textDim);
        g.drawText (getDriveNames()[(int) s.driveType] + "  " + (drive > 0.0001f ? "+" + juce::String (juce::roundToInt (30.0f * drive)) + " dB" : juce::String ("clean")),
                    area.reduced (8.0f, 3.0f).removeFromTop (11.0f), juce::Justification::centredRight);
    }

    //==============================================================================
    FilterPanel::FilterPanel (EffectModel& m, LeadEngine& e)
        : Panel (m, e, "FILTER"),
          typeButtons (m, P::filterType, { "Ladder", "LP", "BP", "HP" }, Colours::filter, "Filter type")
    {
        typeButtons.setTooltip ("Ladder: 24 dB low pass that screams and self-oscillates at the top. LP / BP / HP: 12 dB low, band and high pass");
        addAndMakeVisible (typeButtons);

        addKnob (P::cutoff, "Cutoff", Colours::filter, 1000.0f);
        addKnob (P::resonance, "Resonance", Colours::filter);
        addKnob (P::keyTrack, "Key Track", Palette::textDim);
        addKnob (P::filterEnv, "Env", Colours::modEnv);
        addKnob (P::filterVelocity, "Velocity", Palette::textDim);
    }

    void FilterPanel::resized()
    {
        auto top = titleRow();
        top.removeFromLeft (50);
        typeButtons.setBounds (top.removeFromRight (juce::jmin (200, top.getWidth())));

        auto r = content();
        layoutRow (r.removeFromBottom (knobHeight), { knobs[0], knobs[1], knobs[2], knobs[3], knobs[4] });
        display = r.withTrimmedBottom (4);
    }

    void FilterPanel::refresh()
    {
        Panel::refresh();
        typeButtons.refresh();
        repaint (display);
    }

    void FilterPanel::paint (juce::Graphics& g)
    {
        Panel::paint (g);
        auto s = settings();
        const double rate = engine.getSampleRate();

        auto area = display.toFloat();
        drawPlot (g, area);
        auto p = area.reduced (6.0f, 6.0f).withTrimmedBottom (12.0f);
        EffectUI::drawFrequencyGrid (g, p, axis, true);

        constexpr float minDb = -36.0f, maxDb = 24.0f;
        auto yFor = [&] (double db) { return juce::jmap (juce::jlimit (minDb - 6.0f, maxDb + 6.0f, (float) db), minDb, maxDb, p.getBottom(), p.getY()); };
        g.setColour (Palette::panelOutline.withAlpha (0.7f));
        g.drawHorizontalLine ((int) yFor (0.0), p.getX(), p.getRight());

        // With the resonance as the newest voice has it.
        if (engine.activeVoices.load() > 0)
            s.resonance = juce::jlimit (0.0f, 1.0f, s.resonance + engine.destinationValue[(size_t) Destination::resonance].load());

        auto curve = [&] (double cutoffHz)
        {
            juce::Path path;
            for (float x = p.getX(); x <= p.getRight(); x += 2.0f)
            {
                const auto y = yFor (LeadEngine::getFilterResponseDb (s, cutoffHz, axis.toHz (x, p), rate));
                if (x == p.getX()) path.startNewSubPath (x, y); else path.lineTo (x, y);
            }
            return path;
        };

        g.saveState();
        g.reduceClipRegion (p.toNearestInt());

        const LeadEngine::VoiceView* loudest = nullptr;
        for (auto& v : engine.voiceViews)
            if (v.note.load() >= 0 && (loudest == nullptr || v.amp.load() > loudest->amp.load()))
                loudest = &v;

        if (loudest != nullptr)
        {
            auto fill = curve (loudest->cutoff.load());
            fill.lineTo (p.getRight(), p.getBottom() + 10.0f);
            fill.lineTo (p.getX(), p.getBottom() + 10.0f);
            fill.closeSubPath();
            const auto c = Colours::forCutoff (loudest->cutoff.load());
            g.setGradientFill (juce::ColourGradient::vertical (c.withAlpha (0.25f), p.getY(), c.withAlpha (0.02f), p.getBottom()));
            g.fillPath (fill);
        }

        for (auto& v : engine.voiceViews)
            if (v.note.load() >= 0)
            {
                const auto hz = v.cutoff.load();
                g.setColour (Colours::forCutoff (hz).withAlpha (0.25f + 0.65f * v.amp.load()));
                g.strokePath (curve (hz), juce::PathStrokeType (1.4f));
                g.fillRect (axis.toX (hz, p) - 1.0f, p.getBottom() - 6.0f, 2.0f, 6.0f);
            }

        auto set = curve (s.cutoff);
        const float dashes[] { 4.0f, 3.0f };
        juce::PathStrokeType (1.2f).createDashedStroke (set, set, dashes, 2);
        g.setColour (Palette::text.withAlpha (0.6f));
        g.fillPath (set);
        g.restoreState();

        g.setFont (juce::FontOptions (10.0f));
        g.setColour (Palette::textDim);
        g.drawText (model.parameter (P::cutoff)->getCurrentText() + "  -  " + getFilterTypeNames()[(int) s.filterType]
                        + (s.filterType == LeadEngine::FilterType::ladder && s.resonance > 0.96f ? "  -  self-oscillating" : ""),
                    p.reduced (6.0f, 2.0f).removeFromTop (12.0f), juce::Justification::centredRight);
    }

    //==============================================================================
    EnvelopePanel::EnvelopePanel (EffectModel& m, LeadEngine& e, bool amp)
        : Panel (m, e, amp ? "AMP ENVELOPE" : "FILTER ENVELOPE"), isAmp (amp), colour (amp ? Colours::ampEnv : Colours::modEnv)
    {
        addKnob (first() + 0, "Attack", colour, 0.5f);
        addKnob (first() + 1, "Decay", colour, 0.8f);
        addKnob (first() + 2, "Sustain", colour);
        addKnob (first() + 3, "Release", colour, 0.8f);
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
        Panel::refresh();
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
        Panel::paint (g);
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

        g.setFont (juce::FontOptions (9.0f));
        g.setColour (Palette::textDisabled);
        g.drawText ("A", juce::Rectangle<float> (l.plot.getX(), l.plot.getBottom() - 11.0f, l.attackEnd - l.plot.getX(), 11.0f), juce::Justification::centred);
        g.drawText ("D", juce::Rectangle<float> (l.attackEnd, l.plot.getBottom() - 11.0f, l.decayEnd - l.attackEnd, 11.0f), juce::Justification::centred);
        g.drawText ("S", juce::Rectangle<float> (l.decayEnd, l.plot.getBottom() - 11.0f, l.sustainEnd - l.decayEnd, 11.0f), juce::Justification::centred);
        g.drawText ("R", juce::Rectangle<float> (l.sustainEnd, l.plot.getBottom() - 11.0f, l.releaseEnd - l.sustainEnd, 11.0f), juce::Justification::centred);

        // Each sounding voice where it is on the shape (a legato voice stays where it was).
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

        model.beginUndoTransaction (isAmp ? "Amp envelope" : "Filter envelope");
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
            auto& spec = LeadParameters::get (first() + part);
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
}
