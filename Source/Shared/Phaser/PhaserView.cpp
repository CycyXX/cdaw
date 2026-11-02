#include "PhaserView.h"
#include "../../UI/LookAndFeel/Palette.h"

namespace cdaw
{
    namespace
    {
        using P = PhaserParameters::ID;
        constexpr double twoPi = juce::MathConstants<double>::twoPi;
        constexpr float minDb = -36.0f, maxDb = 18.0f;

        const juce::Colour leftColour { 0xff4fd6c8 }, rightColour { 0xffd77cff }, sweepColour { 0xfff2b134 };

        PhaserEngine::Settings settingsOf (EffectModel& m)
        {
            return PhaserParameters::toSettings ([&m] (int id) { return m.value (id); });
        }

        /** Response (dB) to colour: notches deep blue, unity teal, peaks amber to white. */
        juce::Colour heat (float db)
        {
            static const juce::Colour stops[] { juce::Colour (0xff0b0d1a), juce::Colour (0xff1e2a5a), juce::Colour (0xff27609a),
                                                juce::Colour (0xff2fa7b8), juce::Colour (0xfff2b134), juce::Colour (0xfffff3d6) };
            constexpr int numStops = (int) std::size (stops);
            const float t = juce::jlimit (0.0f, 1.0f, db < 0.0f ? 0.62f * (db + 30.0f) / 30.0f : 0.62f + 0.38f * db / 15.0f);
            auto pos = t * (float) (numStops - 1);
            auto i = juce::jmin ((int) pos, numStops - 2);
            return stops[i].interpolatedWith (stops[i + 1], pos - (float) i);
        }
    }

    //==============================================================================
    class PhaserView::ResponseView : public juce::Component
    {
    public:
        ResponseView (EffectModel& m, PhaserEngine& e) : model (m), engine (e) { setOpaque (true); }

        juce::Rectangle<float> plotArea() const
        {
            return getLocalBounds().toFloat().withTrimmedLeft (36.0f).withTrimmedRight (12.0f).withTrimmedTop (28.0f).withTrimmedBottom (20.0f);
        }

        float dbToY (double db) const
        {
            auto p = plotArea();
            return juce::jmap (juce::jlimit (minDb, maxDb, (float) db), minDb, maxDb, p.getBottom(), p.getY());
        }

        float railY() const                 { return plotArea().getY() + 12.0f; }

        struct Handles { juce::Point<float> centre, low, high; };

        Handles getHandles (const PhaserEngine::Settings& s) const
        {
            auto p = plotArea();
            const double rate = engine.getSampleRate();
            return { { axis.toX (s.centreHz, p), railY() },
                     { axis.toX (PhaserEngine::getSweepHz (s, 0.75, rate), p), railY() },
                     { axis.toX (PhaserEngine::getSweepHz (s, 0.25, rate), p), railY() } };
        }

        void paint (juce::Graphics& g) override
        {
            const auto s = settingsOf (model);
            const double rate = engine.getSampleRate();
            const double phase = engine.lfoPhase.load();

            g.fillAll (Palette::panelBackground);
            EffectUI::drawPanel (g, getLocalBounds().toFloat(), "RESPONSE");

            auto p = plotArea();
            g.setColour (EffectUI::plotBackground);
            g.fillRect (p);
            EffectUI::drawFrequencyGrid (g, p, axis, true);

            g.setFont (juce::FontOptions (9.0f));
            for (float db = maxDb - 6.0f; db >= minDb; db -= 12.0f)
            {
                g.setColour (Palette::panelOutline.withAlpha (std::abs (db) < 0.1f ? 0.8f : 0.3f));
                g.drawHorizontalLine ((int) dbToY (db), p.getX(), p.getRight());
                g.setColour (Palette::textDisabled);
                g.drawText ((db > 0 ? "+" : "") + juce::String ((int) db), juce::Rectangle<float> (p.getX() - 34.0f, dbToY (db) - 6.0f, 30.0f, 12.0f),
                            juce::Justification::centredRight);
            }

            // The range the sweep covers.
            const auto handles = getHandles (s);
            g.setGradientFill (juce::ColourGradient::vertical (sweepColour.withAlpha (0.10f), p.getY(), sweepColour.withAlpha (0.02f), p.getBottom()));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (handles.low.x, p.getY(), handles.high.x, p.getBottom()));
            g.setColour (sweepColour.withAlpha (0.35f));
            g.drawLine (handles.low.x, railY(), handles.high.x, railY(), 2.0f);

            g.saveState();
            g.reduceClipRegion (p.toNearestInt());

