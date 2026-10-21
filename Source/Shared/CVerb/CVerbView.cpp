#include "CVerbView.h"
#include "../UI/SpectrumAnalyser.h"
#include "../../UI/LookAndFeel/Palette.h"

namespace cdaw
{
    namespace
    {
        constexpr int headerHeight = 34, knobsHeight = 128, scopeHeight = 96;
        const juce::Colour wetColour { 0xff4fd6c8 };
        const juce::Colour dryColour { 0xff9aa6b8 };
        const juce::Colour preColour { 0xffb39ddb };
        const juce::Colour spectrumColour { 0xff8fa6c0 };
        const juce::Colour plotBackground { 0xff15171b };

        float toDb (float gain)     { return juce::Decibels::gainToDecibels (gain, -100.0f); }

        juce::String formatHz (double hz)
        {
            return hz >= 1000.0 ? juce::String (hz / 1000.0, hz >= 10000.0 ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (hz)) + " Hz";
        }

        void drawPanel (juce::Graphics& g, juce::Rectangle<float> r)
        {
            g.setColour (Palette::windowBackground);
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (Palette::panelOutline.withAlpha (0.7f));
            g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
        }

        /** Level (0 = 60 dB down, 1 = loudest) to colour: deep blue through teal and amber to white. */
        juce::Colour heat (float proportion)
        {
            static const juce::Colour stops[] { juce::Colour (0xff1e2a5a), juce::Colour (0xff27609a),
                                                juce::Colour (0xff2fa7b8), juce::Colour (0xfff2b134), juce::Colour (0xfffff3d6) };
            constexpr int numStops = (int) std::size (stops);
            auto p = juce::jlimit (0.0f, 1.0f, proportion) * (float) (numStops - 1);
            auto i = juce::jmin ((int) p, numStops - 2);
            return stops[i].interpolatedWith (stops[i + 1], p - (float) i);
        }
    }

    //==============================================================================
    class CVerbView::SpaceView : public juce::Component
    {
    public:
        explicit SpaceView (CVerbModel& m) : model (m), spectrum (m.getEngine().wetFifo)
        {
            setOpaque (true);
        }

        //==============================================================================
        juce::Rectangle<float> plotArea() const
        {
            return getLocalBounds().toFloat().withTrimmedLeft (44.0f).withTrimmedRight (40.0f).withTrimmedTop (24.0f).withTrimmedBottom (18.0f);
        }

        float hzToX (double hz) const
        {
            auto p = plotArea();
            return p.getX() + (float) (std::log (hz / 20.0) / std::log (1000.0)) * p.getWidth();
        }

        double xToHz (float x) const
        {
            auto p = plotArea();
            return 20.0 * std::pow (1000.0, juce::jlimit (0.0, 1.0, (double) (x - p.getX()) / p.getWidth()));
        }

        /** Time rises from the bottom. */
        float timeToY (double t) const
        {
            auto p = plotArea();
            return p.getBottom() - (float) (t / timeRange) * p.getHeight();
        }

        double yToTime (float y) const
        {
            auto p = plotArea();
            return juce::jlimit (0.0, timeRange, (double) (p.getBottom() - y) / p.getHeight() * timeRange);
        }

        /** The cuts' own scale (right axis). */
        float cutDbToY (double db) const
        {
            auto p = plotArea();
            return juce::jmap ((float) juce::jlimit (-36.0, 6.0, db), -36.0f, 6.0f, p.getBottom(), p.getY());
        }

        float spectrumDbToY (float db) const
        {
            auto p = plotArea();
            return juce::jmap (juce::jlimit (-100.0f, 0.0f, db), -100.0f, 0.0f, p.getBottom(), p.getY());
        }

        //==============================================================================
        void setActive (bool active)    { model.getEngine().wetFifo->active = active; }

        void updateSpectrum()
        {
            if (spectrum.update (columnHz, model.getEngine().getSampleRate()))
                repaint();
        }

        juce::String settingsKey() const
        {
            juce::String key;
            for (int i = 0; i < CVerbParameters::count; ++i)
                if (i != CVerbParameters::mix && i != CVerbParameters::modulation && i != CVerbParameters::width)
                    key << model.value ((CVerbParameters::ID) i) << ",";
            return key << getWidth() << "x" << getHeight();
        }

        void refreshIfChanged()
        {
            auto key = settingsKey();
            if (key != lastKey)
            {
                lastKey = key;
                image = {};
                repaint();
            }
        }

