#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "CVerbParameters.h"
#include "../UI/ParamKnob.h"

namespace cdaw
{
    /** What the C-Verb view needs from whoever hosts it: CDAW's built-in plugin or the VST3. */
    struct CVerbModel
    {
        virtual ~CVerbModel() = default;

        virtual std::shared_ptr<KnobParameter> parameter (CVerbParameters::ID) = 0;
        virtual ReverbEngine& getEngine() = 0;

        /** Groups the following changes into one undo step (where the host has undo). */
        virtual void beginUndoTransaction (const juce::String&) {}

        float value (CVerbParameters::ID id)        { return parameter (id)->getValue(); }
        ReverbEngine::Settings getSettings()        { return CVerbParameters::toSettings ([this] (CVerbParameters::ID id) { return value (id); }); }
    };

    /** C-Verb's editor, shared by both builds:
        - one large view with frequency along the bottom: the decay map rises from the bottom
          (how long each frequency rings, from pre-delay, early reflections, decay per band and
          the cuts) with the RT60 line across it; the pre and post cuts drawn over it on their own
          dB scale with draggable handles; the spectrum of the reverb's output faintly over it.
          Drag up / down to set the decay, drag a handle sideways to move a cut.
        - tail scope: the dry signal and the reverb coming out, live
        - knobs grouped by Space, Tone, Filters and Output, plus Freeze. */
    class CVerbView : public juce::Component,
                      private juce::Timer
    {
    public:
        explicit CVerbView (CVerbModel&);
        ~CVerbView() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        void visibilityChanged() override;

        /** One frame of meter values (gain): dry input and reverb output. */
        void tick (float dry, float wet);

        /** Reads new audio into the spectrum (the timer does this while showing). */
        void updateSpectrum();

        static constexpr int defaultWidth = 1140, defaultHeight = 680, minimumWidth = 980, minimumHeight = 600;

        class SpaceView;
        class TailScope;

    private:
        void timerCallback() override;

        CVerbModel& model;
        std::unique_ptr<SpaceView> spaceView;
        std::unique_ptr<TailScope> tailScope;
        juce::OwnedArray<ParamKnob> knobs;
        juce::TextButton freezeButton { "Freeze" };

        struct Group { const char* title; int first, count; juce::Rectangle<int> area; };
        std::vector<Group> groups;
        int emptyTicks = 0;
        unsigned frame = 0;
    };
}
