#include "LeadEngine.h"
#include "FractionalDelay.h"
#include <complex>

namespace cdaw
{
    namespace
    {
        constexpr double pi = juce::MathConstants<double>::pi;
        constexpr double twoPi = juce::MathConstants<double>::twoPi;
        constexpr float voiceHeadroom = 0.5f;       // a lead is mostly one voice; poly presets set their volume lower
        constexpr int oversampling = 2;
        constexpr int decimatorLength = 63;         // half-band, odd length
        constexpr double ladderMaxFeedback = 4.15;  // just past self-oscillation (4)

        double noteHz (double note)     { return 440.0 * std::pow (2.0, (note - 69.0) / 12.0); }
        int index (LeadEngine::Source s)       { return (int) s; }
        int index (LeadEngine::Destination d)  { return (int) d; }

        /** The correction for a step of height 2 just after (t < dt) or before (t > 1 - dt) it. */
        inline double polyBlep (double t, double dt) noexcept
        {
            if (t < dt)
            {
                const double x = t / dt;
                return x + x - x * x - 1.0;
            }
            if (t > 1.0 - dt)
            {
                const double x = (t - 1.0) / dt;
                return x * x + x + x + 1.0;
            }
            return 0.0;
        }

        /** A wave at a phase without any band-limiting (for the sync correction and displays). */
        inline double naive (LeadEngine::Wave wave, double p, double width) noexcept
        {
            switch (wave)
            {
                case LeadEngine::Wave::pulse:     return (p < width ? 1.0 : -1.0) - (2.0 * width - 1.0);
                case LeadEngine::Wave::triangle:  return 4.0 * std::abs (p - 0.5) - 1.0;
                case LeadEngine::Wave::sine:      return std::sin (twoPi * p);
                case LeadEngine::Wave::saw:
                case LeadEngine::Wave::numWaves:
                default:                          return 2.0 * p - 1.0;
            }
        }

        /** A wave at a phase, its edges band-limited. */
        inline double bandLimited (LeadEngine::Wave wave, double p, double dt, double width) noexcept
        {
            switch (wave)
            {
                case LeadEngine::Wave::saw:
                    return 2.0 * p - 1.0 - polyBlep (p, dt);
                case LeadEngine::Wave::pulse:
                {
                    double q = p - width;
                    if (q < 0.0) q += 1.0;
                    return (p < width ? 1.0 : -1.0) + polyBlep (p, dt) - polyBlep (q, dt) - (2.0 * width - 1.0);
                }
                default:
                    return naive (wave, p, width);
            }
        }

        double besselI0 (double x)
        {
            double sum = 1.0, term = 1.0;
            for (int k = 1; k < 32; ++k)
            {
                term *= (x / (2.0 * k)) * (x / (2.0 * k));
                sum += term;
            }
            return sum;
        }

        float cycleRandom (double cycle, int salt)
        {
            const double x = std::sin (cycle * 12.9898 + salt * 78.233) * 43758.5453;
            return (float) (x - std::floor (x)) * 2.0f - 1.0f;
        }
    }

    LeadEngine::LeadEngine()
    {
        heldNotes.reserve (128);

        // The half-band filter (Kaiser-windowed sinc, cutoff at a quarter of the inner rate).
        decimatorTaps.resize ((size_t) decimatorLength);
        const int middle = decimatorLength / 2;
        constexpr double beta = 8.0;
        double sum = 0.0;
        for (int n = 0; n < decimatorLength; ++n)
        {
            const double x = (n - middle) * 0.5;
            const double sinc = n == middle ? 1.0 : std::sin (pi * x) / (pi * x);
            const double r = (double) (n - middle) / middle;
            const double window = besselI0 (beta * std::sqrt (juce::jmax (0.0, 1.0 - r * r))) / besselI0 (beta);
            decimatorTaps[(size_t) n] = (float) (sinc * window);
            sum += decimatorTaps[(size_t) n];
        }
        for (auto& t : decimatorTaps)
            t = (float) (t / sum);
    }

    //==============================================================================
    LeadEngine::EnvelopeCoefficients LeadEngine::EnvelopeCoefficients::from (const Envelope& e, double sampleRate)
    {
        EnvelopeCoefficients c;
        c.attackStep = (float) (1.0 / (juce::jmax (0.001, (double) e.attack) * sampleRate));
        c.decay = (float) std::exp (-4.6 / (juce::jmax (0.001, (double) e.decay) * sampleRate));
        c.release = (float) std::exp (-6.9 / (juce::jmax (0.005, (double) e.release) * sampleRate));
        c.sustain = juce::jlimit (0.0f, 1.0f, e.sustain);
        c.follow = (float) std::exp (-1.0 / (0.02 * sampleRate));
        return c;
    }

    float LeadEngine::EnvelopeState::next (const EnvelopeCoefficients& c) noexcept
    {
        switch (stage)
        {
            case Stage::attack:
                level += c.attackStep;
                if (level >= 1.0f) { level = 1.0f; stage = Stage::decay; }
                break;
            case Stage::decay:
                level = c.sustain + (level - c.sustain) * c.decay;
                if (std::abs (level - c.sustain) < 1.0e-4f) stage = Stage::sustain;
                break;
            case Stage::sustain:
                level = c.sustain + (level - c.sustain) * c.follow;
                break;
            case Stage::release:
                level *= c.release;
                if (level < 1.0e-4f) { level = 0.0f; stage = Stage::idle; }
                break;
            case Stage::idle:
            default:
                level = 0.0f;
                break;
        }
        return level;
    }

