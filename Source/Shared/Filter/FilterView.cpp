#include "FilterView.h"
#include "FilterIcons.h"
#include "../UI/SpectrumAnalyser.h"
#include "../../UI/LookAndFeel/Palette.h"
#include <deque>

namespace cdaw
{
    namespace
    {
        using P = FilterParameters::ID;
        using Type = FilterEngine::Type;
        constexpr float minDb = -42.0f, maxDb = 30.0f;

        const juce::Colour leftColour { 0xff4fd6c8 }, rightColour { 0xffd77cff }, rangeColour { 0xfff2b134 };
        const juce::Colour spectrumColour { 0xff5f8fbf };

        FilterEngine::Settings settingsOf (EffectModel& m)
        {
            return FilterParameters::toSettings ([&m] (int id) { return m.value (id); });
        }

        bool isSteepCut (const FilterEngine::Settings& s)
        {
            return s.steep && (s.type == Type::lowPass || s.type == Type::highPass);
        }

        /** The resonance shown as a height (dB above the plain filter), and back. */
        double resonanceDb (const FilterEngine::Settings& s)
        {
            return 20.0 * std::log10 (FilterEngine::getQ (s) / (isSteepCut (s) ? 1.3066 : 0.7071));
        }

        float resonanceFor (const FilterEngine::Settings& s, double db)
        {
            const double growth = isSteepCut (s) ? 12.0 : 25.0;
            return (float) juce::jlimit (0.0, 1.0, db / 20.0 * std::log (10.0) / std::log (growth));
        }

        /** How far the LFO and the envelope can take the cutoff. */
        juce::Range<double> modulationSpan (const FilterEngine::Settings& s, double sampleRate)
        {
            const float env = s.envAmount;
            const double low = FilterEngine::modulatedCutoff (s, -1.0f, 0.0f, sampleRate);
            const double high = FilterEngine::modulatedCutoff (s, 1.0f, 0.0f, sampleRate);
            const double withEnv = FilterEngine::modulatedCutoff (s, env >= 0.0f ? 1.0f : -1.0f, 1.0f, sampleRate);
            return { juce::jmin (low, withEnv), juce::jmax (high, withEnv) };
        }
    }

    //==============================================================================
    class FilterView::ResponseView : public juce::Component
    {
    public:
        ResponseView (EffectModel& m, FilterEngine& e) : model (m), engine (e), spectrum (e.outputFifo) { setOpaque (true); }

        juce::Rectangle<float> plotArea() const
        {
            return getLocalBounds().toFloat().withTrimmedLeft (36.0f).withTrimmedRight (12.0f).withTrimmedTop (28.0f).withTrimmedBottom (20.0f);
        }

        float dbToY (double db) const
        {
            auto p = plotArea();
            return juce::jmap (juce::jlimit (minDb - 6.0f, maxDb + 6.0f, (float) db), minDb, maxDb, p.getBottom(), p.getY());
        }

        double yToDb (float y) const
        {
            auto p = plotArea();
            return juce::jmap (y, p.getBottom(), p.getY(), minDb, maxDb);
        }

        juce::Point<float> handle (const FilterEngine::Settings& s) const
        {
            return { axis.toX (s.cutoff, plotArea()), dbToY (resonanceDb (s)) };
        }

        void setActive (bool active)        { engine.outputFifo->active = active; }

        void updateSpectrum()
        {
            if (spectrum.update (columnHz, engine.getSampleRate()))
                repaint();
        }

        void pushCutoff (float left, float right)
        {
            trail.push_front ({ left, right });
            while (trail.size() > 14)
                trail.pop_back();
            repaint();
        }

        void resized() override
        {
            auto p = plotArea();
            columnHz.resize ((size_t) juce::jmax (1, (int) p.getWidth()));
            for (size_t c = 0; c < columnHz.size(); ++c)
                columnHz[c] = axis.toHz (p.getX() + (float) c + 0.5f, p);
        }

        juce::Path curveFor (const FilterEngine::Settings& s, double cutoff) const
        {
            auto p = plotArea();
            const double rate = engine.getSampleRate();
            juce::Path curve;
            for (float x = p.getX(); x <= p.getRight(); x += 1.5f)
            {
                const auto y = dbToY (FilterEngine::getResponseDb (s, cutoff, axis.toHz (x, p), rate));
                if (x == p.getX()) curve.startNewSubPath (x, y);
                else               curve.lineTo (x, y);
            }
            return curve;
        }

