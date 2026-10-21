#include "SpectrumView.h"
#include "../LookAndFeel/Palette.h"
#include "../../Engine/MasterAnalyser.h"

namespace cdaw
{
    SpectrumView::SpectrumView (std::shared_ptr<AnalyserFifo> f) : fifo (std::move (f))
    {
        setOpaque (true);
        startTimerHz (30);
    }

    SpectrumView::~SpectrumView()
    {
        if (fifo != nullptr)
            fifo->active = false;
    }

    void SpectrumView::visibilityChanged()
    {
        if (fifo != nullptr)
            fifo->active = isShowing();
    }

    juce::Rectangle<float> SpectrumView::getPlotArea() const
    {
        return getLocalBounds().toFloat().withTrimmedTop (22.0f).withTrimmedLeft (30.0f).withTrimmedRight (6.0f).withTrimmedBottom (16.0f);
    }

    float SpectrumView::hzToX (float hz) const
    {
        auto plot = getPlotArea();
        auto norm = std::log (hz / minHz) / std::log (maxHz / minHz);
        return plot.getX() + norm * plot.getWidth();
    }

    float SpectrumView::xToHz (float x) const
    {
        auto plot = getPlotArea();
        auto norm = (x - plot.getX()) / juce::jmax (1.0f, plot.getWidth());
        return minHz * std::pow (maxHz / minHz, norm);
    }

    float SpectrumView::dbToY (float db) const
    {
        auto plot = getPlotArea();
        return juce::jmap (juce::jlimit (minDb, maxDb, db), minDb, maxDb, plot.getBottom(), plot.getY());
    }

    void SpectrumView::resized()
    {
        auto columns = (size_t) juce::jmax (1, (int) getPlotArea().getWidth());
        level.assign (columns, minDb);
        peak.assign (columns, minDb);
        peakHoldTime.assign (columns, 0.0f);
        background = {};    // the grid is redrawn for the new size
    }

    void SpectrumView::drawBackground (juce::Graphics& g)
    {
        // Title, grid and labels never change between frames: draw them once into an image.
        const auto scale = (float) g.getInternalContext().getPhysicalPixelScaleFactor();

        // Rebuild only when the size or display scale changes (the image is in physical pixels).
        if (background.isNull() || backgroundSize != getLocalBounds() || backgroundScale != scale)
        {
            backgroundSize = getLocalBounds();
            backgroundScale = scale;
            background = juce::Image (juce::Image::RGB, juce::jmax (1, juce::roundToInt ((float) getWidth() * scale)),
                                      juce::jmax (1, juce::roundToInt ((float) getHeight() * scale)), true);
            juce::Graphics bg (background);
            bg.addTransform (juce::AffineTransform::scale (scale));
            drawGrid (bg);
        }

        g.drawImage (background, getLocalBounds().toFloat());
    }

    void SpectrumView::timerCallback()
    {
        if (fifo == nullptr || ! isShowing())
            return;

        fifo->active = true;
        update();
    }

    void SpectrumView::update()
    {
        if (fifo == nullptr)
            return;

        auto append = [this] (int count)
        {
            std::move (history.begin() + count, history.end(), history.begin());
            std::copy_n (scratch.begin(), count, history.end() - count);
        };

        // Take everything waiting (so the display never lags behind), keeping the newest fftSize samples.
        int total = 0;
        float loudest = 0.0f;

        for (int got; (got = fifo->pull (scratch.data(), fftSize)) > 0;)
        {
            for (int i = 0; i < got; ++i)
                loudest = juce::jmax (loudest, std::abs (scratch[(size_t) i]));

            append (got);
            total += got;
        }

        // Silence (nothing arriving, or the engine's stream of zeros while stopped) and the display
        // has already fallen to the floor: nothing will change, so skip the FFT and the repaint.
        if (loudest < 1.0e-5f && isAtFloor())
            return;

        // Stopped: feed silence so the display falls away.
        if (total == 0)
        {
            auto count = juce::jlimit (1, fftSize, (int) (fifo->sampleRate.load() / 30.0));
            std::fill_n (scratch.begin(), count, 0.0f);
            append (count);
        }

        computeSpectrum();
        repaint();
    }