    float LeadEngine::envelopeAt (const Envelope& e, double t, double heldFor)
    {
        auto held = [&e] (double time)
        {
            const double a = juce::jmax (0.001, (double) e.attack);
            if (time < a)
                return time / a;
            const double s = juce::jlimit (0.0f, 1.0f, e.sustain);
            return s + (1.0 - s) * std::exp (-4.6 * (time - a) / juce::jmax (0.001, (double) e.decay));
        };

        if (heldFor < 0.0 || t <= heldFor)
            return (float) held (juce::jmax (0.0, t));

        return (float) (held (heldFor) * std::exp (-6.9 * (t - heldFor) / juce::jmax (0.005, (double) e.release)));
    }

    //==============================================================================
    double LeadEngine::Ladder::process (double x, double g, double k, bool saturate) noexcept
    {
        // Four zero-delay one-pole stages with the output fed back: y = (G^4 x + S) / (1 + k G^4),
        // where S is what the stages' states add. The feedback goes through a soft saturation, so
        // full resonance rings without running away.
        const double G = g / (1.0 + g), G2 = G * G, G4 = G2 * G2;
        const double S = (1.0 - G) * (G2 * G * s[0] + G2 * s[1] + G * s[2] + s[3]);
        const double y = (G4 * x + S) / (1.0 + k * G4);
        double u = x - k * y;
        if (saturate)
            u = std::tanh (u);

        for (auto& state : s)
        {
            const double v = (u - state) * G;
            const double out = v + state;
            state = out + v;
            u = out;
        }

        return u * (1.0 + 0.5 * k);     // makes up for the low end the resonance takes
    }

    double LeadEngine::getFilterResponseDb (const Settings& s, double cutoffHz, double hz, double sampleRate)
    {
        const double inner = sampleRate * oversampling;
        const double warped = std::tan (pi * juce::jmin (hz, inner * 0.4999) / inner) / std::tan (pi * cutoffHz / inner);
        const std::complex<double> sj (0.0, warped);
        const double r = juce::jlimit (0.0f, 1.0f, s.resonance);
        std::complex<double> h;

        if (s.filterType == FilterType::ladder)
        {
            const double k = ladderMaxFeedback * r;
            const auto stage = 1.0 / (1.0 + sj);
            const auto g4 = stage * stage * stage * stage;
            h = g4 / (1.0 + k * g4) * (1.0 + 0.5 * k);
        }
        else
        {
            const double k = 1.0 / (0.7071 * std::pow (25.0, r));
            const auto den = sj * sj + k * sj + 1.0;
            h = s.filterType == FilterType::highPass ? sj * sj / den
              : s.filterType == FilterType::bandPass ? k * sj / den
                                                     : 1.0 / den;
        }

        return 20.0 * std::log10 (juce::jmax (1.0e-6, std::abs (h)));
    }

    namespace
    {
        /** The drive curve at one setting, with what only depends on the setting worked out once. */
        struct DriveShape
        {
            using DriveType = LeadEngine::DriveType;

            DriveShape (DriveType t, float d)
                : type (t), drive (juce::jlimit (0.0f, 1.0f, d)), bypassed (drive <= 0.0001f)
            {
                if (bypassed || type == DriveType::fold)
                    return;

                g = juce::Decibels::decibelsToGain (30.0f * drive);
                atFullScale = type == DriveType::tube ? tube (g) : std::tanh (g);
            }

            // Uneven halves: the even harmonics of a tube stage.
            static float tube (float y)     { return y >= 0.0f ? std::tanh (y) : std::tanh (0.6f * y) / 0.6f * 0.8f; }

            float process (float x) const
            {
                if (bypassed)
                    return x;

                switch (type)
                {
                    case DriveType::tube:
                        return tube (g * x) / atFullScale;
                    case DriveType::fold:
                    {
                        const float folded = std::sin (juce::MathConstants<float>::halfPi * x * (1.0f + 5.0f * drive));
                        return x + juce::jmin (1.0f, drive * 4.0f) * (folded - x);
                    }
                    case DriveType::soft:
                    default:
                        return std::tanh (g * x) / atFullScale;
                }
            }

            DriveType type;
            float drive;
            bool bypassed;
            float g = 1.0f, atFullScale = 1.0f;
        };
    }

    float LeadEngine::driveCurve (DriveType type, float drive, float x)
    {
        return DriveShape (type, drive).process (x);
    }

    float LeadEngine::waveAt (const Settings& s, int o, double phase)
    {
        const auto& a = s.osc[0];
        const double pA = phase - std::floor (phase);
        const double valueA = naive (a.wave, pA, a.pulseWidth);
        if (o == 0)
            return (float) valueA;

        // B over one cycle of A: its own ratio, restarted by A when synced, bent by A's FM.
        const auto& b = s.osc[1];
        const double ratio = std::pow (2.0, (b.octave * 12 + b.semitones + b.fine * 0.01 - (a.octave * 12 + a.semitones + a.fine * 0.01)) / 12.0);
        double pB = 0.0;
        constexpr int steps = 64;
        const double target = s.sync ? pA : phase;
        for (int i = 0; i < steps; ++i)
        {
            const double t = target * (double) i / steps;
            const double am = naive (a.wave, t - std::floor (t), a.pulseWidth);
            pB += ratio * juce::jmax (0.0, 1.0 + s.fm * 4.0 * am) * target / steps;
        }

        const double valueB = naive (b.wave, pB - std::floor (pB), b.pulseWidth);
        return (float) (valueB * (1.0 - s.ring) + valueA * valueB * s.ring);
    }

    //==============================================================================
    ChorusEngine::Settings LeadEngine::chorusSettings (const Settings& s)
    {
        // Two voices, a little thickening without smearing the pitch.
        ChorusEngine::Settings c;
        c.voices = 2;
        c.rateHz = 0.5f;
        c.depth = s.chorusDepth;
        c.delayMs = 8.0f;
        c.feedback = 0.0f;
        c.spread = 1.0f;
        c.tone = 12000.0f;
        c.width = 1.0f;
        c.mix = s.chorusMix;
        return c;
    }

