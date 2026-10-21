#pragma once

#include "../../PCH.h"
#include "../../Model/EditViewState.h"
#include "../Common/PanKnob.h"

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
        void timerCallback() override           { if (isShowing() || track == nullptr) refreshState(); }   // idle while hidden
        void visibilityChanged() override       { if (isShowing()) refreshState(); }
        void refreshState();
        void setTrack (te::Track*);
        te::Track* findSelectedTrack() const;

        EditViewState& viewState;
        te::Track::Ptr track;

        juce::Label nameLabel;
        juce::TextButton inputButton, outputButton, drumMapButton;
        std::unique_ptr<InsertSlots> inserts;
        std::unique_ptr<SendSlots> sends;
        juce::Slider volume;
        PanKnob pan;
        std::unique_ptr<LevelMeter> meter;
        juce::Label volumeLabel;
        juce::Rectangle<int> insertsHeaderArea, sendsHeaderArea, routingHeaderArea, faderHeaderArea;
        bool updating = false;
        juce::var shownDrumMapSetting { "<unset>" };   // drum map lookups read files: only redo them when this changes
    };
}
