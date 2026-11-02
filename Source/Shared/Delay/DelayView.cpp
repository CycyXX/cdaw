#include "DelayView.h"
#include "../DSP/TempoSync.h"
#include "../../UI/LookAndFeel/Palette.h"

namespace cdaw
{
    namespace
    {
        using P = DelayParameters::ID;

        const juce::Colour echoColour { 0xff4fd6c8 };       // a fresh repeat
        const juce::Colour darkEchoColour { 0xff3b55a8 };   // after many passes through the cuts
        const juce::Colour inputColour { 0xff9aa6b8 };
        const juce::Colour duckColour { 0xfff2b134 };
        const juce::Colour feedbackColour { 0xffd77cff };

        constexpr float floorDb = -60.0f;
        constexpr int ticksPerSecond = 60;
        constexpr double longestRange = 16.0;
        constexpr int historyLength = (int) longestRange * ticksPerSecond;

        float toDb (float gain)     { return juce::Decibels::gainToDecibels (gain, -100.0f); }

        DelayEngine::Settings settingsOf (EffectModel& m)
        {
            return DelayParameters::toSettings ([&m] (int id) { return m.value (id); });
        }

        /** The colour of a repeat after `passes` trips through the loop: duller as its highs go. */
        juce::Colour colourFor (const DelayEngine::Settings& s, int passes, double sampleRate)
        {
            const double highLoss = (DelayEngine::getLoopFilterDb (s, 1000.0, sampleRate) - DelayEngine::getLoopFilterDb (s, 6000.0, sampleRate)) * passes;
            return echoColour.interpolatedWith (darkEchoColour, (float) juce::jlimit (0.0, 1.0, highLoss / 24.0));
        }

        juce::String timeText (double seconds)
        {
            return seconds < 1.0 ? juce::String (juce::roundToInt (seconds * 1000.0)) + " ms" : juce::String (seconds, seconds < 10.0 ? 2 : 1) + " s";
        }
    }

    //==============================================================================
    class DelayView::EchoMap : public juce::Component
    {
    public:
        EchoMap (EffectModel& m, DelayEngine& e) : model (m), engine (e)
        {
            setOpaque (true);
            history.assign ((size_t) historyLength, Frame {});
        }

        struct Frame { float in[2] { -100.0f, -100.0f }; };

        void push (float inL, float inR, float duckDb)
        {
            history[(size_t) write] = { { toDb (inL), toDb (inR) } };
            write = (write + 1) % historyLength;
            duck = duckDb >= duck ? duckDb : juce::jmax (duckDb, duck - 0.6f);
            repaint();
        }

        //==============================================================================
        juce::Rectangle<float> plotArea() const
        {
            return getLocalBounds().toFloat().withTrimmedLeft (30.0f).withTrimmedRight (50.0f).withTrimmedTop (28.0f).withTrimmedBottom (20.0f);
        }

        float halfHeight() const                { return plotArea().getHeight() * 0.5f - 6.0f; }
        float timeToX (double t) const          { auto p = plotArea(); return p.getX() + (float) (t / range) * p.getWidth(); }
        double xToTime (float x) const          { auto p = plotArea(); return juce::jmax (0.0, (double) (x - p.getX()) / p.getWidth() * range); }
        float offsetFor (float db) const        { return juce::jlimit (0.0f, 1.0f, (db - floorDb) / -floorDb) * halfHeight(); }
        float dbForOffset (float offset) const  { return floorDb + offset / juce::jmax (1.0f, halfHeight()) * -floorDb; }

        float yFor (int channel, float db) const
        {
            auto centre = plotArea().getCentreY();
            return channel == 1 ? centre + offsetFor (db) : centre - offsetFor (db);
        }

        /** The input's level (0..1) a while ago, on one side (2 = the louder). With `spread`, the
            loudest of the frames either side too (a hit can fall between two display frames). */
        float inputAt (double age, int channel, int spread = 0) const
        {
            const int centre = juce::roundToInt (age * ticksPerSecond);
            float db = -100.0f;

            for (int framesAgo = centre - spread; framesAgo <= centre + spread; ++framesAgo)
            {
                if (framesAgo < 0 || framesAgo >= historyLength - 1)
                    continue;

                auto& f = history[(size_t) ((write - 1 - framesAgo + historyLength * 2) % historyLength)];
                db = juce::jmax (db, channel == 2 ? juce::jmax (f.in[0], f.in[1]) : f.in[channel]);
            }

            return juce::jlimit (0.0f, 1.0f, (db - floorDb) / -floorDb);
        }