            // Right side dashed, left side solid; the stages' frequency marked on each.
            const double sweepHz[2] { PhaserEngine::getSweepHz (s, phase, rate), PhaserEngine::getSweepHz (s, phase + 0.5 * s.spread, rate) };
            std::vector<float> leftDb;

            for (int ch = 1; ch >= 0; --ch)
            {
                if (ch == 1 && s.spread < 0.01f)
                    continue;

                juce::Path curve;
                for (float x = p.getX(); x <= p.getRight(); x += 1.0f)
                {
                    const auto db = (float) PhaserEngine::getResponseDb (s, sweepHz[ch], axis.toHz (x, p), rate);
                    if (ch == 0)
                        leftDb.push_back (db);

                    const auto y = dbToY (db);
                    if (x == p.getX()) curve.startNewSubPath (x, y);
                    else               curve.lineTo (x, y);
                }

                const auto colour = ch == 0 ? leftColour : rightColour;
                g.setColour (colour.withAlpha (0.5f));
                g.drawVerticalLine ((int) axis.toX (sweepHz[ch], p), p.getY(), p.getBottom());

                if (ch == 1)
                {
                    const float dashes[] { 4.0f, 3.0f };
                    juce::PathStrokeType (1.3f).createDashedStroke (curve, curve, dashes, 2);
                    g.setColour (colour.withAlpha (0.7f));
                    g.fillPath (curve);
                }
                else
                {
                    juce::Path fill (curve);
                    fill.lineTo (p.getRight(), p.getBottom());
                    fill.lineTo (p.getX(), p.getBottom());
                    fill.closeSubPath();
                    g.setGradientFill (juce::ColourGradient::vertical (colour.withAlpha (0.28f), p.getY(), colour.withAlpha (0.02f), p.getBottom()));
                    g.fillPath (fill);
                    g.setColour (colour.withAlpha (0.2f));
                    g.strokePath (curve, juce::PathStrokeType (6.0f));
                    g.setColour (colour);
                    g.strokePath (curve, juce::PathStrokeType (2.0f));
                }
            }

            // The notches: the deepest dips of the left side.
            for (size_t i = 2; i + 2 < leftDb.size(); ++i)
                if (leftDb[i] < -9.0f && leftDb[i] <= leftDb[i - 1] && leftDb[i] < leftDb[i + 1] && leftDb[i] <= leftDb[i - 2])
                {
                    const auto x = p.getX() + (float) i;
                    juce::Path marker;
                    marker.addTriangle (x, p.getBottom() - 12.0f, x - 5.0f, p.getBottom() - 3.0f, x + 5.0f, p.getBottom() - 3.0f);
                    g.setColour (leftColour.withAlpha (0.8f));
                    g.fillPath (marker);
                }

            g.restoreState();

            EffectUI::drawHandle (g, handles.low, sweepColour, hover == 2 || dragging == 2);
            EffectUI::drawHandle (g, handles.high, sweepColour, hover == 3 || dragging == 3);
            EffectUI::drawHandle (g, handles.centre, Palette::accent, hover == 1 || dragging == 1, "C");

            drawLfoDial (g, phase, s);
        }

        /** The LFO's position on each side, going round. */
        void drawLfoDial (juce::Graphics& g, double phase, const PhaserEngine::Settings& s)
        {
            const auto centre = juce::Point<float> ((float) getWidth() - 30.0f, 15.0f);
            const float r = 8.0f;
            g.setColour (Palette::panelOutline);
            g.drawEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (centre), 1.0f);

