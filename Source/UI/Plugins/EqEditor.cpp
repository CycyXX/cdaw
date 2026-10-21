#include "EqEditor.h"
#include "TeKnobParameter.h"
#include "../../Plugins/EqPlugin.h"
#include "../../Engine/MasterAnalyser.h"
#include "../LookAndFeel/Palette.h"
#include "../../Shared/UI/SpectrumAnalyser.h"

namespace cdaw
{
    using EqMath::BandType;

    namespace
    {
        constexpr int headerHeight = 32, stripHeight = 184;

        juce::String formatHz (double hz)
        {
            return hz >= 1000.0 ? juce::String (hz / 1000.0, hz >= 10000.0 ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (hz)) + " Hz";
        }

        juce::String formatDb (double db)
        {
            return (db > 0.04 ? "+" : "") + juce::String (db, 1) + " dB";
        }

        // Shape menu / combo ids: cuts carry their slope.
        int shapeToId (BandType t, int slope)
        {
            switch (t)
            {
                case BandType::lowCut:    return 1 + slope;
                case BandType::lowShelf:  return 5;
                case BandType::peak:      return 6;
                case BandType::highShelf: return 7;
                case BandType::highCut:   return 8 + slope;
                case BandType::notch:     return 12;
                case BandType::numTypes:  break;
            }
            return 6;
        }

        std::pair<BandType, int> idToShape (int id, int currentSlope)
        {
            if (id >= 1 && id <= 4)   return { BandType::lowCut, id - 1 };
            if (id == 5)              return { BandType::lowShelf, currentSlope };
            if (id == 7)              return { BandType::highShelf, currentSlope };
            if (id >= 8 && id <= 11)  return { BandType::highCut, id - 8 };
            if (id == 12)             return { BandType::notch, currentSlope };
            return { BandType::peak, currentSlope };
        }

        void addShapeItems (juce::PopupMenu& m, int currentId, std::function<void (int)> choose)
        {
            for (int s = 0; s < 4; ++s)
                m.addItem ("Low Cut " + juce::String (EqMath::slopes[(size_t) s]) + " dB/oct", true, currentId == 1 + s, [choose, s] { choose (1 + s); });
            m.addItem ("Low Shelf", true, currentId == 5, [choose] { choose (5); });
            m.addItem ("Peak", true, currentId == 6, [choose] { choose (6); });
            m.addItem ("High Shelf", true, currentId == 7, [choose] { choose (7); });
            for (int s = 0; s < 4; ++s)
                m.addItem ("High Cut " + juce::String (EqMath::slopes[(size_t) s]) + " dB/oct", true, currentId == 8 + s, [choose, s] { choose (8 + s); });
            m.addItem ("Notch", true, currentId == 12, [choose] { choose (12); });
        }

        void applyShape (EqPlugin& plugin, int band, int id)
        {
            plugin.edit.getUndoManager().beginNewTransaction ("EQ band shape");
            auto [type, slope] = idToShape (id, plugin.getSlope (band));
            plugin.setType (band, type);
            plugin.setSlope (band, slope);
        }
    }

    juce::Colour EqEditor::getBandColour (int band)
    {
        static const juce::Colour colours[EqPlugin::numBands] {
            juce::Colour (0xffe5484d), juce::Colour (0xfff28c28), juce::Colour (0xfff2d034), juce::Colour (0xff3ccf6e),
            juce::Colour (0xff4fd6c8), juce::Colour (0xff4fa3ff), juce::Colour (0xff9c8cff), juce::Colour (0xffff6fae)
        };
        return colours[juce::jlimit (0, EqPlugin::numBands - 1, band)];
    }

    //==============================================================================
    class EqEditor::Graph : public juce::Component
    {
    public:
        Graph (EqEditor& o, EqPlugin& p) : owner (o), plugin (p), post (p.postFifo), pre (p.preFifo)
        {
            setOpaque (true);
        }

        bool showPre = false, showPost = true;
        float range = 12.0f;        // EQ scale: +-range dB

        void setActive (bool active)
        {
            plugin.postFifo->active = active && showPost;
            plugin.preFifo->active = active && showPre;
        }

        void updateSpectrum()
        {
            bool changed = false;
            if (showPost) changed |= post.update (columnHz, plugin.getSampleRate());
            if (showPre)  changed |= pre.update (columnHz, plugin.getSampleRate());

            if (curveKey() != lastCurveKey)
                changed = true;

            if (changed)
                repaint();
        }

