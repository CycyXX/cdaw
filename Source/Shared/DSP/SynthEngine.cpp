#include "SynthEngine.h"

namespace cdaw
{
    namespace
    {
        constexpr double pi = juce::MathConstants<double>::pi;
        constexpr double twoPi = juce::MathConstants<double>::twoPi;
        constexpr float voiceHeadroom = 0.2f;       // a chord of full voices still leaves room

        double noteHz (double note)     { return 440.0 * std::pow (2.0, (note - 69.0) / 12.0); }

        /** A steady pseudo-random value (-1..1) for an LFO cycle (the random shape's steps). */
        float cycleRandom (double cycle, int salt)
        {
            const double x = std::sin (cycle * 12.9898 + salt * 78.233) * 43758.5453;
            return (float) (x - std::floor (x)) * 2.0f - 1.0f;
        }

        int index (SynthEngine::Source s)       { return (int) s; }
        int index (SynthEngine::Destination d)  { return (int) d; }
    }

    SynthEngine::SynthEngine() : tables (Wavetables::get())
    {
        heldNotes.reserve (128);
    }

    //==============================================================================
    SynthEngine::EnvelopeCoefficients SynthEngine::EnvelopeCoefficients::from (const Envelope& e, double sampleRate)
    {
        EnvelopeCoefficients c;
        c.attackStep = (float) (1.0 / (juce::jmax (0.001, (double) e.attack) * sampleRate));
        c.decay = (float) std::exp (-4.6 / (juce::jmax (0.001, (double) e.decay) * sampleRate));       // within 1 % at the decay time
        c.release = (float) std::exp (-6.9 / (juce::jmax (0.005, (double) e.release) * sampleRate));   // 60 dB down at the release time
        c.sustain = juce::jlimit (0.0f, 1.0f, e.sustain);
        c.follow = (float) std::exp (-1.0 / (0.02 * sampleRate));
        return c;
    }

    float SynthEngine::EnvelopeState::next (const EnvelopeCoefficients& c) noexcept
    {
        switch (stage)
        {
            case Stage::attack:
                level += c.attackStep;
                if (level >= 1.0f)
                {
                    level = 1.0f;
                    stage = Stage::decay;
                }
                break;

            case Stage::decay:
                level = c.sustain + (level - c.sustain) * c.decay;
                if (std::abs (level - c.sustain) < 1.0e-4f)
                    stage = Stage::sustain;
                break;

            case Stage::sustain:
                level = c.sustain + (level - c.sustain) * c.follow;
                break;

            case Stage::release:
                level *= c.release;
                if (level < 1.0e-4f)
                {
                    level = 0.0f;
                    stage = Stage::idle;
                }
                break;

            case Stage::idle:
            default:
                level = 0.0f;
                break;
        }

        return level;
    }

    float SynthEngine::envelopeAt (const Envelope& e, double t, double heldFor)
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

    FilterEngine::Settings SynthEngine::filterSettings (const Settings& s)
    {
        FilterEngine::Settings f;
        f.type = s.filterType;
        f.steep = s.filterSteep;
        f.cutoff = s.cutoff;
        f.resonance = s.resonance;
        f.mix = 1.0f;
        f.outputDb = 0.0f;
        return f;
    }

    bool SynthEngine::isBipolar (Source s)
    {
        return s == Source::lfo1 || s == Source::lfo2 || s == Source::key || s == Source::random;
    }

    //==============================================================================
    void SynthEngine::prepare (double sampleRate, int maxBlockSize)
    {
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        blockSize = juce::jmax (32, maxBlockSize);
        mix.setSize (2, blockSize, false, true, false);

        chorus.prepare (rate, blockSize);
        delay.prepare (rate, blockSize);
        reverb.prepare (rate, blockSize, ReverbEngine::Settings {});
        volume.reset (rate, 0.02);
        outputFifo->sampleRate = rate;
        reset();
    }

    void SynthEngine::reset()
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