        void paint (juce::Graphics& g) override
        {
            const auto s = settingsOf (model);
            const double rate = engine.getSampleRate();

            g.fillAll (Palette::panelBackground);
            EffectUI::drawPanel (g, getLocalBounds().toFloat(), "RESPONSE");

            auto p = plotArea();
            g.setColour (EffectUI::plotBackground);
            g.fillRect (p);
            EffectUI::drawFrequencyGrid (g, p, axis, true);

            g.setFont (juce::FontOptions (9.0f));
            for (float db = 24.0f; db >= minDb; db -= 12.0f)
            {
                g.setColour (Palette::panelOutline.withAlpha (std::abs (db) < 0.1f ? 0.8f : 0.3f));
                g.drawHorizontalLine ((int) dbToY (db), p.getX(), p.getRight());
                g.setColour (Palette::textDisabled);
                g.drawText ((db > 0 ? "+" : "") + juce::String ((int) db), juce::Rectangle<float> (p.getX() - 34.0f, dbToY (db) - 6.0f, 30.0f, 12.0f),
                            juce::Justification::centredRight);
            }

            g.saveState();
            g.reduceClipRegion (p.toNearestInt());

            // The output's spectrum behind everything.
            if (! spectrum.levels.empty())
            {
                juce::Path fill;
                fill.startNewSubPath (p.getX(), p.getBottom());
                for (size_t c = 0; c < spectrum.levels.size(); ++c)
                    fill.lineTo (p.getX() + (float) c, juce::jmap (juce::jlimit (-100.0f, 0.0f, spectrum.levels[c]), -100.0f, 0.0f, p.getBottom(), p.getY()));
                fill.lineTo (p.getRight(), p.getBottom());
                fill.closeSubPath();
                g.setGradientFill (juce::ColourGradient::vertical (spectrumColour.withAlpha (0.35f), p.getY(), spectrumColour.withAlpha (0.05f), p.getBottom()));
                g.fillPath (fill);
            }

            // Where the modulation can take the cutoff.
            const auto span = modulationSpan (s, rate);
            if (span.getLength() > 1.0)
            {
                auto band = juce::Rectangle<float>::leftTopRightBottom (axis.toX (span.getStart(), p), p.getY(), axis.toX (span.getEnd(), p), p.getBottom());
                g.setGradientFill (juce::ColourGradient::vertical (rangeColour.withAlpha (0.12f), p.getY(), rangeColour.withAlpha (0.0f), p.getBottom()));
                g.fillRect (band);
                g.setColour (rangeColour.withAlpha (0.4f));
                g.fillRect (band.withHeight (2.0f));
            }

            // The set curve, faintly.
            {
                auto set = curveFor (s, s.cutoff);
                const float dashes[] { 3.0f, 3.0f };
                juce::PathStrokeType (1.0f).createDashedStroke (set, set, dashes, 2);
                g.setColour (Palette::text.withAlpha (0.35f));
                g.fillPath (set);
            }

            // Recent positions, fading, then where it is now.
            for (int i = (int) trail.size() - 1; i >= 1; --i)
            {
                g.setColour (leftColour.withAlpha (0.28f * (1.0f - (float) i / (float) trail.size())));
                g.strokePath (curveFor (s, trail[(size_t) i].first), juce::PathStrokeType (1.2f));
            }

            const float nowLeft = trail.empty() ? s.cutoff : trail.front().first;
            const float nowRight = trail.empty() ? s.cutoff : trail.front().second;

            if (s.lfoStereo > 0.01f && s.lfoAmount > 0.001f)
            {
                auto right = curveFor (s, nowRight);
                const float dashes[] { 5.0f, 3.0f };
                juce::PathStrokeType (1.4f).createDashedStroke (right, right, dashes, 2);
                g.setColour (rightColour.withAlpha (0.85f));
                g.fillPath (right);
            }

            auto live = curveFor (s, nowLeft);
            juce::Path fill (live);
            fill.lineTo (p.getRight(), p.getBottom() + 10.0f);
            fill.lineTo (p.getX(), p.getBottom() + 10.0f);
            fill.closeSubPath();
            g.setGradientFill (juce::ColourGradient::vertical (leftColour.withAlpha (0.22f), p.getY(), leftColour.withAlpha (0.02f), p.getBottom()));
            g.fillPath (fill);
            g.setColour (leftColour.withAlpha (0.25f));
            g.strokePath (live, juce::PathStrokeType (7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (leftColour);
            g.strokePath (live, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            g.setColour (leftColour.withAlpha (0.6f));
            g.drawVerticalLine ((int) axis.toX (nowLeft, p), p.getY(), p.getBottom());
            g.restoreState();

            EffectUI::drawHandle (g, handle (s), Palette::text, hover || dragging);

            // Read-out next to the node while it's in use.
            if (hover || dragging)
            {
                const auto h = handle (s);
                auto text = model.parameter (P::cutoff)->getCurrentText() + "   res " + model.parameter (P::resonance)->getCurrentText();
                g.setFont (juce::FontOptions (11.5f));
                const auto w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text) + 16.0f;
                juce::Rectangle<float> box (h.x + 14.0f, h.y - 30.0f, w, 22.0f);
                if (box.getRight() > p.getRight()) box.setX (h.x - 14.0f - w);
                if (box.getY() < p.getY()) box.setY (h.y + 12.0f);
                g.setColour (juce::Colour (0xee101215));
                g.fillRoundedRectangle (box, 4.0f);
                g.setColour (Palette::text);
                g.drawText (text, box, juce::Justification::centred);
            }
        }

        //==============================================================================
        void mouseMove (const juce::MouseEvent& e) override
        {
            const bool over = e.position.getDistanceFrom (handle (settingsOf (model))) < 13.0f;
            if (over != hover)
            {
                hover = over;
                setMouseCursor (over ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
                repaint();
            }
        }

        void mouseExit (const juce::MouseEvent&) override
        {
            hover = false;
            repaint();
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            // Grab the node, or click anywhere in the plot to bring the cutoff there.
            if (! plotArea().contains (e.position))
                return;

            dragging = true;
            model.beginUndoTransaction ("Filter cutoff");
            model.parameter (P::cutoff)->beginGesture();
            model.parameter (P::resonance)->beginGesture();
            mouseDrag (e);
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (! dragging)
                return;

            const auto s = settingsOf (model);
            model.parameter (P::cutoff)->setValue ((float) juce::jlimit ((double) FilterEngine::minHz, (double) FilterEngine::maxHz, axis.toHz (e.position.x, plotArea())));
            model.parameter (P::resonance)->setValue (resonanceFor (s, yToDb (e.position.y)));
            repaint();
        }

        void mouseUp (const juce::MouseEvent&) override
        {
            if (dragging)
            {
                model.parameter (P::cutoff)->endGesture();
                model.parameter (P::resonance)->endGesture();
            }
            dragging = false;
        }

        void mouseDoubleClick (const juce::MouseEvent&) override
        {
            model.beginUndoTransaction ("Reset filter");
            for (auto id : { P::cutoff, P::resonance })
            {
                auto param = model.parameter (id);
                param->beginGesture();
                param->setValue (FilterParameters::get (id).defaultValue);
                param->endGesture();
            }
        }

        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
        {
            auto param = model.parameter (P::resonance);
            param->setValue (juce::jlimit (0.0f, 1.0f, param->getValue() + wheel.deltaY * (wheel.isReversed ? -0.15f : 0.15f)));
            repaint();
        }

        EffectModel& model;
        FilterEngine& engine;
        SpectrumAnalyser spectrum;
        EffectUI::FrequencyAxis axis;
        std::vector<double> columnHz;
        std::deque<std::pair<float, float>> trail;
        bool hover = false, dragging = false;
    };

    //==============================================================================
    class FilterView::MotionView : public juce::Component
    {
    public:
        MotionView (EffectModel& m, FilterEngine& e) : model (m), engine (e) { setOpaque (true); }

        juce::Rectangle<int> plotArea() const
        {
            return getLocalBounds().withTrimmedLeft (36).withTrimmedRight (12).withTrimmedTop (30).withTrimmedBottom (8);
        }

        /** Scrolls down a row and draws where the cutoff is now at the top. */
        void push (float left, float right, float envelope, float activity)
        {
            auto p = plotArea();
            if (p.isEmpty())
                return;

            if (image.getWidth() != p.getWidth() || image.getHeight() != p.getHeight())
                image = juce::Image (juce::Image::RGB, p.getWidth(), p.getHeight(), true);

            image.moveImageSection (0, 1, 0, 0, image.getWidth(), image.getHeight() - 1);

            const auto s = settingsOf (model);
            const auto local = juce::Rectangle<float> ((float) image.getWidth(), 1.0f);
            const float xLeft = axis.toX (left, local), xRight = axis.toX (right, local), xSet = axis.toX (s.cutoff, local);

            // Each row joins up with the one before, so a fast LFO still draws a line, not dots.
            if (lastX[0] < -50.0f)
            {
                lastX[0] = xLeft;
                lastX[1] = xRight;
            }

            const juce::Range<float> leftSpan (juce::jmin (xLeft, lastX[0]), juce::jmax (xLeft, lastX[0]));
            const juce::Range<float> rightSpan (juce::jmin (xRight, lastX[1]), juce::jmax (xRight, lastX[1]));
            lastX[0] = xLeft;
            lastX[1] = xRight;
            auto distance = [] (juce::Range<float> span, float x) { return x < span.getStart() ? span.getStart() - x : x > span.getEnd() ? x - span.getEnd() : 0.0f; };
            const auto span = modulationSpan (s, engine.getSampleRate());
            const float spanLow = axis.toX (span.getStart(), local), spanHigh = axis.toX (span.getEnd(), local);
            const bool stereo = std::abs (xLeft - xRight) > 0.5f;
            const float brightness = 0.45f + 0.55f * activity;
            const auto background = EffectUI::plotBackground.interpolatedWith (rangeColour, 0.10f * envelope);

            juce::Image::BitmapData row (image, 0, 0, image.getWidth(), 1, juce::Image::BitmapData::writeOnly);
            for (int x = 0; x < image.getWidth(); ++x)
            {
                auto c = background;
                const auto fx = (float) x;

                if (fx >= spanLow && fx <= spanHigh)
                    c = c.interpolatedWith (rangeColour, 0.06f);
                if (std::abs (fx - xSet) < 0.5f)
                    c = c.interpolatedWith (Palette::text, 0.18f);

                const float l = std::exp (-0.5f * juce::square (distance (leftSpan, fx) / 1.8f));
                c = c.interpolatedWith (leftColour.withMultipliedBrightness (brightness), juce::jmin (1.0f, l * 1.3f));

                if (stereo)
                {
                    const float r = std::exp (-0.5f * juce::square (distance (rightSpan, fx) / 1.8f));
                    c = c.interpolatedWith (rightColour.withMultipliedBrightness (brightness), juce::jmin (1.0f, r * 1.3f));
                }

                row.setPixelColour (x, 0, c);
            }

            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (Palette::panelBackground);
            EffectUI::drawPanel (g, getLocalBounds().toFloat(), "MOTION");

            auto p = plotArea();
            g.setColour (EffectUI::plotBackground);
            g.fillRect (p);

            if (image.isValid())
                g.drawImageAt (image, p.getX(), p.getY());

            g.setFont (juce::FontOptions (9.5f));
            g.setColour (Palette::textDisabled);
            const auto pf = p.toFloat();
            g.drawText ("now", juce::Rectangle<float> (pf.getX() - 34.0f, pf.getY(), 30.0f, 12.0f), juce::Justification::centredRight);
            g.drawText ("-" + juce::String (pf.getHeight() / 60.0f, 1) + " s", juce::Rectangle<float> (pf.getX() - 34.0f, pf.getBottom() - 12.0f, 30.0f, 12.0f),
                        juce::Justification::centredRight);
        }

        EffectModel& model;
        FilterEngine& engine;
        EffectUI::FrequencyAxis axis;
        juce::Image image;
        float lastX[2] { -100.0f, -100.0f };
    };

    //==============================================================================
    FilterView::FilterView (EffectModel& m, FilterEngine& e)
        : model (m), engine (e),
          typeButtons (m, P::type, FilterParameters::getTypeNames(), leftColour, "Filter type"),
          slopeButtons (m, P::slope, { "12 dB", "24 dB" }, leftColour, "Filter slope"),
          shapeButtons (m, P::lfoShape, FilterParameters::getShapeNames(), Palette::automation, "LFO shape"),
          syncButton (m, P::lfoSync, "Sync", Palette::tempo, "LFO rate in notes, following the tempo")
    {
        setOpaque (true);

        response = std::make_unique<ResponseView> (model, engine);
        motion = std::make_unique<MotionView> (model, engine);
        addAndMakeVisible (*response);
        addAndMakeVisible (*motion);

        typeButtons.drawIcon = FilterIcons::drawType;
        shapeButtons.drawIcon = FilterIcons::drawShape;
        slopeButtons.setTooltip ("12 dB per octave, or a steeper 24");
        for (auto* c : std::initializer_list<juce::Component*> { &typeButtons, &slopeButtons, &shapeButtons, &syncButton })
            addAndMakeVisible (c);

        auto add = [this] (P id, const char* label, juce::Colour c, float mid)
        {
            auto* k = knobs.add (new ParamKnob (model.parameter (id), label, c, FilterParameters::get (id).defaultValue, mid));
            addAndMakeVisible (k);
        };

        add (P::cutoff, "Cutoff", leftColour, 1000.0f);
        add (P::resonance, "Resonance", leftColour, 0.0f);
        add (P::drive, "Drive", rangeColour, 0.0f);
        add (P::lfoRate, "Rate", Palette::automation, 1.5f);
        add (P::lfoAmount, "Amount", Palette::automation, 0.0f);
        add (P::lfoStereo, "Stereo", rightColour, 0.0f);
        add (P::envAmount, "Amount", Palette::marker, 0.0f);
        add (P::envRelease, "Release", Palette::marker, 200.0f);
        add (P::mix, "Mix", Palette::accent, 0.0f);
        add (P::output, "Output", Palette::accent, 0.0f);

        groups = { { "FILTER", 0, 3, {} }, { "LFO", 3, 3, {} }, { "ENVELOPE", 6, 2, {} }, { "OUTPUT", 8, 2, {} } };

        updateRateKnob();
        setSize (defaultWidth, defaultHeight);
        startTimerHz (60);
    }

    FilterView::~FilterView()
    {
        stopTimer();
        response->setActive (false);
    }

    void FilterView::visibilityChanged()
    {
        response->setActive (isShowing());
    }

    void FilterView::updateRateKnob()
    {
        const bool note = model.isOn (P::lfoSync);
        if (note == showingNote && rateKnobReady)
            return;

        showingNote = note;
        rateKnobReady = true;
        auto id = note ? P::lfoDivision : P::lfoRate;
        knobs[3]->setParameter (model.parameter (id), FilterParameters::get (id).defaultValue, note ? 0.0f : 1.5f);
        knobs[3]->setLabel (note ? "Note" : "Rate");
    }

    void FilterView::tick (float input, float output, float cutoffLeft, float cutoffRight, float envelope)
    {
        juce::ignoreUnused (input);
        const auto level = juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (output, -100.0f) + 48.0f) / 48.0f);
        response->pushCutoff (cutoffLeft, cutoffRight);
        motion->push (cutoffLeft, cutoffRight, envelope, level);
    }