        //==============================================================================
        juce::Rectangle<float> plotArea() const
        {
            return getLocalBounds().toFloat().withTrimmedLeft (36.0f).withTrimmedRight (34.0f).withTrimmedTop (8.0f).withTrimmedBottom (18.0f);
        }

        float hzToX (double hz) const
        {
            auto p = plotArea();
            return p.getX() + (float) (std::log (hz / EqPlugin::minFreq) / std::log (EqPlugin::maxFreq / EqPlugin::minFreq)) * p.getWidth();
        }

        double xToHz (float x) const
        {
            auto p = plotArea();
            return EqPlugin::minFreq * std::pow (EqPlugin::maxFreq / EqPlugin::minFreq, juce::jlimit (0.0, 1.0, (double) ((x - p.getX()) / p.getWidth())));
        }

        float eqDbToY (double db) const
        {
            auto p = plotArea();
            return juce::jmap ((float) db, -range, range, p.getBottom(), p.getY());
        }

        double yToEqDb (float y) const
        {
            auto p = plotArea();
            return juce::jmap (y, p.getBottom(), p.getY(), -range, range);
        }

        float spectrumDbToY (float db) const
        {
            auto p = plotArea();
            return juce::jmap (juce::jlimit (spectrumMin, spectrumMax, db), spectrumMin, spectrumMax, p.getBottom(), p.getY());
        }

        juce::Point<float> nodePosition (int band) const
        {
            auto& b = plugin.getBand (band);
            auto type = plugin.getType (band);
            auto y = EqMath::hasGain (type) ? eqDbToY (juce::jlimit (-range, range, b.gain.getCurrentValue())) : eqDbToY (0.0);
            return { hzToX (b.freq.getCurrentValue()), y };
        }

        int nodeAt (juce::Point<float> p) const
        {
            int best = -1;
            float bestDistance = 12.0f;

            for (int i = 0; i < EqPlugin::numBands; ++i)
            {
                auto d = p.getDistanceFrom (nodePosition (i)) - (i == owner.selectedBand ? 2.0f : 0.0f);
                if (d < bestDistance)
                {
                    best = i;
                    bestDistance = d;
                }
            }

            return best;
        }

        //==============================================================================
        void resized() override
        {
            auto p = plotArea();
            columnHz.resize ((size_t) juce::jmax (1, (int) p.getWidth()));
            for (size_t c = 0; c < columnHz.size(); ++c)
                columnHz[c] = xToHz (p.getX() + (float) c + 0.5f);

            background = {};
            lastCurveKey = {};
        }

        juce::String curveKey() const
        {
            juce::String key;
            for (int i = 0; i < EqPlugin::numBands; ++i)
            {
                auto& b = plugin.getBand (i);
                key << (int) plugin.isOn (i) << (int) plugin.getType (i) << plugin.getSlope (i)
                    << b.freq.getCurrentValue() << b.gain.getCurrentValue() << b.q.getCurrentValue() << ";";
            }
            return key << plugin.output.getCurrentValue() << range << getWidth() << owner.selectedBand << hoverBand;
        }

        void rebuildCurve()
        {
            auto p = plotArea();
            const double rate = plugin.getSampleRate();
            total.clear();

            for (size_t c = 0; c < columnHz.size(); ++c)
            {
                auto y = eqDbToY (juce::jlimit ((double) -range - 3.0, (double) range + 3.0, plugin.getResponseDb (columnHz[c], rate)));
                if (c == 0) total.startNewSubPath (p.getX(), y);
                else        total.lineTo (p.getX() + (float) c, y);
            }

            lastCurveKey = curveKey();
        }

        juce::Path bandCurve (int band, bool closed) const
        {
            auto p = plotArea();
            const double rate = plugin.getSampleRate();
            auto filter = plugin.designBand (band, rate);
            juce::Path path;
            auto zero = eqDbToY (0.0);

            if (closed)
                path.startNewSubPath (p.getX(), zero);

            for (size_t c = 0; c < columnHz.size(); ++c)
            {
                auto y = eqDbToY (juce::jlimit ((double) -range - 3.0, (double) range + 3.0, filter.magnitudeDb (columnHz[c], rate)));
                if (c == 0 && ! closed) path.startNewSubPath (p.getX(), y);
                else                    path.lineTo (p.getX() + (float) c, y);
            }

            if (closed)
            {
                path.lineTo (p.getRight(), zero);
                path.closeSubPath();
            }

            return path;
        }

