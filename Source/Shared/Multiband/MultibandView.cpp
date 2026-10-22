#include "MultibandView.h"
#include "../UI/SpectrumAnalyser.h"
#include "../../UI/LookAndFeel/Palette.h"

namespace cdaw
{
    namespace
    {
        constexpr int headerHeight = 32;
        constexpr float meterMinDb = -60.0f, maxReductionDb = 24.0f, fallPerTick = 0.9f;
        const juce::Colour reductionColour { 0xffff5a4f };
        const juce::Colour thresholdColour { 0xfff2b134 };

        float toDb (float gain)     { return juce::Decibels::gainToDecibels (gain, -100.0f); }

        juce::String formatHz (double hz)
        {
            return hz >= 1000.0 ? juce::String (hz / 1000.0, hz >= 10000.0 ? 1 : 2) + "k" : juce::String (juce::roundToInt (hz));
        }

        void fall (float& shown, float value)
        {
            shown = value >= shown ? value : juce::jmax (value, shown - fallPerTick);
        }

        void drawPanel (juce::Graphics& g, juce::Rectangle<float> r)
        {
            g.setColour (Palette::windowBackground);
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (Palette::panelOutline.withAlpha (0.7f));
            g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
        }
    }

    juce::Colour MultibandView::getBandColour (int band)
    {
        static const juce::Colour colours[MultibandEngine::numBands] {
            juce::Colour (0xffe5484d), juce::Colour (0xfff2b134), juce::Colour (0xff3ccf6e), juce::Colour (0xff4fa3ff)
        };
        return colours[juce::jlimit (0, MultibandEngine::numBands - 1, band)];
    }

    //==============================================================================
    class MultibandView::BandPanel : public juce::Component
    {
    public:
        BandPanel (MultibandView& o, int b) : owner (o), band (b)
        {
            auto colour = getBandColour (band);

            auto setupButton = [this] (juce::TextButton& button, const char* tip, juce::Colour onColour, std::function<void()> click)
            {
                button.setTooltip (tip);
                button.setColour (juce::TextButton::buttonOnColourId, onColour);
                button.onClick = std::move (click);
                addAndMakeVisible (button);
            };

            setupButton (activeButton, "Compress this band (off: it passes through untouched)", colour.withAlpha (0.8f), [this]
            {
                owner.model.beginUndoTransaction ("Multiband band on/off");
                owner.toggle (id (MultibandParameters::bandOn));
            });
            setupButton (soloButton, "Hear only this band (several can be soloed)", Palette::mute, [this]
            {
                owner.toggle (id (MultibandParameters::bandSolo));
            });

            auto add = [this] (MultibandParameters::BandParam what, const char* label, juce::Colour c, float mid)
            {
                auto* k = knobs.add (new ParamKnob (owner.model.parameter (id (what)), label, c, MultibandParameters::get (id (what)).defaultValue, mid));
                addAndMakeVisible (k);
            };

            add (MultibandParameters::bandThreshold, "Threshold", thresholdColour, 0.0f);
            add (MultibandParameters::bandRatio, "Ratio", colour, 4.0f);
            add (MultibandParameters::bandGain, "Gain", colour, 0.0f);
            add (MultibandParameters::bandAttack, "Attack", Palette::tempo, 15.0f);
            add (MultibandParameters::bandRelease, "Release", Palette::tempo, 150.0f);
            refresh();
        }

        int id (MultibandParameters::BandParam what) const  { return MultibandParameters::bandParam (band, what); }
        float threshold() const                             { return owner.value (id (MultibandParameters::bandThreshold)); }

        void refresh()
        {
            activeButton.setToggleState (owner.isOn (band), juce::dontSendNotification);
            activeButton.setButtonText (owner.isOn (band) ? "On" : "Off");
            soloButton.setToggleState (owner.isSoloed (band), juce::dontSendNotification);

            for (auto* k : knobs)
            {
                k->setEnabled (owner.isOn (band));
                k->refresh();
            }

            auto range = owner.bandRange (band);
            auto text = formatHz (range.getStart()) + " - " + formatHz (range.getEnd()) + " Hz";
            if (text != rangeText)
            {
                rangeText = text;
                repaint();
            }
        }

        void tick (float inGain, float outGain, float reduction)
        {
            const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
            fall (inDb, toDb (inGain));
            fall (outDb, toDb (outGain));
            grDb = reduction >= grDb ? reduction : juce::jmax (reduction, grDb - fallPerTick);

            if (grDb >= grHold)            { grHold = grDb; holdTime = now; }
            else if (now - holdTime > 1.5) { grHold = juce::jmax (grDb, grHold - fallPerTick * 0.5f); }

            history[(size_t) historyWrite] = { toDb (inGain), toDb (outGain), reduction };
            historyWrite = (historyWrite + 1) % (int) history.size();

            repaint (displayArea.toNearestInt().expanded (8));
        }

