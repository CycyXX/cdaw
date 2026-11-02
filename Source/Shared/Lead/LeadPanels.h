#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "LeadParameters.h"
#include "../UI/EffectWidgets.h"
#include "../UI/SpectrumAnalyser.h"
#include <deque>

/** C-Lead's panels (in their own namespace, beside C-Synth's panels of the same names). */
namespace cdaw::LeadUI
{
    /** C-Lead's colours, so each part reads the same everywhere (knobs, curves, rings). */
    namespace Colours
    {
        inline const juce::Colour oscA { 0xff4fd6c8 }, oscB { 0xffd77cff }, pitch { 0xffffc857 }, drive { 0xffff7a45 },
                                  filter { 0xff4fa3ff }, ampEnv { 0xff3ccf6e }, modEnv { 0xffff8a4f }, lfo1 { 0xfff2d034 },
                                  lfo2 { 0xffff6fae }, macro { 0xffb39ddb }, effects { 0xff8bd34f };

        inline juce::Colour forOscillator (int o)   { return o == 0 ? oscA : oscB; }
        inline juce::Colour forLfo (int l)          { return l == 0 ? lfo1 : lfo2; }

        /** A cutoff (Hz) as a colour: closed filters deep blue, open ones warm. */
        juce::Colour forCutoff (float hz);

        /** A colour per modulation slot. */
        juce::Colour forSlot (int slot);
    }

    /** A note number as a name ("A4"). */
    juce::String noteName (int note);

    //==============================================================================
    /** Base of C-Lead's panels: a dark titled panel with knobs bound to parameters. */
    class Panel : public juce::Component
    {
    public:
        Panel (EffectModel&, LeadEngine&, const juce::String& title);

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
        LeadEngine::Settings settings() const;

        /** Draws a dark plot area. */
        static void drawPlot (juce::Graphics&, juce::Rectangle<float>);

        /** Draws "OFF" over a display. */
        static void drawOff (juce::Graphics&, juce::Rectangle<float>);

        EffectModel& model;
        LeadEngine& engine;
        juce::String title;
        juce::OwnedArray<ParamKnob> knobs;
        std::vector<int> knobIds;
    };

    //==============================================================================
    /** The headline: the pitch actually playing, scrolling past a keyboard on its side, so
        glides, vibrato (and when it starts) and bends show as they happen. The keys held are
        faint lanes, the note the voice is heading for a dashed line. Under it: voice mode,
        glide and the vibrato. */
    class PitchPanel : public Panel
    {
    public:
        PitchPanel (EffectModel&, LeadEngine&);

        void paint (juce::Graphics&) override;
        void resized() override;
        void refresh() override;

    private:
        static constexpr int length = 240;      // 4 s at 60 Hz
        struct Frame { float pitch[LeadEngine::maxVoices]; float amp[LeadEngine::maxVoices]; int note[LeadEngine::maxVoices]; };

        ChoiceButtons modeButtons, glideModeButtons;
        std::vector<Frame> history;
        int write = 0;
        float low = 55.0f, high = 79.0f;        // the pitch range shown (eases towards what's playing)
        juce::Rectangle<int> display;
    };

    /** A phase-locked picture of the voice before the effects: two cycles held still, with the
        last few frames fading behind, so sync sweeps, pulse width and resonance show plainly. */
    class ScopePanel : public Panel
    {
    public:
        ScopePanel (EffectModel&, LeadEngine&);

        void paint (juce::Graphics&) override;
        void refresh() override;

    private:
        static constexpr int points = 256, ghosts = 4;
        std::array<std::array<float, points>, ghosts> frames {};
        int newest = 0, filled = 0;
        float scale = 1.0f, hz = 0.0f;
    };

    /** An oscillator: one cycle of its wave as it plays (for B: over a cycle of A, with sync, FM
        and ring as they shape it), its harmonics, and knobs. */
    class OscillatorPanel : public Panel
    {
    public:
        OscillatorPanel (EffectModel&, LeadEngine&, int index);

        void paint (juce::Graphics&) override;
        void resized() override;
        void refresh() override;

    private:
        void drawWave (juce::Graphics&, juce::Rectangle<float>, const LeadEngine::Settings&);
        void drawHarmonics (juce::Graphics&, juce::Rectangle<float>, const LeadEngine::Settings&);
        LeadEngine::Settings playing() const;      // the settings with the live modulation applied

        const int index;
        ParamToggle onButton;
        std::unique_ptr<ParamToggle> syncButton;
        ChoiceButtons waveButtons;
        juce::Rectangle<int> display;
    };

    /** Drive (its curve, with a sine through it) and the sub and noise mixed in before it. */
    class DrivePanel : public Panel
    {
    public:
        DrivePanel (EffectModel&, LeadEngine&);

        void paint (juce::Graphics&) override;
        void resized() override;
        void refresh() override;

    private:
        ChoiceButtons typeButtons, subShape;
        juce::Rectangle<int> display;
    };

    /** The filter: its curve at the set cutoff, and each sounding voice's curve where its
        envelope and modulation have taken it, coloured by how open it is. */
    class FilterPanel : public Panel
    {
    public:
        FilterPanel (EffectModel&, LeadEngine&);

        void paint (juce::Graphics&) override;
        void resized() override;
        void refresh() override;

    private:
        ChoiceButtons typeButtons;
        juce::Rectangle<int> display;
        EffectUI::FrequencyAxis axis;
    };