        void chooseRange (const std::vector<DelayEngine::Echo>& echoes, double firstTime)
        {
            if (dragging != Drag::none)
                return;

            const double longest = echoes.empty() ? firstTime : echoes.back().seconds;
            const double wanted = juce::jmax (longest * 1.08, firstTime * 4.5, 0.3);
            for (double r : { 0.25, 0.5, 0.75, 1.0, 1.5, 2.0, 3.0, 4.0, 6.0, 8.0, 12.0, longestRange })
            {
                range = r;
                if (r >= wanted)
                    break;
            }
        }

        //==============================================================================
        struct Handles { juce::Point<float> time, feedback; int feedbackChannel = 0; };

        Handles getHandles (const DelayEngine::Settings& s, const std::vector<DelayEngine::Echo>& echoes) const
        {
            Handles h;
            const double bpm = model.getBpm(), rate = engine.getSampleRate();
            const double passDb = DelayEngine::getLoopFilterDb (s, 1000.0, rate);
            const double left = DelayEngine::getTimeSeconds (s, bpm, 0);

            h.time = { timeToX (left), yFor (0, (float) passDb) };

            // The second pass: on the left again (stereo) or on the right (ping-pong).
            const bool pingPong = s.mode == DelayEngine::Mode::pingPong;
            const double secondTime = pingPong ? left + DelayEngine::getTimeSeconds (s, bpm, 1) : 2.0 * left;
            h.feedbackChannel = pingPong ? 1 : 0;
            float secondDb = floorDb;

            for (auto& e : echoes)
                if (e.passes == 2)
                {
                    secondDb = e.gainDb;
                    break;
                }

            h.feedback = { timeToX (secondTime), yFor (h.feedbackChannel, secondDb) };
            return h;
        }

        //==============================================================================
        void paint (juce::Graphics& g) override
        {
            const auto s = settingsOf (model);
            const double bpm = model.getBpm(), rate = engine.getSampleRate();
            const auto echoes = DelayEngine::getEchoes (s, bpm, rate, floorDb);
            const double firstTime = DelayEngine::getTimeSeconds (s, bpm, 0);
            chooseRange (echoes, firstTime);

            g.fillAll (Palette::panelBackground);
            EffectUI::drawPanel (g, getLocalBounds().toFloat(), "ECHOES");

            auto p = plotArea();
            const auto centre = p.getCentreY();
            g.setColour (EffectUI::plotBackground);
            g.fillRect (p);

            drawGrid (g, p, s, bpm);

            g.saveState();
            g.reduceClipRegion (p.toNearestInt());
            drawInput (g, p);
            drawEchoes (g, p, s, echoes, rate);
            g.restoreState();

            // Centre line and the side labels.
            g.setColour (Palette::panelOutline);
            g.drawHorizontalLine ((int) centre, p.getX(), p.getRight());
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.setColour (Palette::textDim);
            g.drawText ("L", juce::Rectangle<float> (p.getX() - 26.0f, centre - halfHeight() * 0.5f - 7.0f, 20.0f, 14.0f), juce::Justification::centred);
            g.drawText ("R", juce::Rectangle<float> (p.getX() - 26.0f, centre + halfHeight() * 0.5f - 7.0f, 20.0f, 14.0f), juce::Justification::centred);

            // Handles: time on the first echo, feedback on the second.
            auto handles = getHandles (s, echoes);
            EffectUI::drawHandle (g, handles.time, Palette::tempo, hover == Drag::time || dragging == Drag::time, "T");
            EffectUI::drawHandle (g, handles.feedback, feedbackColour, hover == Drag::feedback || dragging == Drag::feedback, "F");

            drawDuckMeter (g, s);

            // What the settings add up to.
            int repeats = 0;
            for (auto& e : echoes)
                repeats = juce::jmax (repeats, e.passes);

            juce::String info;
            if (s.feedback >= 0.999f)
                info = "repeats forever";
            else if (! echoes.empty())
                info = juce::String (repeats) + (repeats == 1 ? " repeat" : " repeats") + "  -  60 dB down after " + timeText (echoes.back().seconds);

            g.setFont (juce::FontOptions (11.0f));
            g.setColour (Palette::textDim);
            g.drawText (info, getLocalBounds().toFloat().reduced (10.0f, 6.0f).removeFromTop (14.0f).withTrimmedRight (40.0f), juce::Justification::centredRight);
        }

