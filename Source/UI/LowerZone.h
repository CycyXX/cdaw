#pragma once

#include "../PCH.h"
#include "../Model/EditViewState.h"

namespace cdaw
{
    class PianoRoll;
    class MixerView;

    /** Bottom panel with two pages: the MIDI editor and the mixer. Resizable from its top edge. */
    class LowerZone : public juce::Component
    {
    public:
        enum class Page { editor, mixer };

        explicit LowerZone (EditViewState&);
        ~LowerZone() override;

        void showPage (Page);
        Page getPage() const                    { return page; }
        void editClip (te::MidiClip&);
        void drumMapChanged (te::Track&);
        PianoRoll* getPianoRoll() const         { return pianoRoll.get(); }

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;

        std::function<void (int newHeight)> onResizeRequest;
        std::function<void()> onClose;

        static constexpr int tabBarHeight = 26;

    private:
        EditViewState& viewState;
        Page page = Page::editor;
        std::unique_ptr<PianoRoll> pianoRoll;
        std::unique_ptr<MixerView> mixer;
        juce::TextButton editorTab { "Editor" }, mixerTab { "Mixer" }, closeButton { "x" };
        int dragStartHeight = 0;

        void updateTabs();
    };
}