    DelayEngine::Settings LeadEngine::delaySettings (const Settings& s)
    {
        // Ping-pong in notes, ducking under the lead so the line stays clear.
        DelayEngine::Settings d;
        d.sync = true;
        d.division = s.delayDivision;
        d.mode = DelayEngine::Mode::pingPong;
        d.feedback = s.delayFeedback;
        d.lowCut = 300.0f;
        d.highCut = 6000.0f;
        d.duck = 0.45f;
        d.mix = s.delayMix;
        return d;
    }

    ReverbEngine::Settings LeadEngine::reverbSettings (const Settings& s)
    {
        // As C-Verb, the mix halved.
        ReverbEngine::Settings r;
        r.mix = s.reverbMix * 0.5f;
        r.size = s.reverbSize;
        r.decay = s.reverbDecay;
        r.preDelay = 40.0f;
        r.diffusion = 0.8f;
        r.modulation = 0.4f;
        r.highDecay = 0.5f;
        r.early = 0.3f;
        r.width = 1.0f;
        r.postLowCut = 250.0f;
        r.postHighCut = 9000.0f;
        return r;
    }

    DistEngine::Settings LeadEngine::distSettings (const Settings& s)
    {
        // C-Dist with four of its voicings, a fixed tight and character, Tone sweeping the high
        // cut and tilting the top; the gate off (a synth has no hum to gate). The synth goes in
        // 12 dB down, at a guitar's level (what C-Dist's drive and level keeping are set for),
        // and comes out 12 dB up again.
        static constexpr DistEngine::Voicing voicings[] { DistEngine::Voicing::crunch, DistEngine::Voicing::lead,
                                                          DistEngine::Voicing::modern, DistEngine::Voicing::fuzz };
        DistEngine::Settings d;
        d.voicing = voicings[juce::jlimit (0, 3, s.distVoicing)];
        d.drive = juce::jlimit (0.0f, 1.0f, s.distDrive);
        d.tight = 90.0f;
        d.character = 0.3f;
        const float tone = juce::jlimit (0.0f, 1.0f, s.distTone);
        d.smooth = 3000.0f * std::pow (16000.0f / 3000.0f, tone);
        d.trebleDb = (tone - 0.5f) * 8.0f;
        d.blend = juce::jlimit (0.0f, 1.0f, s.distMix);
        d.gateOn = false;
        d.inputDb = -12.0f;
        d.levelDb = 12.0f;
        return d;
    }

    //==============================================================================
    void LeadEngine::prepare (double sampleRate, int maxBlockSize)
    {
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        innerRate = rate * oversampling;
        blockSize = juce::jmax (32, maxBlockSize);
        inner.setSize (2, blockSize * oversampling, false, true, false);
        mix.setSize (2, blockSize, false, true, false);
        for (auto& h : decimatorHistory)
            h.assign (64, 0.0f);

        dist.prepare (rate, blockSize);
        chorus.prepare (rate, blockSize);
        delay.prepare (rate, blockSize);
        reverb.prepare (rate, blockSize, ReverbEngine::Settings {});
        volume.reset (rate, 0.02);
        outputFifo->sampleRate = rate;
        reset();
    }

    void LeadEngine::reset()
    {
        for (auto& v : voices)
            v = Voice {};

        noteCounter = 0;
        lastPitch = -1.0;
        heldNotes.clear();
        bend = modWheel = aftertouch = 0.0f;
        sustainPedal = false;
        controlCountdown = 0;
        lfoClock = {};
        for (auto& p : globalLfoPhase)
            p = 0.0;
        for (auto& h : decimatorHistory)
            std::fill (h.begin(), h.end(), 0.0f);
        decimatorWrite = 0;

        dist.reset();
        chorus.reset();
        delay.reset();
        reverb.reset();
        for (auto& wasOn : effectsWereOn)
            wasOn = false;
        volume.value = -1.0f;
    }

    void LeadEngine::allNotesOff()
    {
        heldNotes.clear();
        sustainPedal = false;
        for (auto& v : voices)
            if (v.active)
                releaseVoice (v);
    }

    void LeadEngine::panic()
    {
        for (auto& v : voices)
            v = Voice {};
        heldNotes.clear();
        sustainPedal = false;
        dist.reset();
        chorus.reset();
        delay.reset();
        reverb.reset();
    }

    //==============================================================================
    void LeadEngine::handleMidi (const juce::MidiMessage& m, const Settings& s)
    {
        if (m.isNoteOn())
            noteOn (m.getNoteNumber(), m.getFloatVelocity(), s);
        else if (m.isNoteOff())
            noteOff (m.getNoteNumber(), s);
        else if (m.isPitchWheel())
            bend = (float) (m.getPitchWheelValue() - 8192) / 8192.0f;
        else if (m.isChannelPressure())
            aftertouch = (float) m.getChannelPressureValue() / 127.0f;
        else if (m.isAftertouch())
            aftertouch = (float) m.getAfterTouchValue() / 127.0f;
        else if (m.isAllNotesOff())
            allNotesOff();
        else if (m.isAllSoundOff())
            panic();
        else if (m.isController())
        {
            if (m.getControllerNumber() == 1)
            {
                modWheel = (float) m.getControllerValue() / 127.0f;
            }
            else if (m.getControllerNumber() == 64)
            {
                sustainPedal = m.getControllerValue() >= 64;
                if (! sustainPedal)
                    for (auto& v : voices)
                        if (v.active && v.sustained)
                            releaseVoice (v);
            }
        }
    }

    int LeadEngine::chooseHeldNote (const Settings& s) const
    {
        if (heldNotes.empty())
            return -1;
        if (s.priority == Priority::high)
            return *std::max_element (heldNotes.begin(), heldNotes.end());
        if (s.priority == Priority::low)
            return *std::min_element (heldNotes.begin(), heldNotes.end());
        return heldNotes.back();
    }