        void drawGrid (juce::Graphics& g, juce::Rectangle<float> p, const DelayEngine::Settings& s, double bpm)
        {
            // Level guides, mirrored.
            for (float db : { -12.0f, -24.0f, -36.0f, -48.0f })
                for (int side = 0; side < 2; ++side)
                {
                    g.setColour (juce::Colours::white.withAlpha (0.04f));
                    g.drawHorizontalLine ((int) yFor (side, db), p.getX(), p.getRight());
                }

            // Beats and bars when synced.
            if (s.sync)
            {
                const double beat = 60.0 / juce::jlimit (20.0, 999.0, bpm);
                const bool tooDense = range / beat > 48.0;
                for (int i = 1; i * beat <= range; ++i)
                {
                    const bool bar = i % 4 == 0;
                    if (tooDense && ! bar)
                        continue;

                    g.setColour (Palette::tempo.withAlpha (bar ? 0.22f : 0.09f));
                    g.drawVerticalLine ((int) timeToX (i * beat), p.getY(), p.getBottom());
                }
            }

            // Seconds along the bottom.
            double step = 0.05;
            for (double candidate : { 0.05, 0.1, 0.25, 0.5, 1.0, 2.0 })
            {
                step = candidate;
                if (range / candidate <= 10.0)
                    break;
            }

            g.setFont (juce::FontOptions (9.5f));
            for (int i = 0; i * step <= range + 1.0e-6; ++i)
            {
                const auto x = timeToX (i * step);
                g.setColour (juce::Colours::white.withAlpha (0.05f));
                g.drawVerticalLine ((int) x, p.getY(), p.getBottom());
                g.setColour (Palette::textDisabled);
                g.drawText (i == 0 ? juce::String ("0") : timeText (i * step), juce::Rectangle<float> (x - 26.0f, p.getBottom() + 3.0f, 52.0f, 12.0f),
                            juce::Justification::centred);
            }
        }

        /** What has come in, by age: the conveyor the echoes are taken from. */
        void drawInput (juce::Graphics& g, juce::Rectangle<float> p)
        {
            const auto centre = p.getCentreY();
            const int frames = juce::jmin (historyLength - 1, (int) std::ceil (range * ticksPerSecond));

            for (int side = 0; side < 2; ++side)
            {
                juce::Path area;
                area.startNewSubPath (p.getX(), centre);

                for (int i = 0; i <= frames; ++i)
                {
                    const double age = (double) i / ticksPerSecond;
                    const float level = inputAt (age, side) * halfHeight();
                    area.lineTo (timeToX (age), side == 0 ? centre - level : centre + level);
                }

                area.lineTo (timeToX ((double) frames / ticksPerSecond), centre);
                area.closeSubPath();

                const auto edge = side == 0 ? p.getY() : p.getBottom();
                g.setGradientFill (juce::ColourGradient::vertical (inputColour.withAlpha (0.32f), edge, inputColour.withAlpha (0.08f), centre));
                g.fillPath (area);
            }
        }

