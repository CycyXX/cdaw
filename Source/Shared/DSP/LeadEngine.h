#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "AnalyserFifo.h"
#include "ChorusEngine.h"
#include "DelayEngine.h"
#include "DistEngine.h"
#include "FilterEngine.h"
#include "PeakMeter.h"
#include "ReverbEngine.h"
#include "Svf.h"
#include "TempoSync.h"
#include <array>
#include <atomic>

namespace cdaw
{
    /** C-Lead's DSP, a virtual analog synth made for leads:
        - mono (default), legato or poly (up to 8 voices); note priority last / high / low with a
          note stack (releasing a key falls back to one still held); glide in a set time, always
          or only when playing legato
        - per voice two oscillators (saw, pulse with width, triangle, sine; up to 7 unison
          copies for supersaws), oscillator B optionally hard-synced to A, frequency-modulated by
          A, ring-modulated with it; a sub and noise. Rendered at twice the rate with band-limited
          (PolyBLEP) edges, including the sync resets, then filtered back down, so bright and
          synced sounds stay clean.
        - drive (soft, tube or folding) into a filter: a 24 dB ladder that can self-oscillate, or
          12 dB low / band / high pass
        - amp and filter envelopes, a vibrato with delay and fade (deeper with the mod wheel or
          aftertouch), two LFOs, an 8-slot modulation matrix and 4 macros
        - then a distortion (C-Dist's engine, four voicings; it adds C-Dist's 41 samples of
          latency while on), chorus, delay and reverb (C-Chorus's, C-Delay's and C-Verb's engines).
        Random things (unison phases, noise) are seeded per note, so a render repeats. */
    class LeadEngine
    {
    public:
        static constexpr int maxVoices = 8, maxUnison = 7, numOscillators = 2, numLfos = 2, numSlots = 8, numMacros = 4;
        static constexpr int controlInterval = 32;      // samples (at the output rate) between modulation updates

        enum class Wave { saw = 0, pulse, triangle, sine, numWaves };
        enum class DriveType { soft = 0, tube, fold };
        enum class FilterType { ladder = 0, lowPass, bandPass, highPass };
        enum class VoiceMode { mono = 0, legato, poly };
        enum class Priority { last = 0, high, low };
        enum class GlideMode { always = 0, legatoOnly };
        enum class Stage { idle = 0, attack, decay, sustain, release };

        enum class Source { none = 0, lfo1, lfo2, modEnv, ampEnv, velocity, key, modWheel, aftertouch, random,
                            macro1, macro2, macro3, macro4, numSources };
        enum class Destination { none = 0, pitch, pulseWidth, syncPitch, fm, ring, levelA, levelB, cutoff, resonance,
                                 drive, pan, amp, vibrato, lfo1Rate, lfo2Rate, numDestinations };
        static constexpr int numSources = (int) Source::numSources, numDestinations = (int) Destination::numDestinations;

        struct Oscillator
        {
            bool on = true;
            Wave wave = Wave::saw;
            int octave = 0, semitones = 0;
            float fine = 0.0f;              // cents
            float level = 0.8f;
            float pulseWidth = 0.5f;        // 0.05..0.95
            float pan = 0.0f;
            int unison = 1;
            float detune = 0.2f, spread = 0.5f;
        };

        struct Envelope { float attack = 0.01f, decay = 0.3f, sustain = 0.8f, release = 0.3f; };   // s, s, 0..1, s

        struct Lfo
        {
            FilterEngine::Shape shape = FilterEngine::Shape::sine;
            float rate = 0.5f;
            bool sync = false;
            int division = 15;
            bool retrigger = false;         // restarts with each note (else one LFO for all)
            float fade = 0.0f;              // s to fade in after a note starts
        };

        /** Stereo samples for a goniometer: written by the audio thread while `active`. */
        struct StereoTap
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

        struct Slot { Source source = Source::none; Destination destination = Destination::none; float amount = 0.0f; };

        struct Settings
        {
            Oscillator osc[numOscillators];
            bool sync = false;              // B restarts with each cycle of A
            float fm = 0.0f, ring = 0.0f;   // 0..1
            float subLevel = 0.0f;
            int subOctave = 1;
            bool subSquare = false;
            float noiseLevel = 0.0f;

            DriveType driveType = DriveType::soft;
            float drive = 0.0f;             // 0..1

            FilterType filterType = FilterType::ladder;
            float cutoff = 2500.0f, resonance = 0.25f, keyTrack = 0.5f, filterEnv = 0.35f, filterVelocity = 0.2f;

