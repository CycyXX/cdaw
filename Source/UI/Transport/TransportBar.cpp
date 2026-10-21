#include "TransportBar.h"
#include "../LookAndFeel/Palette.h"
#include "../../Commands/CommandIDs.h"

namespace cdaw
{
    //==============================================================================
    IconButton::IconButton (Icon i, juce::Colour on, const juce::String& tip)
        : juce::Button (tip), icon (i), onColour (on)
    {
        setTooltip (tip);
        setWantsKeyboardFocus (false);
    }

    void IconButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        const bool on = getToggleState();

        g.setColour (on ? onColour.withAlpha (0.22f) : (highlighted ? Palette::panelRaised.brighter (0.1f) : Palette::panelRaised));
        if (down)
            g.setColour (Palette::panelRaised.brighter (0.25f));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (on ? onColour : Palette::panelOutline);
        g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);

        auto c = on ? onColour : Palette::text.withAlpha (isEnabled() ? 0.85f : 0.35f);
        g.setColour (c);

        auto s = juce::jmin (r.getWidth(), r.getHeight()) * 0.5f;
        auto ic = juce::Rectangle<float> (s, s).withCentre (r.getCentre());
        juce::Path p;
        const float stroke = 1.6f;

        switch (icon)
        {
            case Icon::home:
                p.addRectangle (ic.getX(), ic.getY(), ic.getWidth() * 0.15f, ic.getHeight());
                p.addTriangle (ic.getRight(), ic.getY(), ic.getRight(), ic.getBottom(), ic.getX() + ic.getWidth() * 0.2f, ic.getCentreY());
                g.fillPath (p);
                break;
            case Icon::play:
                p.addTriangle (ic.getX() + 1, ic.getY(), ic.getX() + 1, ic.getBottom(), ic.getRight(), ic.getCentreY());
                g.fillPath (p);
                break;
            case Icon::stop:
                g.fillRect (ic.reduced (1.0f));
                break;
            case Icon::record:
                g.setColour (on ? onColour : juce::Colour (0xffe5383b).withAlpha (0.85f));
                g.fillEllipse (ic);
                break;
            case Icon::loop:
                p.addCentredArc (ic.getCentreX(), ic.getCentreY(), ic.getWidth() * 0.5f, ic.getHeight() * 0.38f, 0.0f, 0.4f, juce::MathConstants<float>::twoPi - 0.1f, true);
                g.strokePath (p, juce::PathStrokeType (stroke));
                p.clear();
                p.addTriangle (ic.getCentreX() + 1, ic.getY() + ic.getHeight() * 0.02f, ic.getCentreX() + 6, ic.getY() + ic.getHeight() * 0.12f, ic.getCentreX() + 1, ic.getY() + ic.getHeight() * 0.3f);
                g.fillPath (p);
                break;
            case Icon::punch:
                g.drawVerticalLine ((int) ic.getX() + 1, ic.getY(), ic.getBottom());
                g.drawVerticalLine ((int) ic.getRight() - 1, ic.getY(), ic.getBottom());
                g.fillEllipse (ic.reduced (ic.getWidth() * 0.3f));
                break;
            case Icon::click:
                p.addTriangle (ic.getCentreX(), ic.getY(), ic.getX() + 1, ic.getBottom(), ic.getRight() - 1, ic.getBottom());
                g.strokePath (p, juce::PathStrokeType (stroke));
                g.drawLine (ic.getCentreX(), ic.getBottom() - 2, ic.getRight(), ic.getY() + 2, stroke);
                break;
            case Icon::countIn:
                g.setFont (juce::FontOptions (ic.getHeight() * 0.9f, juce::Font::bold));
                g.drawText ("1-2", r, juce::Justification::centred);
                break;
            case Icon::follow:
                g.drawLine (ic.getX(), ic.getCentreY(), ic.getRight(), ic.getCentreY(), stroke);
                p.addTriangle (ic.getRight(), ic.getCentreY(), ic.getRight() - 6, ic.getCentreY() - 4, ic.getRight() - 6, ic.getCentreY() + 4);
                g.fillPath (p);
                g.drawVerticalLine ((int) ic.getX(), ic.getY(), ic.getBottom());
                break;
            case Icon::snap:
                g.setFont (juce::FontOptions (ic.getHeight() * 0.85f, juce::Font::bold));
                g.drawText ("Snap", r, juce::Justification::centred);
                break;
            case Icon::toolSelect:
                p.startNewSubPath (ic.getX() + 2, ic.getY());
                p.lineTo (ic.getX() + 2, ic.getBottom());
                p.lineTo (ic.getX() + ic.getWidth() * 0.4f, ic.getBottom() - ic.getHeight() * 0.3f);
                p.lineTo (ic.getRight(), ic.getBottom() - ic.getHeight() * 0.3f);
                p.closeSubPath();
                g.fillPath (p);
                break;
            case Icon::toolRange:
                g.drawRect (ic.reduced (0.5f), 1.0f);
                g.fillRect (ic.reduced (ic.getWidth() * 0.25f, ic.getHeight() * 0.3f));
                break;
            case Icon::toolSplit:
                g.drawVerticalLine ((int) ic.getCentreX(), ic.getY() - 2, ic.getBottom() + 2);
                g.drawRect (ic.withWidth (ic.getWidth() * 0.4f).reduced (0, ic.getHeight() * 0.2f));
                g.drawRect (ic.withTrimmedLeft (ic.getWidth() * 0.6f).reduced (0, ic.getHeight() * 0.2f));
                break;
            case Icon::toolDraw:
                g.drawLine (ic.getX(), ic.getBottom(), ic.getRight(), ic.getY(), 2.5f);
                break;
            case Icon::toolErase:
                g.drawLine (ic.getX(), ic.getY(), ic.getRight(), ic.getBottom(), 2.0f);
                g.drawLine (ic.getX(), ic.getBottom(), ic.getRight(), ic.getY(), 2.0f);
                break;
            case Icon::toolMute:
                g.drawEllipse (ic.reduced (1.0f), stroke);
                g.drawLine (ic.getX() + 2, ic.getBottom() - 2, ic.getRight() - 2, ic.getY() + 2, stroke);
                break;
        }
    }

    //==============================================================================
    DragNumberField::DragNumberField()
    {
        setEditable (false, true, false);
        setJustificationType (juce::Justification::centred);
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    }

    void DragNumberField::mouseDown (const juce::MouseEvent& e)
    {
        lastY = e.y;
        if (onDragStart) onDragStart();
        Label::mouseDown (e);
    }

    void DragNumberField::mouseDrag (const juce::MouseEvent& e)
    {
        auto dy = lastY - e.y;
        lastY = e.y;

        if (dy != 0 && onDragDelta)
            onDragDelta (dy * (e.mods.isShiftDown() ? 0.01 : 0.25));
    }

    void DragNumberField::mouseUp (const juce::MouseEvent& e)
    {
        if (onDragEnd) onDragEnd();
        Label::mouseUp (e);
    }

    //==============================================================================
    TransportBar::TransportBar (te::Edit& e, EditViewState& vs, juce::ApplicationCommandManager& cm)
        : edit (e), viewState (vs), commands (cm)
    {
        auto bindCommand = [this] (juce::Button& b, juce::CommandID id)
        {
            b.onClick = [this, id] { commands.invokeDirectly (id, true); };
            addAndMakeVisible (b);
        };

        bindCommand (homeButton, CommandIDs::returnToStart);
        bindCommand (playButton, CommandIDs::playStop);
        bindCommand (stopButton, CommandIDs::stop);
        bindCommand (recordButton, CommandIDs::record);
        bindCommand (loopButton, CommandIDs::toggleLoop);
        bindCommand (punchButton, CommandIDs::togglePunch);
        bindCommand (clickButton, CommandIDs::toggleClick);
        bindCommand (countInButton, CommandIDs::toggleCountIn);
        bindCommand (followButton, CommandIDs::toggleFollowPlayhead);
        bindCommand (snapButton, CommandIDs::toggleSnap);

        const std::pair<IconButton::Icon, const char*> tools[] = {
            { IconButton::Icon::toolSelect, "Select / move tool (1)" },
            { IconButton::Icon::toolRange,  "Range tool (2)" },
            { IconButton::Icon::toolSplit,  "Split tool (3)" },
            { IconButton::Icon::toolDraw,   "Draw tool: draw MIDI clips and notes (4)" },
            { IconButton::Icon::toolErase,  "Erase tool (5)" },
            { IconButton::Icon::toolMute,   "Mute tool (6)" },
        };

        for (int i = 0; i < (int) std::size (tools); ++i)
        {
            auto* b = toolButtons.add (new IconButton (tools[i].first, Palette::accent, tools[i].second));
            b->onClick = [this, i] { viewState.tool = i; refresh(); };
            addAndMakeVisible (b);
        }

        for (auto* l : { &barsBeatsLabel, &timeLabel })
        {
            l->setJustificationType (juce::Justification::centred);
            l->setColour (juce::Label::backgroundColourId, Palette::laneBackground);
            l->setColour (juce::Label::outlineColourId, Palette::panelOutline);
            addAndMakeVisible (*l);
        }

        barsBeatsLabel.setFont (juce::FontOptions (20.0f, juce::Font::bold));
        timeLabel.setFont (juce::FontOptions (14.0f));
        timeLabel.setColour (juce::Label::textColourId, Palette::textDim);

        tempoField.setFont (juce::FontOptions (16.0f, juce::Font::bold));
        tempoField.setColour (juce::Label::backgroundColourId, Palette::laneBackground);
        tempoField.setColour (juce::Label::outlineColourId, Palette::panelOutline);
        tempoField.setTooltip ("Tempo (BPM) at the playhead. Drag up/down or double-click to type.");
        tempoField.onDragStart = [this] { edit.getUndoManager().beginNewTransaction ("Change tempo"); };
        tempoField.onDragDelta = [this] (double delta)
        {
            auto& tempo = edit.tempoSequence.getTempoAt (edit.getTransport().getPosition());
            tempo.setBpm (juce::jlimit (20.0, 400.0, tempo.getBpm() + delta));
        };
        tempoField.onTextChange = [this]
        {
            auto bpm = tempoField.getText().getDoubleValue();
            if (bpm >= 20.0 && bpm <= 400.0)
            {
                edit.getUndoManager().beginNewTransaction ("Change tempo");
                edit.tempoSequence.getTempoAt (edit.getTransport().getPosition()).setBpm (bpm);
            }
            refresh();
        };
        addAndMakeVisible (tempoField);

        sigLabel.setEditable (false, true, false);
        sigLabel.setJustificationType (juce::Justification::centred);
        sigLabel.setFont (juce::FontOptions (16.0f, juce::Font::bold));
        sigLabel.setColour (juce::Label::backgroundColourId, Palette::laneBackground);
        sigLabel.setColour (juce::Label::outlineColourId, Palette::panelOutline);
        sigLabel.setTooltip ("Time signature at the playhead. Double-click to type, e.g. 7/8.");
        sigLabel.onTextChange = [this]
        {
            auto text = sigLabel.getText().trim();
            if (text.containsChar ('/'))
            {
                edit.getUndoManager().beginNewTransaction ("Change time signature");
                edit.tempoSequence.getTimeSigAt (edit.getTransport().getPosition()).setStringTimeSig (text);
            }
            refresh();
        };
        addAndMakeVisible (sigLabel);

        writeAutoButton.setTooltip ("Write automation: tracks in Touch / Latch / Write mode record parameter moves while playing");
        writeAutoButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffffb84f).withAlpha (0.6f));
        writeAutoButton.onClick = [this]
        {
            auto& arm = edit.getAutomationRecordManager();
            arm.setWritingAutomation (! arm.isWritingAutomation());
            refresh();
        };
        addAndMakeVisible (writeAutoButton);

        int id = 1;
        for (auto& gd : EditViewState::getGridDivisions())
            gridBox.addItem (gd.name, id++);
        gridBox.setTooltip ("Grid / snap resolution");
        gridBox.onChange = [this] { viewState.gridIndex = gridBox.getSelectedId() - 1; };
        addAndMakeVisible (gridBox);

        edit.getTransport().addChangeListener (this);
        edit.state.addListener (this);
        refresh();
        startTimerHz (20);
    }

    TransportBar::~TransportBar()
    {
        edit.state.removeListener (this);
        edit.getTransport().removeChangeListener (this);
    }

    void TransportBar::timerCallback()
    {
        updatePositionText();

        if (triggerRefresh)
        {
            triggerRefresh = false;
            refresh();
        }
    }

    void TransportBar::updatePositionText()
    {
        auto& transport = edit.getTransport();
        auto pos = transport.getPosition();

        if (auto epc = transport.getCurrentPlaybackContext(); epc != nullptr && transport.isPlaying())
            pos = epc->getAudibleTimelineTime();

        auto bb = edit.tempoSequence.toBarsAndBeats (pos);
        auto beats = bb.beats.inBeats();
        auto ticks = juce::roundToInt ((beats - std::floor (beats)) * 960.0) % 960;
        auto text = juce::String (bb.bars + 1) + "." + juce::String ((int) std::floor (beats) + 1) + "."
                    + juce::String (ticks).paddedLeft ('0', 3);

        if (barsBeatsLabel.getText() != text)
            barsBeatsLabel.setText (text, juce::dontSendNotification);

        auto secs = pos.inSeconds();
        auto mins = (int) (secs / 60.0);
        auto timeText = juce::String (mins) + ":" + juce::String (secs - mins * 60.0, 3).paddedLeft ('0', 6);

        if (timeLabel.getText() != timeText)
            timeLabel.setText (timeText, juce::dontSendNotification);

        // Tempo / signature can change while playing through a tempo map.
        if (! tempoField.isBeingEdited())
        {
            auto bpmText = juce::String (edit.tempoSequence.getTempoAt (pos).getBpm(), 2);
            if (tempoField.getText() != bpmText)
                tempoField.setText (bpmText, juce::dontSendNotification);
        }

        if (! sigLabel.isBeingEdited())
        {
            auto sigText = edit.tempoSequence.getTimeSigAt (pos).getStringTimeSig();
            if (sigLabel.getText() != sigText)
                sigLabel.setText (sigText, juce::dontSendNotification);
        }
    }

    void TransportBar::refresh()
    {
        auto& transport = edit.getTransport();
        playButton.setToggleState (transport.isPlaying(), juce::dontSendNotification);
        recordButton.setToggleState (transport.isRecording(), juce::dontSendNotification);
        loopButton.setToggleState (transport.looping.get(), juce::dontSendNotification);
        punchButton.setToggleState (edit.recordingPunchInOut.get(), juce::dontSendNotification);
        clickButton.setToggleState (edit.clickTrackEnabled.get(), juce::dontSendNotification);
        countInButton.setToggleState (edit.getCountInMode() != te::Edit::CountIn::none, juce::dontSendNotification);
        followButton.setToggleState (viewState.followPlayhead.get(), juce::dontSendNotification);
        snapButton.setToggleState (viewState.snapEnabled.get(), juce::dontSendNotification);
        writeAutoButton.setToggleState (edit.getAutomationRecordManager().isWritingAutomation(), juce::dontSendNotification);
        gridBox.setSelectedId (viewState.gridIndex.get() + 1, juce::dontSendNotification);

        for (int i = 0; i < toolButtons.size(); ++i)
            toolButtons[i]->setToggleState (viewState.tool.get() == i, juce::dontSendNotification);

        updatePositionText();
    }

    void TransportBar::paint (juce::Graphics& g)
    {
        g.fillAll (Palette::panelBackground);
        g.setColour (Palette::panelOutline);
        g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());

        g.setColour (Palette::textDim);
        g.setFont (juce::FontOptions (10.0f));
        g.drawText ("BPM", tempoField.getBounds().translated (0, -12).withHeight (12), juce::Justification::centred);
        g.drawText ("SIGNATURE", sigLabel.getBounds().translated (0, -12).withHeight (12), juce::Justification::centred);
        g.drawText ("GRID", gridBox.getBounds().translated (0, -12).withHeight (12), juce::Justification::centred);
    }

    void TransportBar::resized()
    {
        auto r = getLocalBounds().reduced (8, 6);
        auto buttonRow = r.withTrimmedTop (r.getHeight() - 34);
        auto place = [&buttonRow] (juce::Component& c, int w, int gap = 4)
        {
            c.setBounds (buttonRow.removeFromLeft (w));
            buttonRow.removeFromLeft (gap);
        };

        place (homeButton, 34);
        place (playButton, 40);
        place (stopButton, 34);
        place (recordButton, 34, 14);
        place (loopButton, 34);
        place (punchButton, 34);
        place (clickButton, 34);
        place (countInButton, 40, 14);

        place (barsBeatsLabel, 130);
        place (timeLabel, 100, 14);

        auto withCaption = [&] (juce::Component& c, int w, int gap = 8)
        {
            c.setBounds (buttonRow.removeFromLeft (w).withTrimmedTop (12));
            buttonRow.removeFromLeft (gap);
        };

        withCaption (tempoField, 76);
        withCaption (sigLabel, 66, 18);
        place (snapButton, 52);
        withCaption (gridBox, 76, 18);

        for (auto* b : toolButtons)
            place (*b, 32, 2);

        buttonRow.removeFromLeft (14);
        place (followButton, 34);
        place (writeAutoButton, 34);
    }
}
