#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "SynthParameters.h"
#include "../UI/EffectWidgets.h"
#include "../UI/SpectrumAnalyser.h"
#include <deque>

namespace cdaw
{
    /** C-Synth's colours, so each part reads the same everywhere (knobs, curves, rings). */
    namespace SynthColours
    {
        inline const juce::Colour oscA { 0xff4fd6c8 }, oscB { 0xffd77cff }, layers { 0xfff2b134 }, filter { 0xff4fa3ff },
                                  ampEnv { 0xff3ccf6e }, modEnv { 0xffff8a4f }, lfo1 { 0xfff2d034 }, lfo2 { 0xffff6fae },
                                  macro { 0xffb39ddb }, effects { 0xff8bd34f };

        inline juce::Colour forOscillator (int o)   { return o == 0 ? oscA : oscB; }
        inline juce::Colour forLfo (int l)          { return l == 0 ? lfo1 : lfo2; }

        /** A cutoff (Hz) as a colour: closed filters deep blue, open ones warm. */
        juce::Colour forCutoff (float hz);

        /** A colour per modulation slot. */
        juce::Colour forSlot (int slot);
    }

    //==============================================================================
    /** Base of C-Synth's panels: a dark titled panel with knobs bound to parameters. */
    class SynthPanel : public juce::Component
    {
    public:
        SynthPanel (EffectModel&, SynthEngine&, const juce::String& title);

        void paint (juce::Graphics&) override;

        /** Picks up changes made elsewhere and redraws live parts (the view's timer calls it). */
        virtual void refresh();

        ParamKnob* knobFor (int id) const;

        static constexpr int titleHeight = 28, knobHeight = 72;

    protected:
        ParamKnob* addKnob (int id, const juce::String& label, juce::Colour, float skewMidPoint = 0.0f);
        void layoutRow (juce::Rectangle<int> row, std::initializer_list<ParamKnob*>);
        juce::Rectangle<int> titleRow() const       { return getLocalBounds().removeFromTop (titleHeight).reduced (8, 4); }
        juce::Rectangle<int> content() const        { return getLocalBounds().withTrimmedTop (titleHeight).reduced (8, 4); }
        SynthEngine::Settings settings() const;

        /** Draws a dark plot area. */
        static void drawPlot (juce::Graphics&, juce::Rectangle<float>);

        EffectModel& model;
        SynthEngine& engine;
        juce::String title;
        juce::OwnedArray<ParamKnob> knobs;
        std::vector<int> knobIds;
    };

    //==============================================================================
    /** An oscillator: its wavetable as a stack of frames in depth with the frame playing now lit
        (and where each voice's modulated position is), the harmonics of that frame, and knobs. */
    class OscillatorPanel : public SynthPanel
    {
    public:
        OscillatorPanel (EffectModel&, SynthEngine&, int index);

        void paint (juce::Graphics&) override;
        void resized() override;
        void refresh() override;

    private:
        void drawWavetable (juce::Graphics&, juce::Rectangle<float>, const SynthEngine::Settings&);
        void drawHarmonics (juce::Graphics&, juce::Rectangle<float>, const SynthEngine::Settings&);

        const int index;
        ParamToggle onButton;
        ChoiceButtons bankButtons;
        juce::Rectangle<int> display;
    };

    /** Sub oscillator, noise (with its colour as a spectrum) and drift (as a wandering line). */
    class LayersPanel : public SynthPanel
    {
    public:
        LayersPanel (EffectModel&, SynthEngine&);

        void paint (juce::Graphics&) override;
        void resized() override;
        void refresh() override;

    private:
        ChoiceButtons subShape;
        juce::Rectangle<int> display;
        double wander = 0.0;
    };

    /** The voice filter: its curve at the set cutoff, and each sounding voice's curve where its
        modulation has taken it, coloured by how open it is. */
    class FilterPanel : public SynthPanel
    {
    public:
        FilterPanel (EffectModel&, SynthEngine&);

        void paint (juce::Graphics&) override;
        void resized() override;
        void refresh() override;

    private:
        ChoiceButtons typeButtons, slopeButtons;
        juce::Rectangle<int> display;
        EffectUI::FrequencyAxis axis;
    };

    /** An envelope: its shape with handles to drag (attack, decay / sustain, release) and a dot
        for each sounding voice where it is on it. */
    class EnvelopePanel : public SynthPanel
    {
    public:
        EnvelopePanel (EffectModel&, SynthEngine&, bool isAmp);

        void paint (juce::Graphics&) override;
        void resized() override;
        void refresh() override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

    private:
        struct Layout { juce::Rectangle<float> plot; float attackEnd, decayEnd, sustainEnd, releaseEnd; };
        Layout layout() const;
        int handleAt (juce::Point<float>) const;
        juce::Point<float> handle (int which) const;
        int first() const                           { return isAmp ? SynthParameters::ampAttack : SynthParameters::modAttack; }

        const bool isAmp;
        juce::Colour colour;
        juce::Rectangle<int> display;
        int hover = -1, dragging = -1;
        juce::Point<float> dragStart;
        float startValues[4] {};
    };

