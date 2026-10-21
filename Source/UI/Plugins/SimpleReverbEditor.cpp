#include "SimpleReverbEditor.h"
#include "TeKnobParameter.h"
#include "../../Plugins/SimpleReverbPlugin.h"
#include "../LookAndFeel/Palette.h"
#include "../../Shared/UI/SpectrumAnalyser.h"

namespace cdaw
{
    namespace
    {
        constexpr int headerHeight = 34, knobsHeight = 108;
        const juce::Colour bodyColour { 0xfff2b134 };      // the low end and body
        const juce::Colour highColour { 0xff4fd6c8 };      // the highs
        const juce::Colour spectrumColour { 0xff8fa6c0 };
    }

    SimpleReverbEditor::SimpleReverbEditor (SimpleReverbPlugin& p)
        : plugin (p), pluginRef (&p), spectrum (std::make_unique<SpectrumAnalyser> (p.engine.wetFifo))
    {
        setOpaque (true);

        auto add = [this] (te::ParameterWithStateValue& param, const char* label, juce::Colour c, float mid)
        {
            auto def = param.getParameter().getDefaultValue().value_or (param.getCurrentValue());
            auto* k = knobs.add (new ParamKnob (knobParameter (param.getParameter()), label, c, def, mid));
            addAndMakeVisible (k);
        };

        add (plugin.size, "Size", Palette::tempo, 0.0f);
        add (plugin.decay, "Decay", Palette::tempo, 2.0f);
        add (plugin.preDelay, "Pre-delay", Palette::tempo, 30.0f);
        add (plugin.tone, "Tone", highColour, 0.0f);
        add (plugin.mix, "Mix", Palette::marker, 0.0f);

        constrainer.setMinimumSize (440, 320);
        setSize (600, 400);
        startTimerHz (60);
    }

    SimpleReverbEditor::~SimpleReverbEditor()
    {
        stopTimer();
        plugin.engine.wetFifo->active = false;
    }

    void SimpleReverbEditor::visibilityChanged()
    {
        plugin.engine.wetFifo->active = isShowing();
    }

