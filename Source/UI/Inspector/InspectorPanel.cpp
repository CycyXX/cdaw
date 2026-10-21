#include "InspectorPanel.h"
#include "../Arrange/TrackHeader.h"
#include "../Common/LevelMeter.h"
#include "../LookAndFeel/Palette.h"
#include "../Mixer/InsertSlots.h"
#include "../../App/AppSettings.h"
#include "../../Model/DrumMapLibrary.h"
#include "../../Model/MixerOps.h"
#include "../../Model/TrackOps.h"

namespace cdaw
{
    InspectorPanel::InspectorPanel (EditViewState& vs) : viewState (vs)
    {
        setOpaque (true);   // paint() fills the whole component
        meter = std::make_unique<LevelMeter>();

        nameLabel.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        nameLabel.setColour (juce::Label::textColourId, Palette::text);
        addAndMakeVisible (nameLabel);

        inputButton.onClick = [this]
        {
            auto at = dynamic_cast<te::AudioTrack*> (track.get());
            if (at == nullptr)
                return;

            juce::PopupMenu m;
            auto current = TrackOps::getAssignedInput (*at);
            m.addItem ("No input", true, current == nullptr, [at] { TrackOps::assignInput (*at, nullptr); });

            for (bool midi : { false, true })
            {
                m.addSectionHeader (midi ? "MIDI inputs" : "Audio inputs");

                for (auto& c : TrackOps::getInputChoices (at->edit, midi))
                {
                    auto* inst = c.instance;
                    m.addItem (c.name, true, inst == current, [at, inst]
                    {
                        at->edit.getUndoManager().beginNewTransaction ("Set input");
                        TrackOps::assignInput (*at, inst);
                    });
                }
            }

            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&inputButton));
        };

        outputButton.onClick = [this]
        {
            if (track != nullptr)
                MixerOps::createOutputMenu (*track).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&outputButton));
        };

        drumMapButton.setTooltip ("Drum map: names the rows in the drum editor");
        drumMapButton.onClick = [this]
        {
            if (track == nullptr)
                return;

            te::Track::Ptr t = track;
            auto current = DrumMapLibrary::getForTrack (*t);
            juce::PopupMenu m;
            m.addItem ("None (piano roll)", true, ! current.has_value(), [this, t]
            {
                DrumMapLibrary::setForTrack (*t, {});
                if (onDrumMapChanged) onDrumMapChanged (*t);
            });

            for (auto& map : DrumMapLibrary::getAll())
            {
                auto name = map.name;
                m.addItem (name, true, current && current->name == name, [this, t, name]
                {
                    DrumMapLibrary::setForTrack (*t, name);
                    if (onDrumMapChanged) onDrumMapChanged (*t);
                });
            }

            m.addSeparator();
            m.addItem ("Open my drum map folder", [] { AppSettings::getUserDrumMapsFolder().startAsProcess(); });
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&drumMapButton));
        };

        volume.setSliderStyle (juce::Slider::LinearHorizontal);
        volume.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        volume.setRange (0.0, 1.0);
        volume.setDoubleClickReturnValue (true, te::decibelsToVolumeFaderPosition (0.0f));
        volume.onValueChange = [this]
        {
            if (! updating && track != nullptr)
                if (auto vp = TrackHeader::getVolumePlugin (*track))
                    vp->setSliderPos ((float) volume.getValue());
        };

        pan.onValueChange = [this]
        {
            if (! updating && track != nullptr)
                if (auto vp = TrackHeader::getVolumePlugin (*track))
                    vp->setPan ((float) pan.getValue());
        };

        volumeLabel.setJustificationType (juce::Justification::centredRight);
        volumeLabel.setColour (juce::Label::textColourId, Palette::textDim);

        for (auto* c : std::initializer_list<juce::Component*> { &inputButton, &outputButton, &drumMapButton,
                                                                 &volume, &pan, &volumeLabel, meter.get() })
            addChildComponent (c);

        viewState.selectionManager.addChangeListener (this);
        setTrack (findSelectedTrack());
        startTimerHz (10);
    }

    InspectorPanel::~InspectorPanel()
    {
        viewState.selectionManager.removeChangeListener (this);
    }

    te::Track* InspectorPanel::findSelectedTrack() const
    {
        for (auto s : viewState.selectionManager.getSelectedObjects())
            if (auto t = dynamic_cast<te::Track*> (s))
                return t;

        for (auto s : viewState.selectionManager.getSelectedObjects())
            if (auto c = dynamic_cast<te::Clip*> (s))
                return c->getTrack();

        return track.get();   // keep showing the last track when the selection is cleared
    }

    void InspectorPanel::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        auto t = findSelectedTrack();

        if (t != track.get())
            setTrack (t);
    }

    void InspectorPanel::setTrack (te::Track* t)
    {
        inserts.reset();
        sends.reset();
        meter->detach();
        track = t;
        shownDrumMapSetting = "<unset>";

        // The track may have been deleted.
        if (track != nullptr && ! te::containsTrack (viewState.edit, *track))
            track = nullptr;

        const bool isAudio = dynamic_cast<te::AudioTrack*> (track.get()) != nullptr;
        const bool hasPlugins = track != nullptr && track->canContainPlugins();
        const auto kind = track != nullptr ? TrackOps::getKind (*track) : TrackKind::other;

        inputButton.setVisible (isAudio);
        outputButton.setVisible (track != nullptr);
        drumMapButton.setVisible (kind == TrackKind::instrument);
        volume.setVisible (track != nullptr && TrackHeader::getVolumePlugin (*track) != nullptr);
        pan.setVisible (volume.isVisible());
        volumeLabel.setVisible (volume.isVisible());
        meter->setVisible (track != nullptr);

        if (hasPlugins)
        {
            inserts = std::make_unique<InsertSlots> (track, false);
            addAndMakeVisible (*inserts);
        }

        if (isAudio && kind != TrackKind::fx)
        {
            sends = std::make_unique<SendSlots> (track, false);
            addAndMakeVisible (*sends);
        }

        if (track != nullptr)
            if (auto mp = TrackHeader::getMeterPlugin (*track))
                meter->attach (&mp->measurer, mp);

        refreshState();
        resized();
        repaint();
    }

    void InspectorPanel::refreshState()
    {
        if (track != nullptr && ! te::containsTrack (viewState.edit, *track))
        {
            setTrack (nullptr);
            return;
        }

        if (track == nullptr)
        {
            nameLabel.setText ({}, juce::dontSendNotification);
            return;
        }

        nameLabel.setText (track->getName(), juce::dontSendNotification);

        if (auto at = dynamic_cast<te::AudioTrack*> (track.get()))
        {
            auto in = TrackOps::getAssignedInput (*at);
            inputButton.setButtonText ("In: " + (in != nullptr ? in->getInputDevice().getAliasOrName() : juce::String ("none")));
        }

        outputButton.setButtonText ("Out: " + MixerOps::describeOutput (*track));

        if (drumMapButton.isVisible())
        {
            auto setting = track->state.getProperty (ViewIDs::cdawDrumMap);

            if (setting != shownDrumMapSetting)
            {
                shownDrumMapSetting = setting;
                auto map = DrumMapLibrary::getForTrack (*track);
                drumMapButton.setButtonText ("Drum map: " + (map ? map->name : juce::String ("none")));
            }
        }

        if (auto vp = TrackHeader::getVolumePlugin (*track))
        {
            juce::ScopedValueSetter<bool> svs (updating, true);

            if (! volume.isMouseButtonDown())
                volume.setValue (vp->getSliderPos(), juce::dontSendNotification);
            if (! pan.isMouseButtonDown())
                pan.setValue (vp->getPan(), juce::dontSendNotification);

            volumeLabel.setText (juce::Decibels::toString (vp->getVolumeDb(), 1), juce::dontSendNotification);
        }
    }

    void InspectorPanel::paint (juce::Graphics& g)
    {
        g.fillAll (Palette::panelBackground);
        g.setColour (Palette::panelOutline);
        g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());

        if (track == nullptr)
        {
            g.setColour (Palette::textDim);
            g.drawText ("Select a track", getLocalBounds().reduced (10), juce::Justification::centredTop);
            return;
        }

        g.setColour (track->getColour());
        g.fillRect (0, 0, getWidth() - 1, 4);

        g.setColour (Palette::textDim);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));

        auto header = [&g] (juce::Rectangle<int> r, const char* text)
        {
            if (! r.isEmpty())
                g.drawText (text, r, juce::Justification::bottomLeft);
        };

        header (routingHeaderArea, "ROUTING");
        header (insertsHeaderArea, "INSERTS");
        header (sendsHeaderArea, "SENDS");
        header (faderHeaderArea, "LEVEL");
    }

    void InspectorPanel::resized()
    {
        auto r = getLocalBounds().reduced (8, 8).withTrimmedRight (1);
        nameLabel.setBounds (r.removeFromTop (24));
        r.removeFromTop (4);

        routingHeaderArea = insertsHeaderArea = sendsHeaderArea = faderHeaderArea = {};

        if (track == nullptr)
            return;

        routingHeaderArea = r.removeFromTop (16);

        for (auto* b : { &inputButton, &outputButton, &drumMapButton })
        {
            if (b->isVisible())
            {
                b->setBounds (r.removeFromTop (24));
                r.removeFromTop (3);
            }
        }

        if (inserts != nullptr)
        {
            r.removeFromTop (6);
            insertsHeaderArea = r.removeFromTop (16);
            inserts->setBounds (r.removeFromTop (inserts->getIdealHeight()));
        }

        if (sends != nullptr)
        {
            r.removeFromTop (6);
            sendsHeaderArea = r.removeFromTop (16);
            sends->setBounds (r.removeFromTop (sends->getIdealHeight()));
        }

        if (volume.isVisible())
        {
            r.removeFromTop (6);
            faderHeaderArea = r.removeFromTop (16);
            auto row = r.removeFromTop (26);
            pan.setBounds (row.removeFromRight (PanKnob::widthForHeight (row.getHeight())));
            volumeLabel.setBounds (row.removeFromRight (64));
            volume.setBounds (row);
            r.removeFromTop (4);
            meter->setBounds (r.removeFromTop (10));
        }
    }
}