        chorus.reset();
        delay.reset();
        reverb.reset();
        effectsWereOn[0] = effectsWereOn[1] = effectsWereOn[2] = false;
        volume.value = -1.0f;      // set on the first block
    }

    void SynthEngine::allNotesOff()
    {
        heldNotes.clear();
        sustainPedal = false;
        for (auto& v : voices)
            if (v.active)
                releaseVoice (v);
    }

    void SynthEngine::panic()
    {
        for (auto& v : voices)
            v = Voice {};

        heldNotes.clear();
        sustainPedal = false;
        chorus.reset();
        delay.reset();
        reverb.reset();
    }

    //==============================================================================
    void SynthEngine::handleMidi (const juce::MidiMessage& m, const Settings& s)
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

    void SynthEngine::noteOn (int note, float velocity, const Settings& s)
    {
        if (s.mode != VoiceMode::poly)
        {
            heldNotes.erase (std::remove (heldNotes.begin(), heldNotes.end(), note), heldNotes.end());
            heldNotes.push_back (note);

            // One voice: legato glides to the new key without starting the envelopes again.
            for (int i = 1; i < maxVoices; ++i)
                if (voices[(size_t) i].active)
                    releaseVoice (voices[(size_t) i]);

            auto& v = voices[0];
            if (s.mode == VoiceMode::legato && v.active && (v.held || v.sustained))
            {
                v.note = note;
                v.targetPitch = note;
                lastPitch = note;
            }
            else
            {
                startVoice (v, note, velocity, s, v.active);
            }
            return;
        }

        const int polyphony = juce::jlimit (1, maxVoices, s.polyphony);
        Voice* chosen = nullptr;

        // The same key again takes over its own voice.
        for (auto& v : voices)
            if (v.active && v.note == note)
                chosen = &v;

        for (int i = 0; i < polyphony && chosen == nullptr; ++i)
            if (! voices[(size_t) i].active)
                chosen = &voices[(size_t) i];

        // All busy: the oldest released voice, else the oldest one.
        if (chosen == nullptr)
            for (int pass = 0; pass < 2 && chosen == nullptr; ++pass)
                for (int i = 0; i < polyphony; ++i)
                {
                    auto& v = voices[(size_t) i];
                    if ((pass == 1 || v.amp.stage == Stage::release) && (chosen == nullptr || v.order < chosen->order))
                        chosen = &v;
                }

        startVoice (*chosen, note, velocity, s, chosen->active);
    }

    void SynthEngine::noteOff (int note, const Settings& s)
    {
        if (s.mode != VoiceMode::poly)
        {
            heldNotes.erase (std::remove (heldNotes.begin(), heldNotes.end(), note), heldNotes.end());
            auto& v = voices[0];

            if (! v.active || v.note != note)
                return;

            if (! heldNotes.empty())
            {
                // Back to the key still held (gliding there, no new attack).
                v.note = heldNotes.back();
                v.targetPitch = v.note;
                lastPitch = v.note;
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

    void SynthEngine::startVoice (Voice& v, int note, float velocity, const Settings& s, bool fromCurrent)
    {
        v.order = ++noteCounter;
        v.noise = 0x9E3779B9u * v.order + 0x2545F491u;      // seeded per note: a render repeats
        for (int i = 0; i < 3; ++i)
            v.nextRandom();

        if (! fromCurrent)
        {
            v.amp = {};
            v.mod = {};
            for (auto& side : v.filter)
                for (auto& f : side)
                    f.clear();

            // Unison copies start at scattered phases, so they don't all click in together.
            for (int o = 0; o < numOscillators; ++o)
                for (int u = 0; u < maxUnison; ++u)
                    v.phase[o][u] = u == 0 ? 0.0 : 0.5 + 0.5 * v.nextRandom();

            v.subPhase = 0.0;
            v.noiseState[0] = v.noiseState[1] = 0.0f;
            v.drift[0] = v.drift[1] = 0.0f;
            v.driftCountdown = 0;
            v.primed = false;
        }

        v.active = true;
        v.held = true;
        v.sustained = false;
        v.note = note;
        v.velocity = velocity;
        v.random = v.nextRandom();
        v.seconds = 0.0;
        v.releasedAt = -1.0;

        // Glide from the last note played.
        const bool glide = s.glide > 0.0005f && lastPitch >= 0.0;
        if (! fromCurrent || ! glide)
            v.pitch = glide ? lastPitch : (double) note;
        v.targetPitch = note;
        lastPitch = note;

        for (int l = 0; l < numLfos; ++l)
            v.lfoPhase[l] = 0.0;

        v.amp.start();
        v.mod.start();
    }

    void SynthEngine::releaseVoice (Voice& v)
    {
        v.held = false;
        v.sustained = false;
        v.amp.release();
        v.mod.release();
        if (v.releasedAt < 0.0)
            v.releasedAt = v.seconds;
    }

    //==============================================================================
    float SynthEngine::lfoAt (const Lfo& lfo, double phase, int lfoIndex)
    {
        return FilterEngine::lfoValue (lfo.shape, phase, cycleRandom (std::floor (phase), lfoIndex));
    }

    double SynthEngine::globalLfoHz (const Settings& s, int l, double bpm) const
    {
        const auto& lfo = s.lfo[l];
        double hz = lfo.sync ? TempoSync::toHz (lfo.division, bpm) : juce::jlimit (0.01, 40.0, (double) lfo.rate);

        // One LFO for all voices: only the sources that are the same for every voice move its rate.
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

    void SynthEngine::updateModulation (Voice& v, const Settings& s, double bpm)
    {
        const double interval = controlInterval / rate;
        float* src = v.sourceValues;
        float* dst = v.destinations;

        // Sources.
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
                value *= (float) juce::jmin (1.0, v.seconds / lfo.fade);

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

        // Destinations.
        std::fill (dst, dst + numDestinations, 0.0f);
        for (auto& slot : s.slots)
            if (slot.source != Source::none && slot.destination != Destination::none)
                dst[index (slot.destination)] += src[index (slot.source)] * slot.amount;

        // Glide (arriving in about the glide time) and drift move once per update.
        if (s.glide > 0.0005f && std::abs (v.targetPitch - v.pitch) > 0.001)
            v.pitch += (v.targetPitch - v.pitch) * (1.0 - std::exp (-4.0 * interval / s.glide));
        else
            v.pitch = v.targetPitch;

        v.driftCountdown -= controlInterval;
        if (v.driftCountdown <= 0)
        {
            v.driftCountdown = (int) (rate * (0.3 + 0.25 * (1.0 + v.nextRandom())));
            v.driftTarget[0] = 0.12f * s.drift * v.nextRandom();
            v.driftTarget[1] = 0.35f * s.drift * v.nextRandom();
        }
        for (int i = 0; i < 2; ++i)
            v.drift[i] += (v.driftTarget[i] - v.drift[i]) * 0.015f;

        const double pitch = v.pitch + bend * s.bendRange + dst[index (Destination::pitch)] * pitchModSemitones + v.drift[0];

        // Oscillators.
        for (int o = 0; o < numOscillators; ++o)
        {
            const auto& osc = s.osc[o];
            v.oscLevel[o] = osc.on ? juce::jlimit (0.0f, 1.0f, osc.level + dst[index (o == 0 ? Destination::levelA : Destination::levelB)]) : 0.0f;
            if (v.oscLevel[o] <= 0.0f)
                continue;

            const int unison = juce::jlimit (1, maxUnison, osc.unison);
            const double hz = noteHz (pitch + osc.octave * 12 + osc.semitones + osc.fine * 0.01);
            const float detune = juce::jlimit (0.0f, 1.0f, osc.detune + dst[index (Destination::detune)]) * maxDetuneCents;
            const float spread = juce::jlimit (0.0f, 1.0f, osc.spread + dst[index (Destination::spread)]);
            const float pan = osc.pan + dst[index (Destination::pan)];
            const float normal = 1.0f / std::sqrt ((float) unison);

            v.level[o] = Wavetables::levelFor (hz * std::pow (2.0, detune / 1200.0), rate);

            for (int u = 0; u < unison; ++u)
            {
                const float offset = unison == 1 ? 0.0f : 2.0f * (float) u / (float) (unison - 1) - 1.0f;
                v.increment[o][u] = hz * std::pow (2.0, offset * detune / 1200.0) / rate;
                const float p = juce::jlimit (-1.0f, 1.0f, pan + offset * spread);
                v.gain[o][u][0] = normal * std::sqrt (1.0f - p);
                v.gain[o][u][1] = normal * std::sqrt (1.0f + p);
            }

            const float position = juce::jlimit (0.0f, 1.0f, osc.position + dst[index (o == 0 ? Destination::positionA : Destination::positionB)]);
            if (! v.primed)
                v.position[o] = position;
            v.positionStep[o] = (position - v.position[o]) / controlInterval;
        }

        v.subIncrement = noteHz (pitch - 12.0 * juce::jlimit (1, 2, s.subOctave)) / rate;
        v.subGain = juce::jlimit (0.0f, 1.0f, s.subLevel);
        v.noiseGain = juce::jlimit (0.0f, 1.0f, s.noiseLevel + dst[index (Destination::noise)]) * 0.5f;

        // Filter.
        const double octaves = s.keyTrack * (v.note - 60) / 12.0 + s.filterEnv * filterEnvOctaves * v.mod.level
                               + dst[index (Destination::cutoff)] * cutoffModOctaves + v.drift[1];
        v.cutoff = (float) juce::jlimit (20.0, juce::jmin (20000.0, rate * 0.45), s.cutoff * std::pow (2.0, octaves));

        auto filter = filterSettings (s);
        filter.resonance = juce::jlimit (0.0f, 1.0f, s.resonance + dst[index (Destination::resonance)]);
        const double q = FilterEngine::getQ (filter);
        const bool steepCut = s.filterSteep && (s.filterType == FilterEngine::Type::lowPass || s.filterType == FilterEngine::Type::highPass);
        v.k1 = steepCut ? 1.0 / 0.5412 : 1.0 / q;
        v.k2 = 1.0 / q;

        const double g = std::tan (pi * v.cutoff / rate);
        const float ampGain = (1.0f - juce::jlimit (0.0f, 1.0f, s.velocity) * (1.0f - v.velocity))
                              * juce::jlimit (0.0f, 2.0f, 1.0f + dst[index (Destination::amp)]);

        if (! v.primed)
        {
            v.g = g;
            v.ampGain = ampGain;
        }

        v.gStep = (g - v.g) / controlInterval;
        v.ampGainStep = (ampGain - v.ampGain) / controlInterval;
        v.primed = true;
    }

    void SynthEngine::renderVoice (Voice& v, const Settings& s, float* left, float* right, int n)
    {
        const bool steep = s.filterSteep;
        const auto type = s.filterType;
        const float driveGain = juce::Decibels::decibelsToGain (24.0f * juce::jlimit (0.0f, 1.0f, s.filterDrive));
        const bool driven = s.filterDrive > 0.001f;

        // Noise colour: dark (low pass) to airy (high pass), one pole.
        const float colour = juce::jlimit (-1.0f, 1.0f, s.noiseColour);
        const double noiseHz = colour < 0.0f ? 20000.0 * std::pow (2.0, 6.0 * colour) : 40.0 * std::pow (2.0, 7.3 * colour);
        const float noiseCoefficient = (float) (1.0 - std::exp (-twoPi * juce::jmin (noiseHz, rate * 0.45) / rate));
        const bool airy = colour > 0.0f;

        int unison[numOscillators], bank[numOscillators];
        for (int o = 0; o < numOscillators; ++o)
        {
            unison[o] = juce::jlimit (1, maxUnison, s.osc[o].unison);
            bank[o] = juce::jlimit (0, Wavetables::numBanks - 1, s.osc[o].bank);
        }

        auto pick = [type] (double low, double band, double high, double in)
        {
            switch (type)
            {
                case FilterEngine::Type::highPass:  return high;
                case FilterEngine::Type::bandPass:  return band;
                case FilterEngine::Type::notch:     return in - band;
                case FilterEngine::Type::lowPass:
                default:                            return low;
            }
        };

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

            for (int o = 0; o < numOscillators; ++o)
            {
                if (v.oscLevel[o] <= 0.0f)
                    continue;

                float ol = 0.0f, orr = 0.0f;
                const float position = v.position[o];
                const auto morph = tables.morphAt (bank[o], position, v.level[o]);
                for (int u = 0; u < unison[o]; ++u)
                {
                    auto& phase = v.phase[o][u];
                    const float x = Wavetables::read (morph, phase);
                    ol += x * v.gain[o][u][0];
                    orr += x * v.gain[o][u][1];
                    phase += v.increment[o][u];
                    if (phase >= 1.0)
                        phase -= 1.0;
                }

                l += ol * v.oscLevel[o];
                r += orr * v.oscLevel[o];
                v.position[o] = juce::jlimit (0.0f, 1.0f, position + v.positionStep[o]);
            }

            if (v.subGain > 0.0f)
            {
                const double p = v.subPhase;
                const float sub = (s.subTriangle ? (float) (1.0 - 4.0 * std::abs (p - 0.5)) : (float) std::sin (twoPi * p)) * v.subGain;
                l += sub;
                r += sub;
            }

            v.subPhase += v.subIncrement;
            if (v.subPhase >= 1.0)
                v.subPhase -= 1.0;

            if (v.noiseGain > 0.0f)
            {
                const float white[2] { v.nextRandom(), v.nextRandom() };
                float coloured[2];
                for (int ch = 0; ch < 2; ++ch)
                {
                    v.noiseState[ch] += noiseCoefficient * (white[ch] - v.noiseState[ch]);
                    coloured[ch] = airy ? white[ch] - v.noiseState[ch] : v.noiseState[ch];
                }
                l += coloured[0] * v.noiseGain;
                r += coloured[1] * v.noiseGain;
            }

            // The voice's filter, each side.
            const Svf::Coefficients stage1 (v.g, v.k1);
            const auto stage2 = steep ? Svf::Coefficients (v.g, v.k2) : stage1;
            double sides[2] { l, r };
            for (int ch = 0; ch < 2; ++ch)
            {
                double x = sides[ch];
                if (driven)
                    x = 2.0 / driveGain * softClip ((float) x * driveGain * 0.5f);

                double low, band, high;
                v.filter[ch][0].process (x, stage1, low, band, high);
                double y = pick (low, band, high, x);

                if (steep)
                {
                    v.filter[ch][1].process (y, stage2, low, band, high);
                    y = pick (low, band, high, y);
                }

                sides[ch] = y;
            }

            const float gain = ampLevel * v.ampGain * voiceHeadroom;
            left[i] += (float) sides[0] * gain;
            right[i] += (float) sides[1] * gain;

            v.g = juce::jmax (1.0e-5, v.g + v.gStep);
            v.ampGain += v.ampGainStep;
        }

        for (auto& side : v.filter)
            for (auto& f : side)
                f.flushDenormals();

        v.seconds += n / rate;
    }

    void SynthEngine::render (const Settings& s, int from, int to, double bpm)
    {
        auto* left = mix.getWritePointer (0);
        auto* right = mix.getWritePointer (1);

        // Modulation updates every controlInterval samples, counted across blocks, so the sound
        // doesn't depend on how the host cuts its blocks.
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
                    renderVoice (v, s, left + pos, right + pos, n);
                }

            if (update)
                for (int l = 0; l < numLfos; ++l)
                    if (! s.lfo[l].retrigger)
                        globalLfoPhase[l] += globalLfoHz (s, l, bpm) * controlInterval / rate;

            controlCountdown -= n;
            pos += n;
        }
    }

    ChorusEngine::Settings SynthEngine::chorusSettings (const Settings& s)
    {
        // A wide three-voice ensemble: what pads want from a chorus.
        ChorusEngine::Settings c;
        c.voices = 3;
        c.rateHz = 0.33f;
        c.depth = s.chorusDepth;
        c.delayMs = 11.0f;
        c.feedback = 0.0f;
        c.spread = 1.0f;
        c.tone = 9000.0f;
        c.width = 1.2f;
        c.mix = s.chorusMix;
        return c;
    }

    DelayEngine::Settings SynthEngine::delaySettings (const Settings& s)
    {
        // Ping-pong in notes, darker repeats, ducking under what's played.
        DelayEngine::Settings d;
        d.sync = true;
        d.division = s.delayDivision;
        d.mode = DelayEngine::Mode::pingPong;
        d.feedback = s.delayFeedback;
        d.lowCut = 250.0f;
        d.highCut = 5000.0f;
        d.duck = 0.3f;
        d.mix = s.delayMix;
        return d;
    }

    ReverbEngine::Settings SynthEngine::reverbSettings (const Settings& s)
    {
        // A smooth, modulated hall with the lows and the harsh top kept out. The mix is halved
        // as in C-Verb: a long tail on held chords soon outweighs them.
        ReverbEngine::Settings r;
        r.mix = s.reverbMix * 0.5f;
        r.size = s.reverbSize;
        r.decay = s.reverbDecay;
        r.preDelay = 30.0f;
        r.diffusion = 0.85f;
        r.modulation = 0.5f;
        r.highDecay = 0.55f;
        r.early = 0.25f;
        r.width = 1.0f;
        r.postLowCut = 150.0f;
        r.postHighCut = 9000.0f;
        return r;
    }

    void SynthEngine::runEffects (const Settings& s, int n, double bpm, const TempoSync::SongPosition& position)
    {
        // An effect switched on starts from silence (no stale tail from when it was last on).
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
            if (! effectsWereOn[2])
                reverb.reset();
            reverb.process (mix, 0, n, reverbSettings (s));
        }

        effectsWereOn[0] = s.chorusOn;
        effectsWereOn[1] = s.delayOn;
        effectsWereOn[2] = s.reverbOn;
    }

    void SynthEngine::process (juce::AudioBuffer<float>& buffer, int start, int n, const juce::MidiBuffer& midi, const Settings& s,
                               double bpm, const TempoSync::SongPosition& position)
    {
        if (n <= 0 || buffer.getNumChannels() == 0)
            return;

        ampCoefficients = EnvelopeCoefficients::from (s.ampEnv, rate);
        modCoefficients = EnvelopeCoefficients::from (s.modEnv, rate);

        // Free LFOs keep in step with the song (see LfoClock); synced ones follow the beats.
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

        for (int piece = 0; piece < n; piece += blockSize)
        {
            const int length = juce::jmin (blockSize, n - piece);
            mix.clear (0, length);

            // Render up to each MIDI event, then act on it.
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

            auto song = position;
            song.seconds += piece / rate;
            song.beats += piece / rate * bpm / 60.0;
            runEffects (s, length, bpm, song);

            // Master volume, out.
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
            scope.push (l, r, length);
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

    void SynthEngine::publish (const Settings& s)
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
            view.positionA.store (v.position[0], std::memory_order_relaxed);
            view.positionB.store (v.position[1], std::memory_order_relaxed);
            view.pan.store (juce::jlimit (-1.0f, 1.0f, v.destinations[index (Destination::pan)]), std::memory_order_relaxed);
            view.seconds.store ((float) v.seconds, std::memory_order_relaxed);
            view.releasedAt.store ((float) v.releasedAt, std::memory_order_relaxed);

            if (newest == nullptr || v.order > newest->order)
                newest = &v;
        }

        activeVoices.store (count, std::memory_order_relaxed);

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