    /** Both LFOs: shape, sync and retrigger, the shape over a cycle with where it is now. */
    class LfoPanel : public SynthPanel
    {
    public:
        LfoPanel (EffectModel&, SynthEngine&);

        void paint (juce::Graphics&) override;
        void resized() override;
        void refresh() override;

    private:
        void updateRateKnob (int lfo);

        juce::OwnedArray<ChoiceButtons> shapes;
        juce::OwnedArray<ParamToggle> syncs, retriggers;
        ParamKnob* rateKnobs[2] {};
        bool showingNote[2] { false, false }, ready[2] { false, false };
        juce::Rectangle<int> sections[2], displays[2];
    };

    //==============================================================================
    /** The modulation matrix: per slot a source, a destination, an amount and a live bar of what
        it's adding now (for the newest voice). */
    class MatrixPanel : public SynthPanel
    {
    public:
        MatrixPanel (EffectModel&, SynthEngine&);

        void paint (juce::Graphics&) override;
        void resized() override;
        void refresh() override;

    private:
        juce::OwnedArray<ParamCombo> sources, destinations;
        ParamKnob* amounts[SynthEngine::numSlots] {};
        juce::Rectangle<int> rows[SynthEngine::numSlots], bars[SynthEngine::numSlots];
    };

    /** What each slot has added over the last seconds, as scrolling lines. */
    class MovementPanel : public SynthPanel
    {
    public:
        MovementPanel (EffectModel&, SynthEngine&);

        void paint (juce::Graphics&) override;
        void refresh() override;

    private:
        static constexpr int length = 300;     // 5 s at 60 Hz
        std::vector<std::array<float, SynthEngine::numSlots>> history;
        int write = 0;
    };

    /** The four macros, their names (double-click to rename) and where each one goes. */
    class MacroPanel : public SynthPanel
    {
    public:
        MacroPanel (EffectModel&, SynthEngine&);

        void paint (juce::Graphics&) override;
        void resized() override;
        void refresh() override;

        static juce::String macroName (EffectModel&, int macro);

    private:
        juce::OwnedArray<juce::Label> names;
        juce::Rectangle<int> columns[SynthEngine::numMacros];
    };

    /** Voice mode, polyphony, glide and bend range. */
    class VoicePanel : public SynthPanel
    {
    public:
        VoicePanel (EffectModel&, SynthEngine&);

        void resized() override;
        void refresh() override;

    private:
        ChoiceButtons modeButtons;
    };

    /** One of the built-in effects, with a picture of what it's doing. */
    class EffectPanel : public SynthPanel
    {
    public:
        enum class Kind { chorus, delay, reverb };
        EffectPanel (EffectModel&, SynthEngine&, Kind);

        void paint (juce::Graphics&) override;
        void resized() override;
        void refresh() override;

    private:
        void drawChorus (juce::Graphics&, juce::Rectangle<float>, const SynthEngine::Settings&);
        void drawDelay (juce::Graphics&, juce::Rectangle<float>, const SynthEngine::Settings&);
        void drawReverb (juce::Graphics&, juce::Rectangle<float>, const SynthEngine::Settings&);

        const Kind kind;
        ParamToggle onButton;
        juce::Rectangle<int> display;
    };

    //==============================================================================
    /** The footer: a keyboard with a glowing orb over each sounding key, as bright as the
        voice is loud, coloured by how open its filter is, trailing as it fades. */
    class VoiceField : public juce::Component
    {
    public:
        VoiceField (SynthEngine&);

        void paint (juce::Graphics&) override;
        void refresh();

        static constexpr int lowestKey = 21, highestKey = 108;

    private:
        float keyX (int key, juce::Rectangle<float> keys) const;
        static bool isBlack (int key);

        SynthEngine& engine;
        struct Trail { int note = -1; std::deque<juce::Point<float>> points; };
        std::array<Trail, SynthEngine::maxVoices> trails;
    };

    /** A goniometer of the output (mid up, side across) and its correlation: how wide the pad is. */
    class StereoField : public juce::Component
    {
    public:
        StereoField (SynthEngine&);
        ~StereoField() override;

        void paint (juce::Graphics&) override;
        void refresh();
        void visibilityChanged() override;

        /** For tests and snapshots: adds samples as if they came from the engine. */
        void add (const float* left, const float* right, int n);

    private:
        SynthEngine& engine;
        static constexpr int length = 2048;
        std::vector<float> left = std::vector<float> ((size_t) length), right = std::vector<float> ((size_t) length);
        int write = 0;
        float scale = 1.0f, correlation = 1.0f;
    };

    /** The output's spectrum. */
    class OutputSpectrum : public juce::Component
    {
    public:
        OutputSpectrum (SynthEngine&);
        ~OutputSpectrum() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        void visibilityChanged() override;
        void refresh();

    private:
        SynthEngine& engine;
        SpectrumAnalyser analyser;
        std::vector<double> columnHz;
        EffectUI::FrequencyAxis axis;
    };
}