        //==============================================================================
        void drawBackground (juce::Graphics& g)
        {
            auto p = plotArea();
            g.setGradientFill (juce::ColourGradient::vertical (juce::Colour (0xff15171b), p.getY(), juce::Colour (0xff1b1e23), p.getBottom()));
            g.fillRect (getLocalBounds());

            g.setFont (juce::FontOptions (9.5f));

            for (double hz : { 20.0, 30.0, 40.0, 50.0, 60.0, 70.0, 80.0, 90.0, 100.0, 200.0, 300.0, 400.0, 500.0, 600.0, 700.0, 800.0, 900.0,
                               1000.0, 2000.0, 3000.0, 4000.0, 5000.0, 6000.0, 7000.0, 8000.0, 9000.0, 10000.0, 20000.0 })
            {
                const bool major = hz == 20.0 || hz == 50.0 || hz == 100.0 || hz == 200.0 || hz == 500.0 || hz == 1000.0
                                    || hz == 2000.0 || hz == 5000.0 || hz == 10000.0 || hz == 20000.0;
                auto x = hzToX (hz);
                g.setColour (Palette::panelOutline.withAlpha (major ? 0.55f : 0.22f));
                g.drawVerticalLine ((int) x, p.getY(), p.getBottom());

                if (major)
                {
                    g.setColour (Palette::textDisabled);
                    auto label = hz >= 1000.0 ? juce::String ((int) (hz / 1000.0)) + "k" : juce::String ((int) hz);
                    g.drawText (label, juce::Rectangle<float> (x - 20.0f, p.getBottom() + 3.0f, 40.0f, 12.0f), juce::Justification::centred);
                }
            }

            // EQ scale on the left, spectrum scale on the right.
            const float step = range <= 6.0f ? 2.0f : range <= 12.0f ? 3.0f : 6.0f;
            for (float db = -range; db <= range + 0.01f; db += step)
            {
                auto y = eqDbToY (db);
                g.setColour (Palette::panelOutline.withAlpha (db == 0.0f ? 0.8f : 0.3f));
                g.drawHorizontalLine ((int) y, p.getX(), p.getRight());
                g.setColour (db == 0.0f ? Palette::textDim : Palette::textDisabled);
                g.drawText ((db > 0 ? "+" : "") + juce::String ((int) db), juce::Rectangle<float> (2.0f, y - 6.0f, 30.0f, 12.0f), juce::Justification::centredRight);
            }

            g.setColour (Palette::textDisabled.withAlpha (0.7f));
            for (float db = spectrumMax; db >= spectrumMin; db -= 20.0f)
                g.drawText (juce::String ((int) db), juce::Rectangle<float> (p.getRight() + 3.0f, spectrumDbToY (db) - 6.0f, 30.0f, 12.0f), juce::Justification::centredLeft);
        }

