#include "StartupSplash.h"
#include "../LookAndFeel/Palette.h"
#include <CDAWBinary.h>

namespace cdaw
{
    StartupSplash::StartupSplash (const juce::String& v) : version (v)
    {
        logo = juce::ImageCache::getFromMemory (CDAWBinary::splash_logo_png, CDAWBinary::splash_logo_pngSize);

        setOpaque (true);
        setSize (520, 230);
        addToDesktop (juce::ComponentPeer::windowAppearsOnTaskbar | juce::ComponentPeer::windowHasDropShadow);
        centreWithSize (getWidth(), getHeight());
        setName ("CDAW");

        // The software renderer paints on request; Direct2D would wait for a vblank
        // that never comes while startup blocks the message thread.
        if (auto* peer = getPeer())
            peer->setCurrentRenderingEngine (0);

        setVisible (true);
        setStep ("Starting", 0.0);
    }

    StartupSplash::~StartupSplash() = default;

    void StartupSplash::setStep (const juce::String& text, double p)
    {
        juce::Logger::writeToLog ("Startup: " + text);
        step = text;
        progress = juce::jlimit (0.0, 1.0, p);
        repaint();

        if (auto* peer = getPeer())
            peer->performAnyPendingRepaintsNow();
    }

    void StartupSplash::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds();
        g.fillAll (Palette::windowBackground);
        g.setColour (Palette::panelOutline);
        g.drawRect (r);

        auto content = r.reduced (24);
        auto logoArea = content.removeFromLeft (150).withSizeKeepingCentre (150, 150);

        if (logo.isValid())
            g.drawImage (logo, logoArea.toFloat(), juce::RectanglePlacement::centred);

        content.removeFromLeft (24);

        g.setColour (Palette::text);
        g.setFont (juce::FontOptions (40.0f, juce::Font::bold));
        g.drawText ("CDAW", content.removeFromTop (54), juce::Justification::bottomLeft, false);

        g.setColour (Palette::textDim);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText ("Version " + version, content.removeFromTop (20), juce::Justification::topLeft, false);

        // Step text and progress bar sit on the bottom of the text column.
        auto bar = content.removeFromBottom (8).toFloat();
        content.removeFromBottom (8);
        g.setColour (Palette::text);
        g.setFont (juce::FontOptions (14.0f));
        g.drawText (progress < 1.0 ? step + juce::String::fromUTF8 ("\xe2\x80\xa6") : step, content.removeFromBottom (20), juce::Justification::bottomLeft, true);

        g.setColour (Palette::panelRaised);
        g.fillRoundedRectangle (bar, 4.0f);

        if (progress > 0.0)
        {
            auto filled = bar.withWidth (juce::jmax (8.0f, bar.getWidth() * (float) progress));
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4fd6c8), bar.getX(), 0.0f,
                                                     juce::Colour (0xffd77cff), bar.getRight(), 0.0f, false));
            g.fillRoundedRectangle (filled, 4.0f);
        }
    }
}