        //==============================================================================
        void resized() override
        {
            auto r = getLocalBounds().reduced (8, 6);
            auto header = r.removeFromTop (24);
            activeButton.setBounds (header.removeFromRight (40));
            header.removeFromRight (4);
            soloButton.setBounds (header.removeFromRight (26));

            r.removeFromTop (6);
            displayArea = r.removeFromTop (juce::jmax (90, r.getHeight() - 130)).toFloat();
            r.removeFromTop (4);

            auto row1 = r.removeFromTop (r.getHeight() / 2);
            const int w1 = row1.getWidth() / 3;
            for (int i = 0; i < 3; ++i)
                knobs[i]->setBounds (row1.removeFromLeft (w1));

            auto row2 = r.withSizeKeepingCentre (w1 * 2, r.getHeight());
            knobs[3]->setBounds (row2.removeFromLeft (w1));
            knobs[4]->setBounds (row2);
        }

        juce::Rectangle<float> levelBar() const     { return displayArea.withWidth (16.0f).withTrimmedTop (16.0f).withTrimmedBottom (2.0f).translated (24.0f, 0.0f); }
        juce::Rectangle<float> reductionBar() const { return levelBar().translated (22.0f, 0.0f).withWidth (10.0f); }
        juce::Rectangle<float> curveArea() const
        {
            auto r = displayArea.withTrimmedLeft (80.0f).reduced (2.0f, 2.0f);
            auto side = juce::jmin (r.getWidth(), r.getHeight());
            return r.withSizeKeepingCentre (side, side).withX (r.getRight() - side);
        }

        float levelY (float db) const
        {
            auto bar = levelBar();
            return juce::jmap (juce::jlimit (meterMinDb, 0.0f, db), meterMinDb, 0.0f, bar.getBottom(), bar.getY());
        }

        void paint (juce::Graphics& g) override
        {
            const bool selected = owner.selectedBand == band;
            const bool on = owner.isOn (band);
            auto colour = getBandColour (band);
            auto r = getLocalBounds().toFloat().reduced (2.0f, 0.0f);

            g.setColour (selected ? Palette::panelRaised.brighter (0.06f) : Palette::panelRaised.withAlpha (0.55f));
            g.fillRoundedRectangle (r, 5.0f);
            g.setColour (colour.withAlpha (on ? 0.9f : 0.3f));
            g.fillRoundedRectangle (r.withHeight (3.0f), 1.5f);

            if (selected)
            {
                g.setColour (colour.withAlpha (0.6f));
                g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
            }

            // Header: band name and range.
            auto header = getLocalBounds().reduced (10, 6).removeFromTop (24).withTrimmedRight (76);
            g.setColour (on ? Palette::text : Palette::textDim);
            g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            g.drawText (MultibandParameters::bandName (band), header, juce::Justification::centredLeft);
            g.setColour (Palette::textDim);
            g.setFont (juce::FontOptions (10.5f));
            g.drawText (rangeText, header, juce::Justification::centredRight);

            drawDisplay (g);
        }

