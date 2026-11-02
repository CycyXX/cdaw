#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "AnalyserFifo.h"
#include "ChorusEngine.h"
#include "DelayEngine.h"
#include "FilterEngine.h"
#include "PeakMeter.h"
#include "ReverbEngine.h"
#include "Svf.h"
#include "TempoSync.h"
#include "Wavetables.h"
#include <array>
#include <atomic>

namespace cdaw
{
    /** Stereo samples for a goniometer: written by the audio thread while `active`, read by the UI. */
    struct StereoScope
    {
        static constexpr int capacity = 1 << 13;

        void push (const float* left, const float* right, int n) noexcept
        {
            if (! active.load (std::memory_order_relaxed))
                return;

            int start1, size1, start2, size2;
            fifo.prepareToWrite (n, start1, size1, start2, size2);
            for (int i = 0; i < size1; ++i) { l[(size_t) (start1 + i)] = left[i]; r[(size_t) (start1 + i)] = right[i]; }
            for (int i = 0; i < size2; ++i) { l[(size_t) (start2 + i)] = left[size1 + i]; r[(size_t) (start2 + i)] = right[size1 + i]; }
            fifo.finishedWrite (size1 + size2);
        }

        /** Returns the number of samples read (at most `max`). */
        int pull (float* left, float* right, int max) noexcept
        {
            int start1, size1, start2, size2;
            fifo.prepareToRead (juce::jmin (max, fifo.getNumReady()), start1, size1, start2, size2);
            for (int i = 0; i < size1; ++i) { left[i] = l[(size_t) (start1 + i)]; right[i] = r[(size_t) (start1 + i)]; }
            for (int i = 0; i < size2; ++i) { left[size1 + i] = l[(size_t) (start2 + i)]; right[size1 + i] = r[(size_t) (start2 + i)]; }
            fifo.finishedRead (size1 + size2);
            return size1 + size2;
        }

        std::atomic<bool> active { false };

    private:
        juce::AbstractFifo fifo { capacity };
        std::vector<float> l = std::vector<float> ((size_t) capacity), r = std::vector<float> ((size_t) capacity);
    };

    //==============================================================================
    /** C-Synth's DSP, a polyphonic wavetable synth made for pads:
        - per voice two wavetable oscillators (a bank of 16 frames morphed by position, up to 7
          detuned unison copies spread across the stereo field), a sub oscillator, coloured noise
          and a slow random drift of pitch and cutoff
        - a stereo state-variable filter per voice (low / high / band pass or notch, 12 or 24
          dB/oct) with key tracking and its own envelope amount
        - an amp and a mod envelope (up to 20 s), two LFOs (per voice or free, synced or not, kept
          in step with the song), an 8-slot modulation matrix and 4 macros
        - poly (up to 16 voices), mono or legato with glide; pitch bend, mod wheel, aftertouch,
          sustain pedal
        - then chorus, delay and reverb (C-Chorus's, C-Delay's and C-Verb's engines).
        Random things (unison phases, drift, noise) are seeded per note, so a render repeats. */
    class SynthEngine
    {
    public:
        static constexpr int maxVoices = 16, maxUnison = 7, numOscillators = 2, numLfos = 2, numSlots = 8, numMacros = 4;
        static constexpr int controlInterval = 32;      // samples between modulation updates (interpolated)

        enum class Source { none = 0, lfo1, lfo2, modEnv, ampEnv, velocity, key, modWheel, aftertouch, random,
                            macro1, macro2, macro3, macro4, numSources };
        enum class Destination { none = 0, pitch, positionA, positionB, levelA, levelB, detune, spread, cutoff, resonance,
                                 noise, pan, amp, lfo1Rate, lfo2Rate, numDestinations };
        enum class VoiceMode { poly = 0, mono, legato };
        enum class Stage { idle = 0, attack, decay, sustain, release };

        static constexpr int numSources = (int) Source::numSources, numDestinations = (int) Destination::numDestinations;

        struct Oscillator
        {
            bool on = true;
            int bank = 0;
            float position = 0.5f;          // 0..1 across the bank
            int octave = 0, semitones = 0;
            float fine = 0.0f;              // cents
            float level = 0.8f, pan = 0.0f;
            int unison = 1;
            float detune = 0.3f;            // 0..1: the outer copies up to +-maxDetuneCents
            float spread = 0.7f;            // 0..1: how far apart the copies are panned
        };

