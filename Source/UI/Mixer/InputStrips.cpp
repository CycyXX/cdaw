#include "InputStrips.h"
#include "../LookAndFeel/Palette.h"
#include "../../Model/TrackOps.h"

namespace cdaw
{
    InputStrip::InputStrip (te::Edit& e, te::WaveInputDevice& d) : edit (e), device (&d)
    {
        setOpaque (true);   // paint() fills the whole component

        if (auto range = TrackOps::getChannelRange (d))
            std::tie (firstChannel, numChannels) = *range;

        device->addSelectableListener (this);
        device->levelMeasurer.addClient (peakClient);
        meter.attach (&device->levelMeasurer);
        meter.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (meter);

        setTooltip ("Input level of " + d.getAliasOrName() + ". Click to reset the peak; right-click to pair or split.");
        startTimerHz (10);
    }

    InputStrip::~InputStrip()
    {
        if (device != nullptr)
        {
            meter.detach();
            device->levelMeasurer.removeClient (peakClient);
            device->removeSelectableListener (this);
        }
    }

    void InputStrip::selectableObjectAboutToBeDeleted (te::Selectable*)
    {
        // The engine is rebuilding its inputs: let go before the measurer goes away.
        stopTimer();
        meter.detach();
        device->levelMeasurer.removeClient (peakClient);
        device = nullptr;

        if (onDeviceDeleted)
            onDeviceDeleted();
    }

    juce::String InputStrip::describeTargets() const
    {
        juce::StringArray names;

        for (auto* in : edit.getAllInputDevices())
            if (&in->getInputDevice() == device)
                for (auto id : in->getTargets())
                    if (auto t = te::findTrackForID (edit, id))
                        names.add (t->getName());

        return names.joinIntoString (", ");
    }

    void InputStrip::timerCallback()
    {
        if (device == nullptr || ! isShowing())
            return;

        auto newPeak = peakDb;
        for (int ch = 0; ch < juce::jmax (1, peakClient.getNumChannelsUsed()); ++ch)
            newPeak = juce::jmax (newPeak, peakClient.getAndClearAudioLevel (ch).dB);

        auto newTargets = describeTargets();

        if (newPeak > peakDb + 0.05f || newTargets != targets)
        {
            peakDb = newPeak;
            targets = newTargets;
            repaint();
        }
    }

    // Layout, top to bottom: colour bar, name, mono/stereo, meter with scale, peak, targets.
    namespace InputStripLayout
    {
        constexpr int barH = 4, nameH = 30, modeH = 14, peakH = 16, targetsH = 30, margin = 4;
    }

    void InputStrip::resized()
    {
        using namespace InputStripLayout;
        auto r = getLocalBounds().reduced (margin, 0);
        r.removeFromTop (barH + 2 + nameH + modeH + 4);
        r.removeFromBottom (targetsH + peakH + 4);
        meter.setBounds (r.removeFromRight (numChannels > 1 ? 16 : 10).translated (-8, 0));
    }

    void InputStrip::paint (juce::Graphics& g)
    {
        using namespace InputStripLayout;
        g.fillAll (Palette::panelBackground);
        g.setColour (Palette::accentDim);
        g.fillRect (0, 0, getWidth(), barH);
        g.setColour (Palette::windowBackground);
        g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());

        auto r = getLocalBounds().reduced (margin, 0);
        r.removeFromTop (barH + 2);

        g.setColour (device != nullptr ? Palette::text : Palette::textDisabled);
        g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
        g.drawFittedText (device != nullptr ? device->getAliasOrName() : juce::String(), r.removeFromTop (nameH),
                          juce::Justification::centred, 2, 0.8f);

        g.setColour (Palette::textDim);
        g.setFont (juce::FontOptions (10.0f));
        g.drawText (numChannels > 1 ? "Stereo" : "Mono", r.removeFromTop (modeH), juce::Justification::centred);

        // Targets (bottom), then the peak readout above them.
        auto targetArea = r.removeFromBottom (targetsH);
        g.setColour (targets.isNotEmpty() ? Palette::text.withAlpha (0.8f) : Palette::textDisabled);
        g.setFont (juce::FontOptions (10.0f));
        g.drawFittedText (targets.isNotEmpty() ? juce::String::fromUTF8 ("\xe2\x86\x92 ") + targets : juce::String ("Not used"),
                          targetArea, juce::Justification::centredTop, 2, 0.8f);

        auto peakArea = r.removeFromBottom (peakH);
        g.setColour (peakDb > -0.05f ? Palette::meterRed : Palette::textDim);
        g.setFont (juce::FontOptions (10.5f));
        g.drawText (peakDb <= -99.0f ? juce::String::fromUTF8 ("-\xe2\x88\x9e") : juce::String (peakDb, 1),
                    peakArea, juce::Justification::centred);