    void FilterView::updateSpectrum()
    {
        response->updateSpectrum();
    }

    void FilterView::timerCallback()
    {
        tick (engine.inputPeak.take(), engine.outputPeak.take(), engine.currentCutoff[0].load(), engine.currentCutoff[1].load(),
              engine.envelopeLevel.load());

        if (isShowing() && (frame++ & 1) == 0)
        {
            response->setActive (true);
            updateSpectrum();
        }

        updateRateKnob();
        for (auto* k : knobs)
            k->refresh();

        typeButtons.refresh();
        slopeButtons.refresh();
        shapeButtons.refresh();
        syncButton.refresh();
        repaint (getLocalBounds().removeFromTop (EffectUI::headerHeight));
    }

    void FilterView::paint (juce::Graphics& g)
    {
        g.fillAll (Palette::panelBackground);

        auto text = [this] (P id) { return model.parameter (id)->getCurrentText(); };
        auto summary = text (P::type) + " " + text (P::slope) + "  -  " + text (P::cutoff) + "  -  resonance " + text (P::resonance);
        if (model.value (P::lfoAmount) > 0.001f)
            summary << "  -  LFO " << text (P::lfoAmount);
        if (std::abs (model.value (P::envAmount)) > 0.001f)
            summary << "  -  envelope " << text (P::envAmount);

        EffectUI::drawHeader (g, getLocalBounds().removeFromTop (EffectUI::headerHeight).withTrimmedRight (300), "C-FILTER", summary);
        EffectUI::paintKnobGroups (g, groups);
    }

    void FilterView::resized()
    {
        auto r = getLocalBounds();
        auto header = r.removeFromTop (EffectUI::headerHeight).reduced (10, 5);
        slopeButtons.setBounds (header.removeFromRight (110));
        header.removeFromRight (8);
        typeButtons.setBounds (header.removeFromRight (168));

        EffectUI::layoutKnobGroups (groups, knobs, r.removeFromBottom (EffectUI::knobRowHeight).reduced (8, 6));

        r.reduce (8, 0);
        response->setBounds (r.removeFromTop (r.getHeight() * 3 / 5));
        r.removeFromTop (8);
        motion->setBounds (r);

        // The LFO's shape and Sync sit in the motion panel's title row.
        auto row = motion->getBounds().removeFromTop (28).reduced (8, 4);
        syncButton.setBounds (row.removeFromRight (56));
        row.removeFromRight (8);
        shapeButtons.setBounds (row.removeFromRight (6 * 34));
    }
}
