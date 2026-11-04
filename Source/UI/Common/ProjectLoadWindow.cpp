#include "ProjectLoadWindow.h"
#include "StartupSplash.h"
#include "Shared/LookAndFeel/Palette.h"

namespace cdaw
{
    ProjectLoadWindow::ProjectLoadWindow (const juce::String& projectName, juce::Component& centreOver)
        : title ("Opening " + projectName)
    {
        setOpaque (true);
        setSize (440, 104);
        setName (title);
        addToDesktop (juce::ComponentPeer::windowHasDropShadow);
        setCentrePosition (centreOver.getScreenBounds().getCentre());

        // The software renderer paints on request; Direct2D would wait for a vblank
        // that never comes while opening blocks the message thread.
        if (auto* peer = getPeer())
            peer->setCurrentRenderingEngine (0);

        setVisible (true);
        toFront (false);
        enterModalState (false);
    }

    void ProjectLoadWindow::setStep (const juce::String& text, double p)
    {
        juce::Logger::writeToLog ("Opening project: " + text);
        step = text;
        progress = juce::jlimit (0.0, 1.0, p);
        repaint();

        if (auto* peer = getPeer())
            peer->performAnyPendingRepaintsNow();
    }

    void ProjectLoadWindow::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds();
        g.fillAll (Palette::windowBackground);
        g.setColour (Palette::panelOutline);
        g.drawRect (r);

        auto content = r.reduced (20, 16);

        g.setColour (Palette::text);
        g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
        g.drawText (title, content.removeFromTop (24), juce::Justification::centredLeft, true);

        g.setColour (Palette::textDim);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (step + juce::String::fromUTF8 ("\xe2\x80\xa6"), content.removeFromTop (22), juce::Justification::centredLeft, true);

        StartupSplash::drawProgressBar (g, content.removeFromBottom (8).toFloat(), progress);
    }
}