        // dB marks left of the meter.
        auto mb = meter.getBounds();
        if (mb.getHeight() > 60)
        {
            g.setFont (juce::FontOptions (9.0f));
            float lastY = -100.0f;

            for (float db : { 0.0f, -6.0f, -12.0f, -18.0f, -24.0f, -36.0f, -48.0f })
            {
                auto y = (float) mb.getBottom() - LevelMeter::dbToProportion (db) * (float) mb.getHeight();
                if (y - lastY < 11.0f)
                    continue;

                lastY = y;
                g.setColour (Palette::textDim.withAlpha (db == 0.0f ? 0.9f : 0.6f));
                g.drawHorizontalLine ((int) std::round (y), (float) mb.getX() - 4.0f, (float) mb.getX() - 1.0f);
                g.drawText (juce::String ((int) db), juce::Rectangle<float> ((float) mb.getX() - 30.0f, y - 6.0f, 25.0f, 12.0f),
                            juce::Justification::centredRight, false);
            }
        }
    }

    void InputStrip::mouseDown (const juce::MouseEvent& e)
    {
        if (e.mods.isPopupMenu())
        {
            showMenu();
            return;
        }

        peakDb = -100.0f;
        repaint();
    }

    void InputStrip::showMenu()
    {
        if (device == nullptr)
            return;

        juce::PopupMenu m;
        auto* e = &edit;

        auto regroup = [e] (int first, int num)
        {
            e->getUndoManager().beginNewTransaction ("Regroup inputs");
            TrackOps::regroupInputs (*e, first, num);
        };

        if (numChannels == 1)
        {
            const int pairStart = firstChannel & ~1;
            juce::String pairName;

            for (auto& c : TrackOps::getAudioInputChoices (edit.engine, ChannelMode::stereo))
                if (c.firstChannel == pairStart)
                    pairName = c.name;

            m.addItem ("Pair as Stereo Input" + (pairName.isNotEmpty() ? " (" + pairName + ")" : juce::String()),
                       pairName.isNotEmpty(), false, [regroup, pairStart] { regroup (pairStart, 2); });
        }
        else
        {
            const int first = firstChannel;
            m.addItem ("Split into Mono Inputs", [regroup, first] { regroup (first, 1); });
        }

        m.addSeparator();
        m.addItem ("Reset Peak", [sp = juce::Component::SafePointer<InputStrip> (this)]
        {
            if (sp != nullptr) { sp->peakDb = -100.0f; sp->repaint(); }
        });

        m.showMenuAsync ({});
    }

    //==============================================================================
    InputsPanel::InputsPanel (EditViewState& vs) : viewState (vs)
    {
        setOpaque (true);   // paint() fills the whole component
        viewport.setViewedComponent (&holder, false);
        viewport.setScrollBarsShown (false, true);
        viewport.setScrollBarThickness (6);
        addAndMakeVisible (viewport);

        foldButton.setTooltip ("Show / hide the audio inputs");
        foldButton.onClick = [this] { setOpen (! isOpen()); };
        addAndMakeVisible (foldButton);

        viewState.edit.engine.getDeviceManager().addChangeListener (this);
        rebuild();
    }

    InputsPanel::~InputsPanel()
    {
        viewState.edit.engine.getDeviceManager().removeChangeListener (this);
    }

    bool InputsPanel::isOpen() const
    {
        return (bool) viewState.state.getProperty (ViewIDs::showMixerInputs, true);
    }

    void InputsPanel::setOpen (bool open)
    {
        viewState.state.setProperty (ViewIDs::showMixerInputs, open, nullptr);
        viewport.setVisible (open);
        resized();
        repaint();

        if (onWidthChanged)
            onWidthChanged();
    }

    void InputsPanel::rebuild()
    {
        cancelPendingUpdate();
        strips.clear();

        for (auto* wi : viewState.edit.engine.getDeviceManager().getWaveInputDevices())
        {
            if (wi->isTrackDevice() || ! wi->isEnabled())
                continue;

            auto* strip = strips.add (new InputStrip (viewState.edit, *wi));
            strip->onDeviceDeleted = [this] { triggerAsyncUpdate(); };
            holder.addAndMakeVisible (strip);
        }

        viewport.setVisible (isOpen());
        resized();
        repaint();

        if (onWidthChanged)
            onWidthChanged();
    }

    int InputsPanel::getIdealWidth() const
    {
        if (! isOpen())
            return foldedWidth;

        return strips.isEmpty() ? 96 : juce::jmin (strips.size(), 8) * InputStrip::width;
    }

    void InputsPanel::resized()
    {
        auto r = getLocalBounds();
        const bool open = isOpen();

        foldButton.setButtonText (juce::String::fromUTF8 (open ? "\xe2\x80\xb9" : "\xe2\x80\xba"));
        foldButton.setBounds (open ? r.removeFromTop (headerH).removeFromRight (headerH).reduced (2)
                                   : juce::Rectangle<int> (0, 0, foldedWidth, headerH).reduced (2));

        if (! open)
            return;

        viewport.setBounds (r);
        const bool scrolls = strips.size() * InputStrip::width > r.getWidth();
        auto h = r.getHeight() - (scrolls ? viewport.getScrollBarThickness() : 0);
        holder.setSize (strips.size() * InputStrip::width, h);

        int x = 0;
        for (auto s : strips)
        {
            s->setBounds (x, 0, InputStrip::width, h);
            x += InputStrip::width;
        }
    }

    void InputsPanel::paint (juce::Graphics& g)
    {
        g.fillAll (Palette::windowBackground);
        g.setColour (Palette::textDim);
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));

        if (isOpen())
        {
            g.drawText ("INPUTS", getLocalBounds().removeFromTop (headerH).withTrimmedLeft (6), juce::Justification::centredLeft);

            if (strips.isEmpty())
                g.drawFittedText ("No audio inputs", getLocalBounds().withTrimmedTop (headerH).reduced (6),
                                  juce::Justification::centredTop, 2);
        }
        else
        {
            // Folded: a tab with the title running up the side.
            g.setColour (Palette::panelBackground);
            g.fillRect (getLocalBounds());
            g.setColour (Palette::textDim);
            juce::Graphics::ScopedSaveState s (g);
            g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi)
                                .translated (0.0f, (float) getHeight()));
            g.drawText ("INPUTS", juce::Rectangle<int> (0, 0, getHeight() - headerH, getWidth()), juce::Justification::centred);
        }
    }

    void InputsPanel::mouseUp (const juce::MouseEvent& e)
    {
        // The folded tab opens on a click anywhere.
        if (! isOpen() && ! e.mods.isPopupMenu())
            setOpen (true);
    }
}