            Envelope ampEnv { 0.005f, 0.4f, 0.8f, 0.25f }, modEnv { 0.002f, 0.5f, 0.3f, 0.3f };
            float velocity = 0.5f;

            float vibratoRate = 5.5f, vibratoDepth = 15.0f, vibratoDelay = 0.35f, vibratoFade = 0.4f;     // Hz, cents, s, s
            float vibratoWheel = 50.0f, vibratoAftertouch = 30.0f;                                           // cents at full

            Lfo lfo[numLfos];
            Slot slots[numSlots];
            float macros[numMacros] {};

            VoiceMode mode = VoiceMode::mono;
            Priority priority = Priority::last;
            int polyphony = 6;
            float glide = 0.06f;
            GlideMode glideMode = GlideMode::legatoOnly;
            float bendUp = 2.0f, bendDown = 2.0f;

            bool chorusOn = true, delayOn = true, reverbOn = true;
            float chorusDepth = 0.2f, chorusMix = 0.15f;
            int delayDivision = 8;
            float delayFeedback = 0.3f, delayMix = 0.18f;
            float reverbSize = 0.6f, reverbDecay = 2.2f, reverbMix = 0.2f;
            float volumeDb = -4.0f;

            // The distortion before the chorus: C-Dist's engine with four of its voicings.
            bool distOn = false;
            int distVoicing = 1;            // Crunch, Lead, Modern, Fuzz
            float distDrive = 0.5f, distTone = 0.5f, distMix = 1.0f;
        };

        static constexpr float maxDetuneCents = 25.0f, cutoffModOctaves = 5.0f, pitchModSemitones = 12.0f, syncModSemitones = 24.0f;

        LeadEngine();

        void prepare (double sampleRate, int maxBlockSize);
        void reset();

        /** Renders `n` samples from `start` into `buffer` (replacing what's there); `midi` holds
            events at sample positions from `start`. */
        void process (juce::AudioBuffer<float>&, int start, int n, const juce::MidiBuffer& midi, const Settings&,
                      double bpm, const TempoSync::SongPosition& = {});

        void allNotesOff();
        void panic();

        double getSampleRate() const        { return rate; }

        //==============================================================================
        /** The ladder or 12 dB filter's response (dB) at `hz` with the cutoff at `cutoffHz`. */
        static double getFilterResponseDb (const Settings&, double cutoffHz, double hz, double sampleRate);

        /** The drive's curve: output for an input of -1..1 at the given settings. */
        static float driveCurve (DriveType, float drive, float x);

        /** One cycle of an oscillator's wave as B plays it (sync, FM and ring from A included), for displays. */
        static float waveAt (const Settings&, int oscillator, double phase);

        /** An envelope's level `t` seconds after a note starts, released after `heldFor` seconds. */
        static float envelopeAt (const Envelope&, double t, double heldFor);

        static ChorusEngine::Settings chorusSettings (const Settings&);
        static DelayEngine::Settings delaySettings (const Settings&);
        static ReverbEngine::Settings reverbSettings (const Settings&);
        static DistEngine::Settings distSettings (const Settings&);
        static bool isBipolar (Source s)    { return s == Source::lfo1 || s == Source::lfo2 || s == Source::key || s == Source::random; }

        const ChorusEngine& getChorus() const   { return chorus; }
        const DistEngine& getDist() const       { return dist; }

        //==============================================================================
        // For the editor.
        struct VoiceView
        {
            std::atomic<int> note { -1 }, stage { 0 };
            std::atomic<bool> held { false };
            std::atomic<float> amp { 0.0f }, mod { 0.0f }, cutoff { 1000.0f }, pan { 0.0f }, pitch { 60.0f };
            std::atomic<float> seconds { 0.0f }, releasedAt { -1.0f };
        };

        std::array<VoiceView, maxVoices> voiceViews;
        std::array<std::atomic<float>, numLfos> lfoValue {}, lfoPhase {};
        std::array<std::atomic<float>, numSlots> slotValue {};
        std::array<std::atomic<float>, numDestinations> destinationValue {};
        std::atomic<int> activeVoices { 0 };

        /** The pitch playing now (semitones, with glide, vibrato and bend), or -1; and the keys held (mono / legato). */
        std::atomic<float> currentPitch { -1.0f }, vibratoAmount { 0.0f };
        std::array<std::atomic<int>, 16> heldKeys {};
        std::atomic<int> numHeldKeys { 0 };

