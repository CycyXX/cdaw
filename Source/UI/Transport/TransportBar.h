#pragma once

#include "../../PCH.h"
#include "../../Model/EditViewState.h"

namespace cdaw
{
    /** Button that draws a vector icon instead of text. */
    class IconButton : public juce::Button
    {
    public:
        enum class Icon { home, play, stop, record, loop, punch, click, countIn, follow,
                          toolSelect, toolRange, toolSplit, toolDraw, toolErase, toolMute, snap };

        IconButton (Icon, juce::Colour onColour, const juce::String& tooltip);
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;

    private:
        Icon icon;
        juce::Colour onColour;
    };

    /** Draggable / editable numeric field (tempo). */
    class DragNumberField : public juce::Label
    {
    public:
        DragNumberField();
        std::function<void (double delta)> onDragDelta;
        std::function<void()> onDragStart, onDragEnd;

        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

    private:
        int lastY = 0;
    };

    /** Top bar: transport, position, tempo / signature, snap, tools. */
    class TransportBar : public juce::Component,
                         private juce::ChangeListener,
                         private juce::ValueTree::Listener,
                         private juce::Timer
    {
    public:
        TransportBar (te::Edit&, EditViewState&, juce::ApplicationCommandManager&);
        ~TransportBar() override;

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override  { refresh(); }
        void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { triggerRefresh = true; }
        void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override             { triggerRefresh = true; }
        void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override      { triggerRefresh = true; }
        void timerCallback() override;
        void refresh();
        void updatePositionText();

        te::Edit& edit;
        EditViewState& viewState;
        juce::ApplicationCommandManager& commands;

        IconButton homeButton    { IconButton::Icon::home,    juce::Colours::white, "Return to start (Home)" },
                   playButton    { IconButton::Icon::play,    juce::Colour (0xff3ccf6e), "Play / Stop (Space)" },
                   stopButton    { IconButton::Icon::stop,    juce::Colours::white, "Stop" },
                   recordButton  { IconButton::Icon::record,  juce::Colour (0xffe5383b), "Record (Numpad *)" },
                   loopButton    { IconButton::Icon::loop,    juce::Colour (0xff4fa3ff), "Loop / cycle (Numpad /)" },
                   punchButton   { IconButton::Icon::punch,   juce::Colour (0xffe5383b), "Punch in/out on the loop range" },
                   clickButton   { IconButton::Icon::click,   juce::Colour (0xfff2b134), "Metronome (C)" },
                   countInButton { IconButton::Icon::countIn, juce::Colour (0xfff2b134), "Count-in before recording" },
                   followButton  { IconButton::Icon::follow,  juce::Colour (0xff4fa3ff), "Follow playhead (F)" },
                   snapButton    { IconButton::Icon::snap,    juce::Colour (0xff4fa3ff), "Snap to grid (J)" };

        juce::OwnedArray<IconButton> toolButtons;
        juce::TextButton writeAutoButton { "W" };

        juce::Label barsBeatsLabel, timeLabel, sigLabel;
        DragNumberField tempoField;
        juce::ComboBox gridBox;
        bool triggerRefresh = false;
    };
}