    void LeadEngine::noteOn (int note, float velocity, const Settings& s)
    {
        const bool canGlide = s.glide > 0.0005f;

        if (s.mode != VoiceMode::poly)
        {
            const bool othersHeld = ! heldNotes.empty();
            heldNotes.erase (std::remove (heldNotes.begin(), heldNotes.end(), note), heldNotes.end());
            heldNotes.push_back (note);

            for (int i = 1; i < maxVoices; ++i)
                if (voices[(size_t) i].active)
                    releaseVoice (voices[(size_t) i]);

            auto& v = voices[0];
            const int target = chooseHeldNote (s);
            const bool sounding = v.active && (v.held || v.sustained);

            // With high or low priority, a key that doesn't win changes nothing.
            if (sounding && target == v.note)
                return;

            const bool legatoPlaying = sounding && othersHeld;
            const bool glide = canGlide && lastPitch >= 0.0 && (s.glideMode == GlideMode::always || legatoPlaying);

            if (s.mode == VoiceMode::legato && legatoPlaying)
                moveVoice (v, target, s, glide);
            else
                startVoice (v, target, velocity, s, v.active, glide);
            return;
        }

        const int polyphony = juce::jlimit (1, maxVoices, s.polyphony);
        Voice* chosen = nullptr;

        for (auto& v : voices)
            if (v.active && v.note == note)
                chosen = &v;

        for (int i = 0; i < polyphony && chosen == nullptr; ++i)
            if (! voices[(size_t) i].active)
                chosen = &voices[(size_t) i];

        if (chosen == nullptr)
            for (int pass = 0; pass < 2 && chosen == nullptr; ++pass)
                for (int i = 0; i < polyphony; ++i)
                {
                    auto& v = voices[(size_t) i];
                    if ((pass == 1 || v.amp.stage == Stage::release) && (chosen == nullptr || v.order < chosen->order))
                        chosen = &v;
                }

        const bool glide = canGlide && lastPitch >= 0.0 && s.glideMode == GlideMode::always;
        startVoice (*chosen, note, velocity, s, chosen->active, glide);
    }

    void LeadEngine::noteOff (int note, const Settings& s)
    {
        if (s.mode != VoiceMode::poly)
        {
            heldNotes.erase (std::remove (heldNotes.begin(), heldNotes.end(), note), heldNotes.end());
            auto& v = voices[0];
            if (! v.active || ! (v.held || v.sustained))
                return;

            if (! heldNotes.empty())
            {
                // Back to a key still held, without a new attack.
                const int target = chooseHeldNote (s);
                if (target != v.note)
                    moveVoice (v, target, s, s.glide > 0.0005f);
            }
            else if (sustainPedal)
            {
                v.held = false;
                v.sustained = true;
            }
            else
            {
                releaseVoice (v);
            }
            return;
        }

        for (auto& v : voices)
            if (v.active && v.note == note && v.held)
            {
                if (sustainPedal)
                {
                    v.held = false;
                    v.sustained = true;
                }
                else
                {
                    releaseVoice (v);
                }
            }
    }

    void LeadEngine::startVoice (Voice& v, int note, float velocity, const Settings& s, bool fromCurrent, bool glide)
    {
        v.order = ++noteCounter;
        v.noise = 0x9E3779B9u * v.order + 0x2545F491u;
        for (int i = 0; i < 3; ++i)
            v.nextRandom();

        if (! fromCurrent)
        {
            v.amp = {};
            v.mod = {};
            for (int ch = 0; ch < 2; ++ch)
            {
                v.ladder[ch].clear();
                v.svf[ch].clear();
                v.dcState[ch] = v.dcInput[ch] = 0.0;
            }

            // Unison copies start scattered, so a supersaw doesn't click in as one.
            for (int o = 0; o < numOscillators; ++o)
                for (int u = 0; u < maxUnison; ++u)
                    v.osc[o][u] = { u == 0 ? 0.0 : 0.5 + 0.5 * v.nextRandom(), 0.0f };

            v.sub = {};
            v.vibratoPhase = 0.0;
            v.primed = false;
        }

        v.active = true;
        v.held = true;
        v.sustained = false;
        v.note = note;
        v.velocity = velocity;
        v.random = v.nextRandom();
        v.seconds = 0.0;
        v.sinceTrigger = 0.0;
        v.releasedAt = -1.0;

        const double from = fromCurrent ? v.pitch : (lastPitch >= 0.0 ? lastPitch : (double) note);
        v.glideFrom = glide ? from : (double) note;
        v.glideTo = note;
        v.glideTime = glide ? s.glide : 0.0;
        v.glideElapsed = 0.0;
        v.pitch = v.glideFrom;
        lastPitch = note;

        for (auto& p : v.lfoPhase)
            p = 0.0;

        v.amp.start();
        v.mod.start();
    }

    void LeadEngine::moveVoice (Voice& v, int note, const Settings& s, bool glide)
    {
        v.note = note;
        v.glideFrom = v.pitch;
        v.glideTo = note;
        v.glideTime = glide ? s.glide : 0.0;
        v.glideElapsed = 0.0;
        if (! glide)
            v.pitch = note;
        lastPitch = note;
    }

    void LeadEngine::releaseVoice (Voice& v)
    {
        v.held = false;
        v.sustained = false;
        v.amp.release();
        v.mod.release();
        if (v.releasedAt < 0.0)
            v.releasedAt = v.seconds;
    }

    //==============================================================================
    float LeadEngine::lfoAt (const Lfo& lfo, double phase, int lfoIndex)
    {
        return FilterEngine::lfoValue (lfo.shape, phase, cycleRandom (std::floor (phase), lfoIndex));
    }