        void drawDisplay (juce::Graphics& g)
        {
            auto colour = getBandColour (band);
            const bool on = owner.isOn (band);
            drawPanel (g, displayArea);

            // Band level with the threshold handle; gain reduction beside it.
            auto bar = levelBar();
            g.setFont (juce::FontOptions (9.0f));
            for (float db : { 0.0f, -12.0f, -24.0f, -36.0f, -48.0f })
            {
                g.setColour (Palette::textDisabled);
                g.drawText (juce::String ((int) db), juce::Rectangle<float> (displayArea.getX() + 2.0f, levelY (db) - 5.0f, 20.0f, 10.0f), juce::Justification::centredRight);
            }

            g.setColour (Palette::textDim);
            g.drawText ("IN", bar.withY (displayArea.getY() + 2.0f).withHeight (12.0f).expanded (4.0f, 0.0f), juce::Justification::centred);
            g.drawText ("GR", reductionBar().withY (displayArea.getY() + 2.0f).withHeight (12.0f).expanded (6.0f, 0.0f), juce::Justification::centred);

            g.setColour (Palette::panelBackground);
            g.fillRect (bar);
            if (inDb > meterMinDb)
            {
                g.setGradientFill (juce::ColourGradient::vertical (colour.brighter (0.3f), bar.getY(), colour.darker (0.4f), bar.getBottom()));
                g.fillRect (bar.withTop (levelY (inDb)));
            }

            auto ty = levelY (threshold());
            g.setColour (thresholdColour);
            g.fillRect (bar.getX() - 3.0f, ty - 1.0f, bar.getWidth() + 6.0f, 2.0f);
            juce::Path arrow;
            arrow.addTriangle (bar.getX() - 9.0f, ty - 5.0f, bar.getX() - 9.0f, ty + 5.0f, bar.getX() - 2.0f, ty);
            g.fillPath (arrow);

            auto gr = reductionBar();
            auto grY = [&] (float db) { return juce::jmap (juce::jlimit (0.0f, maxReductionDb, db), 0.0f, maxReductionDb, gr.getY(), gr.getBottom()); };
            g.setColour (Palette::panelBackground);
            g.fillRect (gr);
            g.setColour (reductionColour);
            g.fillRect (gr.withBottom (grY (grDb)));
            if (grHold > 0.05f)
            {
                g.setColour (Palette::text.withAlpha (0.8f));
                g.fillRect (gr.withY (grY (grHold) - 1.0f).withHeight (2.0f));
            }

            // Transfer curve with where the band is right now.
            auto area = curveArea();
            g.setColour (Palette::panelBackground.withAlpha (0.6f));
            g.fillRect (area);
            auto cx = [&] (float db) { return juce::jmap (juce::jlimit (meterMinDb, 0.0f, db), meterMinDb, 0.0f, area.getX(), area.getRight()); };
            auto cy = [&] (float db) { return juce::jmap (juce::jlimit (meterMinDb, 0.0f, db), meterMinDb, 0.0f, area.getBottom(), area.getY()); };

            drawHistory (g, area, cy);

            g.setColour (Palette::panelOutline.withAlpha (0.4f));
            for (float db = -48.0f; db < 0.0f; db += 12.0f)
            {
                g.drawVerticalLine ((int) cx (db), area.getY(), area.getBottom());
                g.drawHorizontalLine ((int) cy (db), area.getX(), area.getRight());
            }

            g.setColour (Palette::textDisabled.withAlpha (0.5f));
            g.drawLine (area.getX(), area.getBottom(), area.getRight(), area.getY(), 1.0f);

            const float thresholdDb = threshold(), ratio = owner.value (id (MultibandParameters::bandRatio));
            juce::Path curve;
            for (float x = area.getX(); x <= area.getRight(); x += 1.0f)
            {
                auto in = juce::jmap (x, area.getX(), area.getRight(), meterMinDb, 0.0f);
                auto y = cy (on ? MultibandEngine::transfer (in, thresholdDb, ratio) : in);
                if (x == area.getX()) curve.startNewSubPath (x, y);
                else                  curve.lineTo (x, y);
            }

            g.setColour (on ? colour : Palette::textDisabled);
            g.strokePath (curve, juce::PathStrokeType (1.8f));

            if (inDb > meterMinDb)
            {
                juce::Point<float> dot { cx (inDb), cy (inDb - grDb) };
                g.setColour (Palette::text.withAlpha (0.25f));
                g.fillEllipse (juce::Rectangle<float> (13.0f, 13.0f).withCentre (dot));
                g.setColour (Palette::text);
                g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (dot));
            }

            // Current reduction in numbers.
            g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            g.setColour (grDb > 0.05f ? reductionColour : Palette::textDisabled);
            g.drawText (grDb > 0.05f ? "-" + juce::String (grDb, 1) + " dB" : juce::String ("0.0 dB"),
                        area.reduced (4.0f).removeFromTop (14.0f), juce::Justification::centredLeft);
        }

        /** The last few seconds behind the curve, faintly: band level (filled), level after
            compression (line), and gain reduction hanging from the top. Same dB scale as the
            curve's output axis; gain reduction uses the top half for 24 dB. */
        template <typename LevelToY>
        void drawHistory (juce::Graphics& g, juce::Rectangle<float> area, LevelToY levelY)
        {
            const auto colour = getBandColour (band);
            const auto count = (int) history.size();
            const float step = area.getWidth() / (float) (count - 1);
            auto frameAt = [&] (int i) -> const Frame& { return history[(size_t) ((historyWrite + i) % count)]; };
            auto grY = [&] (float gr) { return area.getY() + juce::jlimit (0.0f, 1.0f, gr / maxReductionDb) * area.getHeight() * 0.5f; };

            juce::Path input, output, reduction;
            input.startNewSubPath (area.getX(), area.getBottom());
            reduction.startNewSubPath (area.getX(), area.getY());

            for (int i = 0; i < count; ++i)
            {
                auto& f = frameAt (i);
                auto x = area.getX() + (float) i * step;
                input.lineTo (x, levelY (f.inDb));
                reduction.lineTo (x, grY (f.grDb));

                if (i == 0) output.startNewSubPath (x, levelY (f.outDb));
                else        output.lineTo (x, levelY (f.outDb));
            }

            input.lineTo (area.getRight(), area.getBottom());
            input.closeSubPath();
            reduction.lineTo (area.getRight(), area.getY());
            reduction.closeSubPath();

            g.saveState();
            g.reduceClipRegion (area.toNearestInt());
            g.setColour (colour.withAlpha (0.13f));
            g.fillPath (input);
            g.setColour (colour.withAlpha (0.3f));
            g.strokePath (output, juce::PathStrokeType (1.0f));
            g.setColour (reductionColour.withAlpha (0.22f));
            g.fillPath (reduction);
            g.restoreState();
        }