    void SpectrumView::computeSpectrum()
    {
        std::copy (history.begin(), history.end(), fftData.begin());
        std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);
        window.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
        fft.performFrequencyOnlyForwardTransform (fftData.data(), true);

        // Hann window halves the amplitude; scale so a full-scale sine reads ~0 dB.
        const float norm = 4.0f / (float) fftSize;

        for (size_t i = 0; i < binDb.size(); ++i)
            binDb[i] = juce::Decibels::gainToDecibels (fftData[i] * norm, minDb);

        const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
        const auto dt = (float) juce::jlimit (0.0, 0.2, lastTick > 0.0 ? now - lastTick : 1.0 / 30.0);
        lastTick = now;

        const double sr = fifo->sampleRate.load();
        const double binHz = sr / fftSize;
        auto plot = getPlotArea();

        for (size_t col = 0; col < level.size(); ++col)
        {
            auto f1 = xToHz (plot.getX() + (float) col);
            auto f2 = xToHz (plot.getX() + (float) col + 1.0f);
            float db = minDb;

            if (f2 - f1 < binHz)
            {
                // Columns narrower than a bin (low end): interpolate between the neighbouring bins.
                auto pos = juce::jlimit (1.0, (double) binDb.size() - 2.0, 0.5 * (f1 + f2) / binHz);
                auto i = (size_t) pos;
                auto frac = (float) (pos - (double) i);
                db = binDb[i] + (binDb[i + 1] - binDb[i]) * frac;
            }
            else
            {
                // Wider columns (high end): the loudest bin inside the column.
                auto b1 = juce::jlimit (1, (int) binDb.size() - 1, (int) std::floor (f1 / binHz));
                auto b2 = juce::jlimit (b1, (int) binDb.size() - 1, (int) std::ceil (f2 / binHz));

                for (int b = b1; b <= b2; ++b)
                    db = juce::jmax (db, binDb[(size_t) b]);
            }

            // Rise immediately, fall at 36 dB/s; the peak line holds for 1.5 s then falls at 12 dB/s.
            level[col] = juce::jmax (db, level[col] - 36.0f * dt);

            if (db >= peak[col])
            {
                peak[col] = db;
                peakHoldTime[col] = 1.5f;
            }
            else if ((peakHoldTime[col] -= dt) <= 0.0f)
            {
                peak[col] = juce::jmax (minDb, peak[col] - 12.0f * dt);
            }
        }
    }

    void SpectrumView::drawGrid (juce::Graphics& g)
    {
        g.fillAll (Palette::panelBackground);
        auto plot = getPlotArea();

        g.setColour (Palette::text);
        g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
        g.drawText ("Spectrum", getLocalBounds().removeFromTop (22).reduced (8, 0), juce::Justification::centredLeft);
        g.setColour (Palette::textDim);
        g.setFont (juce::FontOptions (10.5f));
        g.drawText ("master out", getLocalBounds().removeFromTop (22).reduced (8, 0), juce::Justification::centredRight);

        g.setColour (Palette::laneBackground);
        g.fillRect (plot);

        // Grid: frequencies and levels.
        g.setFont (juce::FontOptions (9.5f));

        for (float hz : { 30.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
        {
            auto x = hzToX (hz);
            g.setColour (Palette::gridBeat);
            g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
            g.setColour (Palette::textDim);
            auto label = hz >= 1000.0f ? juce::String ((int) (hz / 1000.0f)) + "k" : juce::String ((int) hz);
            g.drawText (label, juce::Rectangle<float> (x - 14.0f, plot.getBottom() + 1.0f, 28.0f, 14.0f), juce::Justification::centred);
        }

        for (float db : { 0.0f, -12.0f, -24.0f, -36.0f, -48.0f, -60.0f, -72.0f })
        {
            auto y = dbToY (db);
            g.setColour (db == 0.0f ? Palette::gridBar : Palette::gridBeat);
            g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
            g.setColour (Palette::textDim);
            g.drawText (juce::String ((int) db), juce::Rectangle<float> (0.0f, y - 7.0f, plot.getX() - 3.0f, 14.0f), juce::Justification::centredRight);
        }
    }

    bool SpectrumView::isAtFloor() const
    {
        for (size_t i = 0; i < level.size(); ++i)
            if (level[i] > minDb + 0.01f || peak[i] > minDb + 0.01f)
                return false;

        return true;
    }

    void SpectrumView::paint (juce::Graphics& g)
    {
        drawBackground (g);
        auto plot = getPlotArea();

        if (level.empty())
            return;

        // Level: filled curve in the logo's teal-to-violet gradient.
        juce::Path curve;
        curve.startNewSubPath (plot.getX(), plot.getBottom());

        for (size_t col = 0; col < level.size(); ++col)
            curve.lineTo (plot.getX() + (float) col, dbToY (level[col]));

        auto outline = curve;
        curve.lineTo (plot.getRight(), plot.getBottom());
        curve.closeSubPath();

        juce::ColourGradient grad (juce::Colour (0xff4fd6c8).withAlpha (0.55f), plot.getX(), 0.0f,
                                   juce::Colour (0xffd77cff).withAlpha (0.55f), plot.getRight(), 0.0f, false);
        grad.addColour (0.5, juce::Colour (0xff4fa3ff).withAlpha (0.55f));
        g.setGradientFill (grad);
        g.fillPath (curve);

        g.setColour (Palette::text.withAlpha (0.75f));
        g.strokePath (outline, juce::PathStrokeType (1.0f));

        // Peak hold
        juce::Path peaks;
        peaks.startNewSubPath (plot.getX(), dbToY (peak[0]));
        for (size_t col = 1; col < peak.size(); ++col)
            peaks.lineTo (plot.getX() + (float) col, dbToY (peak[col]));

        g.setColour (Palette::automation.withAlpha (0.7f));
        g.strokePath (peaks, juce::PathStrokeType (1.0f));

        // Read-out under the mouse: frequency, nearest note and level.
        if (plot.contains (mousePos))
        {
            auto hz = xToHz (mousePos.x);
            auto col = (size_t) juce::jlimit (0, (int) level.size() - 1, (int) (mousePos.x - plot.getX()));
            auto midi = juce::roundToInt (69.0 + 12.0 * std::log2 (hz / 440.0));
            auto text = (hz >= 1000.0f ? juce::String (hz / 1000.0f, 2) + " kHz" : juce::String (juce::roundToInt (hz)) + " Hz")
                        + "  " + juce::MidiMessage::getMidiNoteName (midi, true, true, 3)
                        + "  " + juce::String (level[col], 1) + " dB";

            g.setColour (Palette::text.withAlpha (0.4f));
            g.drawVerticalLine ((int) mousePos.x, plot.getY(), plot.getBottom());

            auto box = juce::Rectangle<float> (0.0f, 0.0f, 150.0f, 16.0f)
                           .withPosition (juce::jmin (mousePos.x + 8.0f, plot.getRight() - 150.0f), plot.getY() + 4.0f);
            g.setColour (Palette::panelRaised.withAlpha (0.9f));
            g.fillRoundedRectangle (box, 3.0f);
            g.setColour (Palette::text);
            g.setFont (juce::FontOptions (10.5f));
            g.drawText (text, box, juce::Justification::centred);
        }
    }

    void SpectrumView::mouseMove (const juce::MouseEvent& e)
    {
        mousePos = e.position;
    }

    void SpectrumView::mouseExit (const juce::MouseEvent&)
    {
        mousePos = { -1.0f, -1.0f };
    }
}
