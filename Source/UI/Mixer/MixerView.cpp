#include "MixerView.h"
#include "InsertSlots.h"
#include "../Arrange/TrackHeader.h"
#include "../Common/LevelMeter.h"
#include "../LookAndFeel/Palette.h"
#include "../../Model/MixerOps.h"
#include "../../Model/TrackOps.h"

namespace cdaw
{
    ChannelStrip::ChannelStrip (EditViewState& vs, te::Track::Ptr t) : viewState (vs), track (t)
    {
        init();
    }

    ChannelStrip::ChannelStrip (EditViewState& vs) : viewState (vs)
    {
        init();
    }

    te::VolumeAndPanPlugin* ChannelStrip::getVolumePlugin() const
    {
        if (track == nullptr)
            return viewState.edit.getMasterVolumePlugin().get();

        return TrackHeader::getVolumePlugin (*track);
    }

    void ChannelStrip::init()
    {
        meter = std::make_unique<LevelMeter>();
        addAndMakeVisible (*meter);

        nameLabel.setJustificationType (juce::Justification::centred);
        nameLabel.setFont (juce::FontOptions (12.5f, juce::Font::bold));
        nameLabel.setText (track != nullptr ? track->getName() : juce::String ("Master"), juce::dontSendNotification);
        nameLabel.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (nameLabel);

        auto* at = dynamic_cast<te::AudioTrack*> (track.get());

        if (track == nullptr)
        {
            inserts = std::make_unique<InsertSlots> (viewState.edit.getMasterPluginList(), true);
            addAndMakeVisible (*inserts);
        }
        else
        {
            if (track->canContainPlugins())
            {
                inserts = std::make_unique<InsertSlots> (track, true);
                addAndMakeVisible (*inserts);
            }

            if (at != nullptr && TrackOps::getKind (*track) != TrackKind::fx)
            {
                sends = std::make_unique<SendSlots> (track, true);
                addAndMakeVisible (*sends);
            }

            outputButton.setTooltip ("Output routing");
            outputButton.onClick = [this] { MixerOps::createOutputMenu (*track).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&outputButton)); };
            addAndMakeVisible (outputButton);

            muteButton.setColour (juce::TextButton::buttonOnColourId, Palette::mute);
            soloButton.setColour (juce::TextButton::buttonOnColourId, Palette::solo);
            muteButton.onClick = [this] { viewState.edit.getUndoManager().beginNewTransaction ("Mute"); track->setMute (! track->isMuted (false)); };
            soloButton.onClick = [this] { viewState.edit.getUndoManager().beginNewTransaction ("Solo"); track->setSolo (! track->isSolo (false)); };
            addAndMakeVisible (muteButton);
            addAndMakeVisible (soloButton);