        void paint (juce::Graphics& g) override
        {
            const auto scale = g.getInternalContext().getPhysicalPixelScaleFactor();
            if (background.isNull() || backgroundScale != scale || backgroundRange != range)
            {
                backgroundScale = scale;
                backgroundRange = range;
                background = juce::Image (juce::Image::RGB, juce::jmax (1, juce::roundToInt ((float) getWidth() * scale)),
                                          juce::jmax (1, juce::roundToInt ((float) getHeight() * scale)), true);
                juce::Graphics bg (background);
                bg.addTransform (juce::AffineTransform::scale (scale));
                drawBackground (bg);
            }

            g.drawImage (background, getLocalBounds().toFloat());

            auto p = plotArea();
            g.saveState();
            g.reduceClipRegion (p.toNearestInt());

            auto spectrumPath = [&] (const SpectrumAnalyser& a, bool closed)
            {
                juce::Path path;
                if (a.levels.empty())
                    return path;

                if (closed)
                    path.startNewSubPath (p.getX(), p.getBottom());

                for (size_t c = 0; c < a.levels.size(); ++c)
                {
                    auto y = spectrumDbToY (a.levels[c]);
                    if (c == 0 && ! closed) path.startNewSubPath (p.getX(), y);
                    else                    path.lineTo (p.getX() + (float) c, y);
                }

                if (closed)
                {
                    path.lineTo (p.getRight(), p.getBottom());
                    path.closeSubPath();
                }

                return path;
            };

            // Spectrum: before the EQ as a faint line, after it filled.
            if (showPre && ! pre.levels.empty())
            {
                g.setColour (juce::Colour (0xffb0b8c4).withAlpha (0.45f));
                g.strokePath (spectrumPath (pre, false), juce::PathStrokeType (1.0f));
            }

            if (showPost && ! post.levels.empty())
            {
                const juce::Colour spectrumColour (0xff5f8fbf);
                g.setGradientFill (juce::ColourGradient::vertical (spectrumColour.withAlpha (0.45f), p.getY(), spectrumColour.withAlpha (0.08f), p.getBottom()));
                g.fillPath (spectrumPath (post, true));
                g.setColour (spectrumColour.brighter (0.4f).withAlpha (0.7f));
                g.strokePath (spectrumPath (post, false), juce::PathStrokeType (1.0f));
            }

            // The focused band's own curve.
            auto focus = hoverBand >= 0 ? hoverBand : owner.selectedBand;
            if (focus >= 0)
            {
                auto colour = getBandColour (focus);
                g.setColour (colour.withAlpha (plugin.isOn (focus) ? 0.22f : 0.08f));
                g.fillPath (bandCurve (focus, true));
                g.setColour (colour.withAlpha (plugin.isOn (focus) ? 0.8f : 0.35f));
                g.strokePath (bandCurve (focus, false), juce::PathStrokeType (1.2f));
            }

            // Overall curve.
            if (curveKey() != lastCurveKey)
                rebuildCurve();

            juce::Path fill (total);
            fill.lineTo (p.getRight(), eqDbToY (0.0));
            fill.lineTo (p.getX(), eqDbToY (0.0));
            fill.closeSubPath();
            g.setColour (juce::Colours::white.withAlpha (0.07f));
            g.fillPath (fill);
            g.setColour (juce::Colours::white.withAlpha (0.92f));
            g.strokePath (total, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.restoreState();

            // Nodes, the selected one on top.
            for (int pass = 0; pass < 2; ++pass)
                for (int i = 0; i < EqPlugin::numBands; ++i)
                    if ((i == owner.selectedBand) == (pass == 1))
                        drawNode (g, i);

            drawReadout (g);
        }

        void drawNode (juce::Graphics& g, int band)
        {
            auto pos = nodePosition (band);
            auto colour = getBandColour (band);
            const bool on = plugin.isOn (band);
            const bool focused = band == owner.selectedBand || band == hoverBand;
            const float radius = focused ? 10.0f : 8.5f;
            auto circle = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (pos);

            if (on)
            {
                g.setColour (colour.withAlpha (0.25f));
                g.fillEllipse (circle.expanded (focused ? 5.0f : 3.0f));
                g.setColour (colour);
                g.fillEllipse (circle);
                g.setColour (juce::Colours::black.withAlpha (0.85f));
            }
            else
            {
                g.setColour (juce::Colour (0xff15171b).withAlpha (0.85f));
                g.fillEllipse (circle);
                g.setColour (colour.withAlpha (focused ? 0.9f : 0.5f));
                g.drawEllipse (circle, 1.5f);
            }

            if (band == owner.selectedBand)
            {
                g.setColour (Palette::text);
                g.drawEllipse (circle.expanded (2.5f), 1.3f);
                g.setColour (on ? juce::Colours::black.withAlpha (0.85f) : colour);
            }

            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.drawText (juce::String (band + 1), circle, juce::Justification::centred);
        }

        void drawReadout (juce::Graphics& g)
        {
            auto p = plotArea();
            g.setFont (juce::FontOptions (11.5f));

            // Band details next to the focused node.
            auto focus = dragBand >= 0 ? dragBand : hoverBand;
            if (focus >= 0)
            {
                auto& b = plugin.getBand (focus);
                auto type = plugin.getType (focus);
                juce::String line1 = juce::String (focus + 1) + "  " + EqMath::getTypeName (type)
                                     + (EqMath::isCut (type) ? " " + juce::String (EqMath::slopes[(size_t) plugin.getSlope (focus)]) + " dB/oct" : juce::String())
                                     + (plugin.isOn (focus) ? juce::String() : "  (off)");
                juce::String line2 = formatHz (b.freq.getCurrentValue());
                if (EqMath::hasGain (type))
                    line2 << "   " << formatDb (b.gain.getCurrentValue());
                if (! (EqMath::isCut (type) && plugin.getSlope (focus) != 1))
                    line2 << "   Q " << juce::String (b.q.getCurrentValue(), 2);

                auto pos = nodePosition (focus);
                auto w = juce::jmax (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), line1),
                                     juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), line2)) + 16.0f;
                juce::Rectangle<float> box (pos.x + 16.0f, pos.y - 44.0f, w, 38.0f);
                if (box.getRight() > p.getRight()) box.setX (pos.x - 16.0f - w);
                if (box.getY() < p.getY()) box.setY (pos.y + 16.0f);

