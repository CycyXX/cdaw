#include "EsserView.h"
#include "../../UI/LookAndFeel/Palette.h"

namespace cdaw
{
    namespace
    {
        using P = EsserParameters::ID;
        constexpr float minDb = -27.0f, maxDb = 6.0f;
        constexpr float levelMinDb = -60.0f, levelMaxDb = 0.0f;

        const juce::Colour bandColour { 0xff4fd6c8 }, reductionColour { 0xffff7a59 }, thresholdColour { 0xfff2b134 };

        EsserEngine::Settings settingsOf (EffectModel& m)
        {
            return EsserParameters::toSettings ([&m] (int id) { return m.value (id); });
        }
    }

    //==============================================================================
    class EsserView::BandView : public juce::Component
    {
    public:
        BandView (EffectModel& m, EsserEngine& e) : model (m), engine (e) { setOpaque (true); }

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

        void setReduction (float db)
        {
            if (std::abs (db - reductionDb) > 0.05f)
            {
                reductionDb = db;
                repaint();
            }
        }

        void paint (juce::Graphics& g) override
        {
            const auto s = settingsOf (model);
            const double rate = engine.getSampleRate();

            g.fillAll (Palette::panelBackground);
            EffectUI::drawPanel (g, getLocalBounds().toFloat(), "BAND");

            auto p = plotArea();
            g.setColour (EffectUI::plotBackground);
            g.fillRect (p);
            EffectUI::drawFrequencyGrid (g, p, axis, true);

            g.setFont (juce::FontOptions (9.0f));
            for (float db = 0.0f; db >= minDb; db -= 6.0f)
            {
                g.setColour (Palette::panelOutline.withAlpha (std::abs (db) < 0.1f ? 0.8f : 0.3f));
                g.drawHorizontalLine ((int) dbToY (db), p.getX(), p.getRight());
                g.setColour (Palette::textDisabled);
                g.drawText (juce::String ((int) db), juce::Rectangle<float> (p.getX() - 34.0f, dbToY (db) - 6.0f, 30.0f, 12.0f),
                            juce::Justification::centredRight);
            }

            // The band.
            const float lowX = axis.toX (s.lowHz, p), highX = axis.toX (juce::jmax (s.highHz, s.lowHz), p);
            g.setGradientFill (juce::ColourGradient::vertical (bandColour.withAlpha (0.14f), p.getY(), bandColour.withAlpha (0.02f), p.getBottom()));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (lowX, p.getY(), highX, p.getBottom()));
            g.setColour (bandColour.withAlpha (0.4f));
            g.drawLine (lowX, railY(), highX, railY(), 2.0f);

            g.saveState();
            g.reduceClipRegion (p.toNearestInt());

            auto curveFor = [&] (float db)
            {
                juce::Path curve;
                for (float x = p.getX(); x <= p.getRight(); x += 1.0f)
                {
                    const auto y = dbToY (EsserEngine::getResponseDb (s, db, axis.toHz (x, p), rate));
                    if (x == p.getX()) curve.startNewSubPath (x, y);
                    else               curve.lineTo (x, y);
                }
                return curve;
            };

            // At full range, dashed; right now, solid.
            auto full = curveFor (s.rangeDb);
            const float dashes[] { 4.0f, 3.0f };
            juce::PathStrokeType (1.2f).createDashedStroke (full, full, dashes, 2);
            g.setColour (reductionColour.withAlpha (0.45f));
            g.fillPath (full);

            auto now = curveFor (reductionDb);
            juce::Path fill (now);
            fill.lineTo (p.getRight(), dbToY (0.0));
            fill.lineTo (p.getX(), dbToY (0.0));
            fill.closeSubPath();
            g.setColour (reductionColour.withAlpha (0.25f));
            g.fillPath (fill);
            g.setColour (reductionColour.withAlpha (0.2f));
            g.strokePath (now, juce::PathStrokeType (6.0f));
            g.setColour (reductionColour);
            g.strokePath (now, juce::PathStrokeType (2.0f));

            g.restoreState();

