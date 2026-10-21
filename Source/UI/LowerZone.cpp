#include "LowerZone.h"
#include "LookAndFeel/Palette.h"
#include "Mixer/MixerView.h"
#include "PianoRoll/PianoRoll.h"
#include "../Model/DrumMapLibrary.h"

namespace cdaw
{
    LowerZone::LowerZone (EditViewState& vs) : viewState (vs)
    {
        for (auto* b : { &editorTab, &mixerTab })
        {
            b->setClickingTogglesState (false);
            b->setColour (juce::TextButton::buttonOnColourId, Palette::accentDim);
            addAndMakeVisible (b);
        }

        editorTab.setTooltip ("MIDI editor (Ctrl+E)");
        mixerTab.setTooltip ("Mixer (F3)");
        editorTab.onClick = [this] { showPage (Page::editor); };
        mixerTab.onClick = [this] { showPage (Page::mixer); };
        closeButton.setTooltip ("Close the lower panel");
        closeButton.onClick = [this] { if (onClose) onClose(); };
        addAndMakeVisible (closeButton);

        updateTabs();
    }

    LowerZone::~LowerZone() = default;

    void LowerZone::showPage (Page p)
    {
        page = p;

        if (page == Page::mixer && mixer == nullptr)
        {
            mixer = std::make_unique<MixerView> (viewState);
            addChildComponent (*mixer);
        }

        updateTabs();
        resized();
        repaint();
    }

    void LowerZone::editClip (te::MidiClip& clip)
    {
        pianoRoll = std::make_unique<PianoRoll> (viewState, &clip);
        addChildComponent (*pianoRoll);
        showPage (Page::editor);
        pianoRoll->grabKeyboardFocus();
    }

    void LowerZone::drumMapChanged (te::Track& t)
    {
        if (pianoRoll != nullptr && pianoRoll->getClip() != nullptr && pianoRoll->getClip()->getTrack() == &t)
            pianoRoll->setDrumMap (DrumMapLibrary::getForTrack (t));
    }

    void LowerZone::updateTabs()
    {
        editorTab.setToggleState (page == Page::editor, juce::dontSendNotification);
        mixerTab.setToggleState (page == Page::mixer, juce::dontSendNotification);

        if (pianoRoll != nullptr)
            pianoRoll->setVisible (page == Page::editor);

        if (mixer != nullptr)
            mixer->setVisible (page == Page::mixer);
    }

    void LowerZone::paint (juce::Graphics& g)
    {
        g.fillAll (Palette::panelBackground);
        g.setColour (Palette::panelOutline);
        g.fillRect (0, 0, getWidth(), 3);

        if (page == Page::editor && pianoRoll == nullptr)
        {
            g.setColour (Palette::textDim);
            g.setFont (juce::FontOptions (14.0f));
            g.drawText ("Double-click a MIDI clip to edit it. Use the Draw tool (4) in an instrument track to create one.",
                        getLocalBounds().withTrimmedTop (tabBarHeight), juce::Justification::centred);
        }
    }

    void LowerZone::resized()
    {
        auto r = getLocalBounds().withTrimmedTop (3);
        auto tabs = r.removeFromTop (tabBarHeight).reduced (4, 2);
        editorTab.setBounds (tabs.removeFromLeft (80));
        tabs.removeFromLeft (4);
        mixerTab.setBounds (tabs.removeFromLeft (80));
        closeButton.setBounds (tabs.removeFromRight (26));

        if (pianoRoll != nullptr)
            pianoRoll->setBounds (r);

        if (mixer != nullptr)
            mixer->setBounds (r);
    }

    // The 3-pixel strip at the top resizes the panel.
    void LowerZone::mouseMove (const juce::MouseEvent& e)
    {
        setMouseCursor (e.y < 6 ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
    }

    void LowerZone::mouseDown (const juce::MouseEvent& e)
    {
        dragStartHeight = e.y < 6 ? getHeight() : 0;
    }

    void LowerZone::mouseDrag (const juce::MouseEvent& e)
    {
        if (dragStartHeight > 0 && onResizeRequest)
            onResizeRequest (dragStartHeight - e.getDistanceFromDragStartY());
    }
}