                g.setColour (juce::Colour (0xee101215));
                g.fillRoundedRectangle (box, 4.0f);
                g.setColour (getBandColour (focus).withAlpha (0.8f));
                g.drawRoundedRectangle (box, 4.0f, 1.0f);
                g.setColour (Palette::text);
                g.drawText (line1, box.reduced (8.0f, 3.0f).removeFromTop (16.0f), juce::Justification::centredLeft);
                g.setColour (Palette::textDim);
                g.drawText (line2, box.reduced (8.0f, 3.0f).removeFromBottom (16.0f), juce::Justification::centredLeft);
            }
            else if (p.contains (mouse))
            {
                // Frequency and EQ level under the mouse.
                auto text = formatHz (xToHz (mouse.x)) + "   " + formatDb (yToEqDb (mouse.y));
                g.setColour (Palette::textDim);
                g.drawText (text, p.reduced (8.0f, 6.0f).removeFromTop (14.0f), juce::Justification::centredRight);
            }
        }

        //==============================================================================
        void mouseMove (const juce::MouseEvent& e) override
        {
            mouse = e.position;
            auto band = nodeAt (e.position);
            if (band != hoverBand)
                hoverBand = band;

            setMouseCursor (band >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::CrosshairCursor);
            repaint();
        }

        void mouseExit (const juce::MouseEvent&) override
        {
            hoverBand = -1;
            mouse = { -1.0f, -1.0f };
            repaint();
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            auto band = nodeAt (e.position);

            if (e.mods.isPopupMenu())
            {
                if (band >= 0)
                    showBandMenu (band);
                return;
            }

            if (band < 0)
                return;

            owner.selectBand (band);
            dragBand = band;
            dragStart = e.position;
            auto& b = plugin.getBand (band);
            startFreq = b.freq.getCurrentValue();
            startGain = b.gain.getCurrentValue();

            plugin.edit.getUndoManager().beginNewTransaction ("EQ band " + juce::String (band + 1));
            if (! plugin.isOn (band))
                plugin.setOn (band, true);

            b.freq.getParameter().parameterChangeGestureBegin();
            b.gain.getParameter().parameterChangeGestureBegin();
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (dragBand < 0)
                return;

            mouse = e.position;
            auto& b = plugin.getBand (dragBand);
            const float fine = e.mods.isShiftDown() ? 0.15f : 1.0f;

            // Relative drag (so Shift can slow it down) in the graph's own log / dB space.
            auto x = hzToX (startFreq) + (e.position.x - dragStart.x) * fine;
            b.freq.setParameter ((float) juce::jlimit ((double) EqPlugin::minFreq, (double) EqPlugin::maxFreq, xToHz (x)), juce::sendNotification);

            if (EqMath::hasGain (plugin.getType (dragBand)))
            {
                auto y = eqDbToY (startGain) + (e.position.y - dragStart.y) * fine;
                b.gain.setParameter ((float) juce::jlimit (-(double) EqPlugin::maxGain, (double) EqPlugin::maxGain, yToEqDb (y)), juce::sendNotification);
            }

            repaint();
        }

        void mouseUp (const juce::MouseEvent&) override
        {
            if (dragBand >= 0)
            {
                auto& b = plugin.getBand (dragBand);
                b.freq.getParameter().parameterChangeGestureEnd();
                b.gain.getParameter().parameterChangeGestureEnd();
            }

            dragBand = -1;
            repaint();
        }

        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            auto band = nodeAt (e.position);

            if (band >= 0)
            {
                // The first click of the double-click switched an off band on; this one undoes it.
                plugin.edit.getUndoManager().beginNewTransaction ("EQ band on/off");
                plugin.setOn (band, ! plugin.isOn (band));
                return;
            }

            if (! plotArea().contains (e.position))
                return;

            // Add a band: the unused band whose home frequency is nearest, as a peak, right here.
            auto hz = xToHz (e.position.x);
            int best = -1;
            double bestDistance = 1.0e9;

            for (int i = 0; i < EqPlugin::numBands; ++i)
            {
                auto d = std::abs (std::log (hz / EqPlugin::getDefaultFreq (i)));
                if (! plugin.isOn (i) && d < bestDistance)
                {
                    best = i;
                    bestDistance = d;
                }
            }

            if (best < 0)
                return;

            plugin.edit.getUndoManager().beginNewTransaction ("Add EQ band");
            auto& b = plugin.getBand (best);
            plugin.setType (best, BandType::peak);
            b.freq.setParameter ((float) hz, juce::sendNotification);
            b.gain.setParameter ((float) juce::jlimit (-(double) EqPlugin::maxGain, (double) EqPlugin::maxGain, yToEqDb (e.position.y)), juce::sendNotification);
            b.q.setParameter (EqPlugin::getDefaultQ (BandType::peak), juce::sendNotification);
            plugin.setOn (best, true);
            owner.selectBand (best);
        }

        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
        {
            auto band = nodeAt (e.position);
            if (band < 0)
                band = owner.selectedBand;

            if (band < 0)
                return;

            // Q: wheel up narrows (higher Q), like most EQs.
            auto& q = plugin.getBand (band).q;
            auto factor = std::pow (2.0f, wheel.deltaY * (wheel.isReversed ? -1.5f : 1.5f));
            q.setParameter (juce::jlimit (EqPlugin::minQ, EqPlugin::maxQ, q.getCurrentValue() * factor), juce::sendNotification);
            repaint();
        }

        void showBandMenu (int band)
        {
            owner.selectBand (band);
            juce::PopupMenu m;
            auto* p = &plugin;
            m.addItem (plugin.isOn (band) ? "Switch off" : "Switch on", [p, band]
            {
                p->edit.getUndoManager().beginNewTransaction ("EQ band on/off");
                p->setOn (band, ! p->isOn (band));
            });
            m.addSeparator();
            addShapeItems (m, shapeToId (plugin.getType (band), plugin.getSlope (band)), [p, band] (int id) { applyShape (*p, band, id); });
            m.addSeparator();
            m.addItem ("Reset band", [p, band]
            {
                p->edit.getUndoManager().beginNewTransaction ("Reset EQ band");
                auto& b = p->getBand (band);
                auto type = EqPlugin::getDefaultType (band);
                p->setType (band, type);
                p->setSlope (band, 2);
                b.freq.setParameter (EqPlugin::getDefaultFreq (band), juce::sendNotification);
                b.gain.setParameter (0.0f, juce::sendNotification);
                b.q.setParameter (EqPlugin::getDefaultQ (type), juce::sendNotification);
                p->setOn (band, false);
            });
            m.showMenuAsync ({});
        }

        EqEditor& owner;
        EqPlugin& plugin;
        SpectrumAnalyser post, pre;
        std::vector<double> columnHz;
        juce::Path total;
        juce::String lastCurveKey;
        juce::Image background;
        float backgroundScale = 0.0f, backgroundRange = 0.0f;
        int hoverBand = -1, dragBand = -1;
        juce::Point<float> mouse { -1.0f, -1.0f }, dragStart;
        float startFreq = 1000.0f, startGain = 0.0f;
        static constexpr float spectrumMin = -100.0f, spectrumMax = 0.0f;
    };

    //==============================================================================
    class EqEditor::BandStrip : public juce::Component
    {
    public:
        BandStrip (EqEditor& o, EqPlugin& p, int b) : owner (o), plugin (p), band (b)
        {
            auto& bandParams = plugin.getBand (band);
            auto colour = getBandColour (band);

            onButton.setButtonText (juce::String (band + 1));
            onButton.setClickingTogglesState (false);
            onButton.setTooltip ("Band " + juce::String (band + 1) + " on / off");
            onButton.onClick = [this]
            {
                plugin.edit.getUndoManager().beginNewTransaction ("EQ band on/off");
                plugin.setOn (band, ! plugin.isOn (band));
                owner.selectBand (band);
            };
            addAndMakeVisible (onButton);

            // Short names: the strips are narrow (the graph's menu has the full ones).
            for (int s = 0; s < 4; ++s)  shapeBox.addItem ("LC " + juce::String (EqMath::slopes[(size_t) s]), 1 + s);
            shapeBox.addItem ("LShelf", 5);
            shapeBox.addItem ("Peak", 6);
            shapeBox.addItem ("HShelf", 7);
            for (int s = 0; s < 4; ++s)  shapeBox.addItem ("HC " + juce::String (EqMath::slopes[(size_t) s]), 8 + s);
            shapeBox.addItem ("Notch", 12);

            shapeBox.onChange = [this]
            {
                if (! updating)
                    applyShape (plugin, band, shapeBox.getSelectedId());
            };
            addAndMakeVisible (shapeBox);

            freq = std::make_unique<ParamKnob> (knobParameter (bandParams.freq.getParameter()), "Freq", colour, EqPlugin::getDefaultFreq (band), 1000.0f);
            gain = std::make_unique<ParamKnob> (knobParameter (bandParams.gain.getParameter()), "Gain", colour, 0.0f);
            q = std::make_unique<ParamKnob> (knobParameter (bandParams.q.getParameter()), "Q", colour, EqPlugin::getDefaultQ (plugin.getType (band)), 1.0f);

            for (auto* k : { freq.get(), gain.get(), q.get() })
                addAndMakeVisible (k);

            refresh();
        }

        void refresh()
        {
            juce::ScopedValueSetter<bool> svs (updating, true);
            const bool on = plugin.isOn (band);
            auto type = plugin.getType (band);
            auto colour = getBandColour (band);

            onButton.setColour (juce::TextButton::buttonColourId, on ? colour : Palette::panelRaised);
            onButton.setColour (juce::TextButton::textColourOffId, on ? juce::Colours::black : colour);
            shapeBox.setSelectedId (shapeToId (type, plugin.getSlope (band)), juce::dontSendNotification);
            shapeBox.setTooltip (juce::String (EqMath::getTypeName (type))
                                 + (EqMath::isCut (type) ? " " + juce::String (EqMath::slopes[(size_t) plugin.getSlope (band)]) + " dB/oct" : juce::String()));

            gain->setEnabled (EqMath::hasGain (type));
            q->setEnabled (! (EqMath::isCut (type) && plugin.getSlope (band) != 1));

            for (auto* k : { freq.get(), gain.get(), q.get() })
            {
                k->setColour (on ? colour : colour.withSaturation (0.15f).withAlpha (0.6f));
                k->refresh();
            }

            if (on != wasOn || selected != wasSelected)
            {
                wasOn = on;
                wasSelected = selected;
                repaint();
            }
        }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat().reduced (2.0f, 0.0f);
            g.setColour (selected ? Palette::panelRaised.brighter (0.06f) : Palette::panelRaised.withAlpha (0.55f));
            g.fillRoundedRectangle (r, 5.0f);

            g.setColour (getBandColour (band).withAlpha (plugin.isOn (band) ? 0.9f : 0.3f));
            g.fillRoundedRectangle (r.removeFromTop (3.0f), 1.5f);

            if (selected)
            {
                g.setColour (getBandColour (band).withAlpha (0.6f));
                g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (2.5f, 0.5f), 5.0f, 1.0f);
            }
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (6, 8);
            auto top = r.removeFromTop (22);
            onButton.setBounds (top.removeFromLeft (22));
            top.removeFromLeft (3);
            shapeBox.setBounds (top);

            r.removeFromTop (4);
            auto knobs = r.removeFromTop (r.getHeight() / 2);
            freq->setBounds (knobs.removeFromLeft (knobs.getWidth() / 2));
            gain->setBounds (knobs);
            q->setBounds (r.withSizeKeepingCentre (juce::jmin (r.getWidth(), 70), r.getHeight()));
        }

        void mouseDown (const juce::MouseEvent&) override       { owner.selectBand (band); }

        EqEditor& owner;
        EqPlugin& plugin;
        const int band;
        juce::TextButton onButton;
        juce::ComboBox shapeBox;
        std::unique_ptr<ParamKnob> freq, gain, q;
        bool updating = false, selected = false, wasOn = false, wasSelected = false;
    };

    //==============================================================================
    EqEditor::EqEditor (EqPlugin& p) : plugin (p), pluginRef (&p)
    {
        setOpaque (true);
        graph = std::make_unique<Graph> (*this, plugin);
        addAndMakeVisible (*graph);

        for (int i = 0; i < EqPlugin::numBands; ++i)
            addAndMakeVisible (strips.add (new BandStrip (*this, plugin, i)));

        outputKnob = std::make_unique<ParamKnob> (knobParameter (plugin.output.getParameter()), "Output", Palette::text, 0.0f);
        addAndMakeVisible (*outputKnob);

        auto setupToggle = [this] (juce::TextButton& b, bool on, const char* tip, std::function<void (bool)> apply)
        {
            b.setClickingTogglesState (true);
            b.setToggleState (on, juce::dontSendNotification);
            b.setTooltip (tip);
            b.setColour (juce::TextButton::buttonOnColourId, Palette::accentDim);
            b.onClick = [this, &b, apply] { apply (b.getToggleState()); graph->setActive (isShowing()); graph->repaint(); };
            addAndMakeVisible (b);
        };

        setupToggle (preButton, false, "Show the spectrum before the EQ", [this] (bool on) { graph->showPre = on; });
        setupToggle (postButton, true, "Show the spectrum after the EQ", [this] (bool on) { graph->showPost = on; });

        rangeButton.setTooltip ("EQ display range");
        rangeButton.onClick = [this]
        {
            graph->range = graph->range >= 24.0f ? 6.0f : graph->range * 2.0f;
            rangeButton.setButtonText (juce::String::fromUTF8 ("\xc2\xb1") + juce::String ((int) graph->range) + " dB");
            graph->repaint();
        };
        rangeButton.setButtonText (juce::String::fromUTF8 ("\xc2\xb1") + juce::String ((int) graph->range) + " dB");
        addAndMakeVisible (rangeButton);

        // Start with the first band that's on, if any.
        for (int i = 0; i < EqPlugin::numBands && selectedBand < 0; ++i)
            if (plugin.isOn (i))
                selectBand (i);

        constrainer.setMinimumSize (760, 460);
        setSize (1000, 600);
        startTimerHz (30);
    }

    EqEditor::~EqEditor()
    {
        stopTimer();
        graph->setActive (false);
    }

    void EqEditor::visibilityChanged()
    {
        graph->setActive (isShowing());
    }

    void EqEditor::selectBand (int band)
    {
        if (band == selectedBand)
            return;

        selectedBand = band;
        for (int i = 0; i < strips.size(); ++i)
            strips[i]->selected = i == band;

        graph->repaint();
    }

    void EqEditor::updateSpectrum()
    {
        graph->updateSpectrum();
    }

    void EqEditor::timerCallback()
    {
        if (! isShowing())
            return;

        graph->setActive (true);
        graph->updateSpectrum();

        for (auto* s : strips)
            s->refresh();

        outputKnob->refresh();
    }

    void EqEditor::paint (juce::Graphics& g)
    {
        g.fillAll (Palette::panelBackground);

        auto header = getLocalBounds().removeFromTop (headerHeight).reduced (12, 0);
        g.setColour (Palette::text);
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.drawText ("EQ", header, juce::Justification::centredLeft);

        g.setColour (Palette::textDim);
        g.setFont (juce::FontOptions (11.5f));
        g.drawText ("Drag a node: frequency / gain (Shift: fine)  -  Wheel: Q  -  Double-click: add band / on-off  -  Right-click: shape",
                    header.withTrimmedLeft (44).withTrimmedRight (250), juce::Justification::centredLeft);
    }

    void EqEditor::resized()
    {
        auto r = getLocalBounds();
        auto header = r.removeFromTop (headerHeight).reduced (10, 5);
        rangeButton.setBounds (header.removeFromRight (64));
        header.removeFromRight (10);
        postButton.setBounds (header.removeFromRight (52));
        header.removeFromRight (4);
        preButton.setBounds (header.removeFromRight (52));

        auto bottom = r.removeFromBottom (stripHeight).reduced (8, 6);
        outputKnob->setBounds (bottom.removeFromRight (72).withSizeKeepingCentre (72, 84));
        bottom.removeFromRight (6);

        const int w = bottom.getWidth() / strips.size();
        for (auto* s : strips)
            s->setBounds (bottom.removeFromLeft (w));

        graph->setBounds (r.reduced (8, 0));
    }
}