    /** An envelope: its shape with handles to drag (attack, decay / sustain, release) and a dot
        for each sounding voice where it is on it. */
    class EnvelopePanel : public Panel
    {
    public:
        EnvelopePanel (EffectModel&, LeadEngine&, bool isAmp);

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
        int first() const                           { return isAmp ? LeadParameters::ampAttack : LeadParameters::modAttack; }

        const bool isAmp;
        juce::Colour colour;
        juce::Rectangle<int> display;
        int hover = -1, dragging = -1;
        juce::Point<float> dragStart;
        float startValues[4] {};
    };

    //==============================================================================
    /** Both LFOs: shape, sync and retrigger, the shape over a cycle with where it is now. */
    class LfoPanel : public Panel
    {
    public:
        LfoPanel (EffectModel&, LeadEngine&);

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

    /** The modulation matrix: per slot a source, a destination, an amount and a live bar of what
        it's adding now (for the newest voice). */
    class MatrixPanel : public Panel
    {
    public:
        MatrixPanel (EffectModel&, LeadEngine&);

        void paint (juce::Graphics&) override;
        void resized() override;
        void refresh() override;

    private:
        juce::OwnedArray<ParamCombo> sources, destinations;
        ParamKnob* amounts[LeadEngine::numSlots] {};
        juce::Rectangle<int> rows[LeadEngine::numSlots], bars[LeadEngine::numSlots];
    };

    /** What each slot has added over the last seconds, as scrolling lines. */
    class MovementPanel : public Panel
    {
    public:
        MovementPanel (EffectModel&, LeadEngine&);

        void paint (juce::Graphics&) override;
        void refresh() override;

    private:
        static constexpr int length = 300;     // 5 s at 60 Hz
        std::vector<std::array<float, LeadEngine::numSlots>> history;
        int write = 0;
    };

    /** The four macros, their names (double-click to rename) and where each one goes. */
    class MacroPanel : public Panel
    {
    public:
        MacroPanel (EffectModel&, LeadEngine&);

        void paint (juce::Graphics&) override;
        void resized() override;
        void refresh() override;

        static juce::String macroName (EffectModel&, int macro);

    private:
        juce::OwnedArray<juce::Label> names;
        juce::Rectangle<int> columns[LeadEngine::numMacros];
    };

    /** Playing: note priority, polyphony, bend range, and how far the wheel and aftertouch deepen the vibrato. */
    class PlayingPanel : public Panel
    {
    public:
        PlayingPanel (EffectModel&, LeadEngine&);

        void resized() override;
        void refresh() override;

    private:
        ChoiceButtons priorityButtons;
    };

    /** One of the built-in effects, with a picture of what it's doing. */
    class EffectPanel : public Panel
    {
    public:
        enum class Kind { chorus, delay, reverb, dist };
        EffectPanel (EffectModel&, LeadEngine&, Kind);

        void paint (juce::Graphics&) override;
        void resized() override;
        void refresh() override;

    private:
        void drawChorus (juce::Graphics&, juce::Rectangle<float>, const LeadEngine::Settings&);
        void drawDelay (juce::Graphics&, juce::Rectangle<float>, const LeadEngine::Settings&);
        void drawReverb (juce::Graphics&, juce::Rectangle<float>, const LeadEngine::Settings&);
        void drawDist (juce::Graphics&, juce::Rectangle<float>, const LeadEngine::Settings&);

        const Kind kind;
        ParamToggle onButton;
        std::unique_ptr<ChoiceButtons> voicingButtons;      // dist only
        juce::Rectangle<int> display;
    };

    //==============================================================================
    /** The footer: a keyboard with a glowing orb over each sounding voice, placed at the pitch
        it plays (so it slides along the keys as it glides and shakes with the vibrato), as bright
        as it is loud, coloured by how open its filter is, trailing as it moves. */
    class VoiceField : public juce::Component
    {
    public:
        VoiceField (LeadEngine&);

        void paint (juce::Graphics&) override;
        void refresh();

        static constexpr int lowestKey = 21, highestKey = 108;

    private:
        float keyX (int key, juce::Rectangle<float> keys) const;
        float pitchX (float pitch, juce::Rectangle<float> keys) const;
        static bool isBlack (int key);

        LeadEngine& engine;
        struct Trail { int note = -1; std::deque<juce::Point<float>> points; };
        std::array<Trail, LeadEngine::maxVoices> trails;
    };

    /** A goniometer of the output (mid up, side across) and its correlation. */
    class StereoField : public juce::Component
    {
    public:
        StereoField (LeadEngine&);
        ~StereoField() override;

        void paint (juce::Graphics&) override;
        void refresh();
        void visibilityChanged() override;

        /** For tests and snapshots: adds samples as if they came from the engine. */
        void add (const float* left, const float* right, int n);

    private:
        LeadEngine& engine;
        static constexpr int length = 2048;
        std::vector<float> left = std::vector<float> ((size_t) length), right = std::vector<float> ((size_t) length);
        int write = 0;
        float scale = 1.0f, correlation = 1.0f;
    };

    /** The output's spectrum. */
    class OutputSpectrum : public juce::Component
    {
    public:
        OutputSpectrum (LeadEngine&);
        ~OutputSpectrum() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        void visibilityChanged() override;
        void refresh();

    private:
        LeadEngine& engine;
        SpectrumAnalyser analyser;
        std::vector<double> columnHz;
        EffectUI::FrequencyAxis axis;
    };
}