        //==============================================================================
        bool overThreshold (juce::Point<float> p) const
        {
            auto bar = levelBar();
            return p.x >= bar.getX() - 12.0f && p.x <= bar.getRight() + 4.0f && std::abs (p.y - levelY (threshold())) < 7.0f;
        }

        void mouseMove (const juce::MouseEvent& e) override
        {
            setMouseCursor (overThreshold (e.position) ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            owner.selectBand (band);
            draggingThreshold = overThreshold (e.position);

            if (draggingThreshold)
            {
                owner.model.beginUndoTransaction ("Multiband threshold");
                owner.model.parameter (id (MultibandParameters::bandThreshold))->beginGesture();
            }
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (! draggingThreshold)
                return;

            auto bar = levelBar();
            auto db = juce::jmap (e.position.y, bar.getBottom(), bar.getY(), meterMinDb, 0.0f);
            owner.model.parameter (id (MultibandParameters::bandThreshold))->setValue (juce::jlimit (meterMinDb, 0.0f, db));
            repaint();
        }

        void mouseUp (const juce::MouseEvent&) override
        {
            if (draggingThreshold)
                owner.model.parameter (id (MultibandParameters::bandThreshold))->endGesture();
            draggingThreshold = false;
        }

        MultibandView& owner;
        const int band;
        juce::TextButton activeButton { "On" }, soloButton { "S" };
        juce::OwnedArray<ParamKnob> knobs;
        juce::Rectangle<float> displayArea;
        juce::String rangeText;
        float inDb = -100.0f, outDb = -100.0f, grDb = 0.0f, grHold = 0.0f;

        struct Frame { float inDb, outDb, grDb; };
        std::vector<Frame> history = std::vector<Frame> (240, Frame { -100.0f, -100.0f, 0.0f });     // 4 s at 60 Hz
        int historyWrite = 0;
        double holdTime = 0.0;
        bool draggingThreshold = false;
    };

    //==============================================================================
    class MultibandView::Graph : public juce::Component
    {
    public:
        explicit Graph (MultibandView& o) : owner (o), post (o.model.getEngine().postFifo), pre (o.model.getEngine().preFifo)
        {
            setOpaque (true);
        }

        bool showPre = false, showPost = true;

        void setActive (bool active)
        {
            owner.model.getEngine().postFifo->active = active && showPost;
            owner.model.getEngine().preFifo->active = active && showPre;
        }

        void updateSpectrum()
        {
            const auto rate = owner.model.getEngine().getSampleRate();
            if (showPost) post.update (columnHz, rate);
            if (showPre)  pre.update (columnHz, rate);
            repaint();
        }

        juce::Rectangle<float> plotArea() const
        {
            return getLocalBounds().toFloat().withTrimmedLeft (8.0f).withTrimmedRight (34.0f).withTrimmedTop (6.0f).withTrimmedBottom (18.0f);
        }

        float hzToX (double hz) const
        {
            auto p = plotArea();
            return p.getX() + (float) (std::log (hz / 20.0) / std::log (1000.0)) * p.getWidth();
        }

        double xToHz (float x) const
        {
            auto p = plotArea();
            return 20.0 * std::pow (1000.0, juce::jlimit (0.0, 1.0, (double) ((x - p.getX()) / p.getWidth())));
        }

        float spectrumY (float db) const
        {
            auto p = plotArea();
            return juce::jmap (juce::jlimit (-100.0f, 0.0f, db), -100.0f, 0.0f, p.getBottom(), p.getY());
        }

        int crossoverAt (float x) const
        {
            auto f = owner.crossovers();
            for (int i = 0; i < MultibandEngine::numBands - 1; ++i)
                if (std::abs (x - hzToX (f[(size_t) i])) < 7.0f)
                    return i;
            return -1;
        }