            if (s.listen != EsserEngine::Listen::off)
            {
                g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
                g.setColour (thresholdColour);
                g.drawText (s.listen == EsserEngine::Listen::band ? "LISTENING TO THE BAND" : "LISTENING TO WHAT IS REMOVED",
                            p.reduced (10.0f, 22.0f).removeFromTop (14.0f), juce::Justification::centredRight);
            }

            EffectUI::drawHandle (g, { lowX, railY() }, bandColour, hover == 1 || dragging == 1, "L");
            EffectUI::drawHandle (g, { highX, railY() }, bandColour, hover == 2 || dragging == 2, "H");
        }

        //==============================================================================
        int hitTest (juce::Point<float> pos) const
        {
            const auto s = settingsOf (model);
            auto p = plotArea();
            const float lowX = axis.toX (s.lowHz, p), highX = axis.toX (s.highHz, p);
            if (pos.getDistanceFrom ({ lowX, railY() }) < 11.0f)     return 1;
            if (pos.getDistanceFrom ({ highX, railY() }) < 11.0f)    return 2;
            if (pos.x > lowX && pos.x < highX && pos.y > p.getY())  return 3;
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
            if (dragging == 0)
                return;

            model.beginUndoTransaction ("De-esser band");
            if (dragging != 2) model.parameter (P::lowfreq)->beginGesture();
            if (dragging != 1) model.parameter (P::highfreq)->beginGesture();
            dragStartLow = model.value (P::lowfreq);
            dragStartHigh = model.value (P::highfreq);
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (dragging == 0)
                return;

            auto p = plotArea();
            auto set = [this] (P id, double hz)
            {
                auto& spec = EsserParameters::get (id);
                model.parameter (id)->setValue ((float) juce::jlimit ((double) spec.minimum, (double) spec.maximum, hz));
            };

            if (dragging == 1)
                set (P::lowfreq, juce::jmin (axis.toHz (e.position.x, p), (double) model.value (P::highfreq) / 1.2));
            else if (dragging == 2)
                set (P::highfreq, juce::jmax (axis.toHz (e.position.x, p), (double) model.value (P::lowfreq) * 1.2));
            else
            {
                // The middle: both edges move together, by the same ratio.
                const double factor = axis.toHz (e.position.x, p) / axis.toHz (e.mouseDownPosition.x, p);
                set (P::lowfreq, dragStartLow * factor);
                set (P::highfreq, dragStartHigh * factor);
            }

            repaint();
        }

