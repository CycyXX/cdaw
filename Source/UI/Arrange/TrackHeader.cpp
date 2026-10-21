#include "TrackHeader.h"
#include "../LookAndFeel/Palette.h"
#include "../../Model/TrackOps.h"

namespace cdaw
{
    HeaderToggle::HeaderToggle (const juce::String& text, juce::Colour onColour, const juce::String& tip)
        : juce::TextButton (text)
    {
        setClickingTogglesState (false);
        setColour (juce::TextButton::buttonOnColourId, onColour);
        setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        setTooltip (tip);
        setWantsKeyboardFocus (false);
    }

    te::VolumeAndPanPlugin* TrackHeader::getVolumePlugin (te::Track& t)
    {
        if (auto at = dynamic_cast<te::AudioTrack*> (&t))
            return at->getVolumePlugin();

        if (auto ft = dynamic_cast<te::FolderTrack*> (&t))
            return ft->getVolumePlugin();

        return nullptr;
    }

    te::LevelMeterPlugin* TrackHeader::getMeterPlugin (te::Track& t)
    {
        if (auto at = dynamic_cast<te::AudioTrack*> (&t))
            return at->getLevelMeterPlugin();

        for (auto p : t.pluginList.getPlugins())
            if (auto m = dynamic_cast<te::LevelMeterPlugin*> (p))
                return m;

        return nullptr;
    }

    TrackHeader::TrackHeader (ArrangeHost& h, te::Track::Ptr t)
        : host (h), viewState (h.getViewState()), track (t)
    {
        nameLabel.setText (track->getName(), juce::dontSendNotification);
        nameLabel.setEditable (false, true, false);
        nameLabel.setFont (juce::FontOptions (13.5f, juce::Font::bold));
        nameLabel.setColour (juce::Label::textColourId, Palette::text);
        nameLabel.setInterceptsMouseClicks (false, false);
        nameLabel.onTextChange = [this]
        {
            track->edit.getUndoManager().beginNewTransaction ("Rename track");
            track->setName (nameLabel.getText());
        };
        addAndMakeVisible (nameLabel);

        auto* at = dynamic_cast<te::AudioTrack*> (track.get());
        auto* ft = dynamic_cast<te::FolderTrack*> (track.get());

        muteButton.onClick = [this]
        {
            track->edit.getUndoManager().beginNewTransaction ("Mute");
            track->setMute (! track->isMuted (false));
            refreshButtons();
        };
        soloButton.onClick = [this]
        {
            track->edit.getUndoManager().beginNewTransaction ("Solo");
            track->setSolo (! track->isSolo (false));
            refreshButtons();
        };

        const bool hasMixControls = at != nullptr || (ft != nullptr && ft->isSubmixFolder());

        if (hasMixControls)
        {
            addAndMakeVisible (muteButton);
            addAndMakeVisible (soloButton);
        }

        if (at != nullptr && TrackOps::getKind (*track) != TrackKind::fx)
        {
            armButton.onClick = [this, at]
            {
                if (TrackOps::getAssignedInput (*at) == nullptr)
                {
                    showInputMenu();
                    return;
                }

                TrackOps::setArmed (*at, ! TrackOps::isArmed (*at));
                refreshButtons();
            };

            monitorButton.onClick = [this, at]
            {
                using MM = te::InputDevice::MonitorMode;
                auto mode = TrackOps::getMonitorMode (*at);
                auto next = mode == MM::automatic ? MM::on : mode == MM::on ? MM::off : MM::automatic;
                TrackOps::setMonitorMode (*at, next);

                if (TrackOps::countTracksSharingInput (*at) > 1)
                    host.getViewState().edit.engine.getUIBehaviour()
                        .showWarningMessage ("Monitoring applies to the input, so it also affects other tracks using it.");

                refreshButtons();
            };

            inputButton.setTooltip ("Input");
            inputButton.onClick = [this] { showInputMenu(); };
            inputButton.setWantsKeyboardFocus (false);

            addAndMakeVisible (armButton);
            addAndMakeVisible (monitorButton);
            addAndMakeVisible (inputButton);
        }

        if (ft != nullptr)
        {
            collapseButton.setButtonText ((bool) track->state.getProperty (ViewIDs::cdawCollapsed, false) ? ">" : "v");
            collapseButton.setTooltip ("Show / hide the tracks in this folder");
            collapseButton.onClick = [this]
            {
                bool collapsed = track->state.getProperty (ViewIDs::cdawCollapsed, false);
                track->state.setProperty (ViewIDs::cdawCollapsed, ! collapsed, nullptr);
                host.trackHeightChanged();
            };
            addAndMakeVisible (collapseButton);
        }

        if (auto vp = getVolumePlugin (*track))
        {
            volumeSlider.setSliderStyle (juce::Slider::LinearHorizontal);
            volumeSlider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
            volumeSlider.setRange (0.0, 1.0);
            volumeSlider.setDoubleClickReturnValue (true, te::decibelsToVolumeFaderPosition (0.0f));
            volumeSlider.setTooltip ("Volume");
            volumeSlider.onDragStart = [vp] { vp->volParam->parameterChangeGestureBegin(); };
            volumeSlider.onDragEnd = [vp] { vp->volParam->parameterChangeGestureEnd(); };
            volumeSlider.onValueChange = [this, vp]
            {
                if (! updatingSliders)
                {
                    vp->setSliderPos ((float) volumeSlider.getValue());
                    volumeSlider.setTooltip (juce::Decibels::toString (vp->getVolumeDb(), 1));
                }
            };
            addAndMakeVisible (volumeSlider);

            panSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            panSlider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
            panSlider.setRange (-1.0, 1.0);
            panSlider.setDoubleClickReturnValue (true, 0.0);
            panSlider.setTooltip ("Pan");
            panSlider.onDragStart = [vp] { vp->panParam->parameterChangeGestureBegin(); };
            panSlider.onDragEnd = [vp] { vp->panParam->parameterChangeGestureEnd(); };
            panSlider.onValueChange = [this, vp] { if (! updatingSliders) vp->setPan ((float) panSlider.getValue()); };
            addAndMakeVisible (panSlider);
        }

        if (auto mp = getMeterPlugin (*track))
            meter.attach (&mp->measurer, mp);

        addAndMakeVisible (meter);
        viewState.selectionManager.addChangeListener (this);
        refreshButtons();
        startTimerHz (8);
    }

