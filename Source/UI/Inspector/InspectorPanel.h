#pragma once

#include "../../PCH.h"
#include "../../Model/EditViewState.h"

namespace cdaw
{
    class InsertSlots;
    class SendSlots;
    class LevelMeter;

    /** Left panel showing the selected track: routing, inserts, sends, fader. */
    class InspectorPanel : public juce::Component,
                           private juce::ChangeListener,
                           private juce::Timer
    {
    public:
        explicit InspectorPanel (EditViewState&);
        ~InspectorPanel() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        /** Called when the drum map choice changes (instrument tracks). */
        std::function<void (te::Track&)> onDrumMapChanged;

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void timerCallback() override;
        void setTrack (te::Track*);
        te::Track* findSelectedTrack() const;

        EditViewState& viewState;
        te::Track::Ptr track;

        juce::Label nameLabel;
        juce::TextButton inputButton, outputButton, drumMapButton;
        std::unique_ptr<InsertSlots> inserts;
        std::unique_ptr<SendSlots> sends;
        juce::Slider volume, pan;
        std::unique_ptr<LevelMeter> meter;
        juce::Label volumeLabel;
        juce::Rectangle<int> insertsHeaderArea, sendsHeaderArea, routingHeaderArea, faderHeaderArea;
        bool updating = false;
    };
}