    double LeadEngine::globalLfoHz (const Settings& s, int l, double bpm) const
    {
        const auto& lfo = s.lfo[l];
        double hz = lfo.sync ? TempoSync::toHz (lfo.division, bpm) : juce::jlimit (0.01, 40.0, (double) lfo.rate);

        // One LFO for all voices: only what's the same for every voice moves its rate.
        const auto destination = l == 0 ? Destination::lfo1Rate : Destination::lfo2Rate;
        float amount = 0.0f;
        for (auto& slot : s.slots)
            if (slot.destination == destination)
            {
                if (slot.source >= Source::macro1 && slot.source <= Source::macro4)
                    amount += s.macros[index (slot.source) - index (Source::macro1)] * slot.amount;
                else if (slot.source == Source::modWheel)
                    amount += modWheel * slot.amount;
                else if (slot.source == Source::aftertouch)
                    amount += aftertouch * slot.amount;
            }

        return hz * std::pow (2.0, 3.0 * amount);
    }

    void LeadEngine::updateModulation (Voice& v, const Settings& s, double bpm)
    {
        const double interval = controlInterval / rate;
        float* src = v.sourceValues;
        float* dst = v.destinations;

        for (int l = 0; l < numLfos; ++l)
        {
            const auto& lfo = s.lfo[l];
            double phase = globalLfoPhase[l];

            if (lfo.retrigger)
            {
                double hz = lfo.sync ? TempoSync::toHz (lfo.division, bpm) : juce::jlimit (0.01, 40.0, (double) lfo.rate);
                hz *= std::pow (2.0, 3.0 * dst[index (l == 0 ? Destination::lfo1Rate : Destination::lfo2Rate)]);
                phase = v.lfoPhase[l];
                v.lfoPhase[l] += hz * interval;
            }

            float value = lfoAt (lfo, phase, l);
            if (lfo.fade > 0.001f)
                value *= (float) juce::jmin (1.0, v.sinceTrigger / lfo.fade);
            src[index (l == 0 ? Source::lfo1 : Source::lfo2)] = value;
        }

        src[index (Source::none)] = 0.0f;
        src[index (Source::modEnv)] = v.mod.level;
        src[index (Source::ampEnv)] = v.amp.level;
        src[index (Source::velocity)] = v.velocity;
        src[index (Source::key)] = juce::jlimit (-1.0f, 1.0f, (float) (v.note - 60) / 60.0f);
        src[index (Source::modWheel)] = modWheel;
        src[index (Source::aftertouch)] = aftertouch;
        src[index (Source::random)] = v.random;
        for (int m = 0; m < numMacros; ++m)
            src[index (Source::macro1) + m] = juce::jlimit (0.0f, 1.0f, s.macros[m]);

        std::fill (dst, dst + numDestinations, 0.0f);
        for (auto& slot : s.slots)
            if (slot.source != Source::none && slot.destination != Destination::none)
                dst[index (slot.destination)] += src[index (slot.source)] * slot.amount;

        // Glide: a straight line in pitch, arriving in the glide time.
        if (v.glideTime > 0.0 && v.glideElapsed < v.glideTime)
        {
            v.glideElapsed += interval;
            v.pitch = v.glideFrom + (v.glideTo - v.glideFrom) * juce::jmin (1.0, v.glideElapsed / v.glideTime);
        }
        else
        {
            v.pitch = v.glideTo;
        }

        // Vibrato: waits for its delay, fades in, deepens with the wheel and aftertouch.
        const double sinceDelay = v.sinceTrigger - s.vibratoDelay;
        const double onset = sinceDelay <= 0.0 ? 0.0 : (s.vibratoFade > 0.001f ? juce::jmin (1.0, sinceDelay / s.vibratoFade) : 1.0);
        const double depthCents = s.vibratoDepth * onset + modWheel * s.vibratoWheel + aftertouch * s.vibratoAftertouch
                                  + dst[index (Destination::vibrato)] * 50.0;
        const double vibrato = std::sin (twoPi * v.vibratoPhase) * juce::jmax (0.0, depthCents) * 0.01;
        v.vibratoPhase += juce::jlimit (0.1, 20.0, (double) s.vibratoRate) * interval;

        const double bendSemitones = bend >= 0.0f ? bend * s.bendUp : bend * s.bendDown;
        const double pitch = v.pitch + bendSemitones + dst[index (Destination::pitch)] * pitchModSemitones + vibrato;
        v.shownPitch = (float) pitch;

        for (int o = 0; o < numOscillators; ++o)
        {
            const auto& osc = s.osc[o];
            v.oscLevel[o] = osc.on ? juce::jlimit (0.0f, 1.0f, osc.level + dst[index (o == 0 ? Destination::levelA : Destination::levelB)]) : 0.0f;
            v.pulseWidth[o] = juce::jlimit (0.05f, 0.95f, osc.pulseWidth + dst[index (Destination::pulseWidth)] * 0.45f);

            const int unison = juce::jlimit (1, maxUnison, osc.unison);
            double oscPitch = pitch + osc.octave * 12 + osc.semitones + osc.fine * 0.01;
            if (o == 1)
                oscPitch += dst[index (Destination::syncPitch)] * syncModSemitones;

            const double hz = noteHz (oscPitch);
            const float detune = juce::jlimit (0.0f, 1.0f, osc.detune) * maxDetuneCents;
            const float spread = juce::jlimit (0.0f, 1.0f, osc.spread);
            const float pan = osc.pan + dst[index (Destination::pan)];
            const float normal = 1.0f / std::sqrt ((float) unison);

            for (int u = 0; u < unison; ++u)
            {
                const float offset = unison == 1 ? 0.0f : 2.0f * (float) u / (float) (unison - 1) - 1.0f;
                v.increment[o][u] = juce::jmin (0.45, hz * std::pow (2.0, offset * detune / 1200.0) / innerRate);
                const float p = juce::jlimit (-1.0f, 1.0f, pan + offset * spread);
                v.gain[o][u][0] = normal * std::sqrt (1.0f - p);
                v.gain[o][u][1] = normal * std::sqrt (1.0f + p);
            }
        }

        v.fm = juce::jlimit (0.0f, 1.0f, s.fm + dst[index (Destination::fm)]);
        v.ring = juce::jlimit (0.0f, 1.0f, s.ring + dst[index (Destination::ring)]);
        v.drive = juce::jlimit (0.0f, 1.0f, s.drive + dst[index (Destination::drive)]);
        v.subIncrement = noteHz (pitch + s.osc[0].octave * 12 - 12.0 * juce::jlimit (1, 2, s.subOctave)) / innerRate;
        v.subGain = juce::jlimit (0.0f, 1.0f, s.subLevel);
        v.noiseGain = juce::jlimit (0.0f, 1.0f, s.noiseLevel) * 0.5f;

        // Filter.
        const double octaves = s.keyTrack * (v.note - 60) / 12.0 + s.filterEnv * 5.0 * v.mod.level
                               + s.filterVelocity * 3.0 * (v.velocity - 1.0) + dst[index (Destination::cutoff)] * cutoffModOctaves;
        v.cutoff = (float) juce::jlimit (20.0, juce::jmin (20000.0, rate * 0.45), s.cutoff * std::pow (2.0, octaves));
        const float resonance = juce::jlimit (0.0f, 1.0f, s.resonance + dst[index (Destination::resonance)]);
        v.k = s.filterType == FilterType::ladder ? ladderMaxFeedback * resonance : 1.0 / (0.7071 * std::pow (25.0, (double) resonance));

        const double g = std::tan (pi * v.cutoff / innerRate);
        const float ampGain = (1.0f - juce::jlimit (0.0f, 1.0f, s.velocity) * (1.0f - v.velocity))
                              * juce::jlimit (0.0f, 2.0f, 1.0f + dst[index (Destination::amp)]);

        if (! v.primed)
        {
            v.g = g;
            v.ampGain = ampGain;
        }

        const int innerInterval = controlInterval * oversampling;
        v.gStep = (g - v.g) / innerInterval;
        v.ampGainStep = (ampGain - v.ampGain) / innerInterval;
        v.pan = juce::jlimit (-1.0f, 1.0f, dst[index (Destination::pan)]);
        v.primed = true;
    }