    TrackHeader::~TrackHeader()
    {
        viewState.selectionManager.removeChangeListener (this);
    }

    void TrackHeader::timerCallback()
    {
        refreshButtons();
    }

    void TrackHeader::refreshButtons()
    {
        if (nameLabel.getText() != track->getName() && ! nameLabel.isBeingEdited())
            nameLabel.setText (track->getName(), juce::dontSendNotification);

        muteButton.setToggleState (track->isMuted (false), juce::dontSendNotification);
        soloButton.setToggleState (track->isSolo (false), juce::dontSendNotification);

        if (auto at = dynamic_cast<te::AudioTrack*> (track.get()))
        {
            auto input = TrackOps::getAssignedInput (*at);
            armButton.setToggleState (TrackOps::isArmed (*at), juce::dontSendNotification);
            armButton.setAlpha (input != nullptr ? 1.0f : 0.5f);

            using MM = te::InputDevice::MonitorMode;
            auto mode = TrackOps::getMonitorMode (*at);
            monitorButton.setButtonText (mode == MM::automatic ? "A" : "I");
            monitorButton.setToggleState (mode == MM::on || (mode == MM::automatic && TrackOps::isMonitoringLive (*at)),
                                          juce::dontSendNotification);
            monitorButton.setAlpha (input != nullptr ? 1.0f : 0.5f);

            auto inName = input != nullptr ? input->getInputDevice().getAliasOrName() : juce::String ("No input");
            if (inputButton.getButtonText() != inName)
                inputButton.setButtonText (inName);
        }

        if (auto vp = getVolumePlugin (*track))
        {
            juce::ScopedValueSetter<bool> svs (updatingSliders, true);

            if (! volumeSlider.isMouseButtonDown())
                volumeSlider.setValue (vp->getSliderPos(), juce::dontSendNotification);
            if (! panSlider.isMouseButtonDown())
                panSlider.setValue (vp->getPan(), juce::dontSendNotification);
        }
    }

