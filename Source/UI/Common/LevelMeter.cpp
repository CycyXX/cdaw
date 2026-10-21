#include "LevelMeter.h"
#include "../LookAndFeel/Palette.h"

namespace cdaw
{
    LevelMeter::LevelMeter()
    {
        setOpaque (false);
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
        // Roughly linear-in-feel scale from -60 dB to +6 dB.
        if (db <= -60.0f)
            return 0.0f;

        auto x = (db + 60.0f) / 66.0f;
        return juce::jlimit (0.0f, 1.0f, std::pow (x, 1.6f));
    }

    void LevelMeter::timerCallback()
    {
        if (measurer == nullptr)
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
                peak[ch] = value;
                peakTime[ch] = now;
                changed = true;
            }

            if (newDb > 0.0f && ! clipped)
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

        auto clipLed = vertical ? r.removeFromTop (4.0f) : r.removeFromRight (4.0f);
        g.setColour (clipped ? Palette::meterRed : Palette::panelOutline);
        g.fillRect (clipLed);

        auto drawBar = [&] (juce::Rectangle<float> area, float db, float pk)
        {
            auto p = dbToProportion (db);
            auto bar = vertical ? area.withTop (area.getBottom() - area.getHeight() * p)
                                : area.withWidth (area.getWidth() * p);

            juce::ColourGradient grad = vertical
                ? juce::ColourGradient (Palette::meterGreen, area.getBottomLeft(), Palette::meterRed, area.getTopLeft(), false)
                : juce::ColourGradient (Palette::meterGreen, area.getTopLeft(), Palette::meterRed, area.getTopRight(), false);
            grad.addColour (dbToProportion (-12.0f), Palette::meterGreen);
            grad.addColour (dbToProportion (-3.0f), Palette::meterYellow);
            g.setGradientFill (grad);
            g.fillRect (bar);

            auto pp = dbToProportion (pk);
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
    }

    void LevelMeter::mouseDown (const juce::MouseEvent&)
    {
        clipped = false;

        for (int i = 0; i < maxChannels; ++i)
            peak[i] = -100.0f;

        repaint();
    }
}