    void LeadEngine::renderVoice (Voice& v, const Settings& s, float* left, float* right, int n)
    {
        const auto type = s.filterType;
        const bool tube = s.driveType == DriveType::tube;
        const double dcCoefficient = std::exp (-twoPi * 10.0 / innerRate);
        const DriveShape driveShape (s.driveType, v.drive);

        int unison[numOscillators];
        Wave wave[numOscillators];
        for (int o = 0; o < numOscillators; ++o)
        {
            unison[o] = juce::jlimit (1, maxUnison, s.osc[o].unison);
            wave[o] = s.osc[o].wave;
        }

        for (int i = 0; i < n; ++i)
        {
            const float ampLevel = v.amp.next (ampCoefficients);
            v.mod.next (modCoefficients);

            if (v.amp.stage == Stage::idle)
            {
                v.active = false;
                break;
            }

            float l = 0.0f, r = 0.0f;
            const bool bOn = v.oscLevel[1] > 0.0f || v.ring > 0.0f;

            // A's copies first (each output one sample late, so a sync reset can still correct
            // the sample before it), noting whether and when each one restarted.
            double valueA[maxUnison] {}, sinceWrap[maxUnison] {};
            bool wrappedA[maxUnison] {};

            for (int u = 0; u < unison[0]; ++u)
            {
                auto& a = v.osc[0][u];
                const double dA = v.increment[0][u];
                a.phase += dA;
                if (a.phase >= 1.0)
                {
                    a.phase -= 1.0;
                    wrappedA[u] = true;
                    sinceWrap[u] = a.phase / dA;
                }

                valueA[u] = a.held;
                a.held = (float) bandLimited (wave[0], a.phase, dA, v.pulseWidth[0]);
                l += (float) valueA[u] * v.gain[0][u][0] * v.oscLevel[0];
                r += (float) valueA[u] * v.gain[0][u][1] * v.oscLevel[0];
            }

            // B's copies, each paired with one of A's (the last one if B has more).
            for (int u = 0; bOn && u < unison[1]; ++u)
            {
                const int partner = juce::jmin (u, unison[0] - 1);
                auto& b = v.osc[1][u];
                const double dB = v.increment[1][u] * juce::jmax (0.0, 1.0 + v.fm * 4.0 * valueA[partner]);
                b.phase += dB;
                double now;

                if (s.sync && wrappedA[partner])
                {
                    // A restarted `x` samples ago: B restarts then too. A step of `jump` that long
                    // ago is band-limited by -(1-x)^2 / 2 of it now and x^2 / 2 of it on the sample before.
                    const double x = juce::jlimit (0.0, 1.0, sinceWrap[partner]);
                    double before = b.phase - x * dB;
                    before -= std::floor (before);
                    const double jump = naive (wave[1], 0.0, v.pulseWidth[1]) - naive (wave[1], before, v.pulseWidth[1]);
                    b.phase = x * dB;
                    b.held += (float) (jump * 0.5 * x * x);
                    now = naive (wave[1], b.phase, v.pulseWidth[1]) - jump * 0.5 * (1.0 - x) * (1.0 - x);
                }
                else
                {
                    if (b.phase >= 1.0)
                        b.phase -= std::floor (b.phase);
                    now = bandLimited (wave[1], b.phase, juce::jmax (1.0e-6, dB), v.pulseWidth[1]);
                }

                const double valueB = b.held;
                b.held = (float) now;
                const double out = valueB * v.oscLevel[1] + valueA[partner] * valueB * v.ring;
                l += (float) out * v.gain[1][u][0];
                r += (float) out * v.gain[1][u][1];
            }

            if (v.subGain > 0.0f)
            {
                auto& sub = v.sub;
                sub.phase += v.subIncrement;
                if (sub.phase >= 1.0)
                    sub.phase -= 1.0;
                const double now = s.subSquare ? bandLimited (Wave::pulse, sub.phase, v.subIncrement, 0.5) : std::sin (twoPi * sub.phase);
                const float value = sub.held;
                sub.held = (float) now;
                l += value * v.subGain;
                r += value * v.subGain;
            }

            if (v.noiseGain > 0.0f)
            {
                l += v.nextRandom() * v.noiseGain;
                r += v.nextRandom() * v.noiseGain;
            }

            double sides[2] { l, r };
            for (int ch = 0; ch < 2; ++ch)
            {
                double x = driveShape.process ((float) sides[ch]);

                if (tube)
                {
                    // The tube's uneven halves leave an offset: take it out.
                    const double y = x - v.dcInput[ch] + dcCoefficient * v.dcState[ch];
                    v.dcInput[ch] = x;
                    v.dcState[ch] = y;
                    x = y;
                }

                if (type == FilterType::ladder)
                {
                    x = v.ladder[ch].process (x, v.g, v.k, true);
                }
                else
                {
                    double low, band, high;
                    v.svf[ch].process (x, v.g, v.k, low, band, high);
                    x = type == FilterType::highPass ? high : type == FilterType::bandPass ? band : low;
                }

                sides[ch] = x;
            }

            const float gain = ampLevel * v.ampGain * voiceHeadroom;
            left[i] += (float) sides[0] * gain;
            right[i] += (float) sides[1] * gain;

            v.g = juce::jlimit (1.0e-5, 20.0, v.g + v.gStep);
            v.ampGain += v.ampGainStep;
        }

        for (int ch = 0; ch < 2; ++ch)
        {
            for (auto& state : v.ladder[ch].s)
                if (std::abs (state) < 1.0e-20)
                    state = 0.0;
            v.svf[ch].flushDenormals();
        }

        v.seconds += n / innerRate;
        v.sinceTrigger += n / innerRate;
    }