            if (at != nullptr && TrackOps::getKind (*track) != TrackKind::fx)
            {
                armButton.setColour (juce::TextButton::buttonOnColourId, Palette::record);
                monitorButton.setColour (juce::TextButton::buttonOnColourId, Palette::monitor);
                armButton.onClick = [at] { TrackOps::setArmed (*at, ! TrackOps::isArmed (*at)); };
                monitorButton.onClick = [at]
                {
                    using MM = te::InputDevice::MonitorMode;
                    auto m = TrackOps::getMonitorMode (*at);
                    TrackOps::setMonitorMode (*at, m == MM::automatic ? MM::on : m == MM::on ? MM::off : MM::automatic);
                };
                addAndMakeVisible (armButton);
                addAndMakeVisible (monitorButton);
            }
        }

        if (auto vp = getVolumePlugin())
        {
            te::Plugin::Ptr keep = vp;
            fader.setSliderStyle (juce::Slider::LinearVertical);
            fader.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
            fader.setRange (0.0, 1.0);
            fader.setDoubleClickReturnValue (true, te::decibelsToVolumeFaderPosition (0.0f));
            fader.onDragStart = [keep, vp] { vp->volParam->parameterChangeGestureBegin(); };
            fader.onDragEnd = [keep, vp] { vp->volParam->parameterChangeGestureEnd(); };
            fader.onValueChange = [this, vp] { if (! updating) vp->setSliderPos ((float) fader.getValue()); };
            addAndMakeVisible (fader);

            pan.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            pan.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
            pan.setRange (-1.0, 1.0);
            pan.setDoubleClickReturnValue (true, 0.0);
            pan.onDragStart = [vp] { vp->panParam->parameterChangeGestureBegin(); };
            pan.onDragEnd = [vp] { vp->panParam->parameterChangeGestureEnd(); };
            pan.onValueChange = [this, vp] { if (! updating) vp->setPan ((float) pan.getValue()); };
            addAndMakeVisible (pan);

            dbLabel.setJustificationType (juce::Justification::centred);
            dbLabel.setFont (juce::FontOptions (11.0f));
            dbLabel.setColour (juce::Label::textColourId, Palette::textDim);
            addAndMakeVisible (dbLabel);
        }

        if (track != nullptr)
        {
            if (auto mp = TrackHeader::getMeterPlugin (*track))
                meter->attach (&mp->measurer, mp);
        }

        viewState.selectionManager.addChangeListener (this);
        timerCallback();
        startTimerHz (10);
    }

    ChannelStrip::~ChannelStrip()
    {
        viewState.selectionManager.removeChangeListener (this);
    }

    void ChannelStrip::timerCallback()
    {
        // Master meter comes from the playback context, which can be recreated.
        if (track == nullptr)
            if (auto epc = viewState.edit.getCurrentPlaybackContext())
                meter->attach (&epc->masterLevels);

        if (auto vp = getVolumePlugin())
        {
            juce::ScopedValueSetter<bool> svs (updating, true);
            if (! fader.isMouseButtonDown()) fader.setValue (vp->getSliderPos(), juce::dontSendNotification);
            if (! pan.isMouseButtonDown())   pan.setValue (vp->getPan(), juce::dontSendNotification);

            auto db = vp->getVolumeDb();
            dbLabel.setText (db <= -96.0f ? juce::String ("-inf") : juce::String (db, 1), juce::dontSendNotification);
        }

        if (track != nullptr)
        {
            if (nameLabel.getText() != track->getName())
                nameLabel.setText (track->getName(), juce::dontSendNotification);

            muteButton.setToggleState (track->isMuted (false), juce::dontSendNotification);
            soloButton.setToggleState (track->isSolo (false), juce::dontSendNotification);
            outputButton.setButtonText (MixerOps::describeOutput (*track));

            if (auto at = dynamic_cast<te::AudioTrack*> (track.get()))
            {
                armButton.setToggleState (TrackOps::isArmed (*at), juce::dontSendNotification);
                using MM = te::InputDevice::MonitorMode;
                auto m = TrackOps::getMonitorMode (*at);
                monitorButton.setButtonText (m == MM::automatic ? "A" : "I");
                monitorButton.setToggleState (m == MM::on || (m == MM::automatic && TrackOps::isMonitoringLive (*at)), juce::dontSendNotification);
            }
        }
    }

    void ChannelStrip::paint (juce::Graphics& g)
    {
        const bool selected = track != nullptr && viewState.selectionManager.isSelected (track.get());
        g.fillAll (selected ? Palette::panelRaised : Palette::panelBackground);

        auto colour = track != nullptr ? track->getColour() : Palette::accent;
        g.setColour (colour);
        g.fillRect (0, 0, getWidth(), 4);

        g.setColour (Palette::windowBackground);
        g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());

        if (track != nullptr)
        {
            g.setColour (Palette::textDim);
            g.setFont (juce::FontOptions (10.0f));
            g.drawText (TrackOps::getKindName (TrackOps::getKind (*track)), getLocalBounds().removeFromBottom (14), juce::Justification::centred);
        }
    }

    void ChannelStrip::resized()
    {
        auto r = getLocalBounds().reduced (4, 6);
        nameLabel.setBounds (r.removeFromTop (20));

        if (outputButton.isVisible())
        {
            outputButton.setBounds (r.removeFromTop (20));
            r.removeFromTop (4);
        }

        auto bottom = r.removeFromBottom (12);
        juce::ignoreUnused (bottom);

        // Buttons row(s)
        auto buttons = r.removeFromBottom (armButton.isVisible() ? 44 : 22);
        {
            auto row1 = buttons.removeFromTop (20);
            auto w = row1.getWidth() / 2;
            muteButton.setBounds (row1.removeFromLeft (w).reduced (1, 0));
            soloButton.setBounds (row1.reduced (1, 0));

            if (armButton.isVisible())
            {
                buttons.removeFromTop (4);
                auto row2 = buttons.removeFromTop (20);
                armButton.setBounds (row2.removeFromLeft (w).reduced (1, 0));
                monitorButton.setBounds (row2.reduced (1, 0));
            }
        }

        r.removeFromBottom (4);
        dbLabel.setBounds (r.removeFromBottom (16));

        // Fader + meter take ~45% of the height, inserts/sends the rest.
        auto faderArea = r.removeFromBottom (juce::jmax (120, (int) (r.getHeight() * 0.48f)));
        pan.setBounds (faderArea.removeFromTop (30).withSizeKeepingCentre (30, 30));
        meter->setBounds (faderArea.removeFromRight (12).reduced (0, 4));
        fader.setBounds (faderArea);

        r.removeFromBottom (4);

        if (sends != nullptr)
        {
            auto h = juce::jmin (sends->getIdealHeight(), r.getHeight() / 2);
            sends->setBounds (r.removeFromBottom (h));
            r.removeFromBottom (4);
        }

        if (inserts != nullptr)
            inserts->setBounds (r.removeFromTop (juce::jmin (inserts->getIdealHeight(), r.getHeight())));
    }

    void ChannelStrip::mouseDown (const juce::MouseEvent&)
    {
        if (track != nullptr)
            viewState.selectionManager.selectOnly (track.get());
    }

    //==============================================================================
    MixerView::MixerView (EditViewState& vs) : viewState (vs)
    {
        viewport.setViewedComponent (&stripHolder, false);
        viewport.setScrollBarsShown (false, true);
        addAndMakeVisible (viewport);

        master = std::make_unique<ChannelStrip> (viewState);
        addAndMakeVisible (*master);

        viewState.edit.state.addListener (this);
        rebuild();
    }

    MixerView::~MixerView()
    {
        viewState.edit.state.removeListener (this);
    }

    void MixerView::rebuild()
    {
        strips.clear();

        for (auto t : TrackOps::getArrangeTracks (viewState.edit))
        {
            // Plain folders have no audio path; groups do.
            if (auto ft = dynamic_cast<te::FolderTrack*> (t); ft != nullptr && ! ft->isSubmixFolder())
                continue;

            stripHolder.addAndMakeVisible (strips.add (new ChannelStrip (viewState, t)));
        }

        resized();
    }

    void MixerView::paint (juce::Graphics& g)
    {
        g.fillAll (Palette::windowBackground);
    }

    void MixerView::resized()
    {
        auto r = getLocalBounds();
        master->setBounds (r.removeFromRight (ChannelStrip::width + 10));
        r.removeFromRight (4);
        viewport.setBounds (r);

        auto h = juce::jmax (100, r.getHeight() - (viewport.isHorizontalScrollBarShown() ? viewport.getScrollBarThickness() : 0));
        stripHolder.setSize (juce::jmax (r.getWidth(), strips.size() * ChannelStrip::width), h);

        int x = 0;
        for (auto s : strips)
        {
            s->setBounds (x, 0, ChannelStrip::width, h);
            x += ChannelStrip::width;
        }
    }
}