        /** A round number of seconds that holds the whole tail (kept while dragging the decay). */
        void chooseTimeRange()
        {
            if (dragging == Drag::decay)
                return;

            const double longest = model.value (CVerbParameters::preDelay) * 0.001
                                   + model.value (CVerbParameters::decay) * juce::jmax (1.0f, model.value (CVerbParameters::lowDecay)) * 1.05;
            timeRange = 0.5;
            while (timeRange < longest && timeRange < 32.0)
                timeRange *= 2.0;
        }

        /** The decay map: one column per frequency, rising in time; transparent below -60 dB. */
        void renderImage()
        {
            chooseTimeRange();
            auto p = plotArea().toNearestInt();
            // Opaque: blended into the panel's background here (translucent images came out dim).
            image = juce::Image (juce::Image::RGB, juce::jmax (1, p.getWidth()), juce::jmax (1, p.getHeight()), false);
            image.clear (image.getBounds(), plotBackground);
            juce::Image::BitmapData pixels (image, juce::Image::BitmapData::writeOnly);

            const bool frozen = model.value (CVerbParameters::freeze) >= 0.5f;
            const double preDelay = model.value (CVerbParameters::preDelay) * 0.001;
            const double buildUp = 0.004 + 0.05 * model.value (CVerbParameters::diffusion) * (0.3 + model.value (CVerbParameters::size));

            std::vector<float> buildDb ((size_t) p.getHeight());
            for (int y = 0; y < p.getHeight(); ++y)
            {
                auto t = ((double) (p.getHeight() - y) - 0.5) / p.getHeight() * timeRange - preDelay;
                buildDb[(size_t) y] = t <= 0.0 ? -200.0f : (float) (20.0 * std::log10 (juce::jmax (1.0e-6, 1.0 - std::exp (-t / buildUp))));
            }

            // Fading into the background as the tail dies away.
            std::array<juce::Colour, 256> colours;
            for (int i = 0; i < 256; ++i)
            {
                auto level = (float) i / 255.0f;
                colours[(size_t) i] = plotBackground.interpolatedWith (heat (level), 0.35f + 0.65f * std::sqrt (level));
            }

            for (int x = 0; x < p.getWidth(); ++x)
            {
                const double hz = xToHz ((float) p.getX() + (float) x + 0.5f);
                const double cuts = ReverbEngine::getCutsDb (model.getSettings(), hz, false, model.getEngine().getSampleRate()) + ReverbEngine::getCutsDb (model.getSettings(), hz, true, model.getEngine().getSampleRate());
                const double dbPerSecond = frozen ? 0.0 : 60.0 / juce::jmax (0.05, ReverbEngine::getDecayAt (model.getSettings(), hz));

                for (int y = 0; y < p.getHeight(); ++y)
                {
                    const double t = ((double) (p.getHeight() - y) - 0.5) / p.getHeight() * timeRange - preDelay;
                    if (t <= 0.0)
                        continue;

                    const double db = cuts - dbPerSecond * t + buildDb[(size_t) y];
                    if (db <= -60.0)
                        continue;

                    auto index = (size_t) juce::jlimit (0, 255, (int) ((1.0 + db / 60.0) * 255.0));
                    pixels.setPixelColour (x, y, colours[index]);
                }
            }
        }

        //==============================================================================
        void resized() override
        {
            auto p = plotArea();
            columnHz.resize ((size_t) juce::jmax (1, (int) p.getWidth()));
            for (size_t c = 0; c < columnHz.size(); ++c)
                columnHz[c] = xToHz (p.getX() + (float) c + 0.5f);

            lastKey = {};
            image = {};
        }

        KnobParameter& cutParam (int i) const
        {
            static constexpr CVerbParameters::ID ids[4] { CVerbParameters::preLowCut, CVerbParameters::preHighCut,
                                                          CVerbParameters::postLowCut, CVerbParameters::postHighCut };
            return *model.parameter (ids[i]);
        }

        static bool isPost (int i)      { return i >= 2; }
        static bool isLowCut (int i)    { return (i % 2) == 0; }

        bool isOff (int i) const
        {
            auto v = cutParam (i).getValue();
            return isLowCut (i) ? v <= ReverbEngine::lowCutOff + 0.5f : v >= ReverbEngine::highCutOff - 1.0f;
        }