        void drawEchoes (juce::Graphics& g, juce::Rectangle<float> p, const DelayEngine::Settings& s,
                         const std::vector<DelayEngine::Echo>& echoes, double rate)
        {
            const auto centre = p.getCentreY();
            const float barWidth = juce::jlimit (3.0f, 8.0f, p.getWidth() / (float) juce::jmax (1, (int) echoes.size()) * 0.35f);

            // The fade of each side, through the tops of its echoes.
            for (int side = 0; side < 2; ++side)
            {
                juce::Path envelope;
                bool started = false;

                for (auto& e : echoes)
                {
                    if (e.channel != side && e.channel != 2)
                        continue;

                    juce::Point<float> top { timeToX (e.seconds), yFor (side, e.gainDb) };
                    if (! started) envelope.startNewSubPath (top);
                    else           envelope.lineTo (top);
                    started = true;
                }

                if (started)
                {
                    const float dashes[] { 3.0f, 4.0f };
                    juce::PathStrokeType (1.0f).createDashedStroke (envelope, envelope, dashes, 2);
                    g.setColour (echoColour.withAlpha (0.35f));
                    g.fillPath (envelope);
                }
            }

            for (auto& e : echoes)
            {
                const auto x = timeToX (e.seconds);
                const auto height = offsetFor (e.gainDb);
                const auto colour = colourFor (s, e.passes, rate);
                const float strength = juce::jlimit (0.0f, 1.0f, height / juce::jmax (1.0f, halfHeight()));

                for (int side = 0; side < 2; ++side)
                {
                    if (e.channel != side && e.channel != 2)
                        continue;

                    // Lit by whatever came in that long ago, as loud as this echo plays it back.
                    const float lit = inputAt (e.seconds, s.mode == DelayEngine::Mode::pingPong ? 2 : side, 1) * strength;
                    const auto tip = juce::Point<float> (x, side == 0 ? centre - height : centre + height);
                    EffectUI::drawGlow (g, tip, 10.0f + 26.0f * lit, colour.brighter (0.3f), lit * 1.4f);

                    auto bar = side == 0 ? juce::Rectangle<float>::leftTopRightBottom (x - barWidth * 0.5f, centre - height, x + barWidth * 0.5f, centre)
                                         : juce::Rectangle<float>::leftTopRightBottom (x - barWidth * 0.5f, centre, x + barWidth * 0.5f, centre + height);
                    g.setGradientFill (juce::ColourGradient::vertical (colour.withAlpha (0.95f), side == 0 ? bar.getY() : bar.getBottom(),
                                                                       colour.withAlpha (0.35f), centre));
                    g.fillRoundedRectangle (bar, juce::jmin (2.5f, barWidth * 0.5f));

                    if (lit > 0.05f)
                    {
                        g.setColour (juce::Colours::white.withAlpha (juce::jmin (0.9f, lit * 1.2f)));
                        g.fillEllipse (juce::Rectangle<float> (barWidth + 1.0f, barWidth + 1.0f).withCentre (tip));
                    }
                }
            }
        }

        void drawDuckMeter (juce::Graphics& g, const DelayEngine::Settings& s)
        {
            auto p = plotArea();
            auto meter = juce::Rectangle<float> (p.getRight() + 16.0f, p.getY() + 14.0f, 16.0f, p.getHeight() - 14.0f);
            const bool active = s.duck > 0.001f;

            g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
            g.setColour (active ? Palette::textDim : Palette::textDisabled);
            g.drawText ("DUCK", meter.withY (p.getY() - 2.0f).withHeight (12.0f).expanded (12.0f, 0.0f), juce::Justification::centred);

            g.setColour (EffectUI::plotBackground);
            g.fillRoundedRectangle (meter, 3.0f);

            if (active && duck > 0.05f)
            {
                auto fill = meter.withHeight (meter.getHeight() * juce::jlimit (0.0f, 1.0f, duck / 30.0f));
                g.setGradientFill (juce::ColourGradient::vertical (duckColour.withAlpha (0.6f), fill.getY(), duckColour, fill.getBottom()));
                g.fillRoundedRectangle (fill, 3.0f);
                g.setColour (duckColour);
                g.setFont (juce::FontOptions (9.5f));
                g.drawText ("-" + juce::String (duck, 0), meter.withY (fill.getBottom() + 2.0f).withHeight (12.0f).expanded (10.0f, 0.0f),
                            juce::Justification::centred);
            }
        }

        //==============================================================================
        enum class Drag { none, time, feedback };

        Drag hitTest (juce::Point<float> pos) const
        {
            const auto s = settingsOf (model);
            const auto handles = getHandles (s, DelayEngine::getEchoes (s, model.getBpm(), engine.getSampleRate(), floorDb));
            if (pos.getDistanceFrom (handles.time) < 12.0f)       return Drag::time;
            if (pos.getDistanceFrom (handles.feedback) < 12.0f)   return Drag::feedback;
            return Drag::none;
        }

