#include "StatusBar.h"
#include "../LookAndFeel/Palette.h"
#include "../../Engine/AudioDeviceSetup.h"

namespace cdaw
{
    StatusBar::StatusBar (te::Engine& e) : engine (e)
    {
        startTimerHz (4);
        timerCallback();
    }

    StatusBar::~StatusBar()
    {
        stopTimer();
    }

    void StatusBar::showMessage (const juce::String& m)
    {
        message = m;
        messageTime = juce::Time::getMillisecondCounter();
        repaint();
    }

    void StatusBar::timerCallback()
    {
        auto newDevice = AudioDeviceSetup::describeCurrentDevice (engine);
        auto newCpu = engine.getDeviceManager().getCpuUsage();
        auto newClipped = engine.getDeviceManager().hasOutputClipped (false);

        if (message.isNotEmpty() && juce::Time::getMillisecondCounter() - messageTime > 6000)
            message = {};

        if (newDevice != device || std::abs (newCpu - cpu) > 0.005f || newClipped != clipped)
        {
            device = newDevice;
            cpu = newCpu;
            clipped = newClipped;
        }

        repaint();
    }

    void StatusBar::paint (juce::Graphics& g)
    {
        g.fillAll (Palette::panelBackground);
        g.setColour (Palette::panelOutline);
        g.drawHorizontalLine (0, 0.0f, (float) getWidth());

        auto r = getLocalBounds().reduced (8, 0);
        g.setFont (juce::FontOptions (13.0f));

        // CPU meter
        auto cpuArea = r.removeFromRight (120);
        auto bar = cpuArea.reduced (0, 7).withTrimmedLeft (40);
        g.setColour (Palette::textDim);
        g.drawText ("CPU", cpuArea.withWidth (36), juce::Justification::centredLeft);
        g.setColour (Palette::laneBackground);
        g.fillRect (bar);
        g.setColour (cpu > 0.85f ? Palette::meterRed : cpu > 0.6f ? Palette::meterYellow : Palette::meterGreen);
        g.fillRect (bar.withWidth (juce::roundToInt (bar.getWidth() * juce::jlimit (0.0f, 1.0f, cpu))));

        if (clipped)
        {
            g.setColour (Palette::meterRed);
            g.drawText ("CLIP", r.removeFromRight (44), juce::Justification::centred);
        }

        r.removeFromRight (12);
        g.setColour (Palette::textDim);
        deviceArea = r.removeFromRight (juce::jmin (520, r.getWidth() / 2));
        g.drawText (device, deviceArea, juce::Justification::centredRight);

        g.setColour (Palette::text);
        g.drawText (message, r, juce::Justification::centredLeft);
    }

    void StatusBar::mouseDown (const juce::MouseEvent& e)
    {
        if (deviceArea.contains (e.getPosition()) && onDeviceClicked)
            onDeviceClicked();
        else if (e.x > getWidth() - 140)
            engine.getDeviceManager().hasOutputClipped (true);   // reset clip indicator
    }
}