        juce::Point<float> handle (int i) const
        {
            return { hzToX (cutParam (i).getValue()), cutDbToY (-3.0) };   // on the curve at the cutoff
        }

        int handleAt (juce::Point<float> pos) const
        {
            for (int i = 3; i >= 0; --i)
                if (pos.getDistanceFrom (handle (i)) < 10.0f)
                    return i;
            return -1;
        }

        //==============================================================================
        void paint (juce::Graphics& g) override
        {
            g.fillAll (Palette::panelBackground);
            drawPanel (g, getLocalBounds().toFloat());
            auto p = plotArea();
            g.setColour (plotBackground);
            g.fillRect (p);

            if (image.isNull())
                renderImage();
            g.drawImage (image, p);

            // The reverb's output spectrum, faintly, over the decay map.
            if (! spectrum.levels.empty())
            {
                juce::Path fill, line;
                fill.startNewSubPath (p.getX(), p.getBottom());
                for (size_t c = 0; c < spectrum.levels.size(); ++c)
                {
                    auto x = p.getX() + (float) c;
                    auto y = spectrumDbToY (spectrum.levels[c]);
                    fill.lineTo (x, y);
                    if (c == 0) line.startNewSubPath (x, y);
                    else        line.lineTo (x, y);
                }
                fill.lineTo (p.getRight(), p.getBottom());
                fill.closeSubPath();

                g.setColour (spectrumColour.withAlpha (0.12f));
                g.fillPath (fill);
                g.setColour (spectrumColour.withAlpha (0.3f));
                g.strokePath (line, juce::PathStrokeType (1.0f));
            }


            // Grid: frequencies along the bottom, time up the left, the cuts' dB on the right.
            g.setFont (juce::FontOptions (9.5f));
            for (double hz : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
            {
                auto x = hzToX (hz);
                g.setColour (juce::Colours::white.withAlpha (0.06f));
                g.drawVerticalLine ((int) x, p.getY(), p.getBottom());
                g.setColour (Palette::textDisabled);
                g.drawText (hz >= 1000.0 ? juce::String ((int) (hz / 1000.0)) + "k" : juce::String ((int) hz),
                            juce::Rectangle<float> (x - 16.0f, p.getBottom() + 3.0f, 32.0f, 12.0f), juce::Justification::centred);
            }

            const double step = timeRange <= 1.0 ? 0.1 : timeRange <= 2.0 ? 0.25 : timeRange <= 4.0 ? 0.5 : timeRange <= 8.0 ? 1.0 : timeRange <= 16.0 ? 2.0 : 4.0;
            for (double t = 0.0; t <= timeRange + 1.0e-6; t += step)
            {
                auto y = timeToY (t);
                g.setColour (juce::Colours::white.withAlpha (0.05f));
                g.drawHorizontalLine ((int) y, p.getX(), p.getRight());
                g.setColour (Palette::textDisabled);
                auto label = juce::Rectangle<float> (2.0f, y - 6.0f, p.getX() - 6.0f, 12.0f);
                label = label.withY (juce::jlimit (p.getY() - 2.0f, p.getBottom() - 10.0f, label.getY()));
                g.drawText (step < 1.0 ? juce::String (t, step < 0.25 ? 1 : 2) + " s" : juce::String ((int) t) + " s",
                            label, juce::Justification::centredRight);
            }

            for (double db : { 0.0, -12.0, -24.0 })
            {
                g.setColour (Palette::textDisabled.withAlpha (0.8f));
                g.drawText (juce::String ((int) db) + " dB", juce::Rectangle<float> (p.getRight() + 3.0f, cutDbToY (db) - 6.0f, 38.0f, 12.0f),
                            juce::Justification::centredLeft);
            }

            g.saveState();
            g.reduceClipRegion (p.toNearestInt());

            const double preDelay = model.value (CVerbParameters::preDelay) * 0.001;

            // Early reflections: ticks up the left edge at their times.
            for (auto& tap : ReverbEngine::getEarlyTaps (model.getSettings()))
            {
                auto y = timeToY (preDelay + tap.seconds);
                g.setColour (juce::Colours::white.withAlpha (juce::jlimit (0.1f, 0.9f, tap.gain * 0.9f)));
                g.fillRect (p.getX(), y - 0.5f, 8.0f + 24.0f * tap.gain, 1.5f);
            }

            // Pre-delay: where the tail starts.
            if (preDelay > 0.0)
            {
                juce::Path line;
                line.startNewSubPath (p.getX(), timeToY (preDelay));
                line.lineTo (p.getRight(), timeToY (preDelay));
                const float dashes[] { 3.0f, 3.0f };
                juce::PathStrokeType (1.0f).createDashedStroke (line, line, dashes, 2);
                g.setColour (Palette::text.withAlpha (0.4f));
                g.fillPath (line);
            }

            // RT60 line: how long each frequency rings.
            if (! model.value (CVerbParameters::freeze) >= 0.5f)
            {
                juce::Path rt;
                for (float x = p.getX(); x <= p.getRight(); x += 2.0f)
                {
                    auto y = timeToY (preDelay + ReverbEngine::getDecayAt (model.getSettings(), xToHz (x)));
                    if (x == p.getX()) rt.startNewSubPath (x, y);
                    else               rt.lineTo (x, y);
                }

                const bool active = dragging == Drag::decay || (hoverHandle < 0 && hovering);
                g.setColour (juce::Colours::white.withAlpha (active ? 0.95f : 0.7f));
                g.strokePath (rt, juce::PathStrokeType (active ? 2.0f : 1.5f));
            }

            // The cuts on their own dB scale: pre (into the reverb) dashed, post (out of it) solid.
            for (int post = 0; post < 2; ++post)
            {
                juce::Path curve;
                for (float x = p.getX(); x <= p.getRight(); x += 1.5f)
                {
                    auto y = cutDbToY (ReverbEngine::getCutsDb (model.getSettings(), xToHz (x), post == 1, model.getEngine().getSampleRate()));
                    if (x == p.getX()) curve.startNewSubPath (x, y);
                    else               curve.lineTo (x, y);
                }

                if (post == 1)
                {
                    g.setColour (wetColour);
                    g.strokePath (curve, juce::PathStrokeType (1.8f));
                }
                else
                {
                    const float dashes[] { 4.0f, 3.0f };
                    juce::PathStrokeType (1.4f).createDashedStroke (curve, curve, dashes, 2);
                    g.setColour (preColour.withAlpha (0.9f));
                    g.fillPath (curve);
                }
            }

            g.restoreState();

            // Handles.
            static const char* names[4] { "Pre low cut", "Pre high cut", "Post low cut", "Post high cut" };
            for (int i = 0; i < 4; ++i)
            {
                auto pos = handle (i);
                auto colour = isPost (i) ? wetColour : preColour;
                const bool active = i == hoverHandle || (dragging == Drag::cut && i == dragHandle);
                auto r = juce::Rectangle<float> (active ? 14.0f : 11.0f, active ? 14.0f : 11.0f).withCentre (pos);

                g.setColour (isOff (i) ? juce::Colour (0xff15171b) : colour);
                g.fillEllipse (r);
                g.setColour (colour.withAlpha (isOff (i) ? 0.6f : 1.0f));
                g.drawEllipse (r, 1.4f);
                g.setColour (isOff (i) ? colour : juce::Colours::black.withAlpha (0.8f));
                g.setFont (juce::FontOptions (8.5f, juce::Font::bold));
                g.drawText (isLowCut (i) ? "L" : "H", r, juce::Justification::centred);

                if (active)
                    drawBubble (g, juce::String (names[i]) + ": " + cutParam (i).getCurrentText(), pos.translated (0.0f, -20.0f));
            }

            // Under the mouse: frequency and how long it rings.
            if (hoverHandle < 0 && hovering && p.contains (mouse))
            {
                auto hz = xToHz (mouse.x);
                drawBubble (g, formatHz (hz) + "  -  RT60 " + juce::String (ReverbEngine::getDecayAt (model.getSettings(), hz), 2) + " s", mouse.translated (0.0f, -18.0f));
            }

            // Title and legend.
            auto top = getLocalBounds().toFloat().removeFromTop (22.0f).reduced (10.0f, 0.0f);
            g.setColour (Palette::textDim);
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.drawText ("DECAY & FILTERS", top, juce::Justification::centredLeft);

            g.setFont (juce::FontOptions (10.5f));
            auto legend = top.removeFromRight (380.0f);
            for (auto [text, colour] : { std::pair ("RT60", juce::Colours::white), std::pair ("- - pre cuts", preColour),
                                         std::pair ("--- post cuts", wetColour), std::pair ("spectrum", spectrumColour) })
            {
                g.setColour (colour);
                g.drawText (text, legend.removeFromLeft (95.0f), juce::Justification::centredRight);
            }
        }

        void drawBubble (juce::Graphics& g, const juce::String& text, juce::Point<float> at)
        {
            auto p = plotArea();
            g.setFont (juce::FontOptions (11.0f));
            auto w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text) + 16.0f;
            auto box = juce::Rectangle<float> (w, 20.0f).withCentre (at);
            box = box.withX (juce::jlimit (p.getX(), p.getRight() - w, box.getX())).withY (juce::jmax (p.getY(), box.getY()));
            g.setColour (juce::Colour (0xee101215));
            g.fillRoundedRectangle (box, 4.0f);
            g.setColour (Palette::text);
            g.drawText (text, box, juce::Justification::centred);
        }