        struct Envelope { float attack = 0.01f, decay = 0.3f, sustain = 0.8f, release = 0.5f; };   // s, s, 0..1, s

        struct Lfo
        {
            FilterEngine::Shape shape = FilterEngine::Shape::sine;
            float rate = 0.5f;
            bool sync = false;
            int division = 15;
            bool retrigger = false;         // restarts with each note (else one LFO for all)
            float fade = 0.0f;              // s to fade in after a note starts
        };

        struct Slot { Source source = Source::none; Destination destination = Destination::none; float amount = 0.0f; };

        struct Settings
        {
            Oscillator osc[numOscillators];
            float subLevel = 0.0f;
            int subOctave = 1;              // 1 or 2 octaves down
            bool subTriangle = false;
            float noiseLevel = 0.0f, noiseColour = 0.0f;     // colour -1 dark .. 0 white .. 1 airy
            float drift = 0.3f;

            FilterEngine::Type filterType = FilterEngine::Type::lowPass;
            bool filterSteep = false;
            float cutoff = 2500.0f, resonance = 0.15f, filterDrive = 0.0f, keyTrack = 0.3f, filterEnv = 0.2f;

            Envelope ampEnv, modEnv;
            float velocity = 0.4f;          // how much velocity sets the level

            Lfo lfo[numLfos];
            Slot slots[numSlots];
            float macros[numMacros] {};

            VoiceMode mode = VoiceMode::poly;
            int polyphony = 12;
            float glide = 0.0f, bendRange = 2.0f;

            bool chorusOn = true, delayOn = false, reverbOn = true;
            float chorusDepth = 0.35f, chorusMix = 0.3f;
            int delayDivision = 8;
            float delayFeedback = 0.35f, delayMix = 0.2f;
            float reverbSize = 0.7f, reverbDecay = 4.5f, reverbMix = 0.35f;
            float volumeDb = -6.0f;
        };

        static constexpr float maxDetuneCents = 30.0f, filterEnvOctaves = 5.0f, cutoffModOctaves = 5.0f, pitchModSemitones = 12.0f;

        SynthEngine();

        void prepare (double sampleRate, int maxBlockSize);
        void reset();

        /** Renders `n` samples from `start` into `buffer` (replacing what's there). `midi` holds
            events at sample positions from `start`. */
        void process (juce::AudioBuffer<float>& buffer, int start, int n, const juce::MidiBuffer& midi, const Settings&,
                      double bpm, const TempoSync::SongPosition& = {});

        void allNotesOff();         // releases every note
        void panic();               // silences everything now

        double getSampleRate() const        { return rate; }

        //==============================================================================
        /** The envelope's level `t` seconds after a note starts, released after `heldFor` seconds. */
        static float envelopeAt (const Envelope&, double t, double heldFor);

        /** The voice filter's settings in C-Filter's terms (for its response maths). */
        static FilterEngine::Settings filterSettings (const Settings&);

        /** A value from a mod source (-1..1 or 0..1), as the matrix uses it. */
        static bool isBipolar (Source);

        /** What the effects are set to, in their own engines' terms (for displays too). */
        static ChorusEngine::Settings chorusSettings (const Settings&);
        static DelayEngine::Settings delaySettings (const Settings&);
        static ReverbEngine::Settings reverbSettings (const Settings&);

        const ChorusEngine& getChorus() const   { return chorus; }

        //==============================================================================
        // For the editor: what each voice is doing, the LFOs, what each slot and destination is
        // adding right now (the newest voice), meters and the output for a spectrum and a goniometer.
        struct VoiceView
        {
            std::atomic<int> note { -1 }, stage { 0 };
            std::atomic<bool> held { false };
            std::atomic<float> amp { 0.0f }, mod { 0.0f }, cutoff { 1000.0f }, positionA { 0.0f }, positionB { 0.0f }, pan { 0.0f };
            std::atomic<float> seconds { 0.0f }, releasedAt { -1.0f };   // since the note started; when it was released
        };

