#include "LowerZone.h"
#include "Shared/LookAndFeel/Palette.h"
#include "Mixer/MixerView.h"
#include "PianoRoll/PianoRoll.h"
#include "Notes/NotesPage.h"
#include "../Model/DrumMapLibrary.h"

namespace cdaw
{
    LowerZone::LowerZone (EditViewState& vs) : viewState (vs)
    {
        setOpaque (true);   // paint() fills the whole component
        for (auto* b : { &editorTab, &mixerTab, &notesTab })
        {
            b->setClickingTogglesState (false);
            b->setColour (juce::TextButton::buttonOnColourId, Palette::accentDim);
            addAndMakeVisible (b);
        }

        editorTab.setTooltip ("MIDI editor (Ctrl+E)");
        mixerTab.setTooltip ("Mixer (F3)");
        notesTab.setTooltip ("Song notes and lyrics by section (F4)");
        editorTab.onClick = [this] { showPage (Page::editor); };
        mixerTab.onClick = [this] { showPage (Page::mixer); };
        notesTab.onClick = [this] { showPage (Page::notes); };
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
            mixer->onTrackMenu = [this] (te::Track& t) { if (onTrackMenu) onTrackMenu (t); };
            addChildComponent (*mixer);
        }

        if (page == Page::notes && notes == nullptr)
        {
            notes = std::make_unique<NotesPage> (viewState);
            addChildComponent (*notes);
        }

        updateTabs();
        resized();
        repaint();
    }

    void LowerZone::editClip (te::MidiClip& clip)
    {
        pianoRoll = std::make_unique<PianoRoll> (viewState, &clip);
        pianoRoll->onClipRemoved = [this]
        {
            // Not from inside its own timer callback.
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<LowerZone> (this)]
            {
                if (safe != nullptr)
                {
                    safe->pianoRoll.reset();
                    safe->repaint();
                }
            });
        };
        addChildComponent (*pianoRoll);
        showPage (Page::editor);
        pianoRoll->grabKeyboardFocus();
    }

    void LowerZone::editLyrics (te::ArrangerClip& section)
    {
        showPage (Page::notes);
        notes->focusSection (section);
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
        notesTab.setToggleState (page == Page::notes, juce::dontSendNotification);

        if (pianoRoll != nullptr)
            pianoRoll->setVisible (page == Page::editor);

        if (mixer != nullptr)
            mixer->setVisible (page == Page::mixer);

        if (notes != nullptr)
            notes->setVisible (page == Page::notes);
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
        tabs.removeFromLeft (4);
        notesTab.setBounds (tabs.removeFromLeft (80));
        closeButton.setBounds (tabs.removeFromRight (26));

        if (pianoRoll != nullptr)
            pianoRoll->setBounds (r);

        if (mixer != nullptr)
            mixer->setBounds (r);

        if (notes != nullptr)
            notes->setBounds (r);
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