    void LeadEngine::render (const Settings& s, int from, int to, double bpm)
    {
        auto* left = inner.getWritePointer (0);
        auto* right = inner.getWritePointer (1);

        // Modulation every controlInterval output samples, counted across blocks (so the sound
        // doesn't depend on how the host cuts them).
        for (int pos = from; pos < to;)
        {
            const bool update = controlCountdown <= 0;
            if (update)
                controlCountdown = controlInterval;

            const int n = juce::jmin (controlCountdown, to - pos);

            for (auto& v : voices)
                if (v.active)
                {
                    if (update || ! v.primed)
                        updateModulation (v, s, bpm);
                    renderVoice (v, s, left + pos * oversampling, right + pos * oversampling, n * oversampling);
                }

            if (update)
                for (int l = 0; l < numLfos; ++l)
                    if (! s.lfo[l].retrigger)
                        globalLfoPhase[l] += globalLfoHz (s, l, bpm) * controlInterval / rate;

            controlCountdown -= n;
            pos += n;
        }
    }

    void LeadEngine::runEffects (const Settings& s, int n, double bpm, const TempoSync::SongPosition& position)
    {
        if (s.distOn)
        {
            if (! effectsWereOn[3])
                dist.reset();
            dist.process (mix, 0, n, distSettings (s));
        }
        effectsWereOn[3] = s.distOn;

        if (s.chorusOn)
        {
            if (! effectsWereOn[0])
                chorus.reset();
            chorus.process (mix, 0, n, chorusSettings (s), position);
        }

        if (s.delayOn)
        {
            if (! effectsWereOn[1])
                delay.reset();
            delay.process (mix, 0, n, delaySettings (s), bpm);
        }

        if (s.reverbOn)
        {
            const auto settings = reverbSettings (s);
            if (! effectsWereOn[2])
            {
                reverb.reset();
                reverb.jumpTo (settings);
            }
            reverb.process (mix, 0, n, settings);
        }

        effectsWereOn[0] = s.chorusOn;
        effectsWereOn[1] = s.delayOn;
        effectsWereOn[2] = s.reverbOn;
    }