    void SimpleReverbEditor::tick (float wet)
    {
        // The shape glows with the reverb: quick to light up, slow to fade.
        auto level = juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (wet, -100.0f) + 50.0f) / 50.0f);
        glow = level > glow ? level : glow * 0.95f;
        repaint (displayArea.toNearestInt());
    }

    void SimpleReverbEditor::updateSpectrum()
    {
        spectrum->update (columnHz, plugin.engine.getSampleRate());
        repaint (displayArea.toNearestInt());
    }

    void SimpleReverbEditor::timerCallback()
    {
        tick (plugin.engine.wetPeak.take());

        if (isShowing() && (frame++ & 1) == 0)
        {
            plugin.engine.wetFifo->active = true;
            updateSpectrum();
        }

        for (auto* k : knobs)
            k->refresh();

        juce::String key;
        for (auto* param : { &plugin.size, &plugin.decay, &plugin.preDelay, &plugin.tone, &plugin.mix })
            key << param->getCurrentValue() << ",";

        if (key != lastKey)
        {
            lastKey = key;
            repaint();
        }
    }

    juce::Rectangle<float> SimpleReverbEditor::plotArea() const
    {
        return displayArea.withTrimmedLeft (42.0f).withTrimmedRight (12.0f).withTrimmedTop (24.0f).withTrimmedBottom (18.0f);
    }

    float SimpleReverbEditor::hzToX (double hz) const
    {
        auto p = plotArea();
        return p.getX() + (float) (std::log (hz / 20.0) / std::log (1000.0)) * p.getWidth();
    }

    double SimpleReverbEditor::xToHz (float x) const
    {
        auto p = plotArea();
        return 20.0 * std::pow (1000.0, juce::jlimit (0.0, 1.0, (double) (x - p.getX()) / p.getWidth()));
    }

    float SimpleReverbEditor::timeToY (double t) const
    {
        auto p = plotArea();
        return p.getBottom() - (float) (t / timeRange) * p.getHeight();
    }

    void SimpleReverbEditor::paint (juce::Graphics& g)
    {
        g.fillAll (Palette::panelBackground);

        auto header = getLocalBounds().removeFromTop (headerHeight).reduced (12, 0);
        g.setColour (Palette::text);
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.drawText ("REVERB", header, juce::Justification::centredLeft);
        g.setColour (Palette::textDim);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (plugin.decay.getParameter().getCurrentValueAsString() + "  -  " + plugin.tone.getParameter().getCurrentValueAsString()
                      + "  -  mix " + plugin.mix.getParameter().getCurrentValueAsString(),
                    header.withTrimmedLeft (90), juce::Justification::centredLeft);

        // The display panel.
        g.setColour (Palette::windowBackground);
        g.fillRoundedRectangle (displayArea, 6.0f);
        g.setColour (Palette::panelOutline.withAlpha (0.7f));
        g.drawRoundedRectangle (displayArea.reduced (0.5f), 6.0f, 1.0f);

        const auto settings = plugin.getSettings();
        const double preDelay = settings.preDelay * 0.001;
        auto p = plotArea();

        if (! dragging)
        {
            const double longest = preDelay + ReverbEngine::getDecayAt (settings, 100.0) * 1.05;
            timeRange = 0.5;
            while (timeRange < longest && timeRange < 16.0)
                timeRange *= 2.0;
        }

        g.setColour (juce::Colour (0xff15171b));
        g.fillRect (p);

        // The reverb's spectrum, faintly, behind.
        if (! spectrum->levels.empty())
        {
            juce::Path fill;
            fill.startNewSubPath (p.getX(), p.getBottom());
            for (size_t c = 0; c < spectrum->levels.size(); ++c)
                fill.lineTo (p.getX() + (float) c, juce::jmap (juce::jlimit (-100.0f, 0.0f, spectrum->levels[c]), -100.0f, 0.0f, p.getBottom(), p.getY()));
            fill.lineTo (p.getRight(), p.getBottom());
            fill.closeSubPath();
            g.setColour (spectrumColour.withAlpha (0.14f));
            g.fillPath (fill);
        }

        // Grid: frequencies along the bottom, time up the left.
        g.setFont (juce::FontOptions (9.5f));
        for (double hz : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
        {
            auto x = hzToX (hz);
            g.setColour (juce::Colours::white.withAlpha (0.05f));
            g.drawVerticalLine ((int) x, p.getY(), p.getBottom());
            g.setColour (Palette::textDisabled);
            g.drawText (hz >= 1000.0 ? juce::String ((int) (hz / 1000.0)) + "k" : juce::String ((int) hz),
                        juce::Rectangle<float> (x - 16.0f, p.getBottom() + 3.0f, 32.0f, 12.0f), juce::Justification::centred);
        }

        const double step = timeRange <= 1.0 ? 0.25 : timeRange <= 4.0 ? 0.5 : timeRange <= 8.0 ? 1.0 : 2.0;
        for (double t = 0.0; t <= timeRange + 1.0e-6; t += step)
        {
            auto y = timeToY (t);
            g.setColour (juce::Colours::white.withAlpha (0.05f));
            g.drawHorizontalLine ((int) y, p.getX(), p.getRight());
            g.setColour (Palette::textDisabled);
            auto label = juce::Rectangle<float> (displayArea.getX() + 2.0f, y - 6.0f, p.getX() - displayArea.getX() - 6.0f, 12.0f);
            label = label.withY (juce::jlimit (p.getY() - 2.0f, p.getBottom() - 10.0f, label.getY()));
            g.drawText (juce::String (t, step < 1.0 ? 2 : 0) + " s", label, juce::Justification::centredRight);
        }

        // How long each frequency rings, from the pre-delay up.
        juce::Path shape, edge;
        shape.startNewSubPath (p.getX(), timeToY (preDelay));
        for (float x = p.getX(); x <= p.getRight(); x += 2.0f)
        {
            auto hz = xToHz (x);
            // The tail is shorter where the reverb's own cuts take the level down.
            auto cuts = ReverbEngine::getCutsDb (settings, hz, true, plugin.engine.getSampleRate());
            auto ring = juce::jmax (0.0, ReverbEngine::getDecayAt (settings, hz) * (1.0 + cuts / 60.0));
            auto y = timeToY (preDelay + ring);
            shape.lineTo (x, y);
            if (x == p.getX()) edge.startNewSubPath (x, y);
            else               edge.lineTo (x, y);
        }
        shape.lineTo (p.getRight(), timeToY (preDelay));
        shape.closeSubPath();

        g.saveState();
        g.reduceClipRegion (p.toNearestInt());

        const float brightness = 0.35f + 0.5f * glow;
        juce::ColourGradient fill (bodyColour.withAlpha (brightness), p.getX(), 0.0f, highColour.withAlpha (brightness), p.getRight(), 0.0f, false);
        g.setGradientFill (fill);
        g.fillPath (shape);

        juce::ColourGradient line (bodyColour, p.getX(), 0.0f, highColour, p.getRight(), 0.0f, false);
        g.setGradientFill (line);
        g.strokePath (edge, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // The quiet gap before the tail, and the early reflections inside it.
        if (preDelay > 0.0)
        {
            g.setColour (Palette::text.withAlpha (0.35f));
            g.drawHorizontalLine ((int) timeToY (preDelay), p.getX(), p.getRight());
        }

        for (auto& tap : ReverbEngine::getEarlyTaps (settings))
        {
            g.setColour (juce::Colours::white.withAlpha (juce::jlimit (0.1f, 0.8f, tap.gain * 1.4f)));
            g.fillRect (p.getX(), timeToY (preDelay + tap.seconds) - 0.5f, 8.0f + 24.0f * tap.gain, 1.5f);
        }

        g.restoreState();

        // Legend.
        auto top = displayArea.reduced (12.0f, 5.0f).removeFromTop (14.0f);
        g.setFont (juce::FontOptions (10.5f));
        g.setColour (bodyColour);
        g.drawText ("body " + juce::String (ReverbEngine::getDecayAt (settings, 800.0), 1) + " s", top.removeFromLeft (90.0f), juce::Justification::centredLeft);
        g.setColour (highColour);
        g.drawText ("highs " + juce::String (ReverbEngine::getDecayAt (settings, 8000.0), 1) + " s", top.removeFromLeft (100.0f), juce::Justification::centredLeft);
        g.setColour (Palette::textDisabled);
        g.drawText ("drag up / down for the decay", top, juce::Justification::centredRight);
    }

    void SimpleReverbEditor::resized()
    {
        auto r = getLocalBounds();
        r.removeFromTop (headerHeight);
        auto knobRow = r.removeFromBottom (knobsHeight).reduced (10, 8);
        displayArea = r.reduced (10, 0).toFloat();

        const int w = knobRow.getWidth() / knobs.size();
        for (auto* k : knobs)
            k->setBounds (knobRow.removeFromLeft (w).reduced (4, 0));

        auto p = plotArea();
        columnHz.resize ((size_t) juce::jmax (1, (int) p.getWidth()));
        for (size_t c = 0; c < columnHz.size(); ++c)
            columnHz[c] = xToHz (p.getX() + (float) c + 0.5f);
    }

    void SimpleReverbEditor::mouseDown (const juce::MouseEvent& e)
    {
        if (! plotArea().contains (e.position))
            return;

        dragging = true;
        plugin.edit.getUndoManager().beginNewTransaction ("Reverb decay");
        plugin.decay.getParameter().parameterChangeGestureBegin();
        mouseDrag (e);
    }

    void SimpleReverbEditor::mouseDrag (const juce::MouseEvent& e)
    {
        if (! dragging)
            return;

        // The body's decay reaches the mouse.
        auto p = plotArea();
        auto t = (p.getBottom() - e.position.y) / p.getHeight() * timeRange - plugin.preDelay.getCurrentValue() * 0.001;
        auto settings = plugin.getSettings();
        auto ratio = ReverbEngine::getDecayAt (settings, xToHz (e.position.x)) / juce::jmax (0.05f, settings.decay);
        plugin.decay.setParameter ((float) juce::jlimit (0.3, 10.0, t / juce::jmax (0.1, ratio)), juce::sendNotification);
        repaint();
    }

    void SimpleReverbEditor::mouseUp (const juce::MouseEvent&)
    {
        if (dragging)
            plugin.decay.getParameter().parameterChangeGestureEnd();
        dragging = false;
        repaint();
    }
}