        int bandAt (float x) const
        {
            auto hz = xToHz (x);
            for (int b = 0; b < MultibandEngine::numBands; ++b)
                if (hz < owner.bandRange (b).getEnd() || b == MultibandEngine::numBands - 1)
                    return b;
            return 0;
        }

        void resized() override
        {
            auto p = plotArea();
            columnHz.resize ((size_t) juce::jmax (1, (int) p.getWidth()));
            for (size_t c = 0; c < columnHz.size(); ++c)
                columnHz[c] = xToHz (p.getX() + (float) c + 0.5f);
        }

        void paint (juce::Graphics& g) override
        {
            auto p = plotArea();
            g.setGradientFill (juce::ColourGradient::vertical (juce::Colour (0xff15171b), p.getY(), juce::Colour (0xff1b1e23), p.getBottom()));
            g.fillAll();

            // Band regions.
            for (int b = 0; b < MultibandEngine::numBands; ++b)
            {
                auto range = owner.bandRange (b);
                auto region = juce::Rectangle<float>::leftTopRightBottom (hzToX (range.getStart()), p.getY(), hzToX (range.getEnd()), p.getBottom());
                auto colour = getBandColour (b);
                const bool audible = ! owner.anySoloed() || owner.isSoloed (b);
                g.setColour (colour.withAlpha (! audible ? 0.02f : b == owner.selectedBand ? 0.13f : 0.06f));
                g.fillRect (region);
            }

            // Grid.
            g.setFont (juce::FontOptions (9.5f));
            for (double hz : { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
            {
                auto x = hzToX (hz);
                g.setColour (Palette::panelOutline.withAlpha (0.4f));
                g.drawVerticalLine ((int) x, p.getY(), p.getBottom());
                g.setColour (Palette::textDisabled);
                g.drawText (hz >= 1000.0 ? juce::String ((int) (hz / 1000.0)) + "k" : juce::String ((int) hz),
                            juce::Rectangle<float> (x - 20.0f, p.getBottom() + 3.0f, 40.0f, 12.0f), juce::Justification::centred);
            }

            for (float db = -20.0f; db >= -80.0f; db -= 20.0f)
            {
                g.setColour (Palette::panelOutline.withAlpha (0.25f));
                g.drawHorizontalLine ((int) spectrumY (db), p.getX(), p.getRight());
                g.setColour (Palette::textDisabled.withAlpha (0.7f));
                g.drawText (juce::String ((int) db), juce::Rectangle<float> (p.getRight() + 3.0f, spectrumY (db) - 6.0f, 30.0f, 12.0f), juce::Justification::centredLeft);
            }

            g.saveState();
            g.reduceClipRegion (p.toNearestInt());

            auto spectrumPath = [&] (const SpectrumAnalyser& a, bool closed)
            {
                juce::Path path;
                if (closed) path.startNewSubPath (p.getX(), p.getBottom());
                for (size_t c = 0; c < a.levels.size(); ++c)
                {
                    auto y = spectrumY (a.levels[c]);
                    if (c == 0 && ! closed) path.startNewSubPath (p.getX(), y);
                    else                    path.lineTo (p.getX() + (float) c, y);
                }
                if (closed) { path.lineTo (p.getRight(), p.getBottom()); path.closeSubPath(); }
                return path;
            };

            if (showPre && ! pre.levels.empty())
            {
                g.setColour (juce::Colour (0xffb0b8c4).withAlpha (0.45f));
                g.strokePath (spectrumPath (pre, false), juce::PathStrokeType (1.0f));
            }

            if (showPost && ! post.levels.empty())
            {
                const juce::Colour spectrumColour (0xff6f8fb0);
                g.setGradientFill (juce::ColourGradient::vertical (spectrumColour.withAlpha (0.4f), p.getY(), spectrumColour.withAlpha (0.06f), p.getBottom()));
                g.fillPath (spectrumPath (post, true));
                g.setColour (spectrumColour.brighter (0.4f).withAlpha (0.7f));
                g.strokePath (spectrumPath (post, false), juce::PathStrokeType (1.0f));
            }

            // Gain reduction hanging from the top of each band.
            for (int b = 0; b < MultibandEngine::numBands; ++b)
            {
                auto range = owner.bandRange (b);
                auto left = hzToX (range.getStart()), right = hzToX (range.getEnd());
                auto gr = owner.panels[b]->grDb;
                auto depth = juce::jlimit (0.0f, 1.0f, gr / maxReductionDb) * p.getHeight() * 0.45f;
                auto colour = getBandColour (b);

                if (depth > 0.5f)
                {
                    auto curtain = juce::Rectangle<float>::leftTopRightBottom (left + 1.0f, p.getY(), right - 1.0f, p.getY() + depth);
                    g.setGradientFill (juce::ColourGradient::vertical (reductionColour.withAlpha (0.55f), curtain.getY(), reductionColour.withAlpha (0.15f), curtain.getBottom()));
                    g.fillRect (curtain);
                    g.setColour (reductionColour);
                    g.fillRect (curtain.withTop (curtain.getBottom() - 2.0f));
                }

                auto labelArea = juce::Rectangle<float>::leftTopRightBottom (left, p.getY() + 4.0f, right, p.getY() + 40.0f).reduced (6.0f, 0.0f);
                g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
                g.setColour (colour.withAlpha (owner.isOn (b) ? 1.0f : 0.4f));
                g.drawText (MultibandParameters::bandName (b) + (owner.isSoloed (b) ? "  S" : "") + (owner.isOn (b) ? "" : "  (off)"),
                            labelArea.removeFromTop (16.0f), juce::Justification::centred, true);
                g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
                g.setColour (gr > 0.05f ? Palette::text : Palette::textDisabled);
                g.drawText (gr > 0.05f ? "-" + juce::String (gr, 1) + " dB" : juce::String ("0.0 dB"), labelArea, juce::Justification::centred, true);
            }

            g.restoreState();

            // Crossover lines with handles.
            auto f = owner.crossovers();
            for (int i = 0; i < MultibandEngine::numBands - 1; ++i)
            {
                auto x = hzToX (f[(size_t) i]);
                const bool active = i == hoverCrossover || i == dragCrossover;
                g.setColour (Palette::text.withAlpha (active ? 0.9f : 0.45f));
                g.fillRect (x - (active ? 1.0f : 0.5f), p.getY(), active ? 2.0f : 1.0f, p.getHeight());

                auto handle = juce::Rectangle<float> (14.0f, 30.0f).withCentre ({ x, p.getCentreY() });
                g.setColour (juce::Colour (0xff15171b));
                g.fillRoundedRectangle (handle, 4.0f);
                g.setColour (Palette::text.withAlpha (active ? 1.0f : 0.6f));
                g.drawRoundedRectangle (handle, 4.0f, 1.2f);
                for (float dy : { -5.0f, 0.0f, 5.0f })
                    g.drawHorizontalLine ((int) (handle.getCentreY() + dy), handle.getX() + 4.0f, handle.getRight() - 4.0f);

                if (active)
                {
                    auto text = formatHz (f[(size_t) i]) + " Hz";
                    auto box = juce::Rectangle<float> (64.0f, 20.0f).withCentre ({ x, handle.getBottom() + 16.0f });
                    g.setColour (juce::Colour (0xee101215));
                    g.fillRoundedRectangle (box, 4.0f);
                    g.setColour (Palette::text);
                    g.setFont (juce::FontOptions (11.5f));
                    g.drawText (text, box, juce::Justification::centred);
                }
            }
        }

        void mouseMove (const juce::MouseEvent& e) override
        {
            auto c = crossoverAt (e.position.x);
            if (c != hoverCrossover)
            {
                hoverCrossover = c;
                repaint();
            }
            setMouseCursor (c >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::PointingHandCursor);
        }

        void mouseExit (const juce::MouseEvent&) override
        {
            hoverCrossover = -1;
            repaint();
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            dragCrossover = crossoverAt (e.position.x);

            if (dragCrossover >= 0)
            {
                owner.model.beginUndoTransaction ("Multiband crossover");
                owner.model.parameter (MultibandParameters::crossover (dragCrossover))->beginGesture();
            }
            else
            {
                owner.selectBand (bandAt (e.position.x));
            }
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (dragCrossover < 0)
                return;

            // Stay between the neighbours (a third of an octave clear), so bands never swap.
            auto f = owner.crossovers();
            auto lo = dragCrossover > 0 ? f[(size_t) dragCrossover - 1] * 1.26f : MultibandEngine::minCrossover;
            auto hi = dragCrossover < MultibandEngine::numBands - 2 ? f[(size_t) dragCrossover + 1] / 1.26f : MultibandEngine::maxCrossover;
            auto hz = (float) juce::jlimit ((double) lo, (double) juce::jmax (lo, hi), xToHz (e.position.x));
            owner.model.parameter (MultibandParameters::crossover (dragCrossover))->setValue (hz);
            repaint();
        }

        void mouseUp (const juce::MouseEvent&) override
        {
            if (dragCrossover >= 0)
                owner.model.parameter (MultibandParameters::crossover (dragCrossover))->endGesture();
            dragCrossover = -1;
        }

        MultibandView& owner;
        SpectrumAnalyser post, pre;
        std::vector<double> columnHz;
        int hoverCrossover = -1, dragCrossover = -1;
    };

    //==============================================================================
    MultibandView::MultibandView (MultibandModel& m) : model (m)
    {
        setOpaque (true);

        // Panels first: the graph reads their gain reduction.
        for (int i = 0; i < MultibandEngine::numBands; ++i)
            addAndMakeVisible (panels.add (new BandPanel (*this, i)));

        graph = std::make_unique<Graph> (*this);
        addAndMakeVisible (*graph);

        outputKnob = std::make_unique<ParamKnob> (model.parameter (MultibandParameters::output), "Output", Palette::text, 0.0f);
        addAndMakeVisible (*outputKnob);

        for (auto [button, on, tip] : { std::tuple (&preButton, false, "Show the spectrum before processing"),
                                       std::tuple (&postButton, true, "Show the spectrum after processing") })
        {
            button->setClickingTogglesState (true);
            button->setToggleState (on, juce::dontSendNotification);
            button->setTooltip (tip);
            button->setColour (juce::TextButton::buttonOnColourId, Palette::accentDim);
            addAndMakeVisible (button);
        }

        preButton.onClick = [this] { graph->showPre = preButton.getToggleState(); graph->setActive (isShowing()); };
        postButton.onClick = [this] { graph->showPost = postButton.getToggleState(); graph->setActive (isShowing()); };

        selectBand (0);
        startTimerHz (60);
    }

    MultibandView::~MultibandView()
    {
        stopTimer();
        graph->setActive (false);
    }

    void MultibandView::visibilityChanged()
    {
        graph->setActive (isShowing());
    }

    void MultibandView::selectBand (int band)
    {
        selectedBand = band;
        for (auto* panel : panels)
            panel->repaint();
        graph->repaint();
    }

    float MultibandView::value (int id) const
    {
        return model.parameter (id)->getValue();
    }

    void MultibandView::toggle (int id)
    {
        auto p = model.parameter (id);
        p->beginGesture();
        p->setValue (p->getValue() >= 0.5f ? 0.0f : 1.0f);
        p->endGesture();
    }

    MultibandEngine::Settings MultibandView::getSettings() const
    {
        return MultibandParameters::toSettings ([this] (int id) { return value (id); });
    }

    std::array<float, MultibandEngine::numBands - 1> MultibandView::crossovers() const
    {
        return MultibandEngine::orderCrossovers (getSettings().crossovers);
    }

    juce::Range<float> MultibandView::bandRange (int band) const
    {
        return MultibandEngine::getBandRange (getSettings(), band);
    }

    bool MultibandView::isOn (int band) const
    {
        return value (MultibandParameters::bandParam (band, MultibandParameters::bandOn)) >= 0.5f;
    }

    bool MultibandView::isSoloed (int band) const
    {
        return value (MultibandParameters::bandParam (band, MultibandParameters::bandSolo)) >= 0.5f;
    }

    bool MultibandView::anySoloed() const
    {
        for (int b = 0; b < MultibandEngine::numBands; ++b)
            if (isSoloed (b))
                return true;

        return false;
    }

    void MultibandView::updateSpectrum()
    {
        graph->updateSpectrum();
    }

    void MultibandView::tick (const Levels& l)
    {
        for (int b = 0; b < MultibandEngine::numBands; ++b)
            panels[b]->tick (l.bandIn[b], l.bandOut[b], l.reduction[b]);

        const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
        fall (inLevel, toDb (juce::jmax (l.in[0], l.in[1])));
        fall (outLevel, toDb (juce::jmax (l.out[0], l.out[1])));
        if (outLevel >= outHold)               { outHold = outLevel; outHoldTime = now; }
        else if (now - outHoldTime > 1.5)      { outHold = juce::jmax (outLevel, outHold - fallPerTick * 0.5f); }

        repaint (outputMeterArea.toNearestInt().expanded (4));
    }

    void MultibandView::timerCallback()
    {
        if (! isShowing())
            return;

        Levels l;
        for (int b = 0; b < MultibandEngine::numBands; ++b)
        {
            auto& band = model.getEngine().bandMeters[(size_t) b];
            l.bandIn[b] = band.inputPeak.take();
            l.bandOut[b] = band.outputPeak.take();
            l.reduction[b] = band.reduction.take();
        }

        for (int ch = 0; ch < 2; ++ch)
        {
            l.in[ch] = model.getEngine().inputPeak[ch].take();
            l.out[ch] = model.getEngine().outputPeak[ch].take();
        }

        // A display tick can fall between audio blocks: skip it rather than show a drop to silence.
        bool any = false;
        for (int b = 0; b < MultibandEngine::numBands; ++b)
            any = any || l.bandIn[b] > 0.0f;
        if (any || ++emptyTicks > 3)
        {
            emptyTicks = any ? 0 : emptyTicks;
            tick (l);
        }

        // The spectrum at half the rate is plenty.
        if ((frame++ & 1) == 0)
        {
            graph->setActive (true);
            graph->updateSpectrum();
        }

        for (auto* panel : panels)
            panel->refresh();

        outputKnob->refresh();
    }

    //==============================================================================
    void MultibandView::paint (juce::Graphics& g)
    {
        g.fillAll (Palette::panelBackground);

        auto header = getLocalBounds().removeFromTop (headerHeight).reduced (12, 0);
        g.setColour (Palette::text);
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.drawText ("C-MULTICOMP", header, juce::Justification::centredLeft);

        drawOutputMeter (g, outputMeterArea);
    }

    void MultibandView::drawOutputMeter (juce::Graphics& g, juce::Rectangle<float> area)
    {
        drawPanel (g, area);
        auto inner = area.reduced (6.0f, 5.0f);
        g.setColour (Palette::textDim);
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawText ("OUT", inner.removeFromTop (13.0f), juce::Justification::centred);
        g.setColour (outHold > 0.0f ? Palette::meterRed : Palette::text);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (outHold <= -99.0f ? juce::String ("-inf") : juce::String (outHold, 1), inner.removeFromTop (14.0f), juce::Justification::centred);
        inner.removeFromTop (4.0f);

        auto y = [&] (juce::Rectangle<float> bar, float db) { return juce::jmap (juce::jlimit (meterMinDb, 6.0f, db), meterMinDb, 6.0f, bar.getBottom(), bar.getY()); };
        auto scale = inner.removeFromLeft (18.0f);
        auto bars = inner.reduced (2.0f, 0.0f);
        auto inBar = bars.removeFromLeft (bars.getWidth() * 0.5f).reduced (1.0f, 0.0f);
        auto outBar = bars.reduced (1.0f, 0.0f);

        g.setFont (juce::FontOptions (9.0f));
        for (float db : { 0.0f, -12.0f, -24.0f, -36.0f, -48.0f })
        {
            g.setColour (Palette::textDisabled);
            g.drawText (juce::String ((int) db), scale.withY (y (inBar, db) - 5.0f).withHeight (10.0f), juce::Justification::centredRight);
        }

        auto gradient = juce::ColourGradient::vertical (Palette::meterRed, y (inBar, 3.0f), Palette::meterGreen, y (inBar, -18.0f));
        gradient.addColour (0.45, Palette::meterYellow);

        for (auto [bar, level, label] : { std::tuple (inBar, inLevel, "in"), std::tuple (outBar, outLevel, "out") })
        {
            g.setColour (Palette::panelBackground);
            g.fillRect (bar);
            if (level > meterMinDb)
            {
                g.setGradientFill (gradient);
                g.fillRect (bar.withTop (y (bar, level)));
            }
            g.setColour (Palette::textDisabled);
            g.drawText (label, bar.withTop (bar.getBottom() + 1.0f).withHeight (11.0f).expanded (6.0f, 0.0f), juce::Justification::centred);
        }

        if (outHold > meterMinDb)
        {
            g.setColour (outHold > 0.0f ? Palette::meterRed : Palette::text.withAlpha (0.8f));
            g.fillRect (outBar.withY (y (outBar, outHold) - 1.0f).withHeight (2.0f));
        }
    }

    void MultibandView::resized()
    {
        auto r = getLocalBounds();
        auto header = r.removeFromTop (headerHeight).reduced (10, 5);
        postButton.setBounds (header.removeFromRight (52));
        header.removeFromRight (4);
        preButton.setBounds (header.removeFromRight (52));

        auto bottom = r.removeFromBottom (juce::jmax (300, r.getHeight() / 2)).reduced (8, 6);
        auto outputColumn = bottom.removeFromRight (84);
        outputKnob->setBounds (outputColumn.removeFromBottom (84).reduced (4, 0));
        outputMeterArea = outputColumn.reduced (4, 0).toFloat().withTrimmedBottom (14.0f);
        bottom.removeFromRight (6);

        const int w = bottom.getWidth() / panels.size();
        for (auto* panel : panels)
            panel->setBounds (bottom.removeFromLeft (w));

        graph->setBounds (r.reduced (8, 0));
    }
}