        //==============================================================================
        void mouseMove (const juce::MouseEvent& e) override
        {
            mouse = e.position;
            hovering = true;
            hoverHandle = handleAt (e.position);
            setMouseCursor (hoverHandle >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::UpDownResizeCursor);
            repaint();
        }

        void mouseExit (const juce::MouseEvent&) override
        {
            hovering = false;
            hoverHandle = -1;
            repaint();
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            dragHandle = handleAt (e.position);

            if (dragHandle >= 0)
            {
                dragging = Drag::cut;
                model.beginUndoTransaction ("Reverb filter");
                cutParam (dragHandle).beginGesture();
            }
            else if (plotArea().contains (e.position))
            {
                dragging = Drag::decay;
                model.beginUndoTransaction ("Reverb decay");
                model.parameter (CVerbParameters::decay)->beginGesture();
                mouseDrag (e);
            }
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            mouse = e.position;

            if (dragging == Drag::cut)
            {
                auto range = cutParam (dragHandle).getRange();
                cutParam (dragHandle).setValue ((float) juce::jlimit ((double) range.getStart(), (double) range.getEnd(), xToHz (e.position.x)));
            }
            else if (dragging == Drag::decay)
            {
                // The RT60 line at the mouse's frequency follows the mouse up and down.
                auto t = yToTime (e.position.y) - model.value (CVerbParameters::preDelay) * 0.001;
                auto ratio = ReverbEngine::getDecayAt (model.getSettings(), xToHz (e.position.x)) / juce::jmax (0.05f, model.value (CVerbParameters::decay));
                model.parameter (CVerbParameters::decay)->setValue ((float) juce::jlimit (0.2, 20.0, t / juce::jmax (0.1, ratio)));
            }

            refreshIfChanged();
            repaint();
        }

