#include "CompressorEditor.h"
#include "TeKnobParameter.h"
#include "../../Plugins/CompressorPlugin.h"
#include "../../Model/MixerOps.h"
#include "../LookAndFeel/Palette.h"

namespace cdaw
{
    namespace
    {
        const juce::Colour reductionColour { 0xffff5a4f };
        const juce::Colour thresholdColour { 0xfff2b134 };
        const juce::Colour outputColour    { 0xff4fd6c8 };
        const juce::Colour inputColour     { 0xff9aa6b8 };

        float toDb (float gain)     { return juce::Decibels::gainToDecibels (gain, -100.0f); }

        juce::String dbText (float db, int decimals = 1)
        {
            return db <= -99.0f ? juce::String ("-inf") : juce::String (db, decimals);
        }

        void drawPanel (juce::Graphics& g, juce::Rectangle<float> r)
        {
            g.setColour (Palette::windowBackground);
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (Palette::panelOutline.withAlpha (0.7f));
            g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
        }

        constexpr int historyLength = 360;      // 6 s at 60 Hz
        constexpr int headerHeight = 34, knobRowHeight = 92;
    }

    CompressorEditor::CompressorEditor (CompressorPlugin& p) : plugin (p), pluginRef (&p)
    {
        setOpaque (true);
        history.assign ((size_t) historyLength, { -100.0f, -100.0f, 0.0f });

        auto add = [this] (te::AutomatableParameter& param, const char* label, juce::Colour c, float def, float mid, bool invert = false)
        {
            auto* knob = knobs.add (new ParamKnob (knobParameter (param), label, c, def, mid, invert));
            addAndMakeVisible (knob);
            return knob;
        };

        add (plugin.thresholdGain.getParameter(), "Threshold", thresholdColour, juce::Decibels::decibelsToGain (-6.0f), juce::Decibels::decibelsToGain (-18.0f));
        add (plugin.ratio.getParameter(), "Ratio", Palette::accent, 0.5f, 0.25f, true);
        add (plugin.knee.getParameter(), "Knee", Palette::accent, 6.0f, 6.0f);
        add (plugin.attackMs.getParameter(), "Attack", Palette::tempo, 100.0f, 10.0f);
        add (plugin.releaseMs.getParameter(), "Release", Palette::tempo, 100.0f, 80.0f);
        add (plugin.outputDb.getParameter(), "Make-up", outputColour, 0.0f, 0.0f);
        sidechainKnob = add (plugin.sidechainDb.getParameter(), "SC Gain", Palette::marker, 0.0f, 0.0f);

        sidechainButton.setTooltip ("Key the compressor from another track");
        sidechainButton.onClick = [this]
        {
            MixerOps::createSidechainMenu (plugin).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&sidechainButton));
        };
        addAndMakeVisible (sidechainButton);
        updateSidechainButton();

        constrainer.setMinimumSize (640, 380);
        setSize (860, 460);
        startTimerHz (60);
    }

    CompressorEditor::~CompressorEditor()
    {
        stopTimer();
    }

    void CompressorEditor::updateSidechainButton()
    {
        auto* source = MixerOps::getSidechainSource (plugin);
        auto text = source != nullptr ? "Sidechain: " + source->getName() : juce::String ("Sidechain: off");

        if (sidechainButton.getButtonText() != text)
            sidechainButton.setButtonText (text);

        sidechainButton.setColour (juce::TextButton::buttonColourId, source != nullptr ? Palette::marker.withAlpha (0.35f) : Palette::panelRaised);
        sidechainKnob->setEnabled (source != nullptr);
    }

    //==============================================================================
    void CompressorEditor::timerCallback()
    {
        tick (plugin.inputPeak[0].take(), plugin.inputPeak[1].take(), plugin.outputPeak[0].take(), plugin.outputPeak[1].take(),
              plugin.detectorPeak.take(), plugin.gainReduction.take());

        for (auto* k : knobs)
            k->refresh();

        updateSidechainButton();
    }

    void CompressorEditor::tick (float inL, float inR, float outL, float outR, float detector, float reductionDb)
    {
        const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
        constexpr float fallPerTick = 0.9f;        // dB per frame (~54 dB/s)
        constexpr double holdSeconds = 1.5;

        auto fall = [] (float& shown, float value)  { shown = value >= shown ? value : juce::jmax (value, shown - fallPerTick); };
        auto hold = [&] (float& held, float value, double& since, bool higherIsMore)
        {
            if (higherIsMore ? value >= held : value >= held)
            {
                held = value;
                since = now;
            }
            else if (now - since > holdSeconds)
            {
                held = juce::jmax (value, held - fallPerTick * 0.5f);
            }
        };

        const float in[2] { toDb (inL), toDb (inR) }, out[2] { toDb (outL), toDb (outR) };

        for (int ch = 0; ch < 2; ++ch)
        {
            fall (inLevel[ch], in[ch]);
            fall (outLevel[ch], out[ch]);
        }

        hold (inHold[0], juce::jmax (inLevel[0], inLevel[1]), holdTime[0], true);
        inHold[1] = inHold[0];
        hold (outHold[0], juce::jmax (outLevel[0], outLevel[1]), holdTime[1], true);
        outHold[1] = outHold[0];

        // Gain reduction rises instantly and recovers like the meters fall.
        grLevel = reductionDb >= grLevel ? reductionDb : juce::jmax (reductionDb, grLevel - fallPerTick);
        hold (grHold, grLevel, holdTime[2], true);

        auto det = toDb (detector);
        detectorDb = det >= detectorDb ? det : juce::jmax (det, detectorDb - fallPerTick);

        // The display ticks at 60 Hz but audio arrives in blocks, so a tick can see no new block:
        // repeat the previous frame for short gaps rather than drawing a drop to silence.
        Frame frame { juce::jmax (in[0], in[1]), juce::jmax (out[0], out[1]), reductionDb };
        const bool noData = inL == 0.0f && inR == 0.0f && outL == 0.0f && outR == 0.0f && detector == 0.0f;
        emptyTicks = noData ? emptyTicks + 1 : 0;

        if (noData && emptyTicks <= 3)
            frame = history[(size_t) ((historyWrite + historyLength - 1) % historyLength)];

        history[(size_t) historyWrite] = frame;
        historyWrite = (historyWrite + 1) % historyLength;

        repaint (getLocalBounds().withTrimmedTop (headerHeight).withTrimmedBottom (knobRowHeight));
    }

    //==============================================================================
    void CompressorEditor::resized()
    {
        auto r = getLocalBounds().reduced (10, 0);
        auto header = r.removeFromTop (headerHeight).reduced (0, 5);
        sidechainButton.setBounds (header.removeFromRight (180));

        auto knobRow = r.removeFromBottom (knobRowHeight).reduced (0, 6);
        const int knobWidth = knobRow.getWidth() / knobs.size();
        for (auto* k : knobs)
            k->setBounds (knobRow.removeFromLeft (knobWidth).reduced (4, 0));

        auto body = r.reduced (0, 4).toFloat();
        inMeterArea = body.removeFromLeft (52.0f);
        body.removeFromLeft (8.0f);
        outMeterArea = body.removeFromRight (52.0f);
        body.removeFromRight (8.0f);
        grMeterArea = body.removeFromRight (40.0f);
        body.removeFromRight (8.0f);

        auto side = juce::jmin (body.getHeight(), body.getWidth() * 0.42f);
        transferArea = body.removeFromLeft (side);
        body.removeFromLeft (8.0f);
        historyArea = body;
    }

    float CompressorEditor::meterY (juce::Rectangle<float> bar, float db) const
    {
        return juce::jmap (juce::jlimit (meterMinDb, meterMaxDb, db), meterMinDb, meterMaxDb, bar.getBottom(), bar.getY());
    }

    static juce::Rectangle<float> transferPlot (juce::Rectangle<float> area)
    {
        // Room for the dB labels on the left and below.
        return area.withTrimmedLeft (30.0f).withTrimmedBottom (18.0f).withTrimmedTop (10.0f).withTrimmedRight (10.0f);
    }

    float CompressorEditor::transferX (float db) const
    {
        auto plot = transferPlot (transferArea);
        return juce::jmap (juce::jlimit (graphMinDb, 0.0f, db), graphMinDb, 0.0f, plot.getX(), plot.getRight());
    }

    float CompressorEditor::transferY (float db) const
    {
        auto plot = transferPlot (transferArea);
        return juce::jmap (juce::jlimit (graphMinDb, 0.0f, db), graphMinDb, 0.0f, plot.getBottom(), plot.getY());
    }

    juce::Point<float> CompressorEditor::getThresholdHandle() const
    {
        auto t = plugin.getThresholdDb();
        return { transferX (t), transferY (t) };
    }

    juce::Point<float> CompressorEditor::getRatioHandle() const
    {
        return { transferX (0.0f), transferY (plugin.transfer (0.0f)) };
    }

    //==============================================================================
    void CompressorEditor::paint (juce::Graphics& g)
    {
        g.fillAll (Palette::panelBackground);

        // Header
        auto header = getLocalBounds().removeFromTop (headerHeight).reduced (12, 0);
        g.setColour (Palette::text);
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.drawText ("COMPRESSOR", header, juce::Justification::centredLeft);

        g.setFont (juce::FontOptions (12.0f));
        g.setColour (Palette::textDim);
        auto summary = juce::String (plugin.getThresholdDb(), 1) + " dB  ·  "
                       + (plugin.getRatio() > 100.0f ? juce::String ("Limit") : juce::String (plugin.getRatio(), 1) + ":1")
                       + "  ·  knee " + juce::String (plugin.getKneeDb(), 0) + " dB";
        g.drawText (summary, header.withTrimmedLeft (130).withTrimmedRight (200), juce::Justification::centredLeft);

        drawMeter (g, inMeterArea, "IN", inLevel[0], inLevel[1], inHold[0], inHold[1], true);
        drawTransfer (g, transferArea);
        drawHistory (g, historyArea);
        drawReductionMeter (g, grMeterArea);
        drawMeter (g, outMeterArea, "OUT", outLevel[0], outLevel[1], outHold[0], outHold[1], false);

        g.setColour (Palette::panelOutline);
        g.drawHorizontalLine (getHeight() - knobRowHeight, 10.0f, (float) getWidth() - 10.0f);
    }

    void CompressorEditor::drawMeter (juce::Graphics& g, juce::Rectangle<float> area, const char* title,
                                      float l, float r, float holdL, float holdR, bool withThreshold)
    {
        drawPanel (g, area);
        auto inner = area.reduced (5.0f, 4.0f);

        g.setColour (Palette::textDim);
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawText (title, inner.removeFromTop (13.0f), juce::Justification::centred);

        // Peak hold read-out, red when over 0 dB.
        auto peak = juce::jmax (holdL, holdR);
        g.setColour (peak > 0.0f ? Palette::meterRed : Palette::text);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (dbText (peak), inner.removeFromTop (14.0f), juce::Justification::centred);
        inner.removeFromTop (4.0f);

        auto scale = inner.removeFromLeft (16.0f);
        auto bars = inner.reduced (1.0f, 0.0f);
        auto barL = bars.removeFromLeft (bars.getWidth() * 0.5f).reduced (1.0f, 0.0f);
        auto barR = bars.reduced (1.0f, 0.0f);

        g.setFont (juce::FontOptions (9.0f));
        for (float db : { 6.0f, 0.0f, -6.0f, -12.0f, -18.0f, -24.0f, -36.0f, -48.0f, -60.0f })
        {
            auto y = meterY (barL, db);
            g.setColour (Palette::textDisabled);
            g.drawText (juce::String ((int) db), scale.withY (y - 5.0f).withHeight (10.0f), juce::Justification::centredRight);
            g.setColour (Palette::panelOutline.withAlpha (0.5f));
            g.drawHorizontalLine ((int) y, barL.getX(), barR.getRight());
        }

        auto gradient = juce::ColourGradient::vertical (Palette::meterRed, meterY (barL, 3.0f), Palette::meterGreen, meterY (barL, -18.0f));
        gradient.addColour (0.45, Palette::meterYellow);

        for (auto [bar, level, held] : { std::tuple (barL, l, holdL), std::tuple (barR, r, holdR) })
        {
            g.setColour (Palette::panelBackground);
            g.fillRect (bar);

            if (level > meterMinDb)
            {
                g.setGradientFill (gradient);
                g.fillRect (bar.withTop (meterY (bar, level)));
            }

            if (held > meterMinDb)
            {
                g.setColour (held > 0.0f ? Palette::meterRed : Palette::text.withAlpha (0.8f));
                g.fillRect (bar.withY (meterY (bar, held) - 1.0f).withHeight (2.0f));
            }
        }

        if (withThreshold)
        {
            // Threshold: a line across the meter with a handle that can be dragged.
            auto y = meterY (barL, plugin.getThresholdDb());
            g.setColour (thresholdColour);
            g.drawHorizontalLine ((int) y, barL.getX() - 2.0f, barR.getRight() + 2.0f);
            juce::Path arrow;
            arrow.addTriangle (area.getRight() - 1.0f, y, area.getRight() + 6.0f, y - 5.0f, area.getRight() + 6.0f, y + 5.0f);
            g.setColour (hover == Handle::meterThreshold || dragging == Handle::meterThreshold ? thresholdColour.brighter (0.4f) : thresholdColour);
            g.fillPath (arrow);
        }
    }

    void CompressorEditor::drawReductionMeter (juce::Graphics& g, juce::Rectangle<float> area)
    {
        drawPanel (g, area);
        auto inner = area.reduced (5.0f, 4.0f);

        g.setColour (Palette::textDim);
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawText ("GR", inner.removeFromTop (13.0f), juce::Justification::centred);

        g.setColour (grHold > 0.05f ? reductionColour : Palette::text);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (grHold > 0.05f ? "-" + juce::String (grHold, 1) : juce::String ("0.0"), inner.removeFromTop (14.0f).expanded (6.0f, 0.0f),
                    juce::Justification::centred);
        inner.removeFromTop (4.0f);

        auto bar = inner.reduced (6.0f, 0.0f);
        auto y = [&] (float reduction) { return juce::jmap (juce::jlimit (0.0f, maxReductionDb, reduction), 0.0f, maxReductionDb, bar.getY(), bar.getBottom()); };

        g.setColour (Palette::panelBackground);
        g.fillRect (bar);

        g.setFont (juce::FontOptions (9.0f));
        for (float db : { 3.0f, 6.0f, 10.0f, 15.0f, 20.0f, 30.0f })
        {
            g.setColour (Palette::panelOutline.withAlpha (0.6f));
            g.drawHorizontalLine ((int) y (db), bar.getX(), bar.getRight());
        }

        // Grows downwards from the top: how far the signal is being pulled down.
        g.setGradientFill (juce::ColourGradient::vertical (reductionColour.withAlpha (0.75f), bar.getY(), reductionColour, y (maxReductionDb)));
        g.fillRect (bar.withBottom (y (grLevel)));

        if (grHold > 0.05f)
        {
            g.setColour (Palette::text.withAlpha (0.8f));
            g.fillRect (bar.withY (y (grHold) - 1.0f).withHeight (2.0f));
        }

        g.setColour (Palette::textDisabled);
        for (float db : { 6.0f, 15.0f, 30.0f })
            g.drawText (juce::String ((int) db), area.withY (y (db) - 5.0f).withHeight (10.0f).withTrimmedRight (2.0f), juce::Justification::centredRight);
    }

    void CompressorEditor::drawTransfer (juce::Graphics& g, juce::Rectangle<float> area)
    {
        drawPanel (g, area);
        auto plot = transferPlot (area);

        // Grid every 12 dB with labels on both axes.
        g.setFont (juce::FontOptions (9.0f));
        for (float db = graphMinDb; db <= 0.0f; db += 12.0f)
        {
            g.setColour (Palette::panelOutline.withAlpha (db == 0.0f ? 0.9f : 0.45f));
            g.drawVerticalLine ((int) transferX (db), plot.getY(), plot.getBottom());
            g.drawHorizontalLine ((int) transferY (db), plot.getX(), plot.getRight());

            g.setColour (Palette::textDisabled);
            g.drawText (juce::String ((int) db), juce::Rectangle<float> (plot.getX() - 26.0f, transferY (db) - 5.0f, 22.0f, 10.0f), juce::Justification::centredRight);
            g.drawText (juce::String ((int) db), juce::Rectangle<float> (transferX (db) - 14.0f, plot.getBottom() + 3.0f, 28.0f, 10.0f), juce::Justification::centred);
        }

        g.setColour (Palette::textDim);
        g.drawText ("in", juce::Rectangle<float> (plot.getRight() - 30.0f, plot.getBottom() - 13.0f, 26.0f, 10.0f), juce::Justification::centredRight);
        g.drawText ("out", juce::Rectangle<float> (plot.getX() + 4.0f, plot.getY() + 2.0f, 30.0f, 10.0f), juce::Justification::centredLeft);

        // 1:1 reference.
        g.setColour (Palette::textDisabled.withAlpha (0.6f));
        juce::Path diagonal;
        diagonal.startNewSubPath (transferX (graphMinDb), transferY (graphMinDb));
        diagonal.lineTo (transferX (0.0f), transferY (0.0f));
        const float dashes[] { 3.0f, 3.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (diagonal, diagonal, dashes, 2);
        g.fillPath (diagonal);

        // Knee region.
        const auto threshold = plugin.getThresholdDb();
        const auto knee = plugin.getKneeDb();
        if (knee > 0.0f)
        {
            g.setColour (thresholdColour.withAlpha (0.08f));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (transferX (threshold - knee * 0.5f), plot.getY(),
                                                                    transferX (threshold + knee * 0.5f), plot.getBottom()));
        }

        g.setColour (thresholdColour.withAlpha (0.5f));
        g.drawVerticalLine ((int) transferX (threshold), plot.getY(), plot.getBottom());

        // The curve, with the area it removes shaded.
        juce::Path curve;
        for (float x = plot.getX(); x <= plot.getRight(); x += 1.0f)
        {
            auto inDb = juce::jmap (x, plot.getX(), plot.getRight(), graphMinDb, 0.0f);
            auto y = transferY (plugin.transfer (inDb));

            if (x == plot.getX()) curve.startNewSubPath (x, y);
            else                  curve.lineTo (x, y);
        }

        g.saveState();
        g.reduceClipRegion (plot.toNearestInt());
        g.setColour (reductionColour.withAlpha (0.12f));
        juce::Path fill (curve);
        for (float x = plot.getRight(); x >= plot.getX(); x -= 1.0f)
            fill.lineTo (x, transferY (juce::jmap (x, plot.getX(), plot.getRight(), graphMinDb, 0.0f)));
        fill.closeSubPath();
        g.fillPath (fill);

        g.setColour (Palette::accent);
        g.strokePath (curve, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.restoreState();

        // Handles.
        auto drawHandle = [&] (juce::Point<float> p, juce::Colour c, bool active)
        {
            auto radius = active ? 7.0f : 5.5f;
            g.setColour (c.withAlpha (active ? 0.35f : 0.2f));
            g.fillEllipse (juce::Rectangle<float> (radius * 3.0f, radius * 3.0f).withCentre (p));
            g.setColour (c);
            g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (p));
            g.setColour (Palette::windowBackground);
            g.drawEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (p), 1.2f);
        };

        drawHandle (getThresholdHandle(), thresholdColour, hover == Handle::threshold || dragging == Handle::threshold);
        drawHandle (getRatioHandle(), Palette::accent, hover == Handle::ratio || dragging == Handle::ratio);

        // Where the signal is now: on the input axis at the detector level, at the level actually
        // coming out (so attack and release show as the dot leaving and returning to the curve).
        if (detectorDb > graphMinDb)
        {
            juce::Point<float> dot { transferX (detectorDb), transferY (detectorDb - grLevel) };
            g.setColour (Palette::text.withAlpha (0.25f));
            g.fillEllipse (juce::Rectangle<float> (16.0f, 16.0f).withCentre (dot));
            g.setColour (Palette::text);
            g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (dot));
        }
    }

    void CompressorEditor::drawHistory (juce::Graphics& g, juce::Rectangle<float> area)
    {
        drawPanel (g, area);
        auto plot = area.reduced (6.0f, 8.0f).withTrimmedRight (24.0f);
        auto levelY = [&] (float db) { return juce::jmap (juce::jlimit (meterMinDb, meterMaxDb, db), meterMinDb, meterMaxDb, plot.getBottom(), plot.getY()); };
        auto grY = [&] (float gr) { return juce::jmap (juce::jlimit (0.0f, maxReductionDb, gr), 0.0f, maxReductionDb, plot.getY(), plot.getBottom()); };

        // Level grid with labels on the right, threshold across.
        g.setFont (juce::FontOptions (9.0f));
        for (float db : { 0.0f, -12.0f, -24.0f, -36.0f, -48.0f })
        {
            g.setColour (Palette::panelOutline.withAlpha (db == 0.0f ? 0.8f : 0.4f));
            g.drawHorizontalLine ((int) levelY (db), plot.getX(), plot.getRight());
            g.setColour (Palette::textDisabled);
            g.drawText (juce::String ((int) db), juce::Rectangle<float> (plot.getRight() + 2.0f, levelY (db) - 5.0f, 22.0f, 10.0f), juce::Justification::centredLeft);
        }

        const float step = plot.getWidth() / (float) (historyLength - 1);
        auto frameAt = [&] (int i) -> const Frame& { return history[(size_t) ((historyWrite + i) % historyLength)]; };

        juce::Path input, outputLine, reduction;
        input.startNewSubPath (plot.getX(), plot.getBottom());
        reduction.startNewSubPath (plot.getX(), plot.getY());

        for (int i = 0; i < historyLength; ++i)
        {
            auto& f = frameAt (i);
            auto x = plot.getX() + (float) i * step;
            input.lineTo (x, levelY (f.inDb));
            reduction.lineTo (x, grY (f.grDb));

            if (i == 0) outputLine.startNewSubPath (x, levelY (f.outDb));
            else        outputLine.lineTo (x, levelY (f.outDb));
        }

        input.lineTo (plot.getRight(), plot.getBottom());
        input.closeSubPath();
        reduction.lineTo (plot.getRight(), plot.getY());
        reduction.closeSubPath();

        g.saveState();
        g.reduceClipRegion (plot.toNearestInt());

        g.setGradientFill (juce::ColourGradient::vertical (inputColour.withAlpha (0.6f), plot.getY(), inputColour.withAlpha (0.2f), plot.getBottom()));
        g.fillPath (input);

        g.setColour (outputColour);
        g.strokePath (outputLine, juce::PathStrokeType (1.4f));

        g.setGradientFill (juce::ColourGradient::vertical (reductionColour.withAlpha (0.65f), plot.getY(), reductionColour.withAlpha (0.25f), grY (12.0f)));
        g.fillPath (reduction);

        // Threshold line on the level scale.
        auto ty = levelY (plugin.getThresholdDb());
        juce::Path threshold;
        threshold.startNewSubPath (plot.getX(), ty);
        threshold.lineTo (plot.getRight(), ty);
        const float dashes[] { 5.0f, 4.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (threshold, threshold, dashes, 2);
        g.setColour (thresholdColour.withAlpha (0.8f));
        g.fillPath (threshold);
        g.restoreState();

        // Legend and the current amount of compression.
        auto legend = plot.reduced (8.0f, 6.0f).removeFromBottom (14.0f);
        g.setFont (juce::FontOptions (10.5f));
        for (auto [text, colour] : { std::pair ("Input", inputColour), std::pair ("Output", outputColour), std::pair ("Gain reduction", reductionColour) })
        {
            g.setColour (colour);
            g.fillRoundedRectangle (legend.removeFromLeft (10.0f).withSizeKeepingCentre (10.0f, 4.0f), 2.0f);
            legend.removeFromLeft (4.0f);
            g.setColour (Palette::textDim);
            auto w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text) + 14.0f;
            g.drawText (text, legend.removeFromLeft (w), juce::Justification::centredLeft);
        }

        auto big = plot.reduced (10.0f, 8.0f).removeFromTop (30.0f).removeFromRight (150.0f);
        g.setColour (grLevel > 0.05f ? reductionColour : Palette::textDim);
        g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
        g.drawText ((grLevel > 0.05f ? "-" : "") + juce::String (grLevel, 1) + " dB", big, juce::Justification::centredRight);
    }

    //==============================================================================
    CompressorEditor::Handle CompressorEditor::hitTest (juce::Point<float> p) const
    {
        if (p.getDistanceFrom (getThresholdHandle()) < 11.0f)   return Handle::threshold;
        if (p.getDistanceFrom (getRatioHandle()) < 11.0f)       return Handle::ratio;

        auto inner = inMeterArea.reduced (5.0f, 4.0f).withTrimmedTop (31.0f).withTrimmedLeft (16.0f);
        if (inMeterArea.expanded (8.0f, 0.0f).contains (p) && std::abs (p.y - meterY (inner, plugin.getThresholdDb())) < 7.0f)
            return Handle::meterThreshold;

        return Handle::none;
    }

    void CompressorEditor::mouseMove (const juce::MouseEvent& e)
    {
        auto h = hitTest (e.position);
        if (h != hover)
        {
            hover = h;
            setMouseCursor (h == Handle::none ? juce::MouseCursor::NormalCursor
                                              : h == Handle::ratio ? juce::MouseCursor::UpDownResizeCursor
                                                                   : h == Handle::meterThreshold ? juce::MouseCursor::UpDownResizeCursor
                                                                                                 : juce::MouseCursor::LeftRightResizeCursor);
            repaint();
        }
    }

    void CompressorEditor::mouseDown (const juce::MouseEvent& e)
    {
        dragging = hitTest (e.position);

        if (dragging == Handle::threshold || dragging == Handle::meterThreshold)
        {
            plugin.edit.getUndoManager().beginNewTransaction ("Compressor threshold");
            plugin.thresholdGain.getParameter().parameterChangeGestureBegin();
        }
        else if (dragging == Handle::ratio)
        {
            plugin.edit.getUndoManager().beginNewTransaction ("Compressor ratio");
            plugin.ratio.getParameter().parameterChangeGestureBegin();
        }
    }

    void CompressorEditor::mouseDrag (const juce::MouseEvent& e)
    {
        auto setThresholdDb = [this] (float db)
        {
            plugin.thresholdGain.setParameter (juce::jlimit (te::CompressorPlugin::getMinThreshold(), te::CompressorPlugin::getMaxThreshold(),
                                                             juce::Decibels::decibelsToGain (db)), juce::sendNotification);
        };

        if (dragging == Handle::threshold)
        {
            auto plot = transferPlot (transferArea);
            setThresholdDb (juce::jmap (e.position.x, plot.getX(), plot.getRight(), graphMinDb, 0.0f));
        }
        else if (dragging == Handle::meterThreshold)
        {
            auto inner = inMeterArea.reduced (5.0f, 4.0f).withTrimmedTop (31.0f);
            setThresholdDb (juce::jmap (e.position.y, inner.getBottom(), inner.getY(), meterMinDb, meterMaxDb));
        }
        else if (dragging == Handle::ratio)
        {
            // Output level at 0 dB in -> slope above the threshold -> ratio.
            auto plot = transferPlot (transferArea);
            auto outDb = juce::jmap (e.position.y, plot.getBottom(), plot.getY(), graphMinDb, 0.0f);
            auto threshold = plugin.getThresholdDb();
            auto over = -threshold;
            auto slope = over > 0.1f ? juce::jlimit (0.0f, 0.95f, (outDb - threshold) / over) : 0.5f;
            plugin.ratio.setParameter (slope, juce::sendNotification);
        }

        repaint();
    }

    void CompressorEditor::mouseUp (const juce::MouseEvent&)
    {
        if (dragging == Handle::threshold || dragging == Handle::meterThreshold)
            plugin.thresholdGain.getParameter().parameterChangeGestureEnd();
        else if (dragging == Handle::ratio)
            plugin.ratio.getParameter().parameterChangeGestureEnd();

        dragging = Handle::none;
    }

    void CompressorEditor::mouseDoubleClick (const juce::MouseEvent& e)
    {
        // Double-click a handle: back to the default.
        auto h = hitTest (e.position);
        if (h == Handle::threshold || h == Handle::meterThreshold)
            plugin.thresholdGain.setParameter (juce::Decibels::decibelsToGain (-6.0f), juce::sendNotification);
        else if (h == Handle::ratio)
            plugin.ratio.setParameter (0.5f, juce::sendNotification);
    }
}