        std::array<VoiceView, maxVoices> voiceViews;
        std::array<std::atomic<float>, numLfos> lfoValue {}, lfoPhase {};
        std::array<std::atomic<float>, numSlots> slotValue {};
        std::array<std::atomic<float>, numDestinations> destinationValue {};
        std::atomic<int> activeVoices { 0 };
        PeakMeter outputPeak[2];
        std::shared_ptr<AnalyserFifo> outputFifo = std::make_shared<AnalyserFifo>();
        StereoScope scope;

    private:
        /** An envelope's per-sample steps, worked out once per block. */
        struct EnvelopeCoefficients
        {
            float attackStep = 1.0f, decay = 0.0f, release = 0.0f, sustain = 1.0f, follow = 0.0f;
            static EnvelopeCoefficients from (const Envelope&, double sampleRate);
        };

        struct EnvelopeState
        {
            Stage stage = Stage::idle;
            float level = 0.0f;

            void start()        { stage = Stage::attack; }
            void release()      { if (stage != Stage::idle) stage = Stage::release; }
            float next (const EnvelopeCoefficients&) noexcept;
        };

        struct Voice
        {
            bool active = false, held = false, sustained = false;
            int note = -1;
            float velocity = 0.0f, random = 0.0f;
            juce::uint32 order = 0;                 // note-on count when it started
            double pitch = 60.0, targetPitch = 60.0;
            double seconds = 0.0, releasedAt = -1.0;
            EnvelopeState amp, mod;

            double phase[numOscillators][maxUnison] {};
            double increment[numOscillators][maxUnison] {};
            float gain[numOscillators][maxUnison][2] {};
            float position[numOscillators] {}, positionStep[numOscillators] {};
            int level[numOscillators] {};
            double subPhase = 0.0, subIncrement = 0.0;

            Svf filter[2][2];                       // [side][stage]
            double g = 0.1, gStep = 0.0, k1 = 1.4, k2 = 1.4;
            float cutoff = 1000.0f;
            float ampGain = 1.0f, ampGainStep = 0.0f, noiseGain = 0.0f, subGain = 0.0f, oscLevel[numOscillators] {};

            double lfoPhase[numLfos] {};
            float destinations[numDestinations] {};
            float sourceValues[numSources] {};

            float drift[2] {}, driftTarget[2] {};   // pitch (semitones), cutoff (octaves)
            int driftCountdown = 0;
            juce::uint32 noise = 1;
            float noiseState[2] {};
            bool primed = false;

            float nextRandom() noexcept             // -1..1, xorshift
            {
                noise ^= noise << 13; noise ^= noise >> 17; noise ^= noise << 5;
                return (float) (noise * (1.0 / 4294967296.0)) * 2.0f - 1.0f;
            }
        };

        void handleMidi (const juce::MidiMessage&, const Settings&);
        void noteOn (int note, float velocity, const Settings&);
        void noteOff (int note, const Settings&);
        void startVoice (Voice&, int note, float velocity, const Settings&, bool retriggerFromCurrent);
        void releaseVoice (Voice&);

        void updateModulation (Voice&, const Settings&, double bpm);
        void renderVoice (Voice&, const Settings&, float* left, float* right, int numSamples);
        void render (const Settings&, int from, int to, double bpm);
        void runEffects (const Settings&, int numSamples, double bpm, const TempoSync::SongPosition&);
        void publish (const Settings&);

        double globalLfoHz (const Settings&, int lfo, double bpm) const;
        static float lfoAt (const Lfo&, double phase, int lfoIndex);

        double rate = 48000.0;
        int blockSize = 512;
        int controlCountdown = 0;                   // samples until the next modulation update
        EnvelopeCoefficients ampCoefficients, modCoefficients;
        const Wavetables& tables;
        std::array<Voice, maxVoices> voices;
        juce::uint32 noteCounter = 0;
        double lastPitch = -1.0;
        std::vector<int> heldNotes;                 // mono / legato: the keys down, in order

        float bend = 0.0f, modWheel = 0.0f, aftertouch = 0.0f;
        bool sustainPedal = false;
        double globalLfoPhase[numLfos] {};
        TempoSync::LfoClock lfoClock;

        juce::AudioBuffer<float> mix;
        ChorusEngine chorus;
        DelayEngine delay;
        ReverbEngine reverb;
        bool effectsWereOn[3] { false, false, false };
        Smoothed volume;
    };
}