        void mouseUp (const juce::MouseEvent&) override
        {
            if (dragging != 2 && dragging != 0) model.parameter (P::lowfreq)->endGesture();
            if (dragging != 1 && dragging != 0) model.parameter (P::highfreq)->endGesture();
            dragging = 0;
        }

        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            if (auto h = hitTest (e.position); h == 1 || h == 2)
            {
                const auto id = h == 1 ? P::lowfreq : P::highfreq;
                model.setValue (id, EsserParameters::get (id).defaultValue, "Reset de-esser band");
            }
        }

        EffectModel& model;
        EsserEngine& engine;
        EffectUI::FrequencyAxis axis;
        float reductionDb = 0.0f;
        double dragStartLow = 0.0, dragStartHigh = 0.0;
        int hover = 0, dragging = 0;
    };

    //==============================================================================
    class EsserView::ActivityView : public juce::Component
    {
    public:
        explicit ActivityView (EffectModel& m) : model (m) { setOpaque (true); }

        struct Frame { float levelDb = -100.0f, thresholdDb = -24.0f, reductionDb = 0.0f; };

        void push (Frame f)
        {
            frames[(size_t) next] = f;
            next = (next + 1) % (int) frames.size();
            meterDb = f.reductionDb > meterDb ? f.reductionDb : meterDb * 0.92f;
            repaint();
        }

        juce::Rectangle<float> plotArea() const
        {
            return getLocalBounds().toFloat().withTrimmedLeft (36.0f).withTrimmedRight (40.0f).withTrimmedTop (26.0f).withTrimmedBottom (10.0f);
        }

        float levelToY (float db, juce::Rectangle<float> p) const
        {
            return juce::jmap (juce::jlimit (levelMinDb, levelMaxDb, db), levelMinDb, levelMaxDb, p.getBottom(), p.getY());
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (Palette::panelBackground);
            EffectUI::drawPanel (g, getLocalBounds().toFloat(), "ACTIVITY");

            auto p = plotArea();
            g.setColour (EffectUI::plotBackground);
            g.fillRect (p);

            g.setFont (juce::FontOptions (9.0f));
            for (float db = levelMaxDb; db >= levelMinDb; db -= 12.0f)
            {
                g.setColour (Palette::panelOutline.withAlpha (0.3f));
                g.drawHorizontalLine ((int) levelToY (db, p), p.getX(), p.getRight());
                g.setColour (Palette::textDisabled);
                g.drawText (juce::String ((int) db), juce::Rectangle<float> (p.getX() - 34.0f, levelToY (db, p) - 6.0f, 30.0f, 12.0f),
                            juce::Justification::centredRight);
            }

            // Newest at the right, one frame per pixel.
            const int count = juce::jmin ((int) frames.size(), (int) p.getWidth());
            juce::Path level, threshold;
            const float reductionScale = p.getHeight() / 24.0f;

            for (int i = 0; i < count; ++i)
            {
                const auto& f = frames[(size_t) ((next - 1 - i + (int) frames.size() * 2) % (int) frames.size())];
                const float x = p.getRight() - (float) i;

                if (f.reductionDb > 0.01f)
                {
                    g.setColour (reductionColour.withAlpha (0.55f));
                    g.fillRect (x - 1.0f, p.getY(), 1.0f, f.reductionDb * reductionScale);
                }

                const float ly = levelToY (f.levelDb, p), ty = levelToY (f.thresholdDb, p);
                if (i == 0) { level.startNewSubPath (x, ly); threshold.startNewSubPath (x, ty); }
                else        { level.lineTo (x, ly);          threshold.lineTo (x, ty); }
            }

            g.setColour (bandColour);
            g.strokePath (level, juce::PathStrokeType (1.5f));
            g.setColour (thresholdColour.withAlpha (0.85f));
            g.strokePath (threshold, juce::PathStrokeType (1.2f));

            g.setFont (juce::FontOptions (9.5f));
            auto legend = getLocalBounds().toFloat().reduced (10.0f, 7.0f).removeFromTop (12.0f).withTrimmedLeft (90.0f);
            auto item = [&] (const juce::String& text, juce::Colour c)
            {
                g.setColour (c);
                g.fillRect (legend.removeFromLeft (10.0f).reduced (0.0f, 4.0f));
                g.setColour (Palette::textDim);
                g.drawText (text, legend.removeFromLeft (90.0f).withTrimmedLeft (4.0f), juce::Justification::centredLeft);
            };
            item ("band level", bandColour);
            item (model.isOn (P::autoThreshold) ? "threshold (auto)" : "threshold", thresholdColour);
            item ("reduction", reductionColour);

            // The reduction meter, hanging from the top.
            auto meter = juce::Rectangle<float> (p.getRight() + 12.0f, p.getY(), 14.0f, p.getHeight());
            g.setColour (EffectUI::plotBackground);
            g.fillRect (meter);
            g.setColour (reductionColour);
            g.fillRect (meter.withHeight (juce::jlimit (0.0f, meter.getHeight(), meterDb * reductionScale)));
            g.setColour (Palette::textDim);
            g.drawText ("-" + juce::String (meterDb, 1), meter.withY (p.getY() - 16.0f).withHeight (12.0f).expanded (10.0f, 0.0f),
                        juce::Justification::centred);
        }

        EffectModel& model;
        std::array<Frame, 1200> frames {};
        int next = 0;
        float meterDb = 0.0f;
    };

    //==============================================================================
    EsserView::EsserView (EffectModel& m, EsserEngine& e)
        : model (m), engine (e),
          modeButtons (m, P::mode, { "Split", "Wideband" }, Palette::accent, "De-esser mode"),
          listenButtons (m, P::listen, { "Off", "Band", "Removed" }, thresholdColour, "De-esser listen"),
          autoButton (m, P::autoThreshold, "Auto", thresholdColour, "The threshold follows the level of the signal")
    {
        setOpaque (true);

        band = std::make_unique<BandView> (model, engine);
        activity = std::make_unique<ActivityView> (model);
        addAndMakeVisible (*band);
        addAndMakeVisible (*activity);
        addAndMakeVisible (modeButtons);
        addAndMakeVisible (listenButtons);
        addAndMakeVisible (autoButton);
        modeButtons.setTooltip ("Split: only the band is turned down. Wideband: the whole signal is");
        listenButtons.setTooltip ("Listen to the band the de-esser hears, or to what it takes away");

        auto add = [this] (P id, const char* label, juce::Colour c, float mid)
        {
            auto* k = knobs.add (new ParamKnob (model.parameter (id), label, c, EsserParameters::get (id).defaultValue, mid));
            addAndMakeVisible (k);
        };

        add (P::threshold, "Threshold", thresholdColour, 0.0f);
        add (P::lowfreq, "Low", bandColour, 5000.0f);
        add (P::highfreq, "High", bandColour, 9000.0f);
        add (P::range, "Range", reductionColour, 0.0f);
        add (P::release, "Release", reductionColour, 60.0f);
        add (P::output, "Output", Palette::marker, 0.0f);

        groups = { { "DETECTION", 0, 3, {} }, { "REDUCTION", 3, 2, {} }, { "OUTPUT", 5, 1, {} } };

        setSize (defaultWidth, defaultHeight);
        startTimerHz (60);
    }

    EsserView::~EsserView()
    {
        stopTimer();
    }

    void EsserView::tick (float bandLevel, float thresholdDb, float reductionDb)
    {
        activity->push ({ juce::Decibels::gainToDecibels (bandLevel, -100.0f), thresholdDb, reductionDb });
        band->setReduction (reductionDb);
    }

    void EsserView::timerCallback()
    {
        tick (engine.bandLevel.take(), model.isOn (P::autoThreshold) ? engine.thresholdNow.load() : model.value (P::threshold),
              engine.reduction.take());
        engine.inputPeak.take();
        engine.outputPeak.take();

        for (auto* k : knobs)
            k->refresh();

        knobs[0]->setAlpha (model.isOn (P::autoThreshold) ? 0.4f : 1.0f);
        modeButtons.refresh();
        listenButtons.refresh();
        autoButton.refresh();
        band->repaint();
        repaint (getLocalBounds().removeFromTop (EffectUI::headerHeight));
    }

    void EsserView::paint (juce::Graphics& g)
    {
        g.fillAll (Palette::panelBackground);

        auto text = [this] (P id) { return model.parameter (id)->getCurrentText(); };
        auto summary = text (P::lowfreq) + " - " + text (P::highfreq) + "  -  "
                       + (model.isOn (P::autoThreshold) ? juce::String ("auto threshold") : "threshold " + text (P::threshold))
                       + "  -  up to " + text (P::range);
        EffectUI::drawHeader (g, getLocalBounds().removeFromTop (EffectUI::headerHeight).withTrimmedRight (430), "C-ESSER", summary);
        EffectUI::paintKnobGroups (g, groups);
    }

    void EsserView::resized()
    {
        auto r = getLocalBounds();
        auto header = r.removeFromTop (EffectUI::headerHeight).reduced (10, 5);
        autoButton.setBounds (header.removeFromRight (64));
        header.removeFromRight (8);
        listenButtons.setBounds (header.removeFromRight (180));
        header.removeFromRight (8);
        modeButtons.setBounds (header.removeFromRight (150));

        EffectUI::layoutKnobGroups (groups, knobs, r.removeFromBottom (EffectUI::knobRowHeight).reduced (8, 6));

        r.reduce (8, 0);
        band->setBounds (r.removeFromTop (r.getHeight() * 11 / 20));
        r.removeFromTop (8);
        activity->setBounds (r);
    }
}