    void LeadEngine::process (juce::AudioBuffer<float>& buffer, int start, int n, const juce::MidiBuffer& midi, const Settings& s,
                              double bpm, const TempoSync::SongPosition& position)
    {
        if (n <= 0 || buffer.getNumChannels() == 0)
            return;

        ampCoefficients = EnvelopeCoefficients::from (s.ampEnv, innerRate);
        modCoefficients = EnvelopeCoefficients::from (s.modEnv, innerRate);

        const bool lock = lfoClock.shouldLock (position, n, rate);
        for (int l = 0; l < numLfos; ++l)
        {
            const auto& lfo = s.lfo[l];
            if (lfo.sync && position.playing)
                globalLfoPhase[l] = position.beats / TempoSync::get (lfo.division).beats;
            else if (lock)
                globalLfoPhase[l] = globalLfoHz (s, l, bpm) * position.seconds;
        }

        const float volumeTarget = juce::Decibels::decibelsToGain (s.volumeDb, -100.0f);
        if (volume.value < 0.0f)
            volume.value = volumeTarget;

        auto midiEvent = midi.cbegin();
        const int nonZeroMiddle = decimatorLength / 2;

        for (int piece = 0; piece < n; piece += blockSize)
        {
            const int length = juce::jmin (blockSize, n - piece);
            inner.clear (0, length * oversampling);

            int pos = 0;
            for (; midiEvent != midi.cend(); ++midiEvent)
            {
                const auto event = *midiEvent;
                const int at = juce::jlimit (0, n - 1, event.samplePosition) - piece;
                if (at >= length)
                    break;

                if (at > pos)
                {
                    render (s, pos, at, bpm);
                    pos = at;
                }

                handleMidi (event.getMessage(), s);
            }

            render (s, pos, length, bpm);

            // Back down to the output rate: every second sample through the half-band filter
            // (whose odd taps are zero, apart from the middle one).
            for (int ch = 0; ch < 2; ++ch)
            {
                const auto* in = inner.getReadPointer (ch);
                auto* out = mix.getWritePointer (ch);
                auto& history = decimatorHistory[ch];
                int write = decimatorWrite;

                for (int i = 0; i < length * oversampling; ++i)
                {
                    history[(size_t) write] = in[i];
                    write = (write + 1) & 63;

                    if ((i & 1) == 1)
                    {
                        float sum = decimatorTaps[(size_t) nonZeroMiddle] * history[(size_t) ((write - 1 - nonZeroMiddle) & 63)];
                        for (int t = 0; t < decimatorLength; t += 2)
                            sum += decimatorTaps[(size_t) t] * history[(size_t) ((write - 1 - t) & 63)];
                        out[i / 2] = sum;
                    }
                }

                if (ch == 1)
                    decimatorWrite = write;
            }

            // The dry sound for the scope.
            {
                const auto* l = mix.getReadPointer (0);
                const auto* r = mix.getReadPointer (1);
                int w = scopeWrite.load (std::memory_order_relaxed);
                for (int i = 0; i < length; ++i)
                    scope[(size_t) ((w + i) % scopeLength)].store (0.5f * (l[i] + r[i]), std::memory_order_relaxed);
                scopeWrite.store ((w + length) % scopeLength, std::memory_order_relaxed);
            }

            auto song = position;
            song.seconds += piece / rate;
            song.beats += piece / rate * bpm / 60.0;
            runEffects (s, length, bpm, song);

            auto* l = mix.getWritePointer (0);
            auto* r = mix.getWritePointer (1);
            float peak[2] {};
            for (int i = 0; i < length; ++i)
            {
                const float g = volume.next (volumeTarget);
                l[i] *= g;
                r[i] *= g;
                peak[0] = juce::jmax (peak[0], std::abs (l[i]));
                peak[1] = juce::jmax (peak[1], std::abs (r[i]));
            }

            outputPeak[0].push (peak[0]);
            outputPeak[1].push (peak[1]);
            stereo.push (l, r, length);
            juce::AudioBuffer<float> view (mix.getArrayOfWritePointers(), 2, 0, length);
            outputFifo->push (view);

            if (buffer.getNumChannels() == 1)
            {
                buffer.copyFrom (0, start + piece, mix, 0, 0, length);
                buffer.addFrom (0, start + piece, mix, 1, 0, length);
                buffer.applyGain (0, start + piece, length, 0.5f);
            }
            else
            {
                buffer.copyFrom (0, start + piece, mix, 0, 0, length);
                buffer.copyFrom (1, start + piece, mix, 1, 0, length);
                for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
                    buffer.clear (ch, start + piece, length);
            }
        }

        publish (s);
    }

    void LeadEngine::publish (const Settings& s)
    {
        const Voice* newest = nullptr;
        int count = 0;

        for (int i = 0; i < maxVoices; ++i)
        {
            const auto& v = voices[(size_t) i];
            auto& view = voiceViews[(size_t) i];
            view.note.store (v.active ? v.note : -1, std::memory_order_relaxed);
            if (! v.active)
                continue;

            ++count;
            view.stage.store ((int) v.amp.stage, std::memory_order_relaxed);
            view.held.store (v.held || v.sustained, std::memory_order_relaxed);
            view.amp.store (v.amp.level, std::memory_order_relaxed);
            view.mod.store (v.mod.level, std::memory_order_relaxed);
            view.cutoff.store (v.cutoff, std::memory_order_relaxed);
            view.pan.store (v.pan, std::memory_order_relaxed);
            view.pitch.store (v.shownPitch, std::memory_order_relaxed);
            view.seconds.store ((float) v.seconds, std::memory_order_relaxed);
            view.releasedAt.store ((float) v.releasedAt, std::memory_order_relaxed);

            if (newest == nullptr || v.order > newest->order)
                newest = &v;
        }

        activeVoices.store (count, std::memory_order_relaxed);
        currentPitch.store (newest != nullptr ? newest->shownPitch : -1.0f, std::memory_order_relaxed);
        vibratoAmount.store (newest != nullptr ? (float) (newest->shownPitch - newest->pitch) : 0.0f, std::memory_order_relaxed);

        const int held = juce::jmin ((int) heldKeys.size(), (int) heldNotes.size());
        for (int i = 0; i < held; ++i)
            heldKeys[(size_t) i].store (heldNotes[(size_t) i], std::memory_order_relaxed);
        numHeldKeys.store (held, std::memory_order_relaxed);

        for (int l = 0; l < numLfos; ++l)
        {
            const bool perVoice = s.lfo[l].retrigger && newest != nullptr;
            const double phase = perVoice ? newest->lfoPhase[l] : globalLfoPhase[l];
            lfoPhase[(size_t) l].store ((float) (phase - std::floor (phase)), std::memory_order_relaxed);
            lfoValue[(size_t) l].store (perVoice ? newest->sourceValues[index (l == 0 ? Source::lfo1 : Source::lfo2)]
                                                 : lfoAt (s.lfo[l], phase, l), std::memory_order_relaxed);
        }

        for (int k = 0; k < numSlots; ++k)
        {
            const auto& slot = s.slots[k];
            float value = 0.0f;
            if (newest != nullptr)
                value = newest->sourceValues[index (slot.source)] * slot.amount;
            else if (slot.source >= Source::macro1 && slot.source <= Source::macro4)
                value = s.macros[index (slot.source) - index (Source::macro1)] * slot.amount;
            slotValue[(size_t) k].store (slot.source == Source::none || slot.destination == Destination::none ? 0.0f : value, std::memory_order_relaxed);
        }

        for (int d = 0; d < numDestinations; ++d)
            destinationValue[(size_t) d].store (newest != nullptr ? newest->destinations[d] : 0.0f, std::memory_order_relaxed);
    }
}