        void mouseMove (const juce::MouseEvent& e) override
        {
            auto h = hitTest (e.position);
            if (h != hover)
            {
                hover = h;
                setMouseCursor (h == Drag::time ? juce::MouseCursor::LeftRightResizeCursor
                                                : h == Drag::feedback ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
                repaint();
            }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            dragging = hitTest (e.position);
            if (dragging == Drag::time)
            {
                model.beginUndoTransaction ("Delay time");
                model.parameter (model.isOn (P::sync) ? P::division : P::time)->beginGesture();
            }
            else if (dragging == Drag::feedback)
            {
                model.beginUndoTransaction ("Delay feedback");
                model.parameter (P::feedback)->beginGesture();
            }
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            const auto s = settingsOf (model);

            if (dragging == Drag::time)
            {
                const double seconds = juce::jlimit (0.001, DelayEngine::maxSeconds, xToTime (e.position.x));
                if (s.sync)
                    model.parameter (P::division)->setValue ((float) TempoSync::nearest (seconds, model.getBpm(), TempoSync::numDelayDivisions));
                else
                    model.parameter (P::time)->setValue ((float) juce::jlimit (1.0, 2000.0, seconds * 1000.0));
            }
            else if (dragging == Drag::feedback)
            {
                const auto centre = plotArea().getCentreY();
                const float offset = std::abs (e.position.y - centre);
                const double passDb = DelayEngine::getLoopFilterDb (s, 1000.0, engine.getSampleRate());
                const double feedbackDb = dbForOffset (offset) - 2.0 * passDb;
                const float feedback = offset < 2.0f ? 0.0f : (float) juce::jlimit (0.0, 1.0, std::pow (10.0, feedbackDb / 20.0));
                model.parameter (P::feedback)->setValue (feedback);
            }

            repaint();
        }

        void mouseUp (const juce::MouseEvent&) override
        {
            if (dragging == Drag::time)
                model.parameter (model.isOn (P::sync) ? P::division : P::time)->endGesture();
            else if (dragging == Drag::feedback)
                model.parameter (P::feedback)->endGesture();

            dragging = Drag::none;
            repaint();
        }

        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            auto h = hitTest (e.position);
            if (h == Drag::time)
            {
                auto id = model.isOn (P::sync) ? P::division : P::time;
                model.setValue (id, DelayParameters::get (id).defaultValue, "Reset delay time");
            }
            else if (h == Drag::feedback)
            {
                model.setValue (P::feedback, DelayParameters::get (P::feedback).defaultValue, "Reset delay feedback");
            }
        }

        EffectModel& model;
        DelayEngine& engine;
        std::vector<Frame> history;
        int write = 0;
        float duck = 0.0f;
        double range = 2.0;
        Drag dragging = Drag::none, hover = Drag::none;
    };

    //==============================================================================
    class DelayView::RepeatsView : public juce::Component
    {
    public:
        RepeatsView (EffectModel& m, DelayEngine& e) : model (m), engine (e)
        {
            setOpaque (true);
        }

        juce::Rectangle<float> plotArea() const
        {
            return getLocalBounds().toFloat().withTrimmedLeft (34.0f).withTrimmedRight (12.0f).withTrimmedTop (28.0f).withTrimmedBottom (88.0f);
        }

        float dbToY (double db) const
        {
            auto p = plotArea();
            return juce::jmap (juce::jlimit (minDb, maxDb, (float) db), minDb, maxDb, p.getBottom(), p.getY());
        }

        juce::Point<float> handleFor (bool high, const DelayEngine::Settings& s) const
        {
            auto p = plotArea();
            const double hz = high ? s.highCut : s.lowCut;
            return { axis.toX (hz, p), dbToY (DelayEngine::getLoopFilterDb (s, hz, engine.getSampleRate())) };
        }