    void TrackHeader::showInputMenu()
    {
        auto at = dynamic_cast<te::AudioTrack*> (track.get());
        if (at == nullptr)
            return;

        auto& edit = track->edit;
        auto current = TrackOps::getAssignedInput (*at);
        juce::PopupMenu m;

        m.addItem ("No input", true, current == nullptr, [at] { TrackOps::assignInput (*at, nullptr); });
        m.addSeparator();
        m.addSectionHeader ("Audio inputs");

        for (auto& choice : TrackOps::getInputChoices (edit, false))
        {
            auto* inst = choice.instance;
            m.addItem (choice.name, true, inst == current, [at, inst]
            {
                at->edit.getUndoManager().beginNewTransaction ("Set input");
                TrackOps::assignInput (*at, inst);
            });
        }

        m.addSectionHeader ("MIDI inputs");

        for (auto& choice : TrackOps::getInputChoices (edit, true))
        {
            auto* inst = choice.instance;
            m.addItem (choice.name, true, inst == current, [at, inst]
            {
                at->edit.getUndoManager().beginNewTransaction ("Set input");
                TrackOps::assignInput (*at, inst);
            });
        }

        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&inputButton));
    }

    void TrackHeader::paint (juce::Graphics& g)
    {
        const bool selected = viewState.selectionManager.isSelected (track.get());
        auto r = getLocalBounds();
        auto indent = TrackOps::getDepth (*track) * 10;

        g.fillAll (selected ? Palette::panelRaised.brighter (0.08f) : Palette::panelBackground);
        g.setColour (track->getColour());
        g.fillRect (r.getX() + indent, r.getY(), 5, r.getHeight() - 1);

        auto kind = TrackOps::getKind (*track);
        g.setColour (Palette::textDim);
        g.setFont (juce::FontOptions (10.5f));
        g.drawText (TrackOps::getKindName (kind), r.withTrimmedLeft (indent + 10).withTrimmedRight (16).removeFromTop (16),
                    juce::Justification::centredRight);

        g.setColour (Palette::windowBackground);
        g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
        g.setColour (Palette::panelOutline);
        g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());
    }

    void TrackHeader::resized()
    {
        auto r = getLocalBounds().reduced (4, 3);
        r.removeFromLeft (TrackOps::getDepth (*track) * 10 + 6);

        meter.setBounds (r.removeFromRight (7));
        r.removeFromRight (4);

        auto top = r.removeFromTop (18);

        if (collapseButton.isVisible())
        {
            collapseButton.setBounds (top.removeFromLeft (18));
            top.removeFromLeft (2);
        }

        nameLabel.setBounds (top.withTrimmedRight (60));

        if (r.getHeight() < 16)
            return;

        r.removeFromTop (3);
        auto row = r.removeFromTop (20);
        const int bw = 22;

        for (auto* b : { (juce::Component*) &muteButton, (juce::Component*) &soloButton,
                         (juce::Component*) &armButton, (juce::Component*) &monitorButton })
        {
            if (b->isVisible())
            {
                b->setBounds (row.removeFromLeft (bw));
                row.removeFromLeft (2);
            }
        }

        if (inputButton.isVisible())
            inputButton.setBounds (row.withTrimmedLeft (4));

        const bool roomForMix = r.getHeight() >= 20;
        volumeSlider.setVisible (roomForMix && getVolumePlugin (*track) != nullptr);
        panSlider.setVisible (roomForMix && getVolumePlugin (*track) != nullptr);

        if (roomForMix)
        {
            r.removeFromTop (3);
            auto mix = r.removeFromTop (20);
            panSlider.setBounds (mix.removeFromRight (22));
            volumeSlider.setBounds (mix);
        }
    }

    void TrackHeader::mouseMove (const juce::MouseEvent& e)
    {
        setMouseCursor (isOnResizeEdge (e.y) ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
    }

    void TrackHeader::mouseDown (const juce::MouseEvent& e)
    {
        if (isOnResizeEdge (e.y))
        {
            resizing = true;
            resizeStartHeight = EditViewState::getTrackHeight (*track);
            return;
        }

        auto& sm = viewState.selectionManager;

        if (e.mods.isPopupMenu())
        {
            if (! sm.isSelected (track.get()))
                sm.selectOnly (track.get());

            host.showTrackMenu (*track);
            return;
        }

        if (e.mods.isCommandDown())
        {
            if (sm.isSelected (track.get())) sm.deselect (track.get());
            else                             sm.addToSelection (track.get());
        }
        else if (e.mods.isShiftDown())
        {
            sm.addToSelection (track.get());
        }
        else
        {
            sm.selectOnly (track.get());
        }

        if (e.getNumberOfClicks() == 2)
            nameLabel.showEditor();
    }

    void TrackHeader::mouseDrag (const juce::MouseEvent& e)
    {
        if (resizing)
        {
            EditViewState::setTrackHeight (*track, resizeStartHeight + e.getDistanceFromDragStartY());
            host.trackHeightChanged();
        }
    }

    void TrackHeader::mouseUp (const juce::MouseEvent&)
    {
        resizing = false;
    }
}
