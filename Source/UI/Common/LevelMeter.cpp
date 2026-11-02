#include "LevelMeter.h"
#include "../LookAndFeel/Palette.h"

namespace cdaw
{
    LevelMeter::LevelMeter()
    {
        setOpaque (true);   // paint() fills the whole area
    }

    LevelMeter::~LevelMeter()
    {
        detach();
    }

    void LevelMeter::attach (te::LevelMeasurer* m, te::Plugin::Ptr keepAlive)
    {
        if (m == measurer)
            return;

        detach();
        measurer = m;
        plugin = keepAlive;

        if (measurer != nullptr)
        {
            measurer->addClient (client);
            startTimerHz (30);
        }
    }

    void LevelMeter::detach()
    {
        stopTimer();

        if (measurer != nullptr)
            measurer->removeClient (client);

        measurer = nullptr;
        plugin = nullptr;

        for (int i = 0; i < maxChannels; ++i)
            level[i] = peak[i] = -100.0f;

        repaint();
    }

    float LevelMeter::dbToProportion (float db)
    {
        // Roughly linear-in-feel scale from -60 dB to 0 dBFS. Full scale reaches the very top of
        // the bar, right under the clip LED; anything above 0 dB lights the LED.
        if (db <= -60.0f)
            return 0.0f;

        auto x = (db + 60.0f) / 60.0f;
        return juce::jlimit (0.0f, 1.0f, std::pow (x, 1.6f));
    }

    float LevelMeter::toProportion (float db) const
    {
        if (scale == Scale::fader)
            return juce::jlimit (0.0f, 1.0f, te::decibelsToVolumeFaderPosition (db));

        return dbToProportion (db);
    }

    void LevelMeter::setScale (Scale s)
    {
        scale = s;
        repaint();
    }

    void LevelMeter::resetPeaks()
    {
        clipped = false;
        heldPeak = -100.0f;

        for (int i = 0; i < maxChannels; ++i)
            peak[i] = -100.0f;

        repaint();
    }

    void LevelMeter::timerCallback()
    {
        // Hidden (e.g. the mixer page isn't showing): no work, no repaints.
        if (measurer == nullptr || ! isShowing())
            return;

        numChannels = juce::jlimit (1, maxChannels, client.getNumChannelsUsed());
        auto now = juce::Time::getMillisecondCounter();
        bool changed = false;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto newDb = client.getAndClearAudioLevel (ch).dB;
            auto decayed = level[ch] - 1.2f;   // ~36 dB/s fall-back
            auto value = juce::jmax (newDb, decayed, -100.0f);

            if (std::abs (value - level[ch]) > 0.05f)
            {
                level[ch] = value;
                changed = true;
            }

            if (value >= peak[ch] || now - peakTime[ch] > 1500)
            {
                // Only a visible change counts: in silence everything sits at -100 dB.
                if (std::abs (value - peak[ch]) > 0.05f)
                    changed = true;

                peak[ch] = value;
                peakTime[ch] = now;
            }

            heldPeak = juce::jmax (heldPeak, newDb);

            if (newDb > clipThresholdDb && ! clipped)
            {
                clipped = true;
                changed = true;
            }
        }

        if (changed)
            repaint();
    }

    void LevelMeter::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat();
        const bool vertical = r.getHeight() > r.getWidth();

        g.setColour (Palette::laneBackground.darker (0.3f));
        g.fillRect (r);

        auto clipLed = vertical ? r.removeFromTop (clipLedHeight) : r.removeFromRight (clipLedHeight);
        g.setColour (clipped ? Palette::meterRed : Palette::panelOutline);
        g.fillRect (clipLed);

        auto drawBar = [&] (juce::Rectangle<float> area, float db, float pk)
        {
            auto p = toProportion (db);
            auto bar = vertical ? area.withTop (area.getBottom() - area.getHeight() * p)
                                : area.withWidth (area.getWidth() * p);

            juce::ColourGradient grad = vertical
                ? juce::ColourGradient (Palette::meterGreen, area.getBottomLeft(), Palette::meterRed, area.getTopLeft(), false)
                : juce::ColourGradient (Palette::meterGreen, area.getTopLeft(), Palette::meterRed, area.getTopRight(), false);
            grad.addColour (toProportion (-12.0f), Palette::meterGreen);
            grad.addColour (toProportion (-3.0f), Palette::meterYellow);

            // Everything above 0 dBFS is red on a scale that goes past it.
            if (scale == Scale::fader)
                grad.addColour (toProportion (0.0f), Palette::meterRed);

            g.setGradientFill (grad);
            g.fillRect (bar);

            auto pp = toProportion (pk);
            if (pp > 0.0f)
            {
                g.setColour (pk > 0.0f ? Palette::meterRed : Palette::text.withAlpha (0.8f));
                if (vertical) g.fillRect (area.getX(), area.getBottom() - area.getHeight() * pp, area.getWidth(), 1.5f);
                else          g.fillRect (area.getX() + area.getWidth() * pp - 1.5f, area.getY(), 1.5f, area.getHeight());
            }
        };

        if (numChannels >= 2)
        {
            auto a = r.reduced (0.5f);
            auto first = vertical ? a.removeFromLeft (a.getWidth() / 2.0f) : a.removeFromTop (a.getHeight() / 2.0f);
            drawBar (first.reduced (vertical ? 0.5f : 0.0f, vertical ? 0.0f : 0.5f), level[0], peak[0]);
            drawBar (a.reduced (vertical ? 0.5f : 0.0f, vertical ? 0.0f : 0.5f), level[1], peak[1]);
        }
        else
        {
            drawBar (r.reduced (0.5f), level[0], peak[0]);
        }

        // Unity mark, level with the fader's 0 dB.
        if (scale == Scale::fader && vertical)
        {
            g.setColour (Palette::text.withAlpha (0.4f));
            g.fillRect (r.getX(), r.getBottom() - r.getHeight() * toProportion (0.0f) - 0.5f, r.getWidth(), 1.0f);
        }
    }

    void LevelMeter::mouseDown (const juce::MouseEvent&)
    {
        resetPeaks();
    }
}