        void paint (juce::Graphics& g) override
        {
            const auto s = settingsOf (model);
            const double rate = engine.getSampleRate();

            g.fillAll (Palette::panelBackground);
            EffectUI::drawPanel (g, getLocalBounds().toFloat(), "REPEATS");

            auto p = plotArea();
            g.setColour (EffectUI::plotBackground);
            g.fillRect (p);
            EffectUI::drawFrequencyGrid (g, p, axis, true);

            g.setFont (juce::FontOptions (9.0f));
            for (float db : { 0.0f, -12.0f, -24.0f })
            {
                g.setColour (Palette::panelOutline.withAlpha (db == 0.0f ? 0.8f : 0.35f));
                g.drawHorizontalLine ((int) dbToY (db), p.getX(), p.getRight());
                g.setColour (Palette::textDisabled);
                g.drawText (juce::String ((int) db), juce::Rectangle<float> (p.getX() - 30.0f, dbToY (db) - 6.0f, 26.0f, 12.0f), juce::Justification::centredRight);
            }

            // One curve per pass: each repeat has been through the cuts once more.
            g.saveState();
            g.reduceClipRegion (p.toNearestInt());
            for (int passes = maxPasses; passes >= 1; --passes)
            {
                juce::Path curve;
                for (float x = p.getX(); x <= p.getRight(); x += 2.0f)
                {
                    auto y = dbToY (passes * DelayEngine::getLoopFilterDb (s, axis.toHz (x, p), rate));
                    if (x == p.getX()) curve.startNewSubPath (x, y);
                    else               curve.lineTo (x, y);
                }

                const auto colour = colourFor (s, passes, rate);
                const float fade = 1.0f - (float) (passes - 1) / (float) maxPasses;

                if (passes == 1)
                {
                    juce::Path fill (curve);
                    fill.lineTo (p.getRight(), p.getBottom());
                    fill.lineTo (p.getX(), p.getBottom());
                    fill.closeSubPath();
                    g.setGradientFill (juce::ColourGradient::vertical (colour.withAlpha (0.25f), p.getY(), colour.withAlpha (0.02f), p.getBottom()));
                    g.fillPath (fill);
                }

                g.setColour (colour.withAlpha (0.25f + 0.7f * fade * fade));
                g.strokePath (curve, juce::PathStrokeType (passes == 1 ? 2.0f : 1.2f));
            }
            g.restoreState();

            EffectUI::drawHandle (g, handleFor (false, s), Palette::accent, hover == 1 || dragging == 1, "L");
            EffectUI::drawHandle (g, handleFor (true, s), Palette::accent, hover == 2 || dragging == 2, "H");

            // Legend: from the first repeat to the sixth.
            auto legend = getLocalBounds().toFloat().reduced (10.0f, 6.0f).removeFromTop (14.0f).removeFromRight (150.0f);
            g.setFont (juce::FontOptions (10.0f));
            g.setColour (Palette::textDim);
            g.drawText ("1st", legend.removeFromLeft (22.0f), juce::Justification::centredLeft);
            g.drawText (juce::String (maxPasses) + "th repeat", legend.removeFromRight (62.0f), juce::Justification::centredRight);
            auto swatch = legend.reduced (4.0f, 5.0f);
            g.setGradientFill (juce::ColourGradient::horizontal (colourFor (s, 1, rate), swatch.getX(), colourFor (s, maxPasses, rate).withAlpha (0.4f), swatch.getRight()));
            g.fillRoundedRectangle (swatch, 2.0f);

            drawDrive (g, s);
            drawWobble (g, s);
        }

        /** The loop's saturation curve: straight at 0 %, bending as the drive goes up. */
        void drawDrive (juce::Graphics& g, const DelayEngine::Settings& s)
        {
            auto r = getLocalBounds().toFloat().reduced (10.0f, 8.0f).removeFromBottom (58.0f);
            auto box = r.removeFromLeft (r.getWidth() * 0.5f - 4.0f);
            drawMiniPanel (g, box, "DRIVE", juce::String (juce::roundToInt (s.drive * 100.0f)) + " %");

            auto plot = box.reduced (8.0f, 6.0f).withTrimmedTop (12.0f).withTrimmedRight (box.getWidth() * 0.5f - 4.0f);
            g.setColour (Palette::panelOutline.withAlpha (0.6f));
            g.drawLine (plot.getX(), plot.getBottom(), plot.getRight(), plot.getY(), 1.0f);

            juce::Path curve;
            for (int i = 0; i <= 40; ++i)
            {
                const float x = -1.5f + 3.0f * (float) i / 40.0f;
                const float y = DelayEngine::saturate (x, s.drive);
                const auto px = plot.getX() + (x + 1.5f) / 3.0f * plot.getWidth();
                const auto py = plot.getBottom() - (y + 1.5f) / 3.0f * plot.getHeight();
                if (i == 0) curve.startNewSubPath (px, py);
                else        curve.lineTo (px, py);
            }

            g.setColour (duckColour.withAlpha (0.4f + 0.6f * s.drive));
            g.strokePath (curve, juce::PathStrokeType (1.6f));
        }