        void mouseUp (const juce::MouseEvent&) override
        {
            if (dragging == Drag::cut)
                cutParam (dragHandle).endGesture();
            else if (dragging == Drag::decay)
                model.parameter (CVerbParameters::decay)->endGesture();

            dragging = Drag::none;
            lastKey = {};
            refreshIfChanged();
        }

        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            // Double-click a handle: switch that cut off.
            auto h = handleAt (e.position);
            if (h >= 0)
                cutParam (h).setValue (isLowCut (h) ? ReverbEngine::lowCutOff : ReverbEngine::highCutOff);
        }

        enum class Drag { none, decay, cut };

        CVerbModel& model;
        SpectrumAnalyser spectrum;
        std::vector<double> columnHz;
        juce::Image image;
        juce::String lastKey;
        double timeRange = 4.0;
        Drag dragging = Drag::none;
        int hoverHandle = -1, dragHandle = -1;
        bool hovering = false;
        juce::Point<float> mouse;
    };

    //==============================================================================
    class CVerbView::TailScope : public juce::Component
    {
    public:
        TailScope()
        {
            frames.assign (360, { -100.0f, -100.0f });
        }

        void push (float dryDb, float wetDb)
        {
            frames[(size_t) write] = { dryDb, wetDb };
            write = (write + 1) % (int) frames.size();
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            drawPanel (g, getLocalBounds().toFloat());
            auto p = getLocalBounds().toFloat().reduced (8.0f, 6.0f).withTrimmedLeft (52.0f).withTrimmedRight (26.0f);
            auto y = [&] (float db) { return juce::jmap (juce::jlimit (-60.0f, 0.0f, db), -60.0f, 0.0f, p.getBottom(), p.getY()); };

            g.setFont (juce::FontOptions (9.0f));
            for (float db : { 0.0f, -20.0f, -40.0f })
            {
                g.setColour (juce::Colours::white.withAlpha (0.06f));
                g.drawHorizontalLine ((int) y (db), p.getX(), p.getRight());
                g.setColour (Palette::textDisabled);
                g.drawText (juce::String ((int) db), juce::Rectangle<float> (p.getRight() + 3.0f, y (db) - 5.0f, 24.0f, 10.0f), juce::Justification::centredLeft);
            }

            const auto count = (int) frames.size();
            const float step = p.getWidth() / (float) (count - 1);
            juce::Path dry, wet, wetLine;
            dry.startNewSubPath (p.getX(), p.getBottom());
            wet.startNewSubPath (p.getX(), p.getBottom());

            for (int i = 0; i < count; ++i)
            {
                auto& f = frames[(size_t) ((write + i) % count)];
                auto x = p.getX() + (float) i * step;
                dry.lineTo (x, y (f.dry));
                wet.lineTo (x, y (f.wet));
                if (i == 0) wetLine.startNewSubPath (x, y (f.wet));
                else        wetLine.lineTo (x, y (f.wet));
            }

            for (auto* path : { &dry, &wet })
            {
                path->lineTo (p.getRight(), p.getBottom());
                path->closeSubPath();
            }

            g.saveState();
            g.reduceClipRegion (p.toNearestInt());
            g.setColour (dryColour.withAlpha (0.28f));
            g.fillPath (dry);
            g.setGradientFill (juce::ColourGradient::vertical (wetColour.withAlpha (0.55f), p.getY(), wetColour.withAlpha (0.1f), p.getBottom()));
            g.fillPath (wet);
            g.setColour (wetColour);
            g.strokePath (wetLine, juce::PathStrokeType (1.3f));

            // The dry signal's outline on top, so it stays visible when the reverb is as loud.
            g.setColour (dryColour.withAlpha (0.75f));
            g.strokePath (dry, juce::PathStrokeType (1.0f));
            g.restoreState();

            auto label = getLocalBounds().toFloat().reduced (10.0f, 6.0f).removeFromLeft (48.0f);
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.setColour (Palette::textDim);
            g.drawText ("TAIL", label.removeFromTop (16.0f), juce::Justification::centredLeft);
            g.setFont (juce::FontOptions (10.5f));
            g.setColour (dryColour);
            g.drawText ("dry", label.removeFromTop (14.0f), juce::Justification::centredLeft);
            g.setColour (wetColour);
            g.drawText ("reverb", label.removeFromTop (14.0f), juce::Justification::centredLeft);
        }

        struct Frame { float dry, wet; };
        std::vector<Frame> frames;
        int write = 0;
    };

    //==============================================================================
    CVerbView::CVerbView (CVerbModel& m) : model (m)
    {
        setOpaque (true);

        spaceView = std::make_unique<SpaceView> (model);
        tailScope = std::make_unique<TailScope>();
        addAndMakeVisible (*spaceView);
        addAndMakeVisible (*tailScope);

        auto add = [this] (CVerbParameters::ID id, const char* label, juce::Colour c, float mid)
        {
            auto* k = knobs.add (new ParamKnob (model.parameter (id), label, c, CVerbParameters::get (id).defaultValue, mid));
            addAndMakeVisible (k);
        };

        using P = CVerbParameters::ID;
        const auto space = Palette::tempo, tone = Palette::automation, filters = Palette::accent, output = Palette::marker;
        add (P::preDelay, "Pre-delay", space, 40.0f);
        add (P::size, "Size", space, 0.0f);
        add (P::decay, "Decay", space, 2.5f);
        add (P::diffusion, "Diffusion", space, 0.0f);
        add (P::modulation, "Modulation", space, 0.0f);
        add (P::lowDecay, "Low decay", tone, 1.0f);
        add (P::highDecay, "High decay", tone, 0.0f);
        add (P::dampFreq, "Damping", tone, 4000.0f);
        add (P::preLowCut, "Pre low", preColour, 150.0f);
        add (P::preHighCut, "Pre high", preColour, 6000.0f);
        add (P::postLowCut, "Post low", filters, 150.0f);
        add (P::postHighCut, "Post high", filters, 6000.0f);
        add (P::early, "Early", output, 0.0f);
        add (P::width, "Width", output, 0.0f);
        add (P::mix, "Mix", output, 0.0f);

        groups = { { "SPACE", 0, 5, {} }, { "TONE", 5, 3, {} }, { "FILTERS", 8, 4, {} }, { "OUTPUT", 12, 3, {} } };

        freezeButton.setClickingTogglesState (false);
        freezeButton.setTooltip ("Hold the tail forever (new sound stops going in)");
        freezeButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff4fa3ff));
        freezeButton.onClick = [this]
        {
            auto freeze = model.parameter (CVerbParameters::freeze);
            freeze->beginGesture();
            freeze->setValue (freeze->getValue() >= 0.5f ? 0.0f : 1.0f);
            freeze->endGesture();
        };
        addAndMakeVisible (freezeButton);

        setSize (defaultWidth, defaultHeight);
        startTimerHz (60);
    }

    CVerbView::~CVerbView()
    {
        stopTimer();
        spaceView->setActive (false);
    }

    void CVerbView::visibilityChanged()
    {
        spaceView->setActive (isShowing());
    }

    void CVerbView::tick (float dry, float wet)
    {
        tailScope->push (toDb (dry), toDb (wet));
    }

    void CVerbView::updateSpectrum()
    {
        spaceView->updateSpectrum();
    }

    void CVerbView::timerCallback()
    {
        auto& engine = model.getEngine();
        auto dry = engine.dryPeak.take(), wet = engine.wetPeak.take();

        // Skip display ticks that fell between audio blocks (rather than draw a drop to silence).
        if (dry > 0.0f || wet > 0.0f || ++emptyTicks > 3)
        {
            emptyTicks = (dry > 0.0f || wet > 0.0f) ? 0 : emptyTicks;
            tick (dry, wet);
        }

        for (auto* k : knobs)
            k->refresh();

        freezeButton.setToggleState (model.value (CVerbParameters::freeze) >= 0.5f, juce::dontSendNotification);
        spaceView->refreshIfChanged();

        // The spectrum at half the rate is plenty.
        if (isShowing() && (frame++ & 1) == 0)
        {
            spaceView->setActive (true);
            spaceView->updateSpectrum();
        }

        // The summary in the header follows the knobs.
        repaint (getLocalBounds().removeFromTop (headerHeight));
    }

    void CVerbView::paint (juce::Graphics& g)
    {
        g.fillAll (Palette::panelBackground);

        auto header = getLocalBounds().removeFromTop (headerHeight).reduced (12, 0);
        g.setColour (Palette::text);
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.drawText ("C-VERB", header, juce::Justification::centredLeft);

        g.setColour (Palette::textDim);
        g.setFont (juce::FontOptions (12.0f));
        auto text = [this] (CVerbParameters::ID id) { return model.parameter (id)->getCurrentText(); };
        auto summary = text (CVerbParameters::decay) + "  -  pre-delay " + text (CVerbParameters::preDelay)
                       + "  -  size " + text (CVerbParameters::size) + "  -  mix " + text (CVerbParameters::mix);
        g.drawText (summary, header.withTrimmedLeft (90).withTrimmedRight (120), juce::Justification::centredLeft);

        // Knob groups: a title and a faint panel behind each.
        for (auto& group : groups)
        {
            auto r = group.area.toFloat();
            g.setColour (Palette::panelRaised.withAlpha (0.45f));
            g.fillRoundedRectangle (r, 5.0f);
            g.setColour (Palette::textDim);
            g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
            g.drawText (group.title, r.removeFromTop (18.0f).reduced (8.0f, 0.0f), juce::Justification::centredLeft);
        }
    }

    void CVerbView::resized()
    {
        auto r = getLocalBounds();
        auto header = r.removeFromTop (headerHeight).reduced (10, 5);
        freezeButton.setBounds (header.removeFromRight (90));

        auto knobArea = r.removeFromBottom (knobsHeight).reduced (8, 6);
        const int gap = 8;
        const int totalKnobs = knobs.size();
        const int knobWidth = (knobArea.getWidth() - gap * ((int) groups.size() - 1)) / totalKnobs;

        for (auto& group : groups)
        {
            group.area = knobArea.removeFromLeft (knobWidth * group.count);
            knobArea.removeFromLeft (gap);

            auto inner = group.area.withTrimmedTop (18).reduced (0, 2);
            for (int i = 0; i < group.count; ++i)
                knobs[group.first + i]->setBounds (inner.removeFromLeft (knobWidth).reduced (2, 0));
        }

        r.reduce (8, 0);
        tailScope->setBounds (r.removeFromBottom (scopeHeight).withTrimmedTop (6));
        spaceView->setBounds (r);
    }
}