            for (int ch = 1; ch >= 0; --ch)
            {
                const double angle = twoPi * (phase + (ch == 1 ? 0.5 * s.spread : 0.0));
                const juce::Point<float> dot { centre.x + r * (float) std::sin (angle), centre.y - r * (float) std::cos (angle) };
                g.setColour (ch == 0 ? leftColour : rightColour);
                g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (dot));
            }

            g.setFont (juce::FontOptions (10.0f));
            g.setColour (Palette::textDim);
            g.drawText ("LFO", juce::Rectangle<float> (centre.x - 48.0f, centre.y - 6.0f, 34.0f, 12.0f), juce::Justification::centredRight);
        }

        //==============================================================================
        int hitTest (juce::Point<float> pos) const
        {
            auto h = getHandles (settingsOf (model));
            if (pos.getDistanceFrom (h.centre) < 11.0f)  return 1;
            if (pos.getDistanceFrom (h.low) < 11.0f)     return 2;
            if (pos.getDistanceFrom (h.high) < 11.0f)    return 3;
            return 0;
        }

        void mouseMove (const juce::MouseEvent& e) override
        {
            auto h = hitTest (e.position);
            if (h != hover)
            {
                hover = h;
                setMouseCursor (h != 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
                repaint();
            }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            dragging = hitTest (e.position);
            if (dragging != 0)
            {
                model.beginUndoTransaction (dragging == 1 ? "Phaser centre" : "Phaser depth");
                model.parameter (dragging == 1 ? P::centre : P::depth)->beginGesture();
            }
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (dragging == 0)
                return;

            const auto hz = axis.toHz (e.position.x, plotArea());

            if (dragging == 1)
            {
                auto& spec = PhaserParameters::get (P::centre);
                model.parameter (P::centre)->setValue ((float) juce::jlimit ((double) spec.minimum, (double) spec.maximum, hz));
            }
            else
            {
                // An edge: the depth is its distance from the centre, in octaves.
                const auto octaves = std::abs (std::log2 (hz / juce::jmax (1.0f, model.value (P::centre))));
                model.parameter (P::depth)->setValue ((float) juce::jlimit (0.0, 1.0, octaves / PhaserEngine::maxOctaves));
            }

            repaint();
        }

        void mouseUp (const juce::MouseEvent&) override
        {
            if (dragging != 0)
                model.parameter (dragging == 1 ? P::centre : P::depth)->endGesture();
            dragging = 0;
        }

        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            if (auto h = hitTest (e.position); h != 0)
            {
                const auto id = h == 1 ? P::centre : P::depth;
                model.setValue (id, PhaserParameters::get (id).defaultValue, "Reset phaser");
            }
        }

        EffectModel& model;
        PhaserEngine& engine;
        EffectUI::FrequencyAxis axis;
        int hover = 0, dragging = 0;
    };

    //==============================================================================
    class PhaserView::SweepView : public juce::Component
    {
    public:
        SweepView (EffectModel& m, PhaserEngine& e) : model (m), engine (e) { setOpaque (true); }

        juce::Rectangle<int> plotArea() const
        {
            return getLocalBounds().withTrimmedLeft (36).withTrimmedRight (12).withTrimmedTop (26).withTrimmedBottom (8);
        }

        /** Scrolls down one row and adds the response as it is now at the top. */
        void push (float activity)
        {
            auto p = plotArea();
            if (p.isEmpty())
                return;

            if (image.getWidth() != p.getWidth() || image.getHeight() != p.getHeight())
            {
                image = juce::Image (juce::Image::RGB, p.getWidth(), p.getHeight(), true);
                columnHz.resize ((size_t) p.getWidth());
                for (int x = 0; x < p.getWidth(); ++x)
                    columnHz[(size_t) x] = axis.toHz ((float) x + 0.5f, juce::Rectangle<float> ((float) p.getWidth(), 1.0f));
            }

            image.moveImageSection (0, 1, 0, 0, image.getWidth(), image.getHeight() - 1);

            const auto s = settingsOf (model);
            const double rate = engine.getSampleRate();
            const double sweepHz = PhaserEngine::getSweepHz (s, engine.lfoPhase.load(), rate);
            const float brightness = 0.35f + 0.65f * activity;

            juce::Image::BitmapData row (image, 0, 0, image.getWidth(), 1, juce::Image::BitmapData::writeOnly);
            for (int x = 0; x < image.getWidth(); ++x)
            {
                const auto db = (float) PhaserEngine::getResponseDb (s, sweepHz, columnHz[(size_t) x], rate);
                row.setPixelColour (x, 0, heat (db).withMultipliedBrightness (brightness));
            }

            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (Palette::panelBackground);
            EffectUI::drawPanel (g, getLocalBounds().toFloat(), "SWEEP");

            auto p = plotArea();
            g.setColour (EffectUI::plotBackground);
            g.fillRect (p);

            if (image.isValid())
                g.drawImageAt (image, p.getX(), p.getY());

            const auto pf = p.toFloat();
            g.setColour (juce::Colours::black.withAlpha (0.25f));
            for (double hz : { 100.0, 1000.0, 10000.0 })
                g.drawVerticalLine ((int) axis.toX (hz, pf), pf.getY(), pf.getBottom());

            g.setFont (juce::FontOptions (9.5f));
            g.setColour (Palette::textDisabled);
            g.drawText ("now", juce::Rectangle<float> (pf.getX() - 34.0f, pf.getY(), 30.0f, 12.0f), juce::Justification::centredRight);
            g.drawText ("-" + juce::String (pf.getHeight() / 60.0f, 1) + " s", juce::Rectangle<float> (pf.getX() - 34.0f, pf.getBottom() - 12.0f, 30.0f, 12.0f),
                        juce::Justification::centredRight);

            // Legend: the colour scale.
            auto legend = getLocalBounds().toFloat().reduced (10.0f, 7.0f).removeFromTop (12.0f).removeFromRight (180.0f);
            g.setFont (juce::FontOptions (9.5f));
            g.setColour (Palette::textDim);
            g.drawText ("notch", legend.removeFromLeft (34.0f), juce::Justification::centredLeft);
            g.drawText ("peak", legend.removeFromRight (30.0f), juce::Justification::centredRight);
            auto bar = legend.reduced (4.0f, 3.0f);
            for (int i = 0; i < (int) bar.getWidth(); ++i)
            {
                g.setColour (heat (-30.0f + 45.0f * (float) i / bar.getWidth()));
                g.fillRect (bar.getX() + (float) i, bar.getY(), 1.0f, bar.getHeight());
            }
        }

        EffectModel& model;
        PhaserEngine& engine;
        EffectUI::FrequencyAxis axis;
        juce::Image image;
        std::vector<double> columnHz;
    };

    //==============================================================================
    PhaserView::PhaserView (EffectModel& m, PhaserEngine& e)
        : model (m), engine (e),
          syncButton (m, P::sync, "Sync", Palette::tempo, "Sweep rate in notes, following the tempo")
    {
        setOpaque (true);

        response = std::make_unique<ResponseView> (model, engine);
        sweep = std::make_unique<SweepView> (model, engine);
        addAndMakeVisible (*response);
        addAndMakeVisible (*sweep);
        addAndMakeVisible (syncButton);

        auto add = [this] (P id, const char* label, juce::Colour c, float mid)
        {
            auto* k = knobs.add (new ParamKnob (model.parameter (id), label, c, PhaserParameters::get (id).defaultValue, mid));
            addAndMakeVisible (k);
        };

        add (P::rate, "Rate", Palette::automation, 1.0f);
        add (P::depth, "Depth", sweepColour, 0.0f);
        add (P::centre, "Centre", Palette::accent, 800.0f);
        add (P::spread, "Spread", rightColour, 0.0f);
        add (P::stages, "Stages", Palette::tempo, 0.0f);
        add (P::feedback, "Feedback", Palette::tempo, 0.0f);
        add (P::mix, "Mix", Palette::marker, 0.0f);

        groups = { { "SWEEP", 0, 4, {} }, { "CHARACTER", 4, 2, {} }, { "OUTPUT", 6, 1, {} } };

        updateRateKnob();
        setSize (defaultWidth, defaultHeight);
        startTimerHz (60);
    }

    PhaserView::~PhaserView()
    {
        stopTimer();
    }

    void PhaserView::updateRateKnob()
    {
        const bool note = model.isOn (P::sync);
        if (note == showingNote && rateKnobReady)
            return;

        showingNote = note;
        rateKnobReady = true;
        auto id = note ? P::division : P::rate;
        knobs[0]->setParameter (model.parameter (id), PhaserParameters::get (id).defaultValue, note ? 0.0f : 1.0f);
        knobs[0]->setLabel (note ? "Note" : "Rate");
    }

    void PhaserView::tick (float input, float output)
    {
        juce::ignoreUnused (input);
        const auto level = juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (output, -100.0f) + 48.0f) / 48.0f);
        activity = level > activity ? level : activity * 0.95f;
        sweep->push (activity);
        response->repaint();
    }

    void PhaserView::timerCallback()
    {
        // The sweep scrolls on every tick; its brightness only falls slowly between audio blocks.
        tick (engine.inputPeak.take(), engine.outputPeak.take());

        updateRateKnob();
        for (auto* k : knobs)
            k->refresh();

        syncButton.refresh();
        repaint (getLocalBounds().removeFromTop (EffectUI::headerHeight));
    }

    void PhaserView::paint (juce::Graphics& g)
    {
        g.fillAll (Palette::panelBackground);

        auto text = [this] (P id) { return model.parameter (id)->getCurrentText(); };
        auto summary = text (P::stages) + "  -  " + (model.isOn (P::sync) ? text (P::division) : text (P::rate))
                       + "  -  centre " + text (P::centre) + "  -  feedback " + text (P::feedback);
        EffectUI::drawHeader (g, getLocalBounds().removeFromTop (EffectUI::headerHeight).withTrimmedRight (90), "C-PHASER", summary);
        EffectUI::paintKnobGroups (g, groups);
    }

    void PhaserView::resized()
    {
        auto r = getLocalBounds();
        auto header = r.removeFromTop (EffectUI::headerHeight).reduced (10, 5);
        syncButton.setBounds (header.removeFromRight (64));

        EffectUI::layoutKnobGroups (groups, knobs, r.removeFromBottom (EffectUI::knobRowHeight).reduced (8, 6));

        r.reduce (8, 0);
        response->setBounds (r.removeFromTop (r.getHeight() * 11 / 20));
        r.removeFromTop (8);
        sweep->setBounds (r);
    }
}