        /** Wow and flutter: the delay time's wander over a second. */
        void drawWobble (juce::Graphics& g, const DelayEngine::Settings& s)
        {
            auto r = getLocalBounds().toFloat().reduced (10.0f, 8.0f).removeFromBottom (58.0f);
            auto box = r.removeFromRight (r.getWidth() * 0.5f - 4.0f);
            drawMiniPanel (g, box, "WOBBLE", juce::String (juce::roundToInt (s.wobble * 100.0f)) + " %");

            auto plot = box.reduced (8.0f, 6.0f).withTrimmedTop (14.0f);
            const auto mid = plot.getCentreY();
            g.setColour (Palette::panelOutline.withAlpha (0.6f));
            g.drawHorizontalLine ((int) mid, plot.getX(), plot.getRight());

            juce::Path wave;
            for (int i = 0; i <= 60; ++i)
            {
                const double t = (double) i / 60.0 * 1.8 + phase;
                const double v = std::sin (juce::MathConstants<double>::twoPi * 0.55 * t) * 0.8
                                 + std::sin (juce::MathConstants<double>::twoPi * 6.3 * t) * 0.15;
                const auto x = plot.getX() + plot.getWidth() * (float) i / 60.0f;
                const auto y = mid - (float) v * s.wobble * plot.getHeight() * 0.5f;
                if (i == 0) wave.startNewSubPath (x, y);
                else        wave.lineTo (x, y);
            }

            g.setColour (Palette::tempo.withAlpha (0.4f + 0.6f * s.wobble));
            g.strokePath (wave, juce::PathStrokeType (1.4f));
        }

        static void drawMiniPanel (juce::Graphics& g, juce::Rectangle<float> box, const juce::String& title, const juce::String& value)
        {
            g.setColour (EffectUI::plotBackground);
            g.fillRoundedRectangle (box, 3.0f);
            g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
            g.setColour (Palette::textDim);
            g.drawText (title, box.reduced (6.0f, 3.0f).removeFromTop (12.0f), juce::Justification::centredLeft);
            g.setFont (juce::FontOptions (9.5f));
            g.drawText (value, box.reduced (6.0f, 3.0f).removeFromTop (12.0f), juce::Justification::centredRight);
        }

        /** Scrolls the wobble picture while it's on. */
        void advance (float wobble)
        {
            if (wobble > 0.001f)
            {
                phase += 1.0 / ticksPerSecond;
                repaint();
            }
        }

        //==============================================================================
        int hitTest (juce::Point<float> pos) const
        {
            const auto s = settingsOf (model);
            if (pos.getDistanceFrom (handleFor (true, s)) < 12.0f)  return 2;
            if (pos.getDistanceFrom (handleFor (false, s)) < 12.0f) return 1;
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
                model.beginUndoTransaction (dragging == 2 ? "Delay high cut" : "Delay low cut");
                model.parameter (dragging == 2 ? P::highCut : P::lowCut)->beginGesture();
            }
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (dragging == 0)
                return;

            const auto id = dragging == 2 ? P::highCut : P::lowCut;
            const auto& spec = DelayParameters::get (id);
            model.parameter (id)->setValue ((float) juce::jlimit ((double) spec.minimum, (double) spec.maximum, axis.toHz (e.position.x, plotArea())));
            repaint();
        }