        /** The last samples before the effects (both sides summed), for a scope. */
        static constexpr int scopeLength = 4096;
        std::array<std::atomic<float>, scopeLength> scope {};
        std::atomic<int> scopeWrite { 0 };

        PeakMeter outputPeak[2];
        std::shared_ptr<AnalyserFifo> outputFifo = std::make_shared<AnalyserFifo>();
        StereoTap stereo;

    private:
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

        /** One band-limited oscillator copy (phase in cycles). The output comes one sample late, so
            a sync reset can correct the sample before it as well as after. */
        struct Osc
        {
            double phase = 0.0;
            float held = 0.0f;          // the previous sample, still to be output
        };

        struct Ladder
        {
            double s[4] {};
            void clear()        { s[0] = s[1] = s[2] = s[3] = 0.0; }
            double process (double x, double g, double k, bool saturate) noexcept;
        };

        struct Voice
        {
            bool active = false, held = false, sustained = false;
            int note = -1;
            float velocity = 0.0f, random = 0.0f;
            juce::uint32 order = 0;
            double pitch = 60.0, glideFrom = 60.0, glideTo = 60.0, glideTime = 0.0, glideElapsed = 0.0;
            double seconds = 0.0, sinceTrigger = 0.0, releasedAt = -1.0;
            EnvelopeState amp, mod;

            Osc osc[numOscillators][maxUnison];
            Osc sub;
            double increment[numOscillators][maxUnison] {};
            float gain[numOscillators][maxUnison][2] {};
            float oscLevel[numOscillators] {}, pulseWidth[numOscillators] {};
            double subIncrement = 0.0;
            float subGain = 0.0f, noiseGain = 0.0f, fm = 0.0f, ring = 0.0f, drive = 0.0f;

            Ladder ladder[2];
            Svf svf[2];
            double dcState[2] {}, dcInput[2] {};
            double g = 0.1, gStep = 0.0, k = 0.0;
            float cutoff = 1000.0f, ampGain = 1.0f, ampGainStep = 0.0f, pan = 0.0f;

            double vibratoPhase = 0.0, lfoPhase[numLfos] {};
            float destinations[numDestinations] {};
            float sourceValues[numSources] {};
            float shownPitch = 60.0f;
            juce::uint32 noise = 1;
            bool primed = false;

            float nextRandom() noexcept
            {
                noise ^= noise << 13; noise ^= noise >> 17; noise ^= noise << 5;
                return (float) (noise * (1.0 / 4294967296.0)) * 2.0f - 1.0f;
            }
        };

        void handleMidi (const juce::MidiMessage&, const Settings&);
        void noteOn (int note, float velocity, const Settings&);
        void noteOff (int note, const Settings&);
        int chooseHeldNote (const Settings&) const;
        void startVoice (Voice&, int note, float velocity, const Settings&, bool retrigger, bool glide);
        void moveVoice (Voice&, int note, const Settings&, bool glide);
        void releaseVoice (Voice&);

        void updateModulation (Voice&, const Settings&, double bpm);
        void renderVoice (Voice&, const Settings&, float* left, float* right, int numSamples);
        void render (const Settings&, int from, int to, double bpm);
        void runEffects (const Settings&, int numSamples, double bpm, const TempoSync::SongPosition&);
        void publish (const Settings&);

        double globalLfoHz (const Settings&, int lfo, double bpm) const;
        static float lfoAt (const Lfo&, double phase, int lfoIndex);

        double rate = 48000.0, innerRate = 96000.0;
        int blockSize = 512;
        int controlCountdown = 0;
        EnvelopeCoefficients ampCoefficients, modCoefficients;
        std::array<Voice, maxVoices> voices;
        juce::uint32 noteCounter = 0;
        double lastPitch = -1.0;
        std::vector<int> heldNotes;

        float bend = 0.0f, modWheel = 0.0f, aftertouch = 0.0f;
        bool sustainPedal = false;
        double globalLfoPhase[numLfos] {};
        TempoSync::LfoClock lfoClock;

        // Twice-the-rate rendering: voices add into `inner`, a half-band filter brings it down.
        juce::AudioBuffer<float> inner, mix;
        std::vector<float> decimatorTaps;
        std::vector<float> decimatorHistory[2];
        int decimatorWrite = 0;

        DistEngine dist;
        ChorusEngine chorus;
        DelayEngine delay;
        ReverbEngine reverb;
        bool effectsWereOn[4] { false, false, false, false };       // chorus, delay, reverb, dist
        Smoothed volume;
    };
}