        void mouseUp (const juce::MouseEvent&) override
        {
            if (dragging != 0)
                model.parameter (dragging == 2 ? P::highCut : P::lowCut)->endGesture();
            dragging = 0;
        }

        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            if (auto h = hitTest (e.position); h != 0)
            {
                const auto id = h == 2 ? P::highCut : P::lowCut;
                model.setValue (id, DelayParameters::get (id).defaultValue, "Reset delay cut");
            }
        }

        EffectModel& model;
        DelayEngine& engine;
        EffectUI::FrequencyAxis axis;
        int hover = 0, dragging = 0;
        double phase = 0.0;
        static constexpr int maxPasses = 6;
        static constexpr float minDb = -30.0f, maxDb = 3.0f;
    };

    //==============================================================================
    DelayView::DelayView (EffectModel& m, DelayEngine& e)
        : model (m), engine (e),
          syncButton (m, P::sync, "Sync", Palette::tempo, "Delay time in notes, following the tempo"),
          modeButtons (m, P::mode, { "Stereo", "Ping-pong" }, Palette::accent, "Delay mode")
    {
        setOpaque (true);

        echoMap = std::make_unique<EchoMap> (model, engine);
        repeats = std::make_unique<RepeatsView> (model, engine);
        addAndMakeVisible (*echoMap);
        addAndMakeVisible (*repeats);
        addAndMakeVisible (syncButton);
        addAndMakeVisible (modeButtons);
        modeButtons.setTooltip ("Stereo: each side echoes itself. Ping-pong: the echoes bounce from side to side");

        auto add = [this] (P id, const char* label, juce::Colour c, float mid)
        {
            auto* k = knobs.add (new ParamKnob (model.parameter (id), label, c, DelayParameters::get (id).defaultValue, mid));
            addAndMakeVisible (k);
        };

        add (P::time, "Time", Palette::tempo, 300.0f);
        add (P::spread, "Spread", Palette::tempo, 0.0f);
        add (P::feedback, "Feedback", feedbackColour, 0.0f);
        add (P::lowCut, "Low cut", Palette::accent, 200.0f);
        add (P::highCut, "High cut", Palette::accent, 4000.0f);
        add (P::drive, "Drive", duckColour, 0.0f);
        add (P::wobble, "Wobble", Palette::automation, 0.0f);
        add (P::duck, "Duck", Palette::automation, 0.0f);
        add (P::width, "Width", Palette::marker, 0.0f);
        add (P::mix, "Mix", Palette::marker, 0.0f);

        groups = { { "TIME", 0, 2, {} }, { "FEEDBACK", 2, 4, {} }, { "MOTION", 6, 2, {} }, { "OUTPUT", 8, 2, {} } };

        updateTimeKnob();
        setSize (defaultWidth, defaultHeight);
        startTimerHz (ticksPerSecond);
    }

    DelayView::~DelayView()
    {
        stopTimer();
    }

    void DelayView::updateTimeKnob()
    {
        const bool note = model.isOn (P::sync);
        if (note == showingNote && timeKnobReady)
            return;

        showingNote = note;
        timeKnobReady = true;
        auto id = note ? P::division : P::time;
        knobs[0]->setParameter (model.parameter (id), DelayParameters::get (id).defaultValue, note ? 0.0f : 300.0f);
        knobs[0]->setLabel (note ? "Note" : "Time");
    }

    void DelayView::tick (float inL, float inR, float wetL, float wetR, float duckDb)
    {
        juce::ignoreUnused (wetL, wetR);
        echoMap->push (inL, inR, duckDb);
        repeats->advance (model.value (P::wobble));
    }

    void DelayView::timerCallback()
    {
        const float in[2] { engine.inputPeak[0].take(), engine.inputPeak[1].take() };
        const float wet[2] { engine.wetPeak[0].take(), engine.wetPeak[1].take() };
        const float duck = engine.duckReduction.take();

        // A display tick can fall between audio blocks: skip a few empty ones rather than draw gaps.
        const bool empty = in[0] == 0.0f && in[1] == 0.0f && wet[0] == 0.0f && wet[1] == 0.0f;
        emptyTicks = empty ? emptyTicks + 1 : 0;
        if (! empty || emptyTicks > 3)
            tick (in[0], in[1], wet[0], wet[1], duck);

        updateTimeKnob();
        for (auto* k : knobs)
            k->refresh();

        syncButton.refresh();
        modeButtons.refresh();
        repeats->repaint();
        repaint (getLocalBounds().removeFromTop (EffectUI::headerHeight));
    }

    void DelayView::paint (juce::Graphics& g)
    {
        g.fillAll (Palette::panelBackground);

        const auto s = settingsOf (model);
        const double seconds = DelayEngine::getTimeSeconds (s, model.getBpm(), 0);
        auto summary = (s.sync ? model.parameter (P::division)->getCurrentText() + " (" + timeText (seconds) + ")" : timeText (seconds))
                       + "  -  feedback " + model.parameter (P::feedback)->getCurrentText()
                       + "  -  " + (s.mode == DelayEngine::Mode::pingPong ? "ping-pong" : "stereo")
                       + "  -  mix " + model.parameter (P::mix)->getCurrentText();
        EffectUI::drawHeader (g, getLocalBounds().removeFromTop (EffectUI::headerHeight).withTrimmedRight (280), "C-DELAY", summary);
        EffectUI::paintKnobGroups (g, groups);
    }

    void DelayView::resized()
    {
        auto r = getLocalBounds();
        auto header = r.removeFromTop (EffectUI::headerHeight).reduced (10, 5);
        syncButton.setBounds (header.removeFromRight (64));
        header.removeFromRight (8);
        modeButtons.setBounds (header.removeFromRight (180));

        EffectUI::layoutKnobGroups (groups, knobs, r.removeFromBottom (EffectUI::knobRowHeight).reduced (8, 6));

        r.reduce (8, 0);
        repeats->setBounds (r.removeFromRight (juce::jlimit (260, 340, r.getWidth() / 3)));
        r.removeFromRight (8);
        echoMap->setBounds (r);
    }
}
