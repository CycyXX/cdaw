#pragma once

// Every DSP engine in Source/Shared/DSP with fixed settings, a fixed input and a fixed note
// script: shared by the null tests (DspNullTests.cpp) and the benchmarks (PerformanceTests.cpp).
// Each scenario switches to a second set of settings halfway, so changes made while running
// are covered too.

#include "PCH.h"
#include "Shared/DSP/ChorusEngine.h"
#include "Shared/DSP/CompressorEngine.h"
#include "Shared/DSP/DelayEngine.h"
#include "Shared/DSP/DistEngine.h"
#include "Shared/DSP/EsserEngine.h"
#include "Shared/DSP/FilterEngine.h"
#include "Shared/DSP/LeadEngine.h"
#include "Shared/DSP/MultibandEngine.h"
#include "Shared/DSP/PhaserEngine.h"
#include "Shared/DSP/ReverbEngine.h"
#include "Shared/DSP/SynthEngine.h"

namespace cdaw::test::dsp
{
    constexpr double bpm = 120.0, switchSeconds = 1.5;
    constexpr int maxBlock = 512;

    struct Block
    {
        juce::AudioBuffer<float>& buffer;
        int start, n;
        const juce::MidiBuffer& midi;
        TempoSync::SongPosition position;
        bool second;    // past the point where the settings change
    };

    using Process = std::function<void (const Block&)>;

    struct Scenario
    {
        juce::String name;
        bool instrument = false;
        double rate = 48000.0;
        std::function<Process (double rate)> create;
    };

    //==============================================================================
    inline void run (CompressorEngine& e, const Block& b, const CompressorEngine::Settings& s)  { e.process (b.buffer, b.start, b.n, s); }
    inline void run (MultibandEngine& e, const Block& b, const MultibandEngine::Settings& s)    { e.process (b.buffer, b.start, b.n, s); }
    inline void run (ReverbEngine& e, const Block& b, const ReverbEngine::Settings& s)          { e.process (b.buffer, b.start, b.n, s); }
    inline void run (EsserEngine& e, const Block& b, const EsserEngine::Settings& s)            { e.process (b.buffer, b.start, b.n, s); }
    inline void run (DistEngine& e, const Block& b, const DistEngine::Settings& s)              { e.process (b.buffer, b.start, b.n, s); }
    inline void run (DelayEngine& e, const Block& b, const DelayEngine::Settings& s)            { e.process (b.buffer, b.start, b.n, s, bpm); }
    inline void run (ChorusEngine& e, const Block& b, const ChorusEngine::Settings& s)          { e.process (b.buffer, b.start, b.n, s, b.position); }
    inline void run (PhaserEngine& e, const Block& b, const PhaserEngine::Settings& s)          { e.process (b.buffer, b.start, b.n, s, bpm, b.position); }
    inline void run (FilterEngine& e, const Block& b, const FilterEngine::Settings& s)          { e.process (b.buffer, b.start, b.n, s, bpm, b.position); }
    inline void run (SynthEngine& e, const Block& b, const SynthEngine::Settings& s)            { e.process (b.buffer, b.start, b.n, b.midi, s, bpm, b.position); }
    inline void run (LeadEngine& e, const Block& b, const LeadEngine::Settings& s)              { e.process (b.buffer, b.start, b.n, b.midi, s, bpm, b.position); }

    template <typename Engine, typename Settings>
    Scenario make (const juce::String& name, const Settings& a, const Settings& b, double rate = 48000.0)
    {
        Scenario s;
        s.name = name;
        s.instrument = std::is_same_v<Engine, SynthEngine> || std::is_same_v<Engine, LeadEngine>;
        s.rate = rate;
        s.create = [a, b] (double r) -> Process
        {
            auto engine = std::make_shared<Engine>();

            if constexpr (std::is_same_v<Engine, CompressorEngine>)
                engine->prepare (r);
            else if constexpr (std::is_same_v<Engine, ReverbEngine>)
            {
                engine->prepare (r, maxBlock, a);
                engine->jumpTo (a);
            }
            else
                engine->prepare (r, maxBlock);

            return [engine, a, b] (const Block& block) { run (*engine, block, block.second ? b : a); };
        };
        return s;
    }

    template <typename Engine, typename Settings>
    Scenario make (const juce::String& name, const Settings& a, double rate = 48000.0)
    {
        return make<Engine> (name, a, a, rate);
    }

    //==============================================================================
    inline std::vector<Scenario> compressorScenarios()
    {
        using E = CompressorEngine;
        std::vector<Scenario> list;
        E::Settings base;
        base.thresholdDb = -18.0f; base.ratio = 4.0f; base.attackMs = 5.0f; base.releaseMs = 80.0f;
        list.push_back (make<E> ("comp_default", base));

        auto makeUp = base;
        makeUp.makeUpDb = 6.0f;
        list.push_back (make<E> ("comp_makeup_change", base, makeUp));

        // Never reaches the threshold: the envelope stays at exactly 0.
        auto idle = base;
        idle.thresholdDb = 0.0f; idle.ratio = 2.0f; idle.kneeDb = 0.0f; idle.makeUpDb = 3.0f;
        list.push_back (make<E> ("comp_no_reduction", idle));

        auto limiter = base;
        limiter.ratio = 100.0f; limiter.attackMs = 0.5f;
        list.push_back (make<E> ("comp_limiter", limiter));

        // With a sidechain: the delayed input keys it.
        Scenario key;
        key.name = "comp_sidechain";
        key.create = [base] (double r) -> Process
        {
            auto engine = std::make_shared<E>();
            engine->prepare (r);
            auto keyBuffer = std::make_shared<std::vector<float>> ((size_t) maxBlock * 2);
            return [engine, base, keyBuffer] (const Block& block)
            {
                auto* in = block.buffer.getReadPointer (1, block.start);
                std::copy (in, in + block.n, keyBuffer->begin());
                engine->process (block.buffer, block.start, block.n, base, keyBuffer->data());
            };
        };
        list.push_back (key);
        return list;
    }

    inline std::vector<Scenario> multibandScenarios()
    {
        using E = MultibandEngine;
        std::vector<Scenario> list;
        E::Settings base;

        for (auto& band : base.bands)
        {
            band.on = true;
            band.threshold = -24.0f;
            band.ratio = 3.0f;
            band.attack = 8.0f;
            band.release = 120.0f;
        }

        list.push_back (make<E> ("multiband_all_on", base));

        auto soloed = base;
        soloed.bands[1].solo = true;
        list.push_back (make<E> ("multiband_solo_change", base, soloed));

        auto off = E::Settings();
        off.bands[2].on = true;
        off.bands[2].gain = 3.0f;
        off.output = -1.5f;
        list.push_back (make<E> ("multiband_one_band", off));

        auto quiet = base;
        for (auto& band : quiet.bands)
            band.threshold = 0.0f;
        list.push_back (make<E> ("multiband_no_reduction", quiet));
        return list;
    }

    inline std::vector<Scenario> reverbScenarios()
    {
        using E = ReverbEngine;
        std::vector<Scenario> list;
        E::Settings base;
        list.push_back (make<E> ("reverb_default", base));
        list.push_back (make<E> ("reverb_44k", base, 44100.0));

        auto big = base;
        big.size = 1.0f; big.decay = 8.0f; big.modulation = 1.0f; big.mix = 1.0f; big.early = 1.0f; big.width = 2.0f;
        big.preDelay = 120.0f; big.preLowCut = 80.0f; big.preHighCut = 12000.0f; big.diffusion = 1.0f;
        list.push_back (make<E> ("reverb_big_change", base, big));

        auto frozen = big;
        frozen.freeze = true;
        list.push_back (make<E> ("reverb_freeze", big, frozen));

        auto small = base;
        small.size = 0.0f; small.decay = 0.2f; small.early = 0.0f; small.modulation = 0.0f;
        list.push_back (make<E> ("reverb_short", small));
        return list;
    }

    inline std::vector<Scenario> delayScenarios()
    {
        using E = DelayEngine;
        std::vector<Scenario> list;
        E::Settings base;
        list.push_back (make<E> ("delay_default", base));

        auto free = base;
        free.sync = false; free.timeMs = 180.0f; free.spread = 0.4f; free.feedback = 0.7f;
        list.push_back (make<E> ("delay_free_change", base, free));

        auto ping = base;
        ping.mode = E::Mode::pingPong; ping.mix = 0.6f; ping.width = 1.6f;
        list.push_back (make<E> ("delay_pingpong", ping));

        auto tape = free;
        tape.drive = 0.7f; tape.lowCut = 200.0f; tape.highCut = 3000.0f;   // not wobble: its drift is random per run
        list.push_back (make<E> ("delay_tape", tape));

        auto ducked = base;
        ducked.duck = 0.8f; ducked.mix = 0.8f;
        list.push_back (make<E> ("delay_duck", ducked));
        return list;
    }

    inline std::vector<Scenario> chorusScenarios()
    {
        using E = ChorusEngine;
        std::vector<Scenario> list;
        E::Settings base;
        list.push_back (make<E> ("chorus_default", base));

        auto wide = base;
        wide.voices = 4; wide.rateHz = 1.7f; wide.depth = 0.9f; wide.feedback = 0.5f; wide.spread = 1.0f; wide.width = 1.5f;
        list.push_back (make<E> ("chorus_voices_change", base, wide));

        auto one = base;
        one.voices = 1; one.tone = 20000.0f; one.feedback = -0.6f;
        list.push_back (make<E> ("chorus_one_voice", one));
        return list;
    }

    inline std::vector<Scenario> phaserScenarios()
    {
        using E = PhaserEngine;
        std::vector<Scenario> list;
        E::Settings base;
        list.push_back (make<E> ("phaser_default", base));

        auto synced = base;
        synced.sync = true; synced.division = 9; synced.stages = 12; synced.feedback = -0.7f; synced.spread = 1.0f;
        list.push_back (make<E> ("phaser_sync_change", base, synced));
        return list;
    }

    inline std::vector<Scenario> filterScenarios()
    {
        using E = FilterEngine;
        std::vector<Scenario> list;
        E::Settings base;
        base.cutoff = 900.0f; base.resonance = 0.6f;
        list.push_back (make<E> ("filter_lowpass", base));

        auto moving = base;
        moving.steep = true; moving.lfoAmount = 0.5f; moving.lfoRate = 3.0f; moving.lfoStereo = 0.5f; moving.drive = 0.5f;
        list.push_back (make<E> ("filter_lfo_change", base, moving));

        auto env = base;
        env.type = E::Type::bandPass; env.envAmount = 0.8f; env.envRelease = 60.0f; env.mix = 0.8f; env.outputDb = 2.0f;
        list.push_back (make<E> ("filter_envelope", env));

        // (Not the random shape: it differs per run.)
        auto notch = base;
        notch.type = E::Type::notch; notch.lfoShape = E::Shape::rampUp; notch.lfoAmount = 0.3f; notch.lfoSync = true;
        list.push_back (make<E> ("filter_synced_ramp", notch));

        auto high = base;
        high.type = E::Type::highPass; high.lfoShape = E::Shape::square; high.lfoAmount = 0.2f;
        list.push_back (make<E> ("filter_highpass", high));
        return list;
    }

    inline std::vector<Scenario> esserScenarios()
    {
        using E = EsserEngine;
        std::vector<Scenario> list;
        E::Settings base;
        list.push_back (make<E> ("esser_split", base));

        auto wide = base;
        wide.mode = E::Mode::wideband;
        list.push_back (make<E> ("esser_wideband", wide));

        auto band = base;
        band.listen = E::Listen::band;
        list.push_back (make<E> ("esser_listen_band_change", base, band));

        auto removed = wide;
        removed.listen = E::Listen::removed;
        list.push_back (make<E> ("esser_listen_removed_change", wide, removed));

        auto automatic = base;
        automatic.autoThreshold = true; automatic.outputDb = 1.0f;
        list.push_back (make<E> ("esser_auto", automatic));

        // Quiet enough never to act.
        auto idle = base;
        idle.thresholdDb = 0.0f;
        list.push_back (make<E> ("esser_no_reduction", idle));
        return list;
    }

    inline std::vector<Scenario> distScenarios()
    {
        using E = DistEngine;
        std::vector<Scenario> list;

        for (int v = 0; v < E::numVoicings; ++v)
        {
            E::Settings s;
            s.voicing = (E::Voicing) v;
            s.drive = 0.6f;
            s.character = 0.4f;

            // The tone knobs move halfway (they must not change the level compensation)...
            auto tone = s;
            tone.bassDb = 4.0f; tone.midDb = -5.0f; tone.midHz = 1200.0f; tone.trebleDb = 3.0f; tone.smooth = 5000.0f;
            list.push_back (make<E> ("dist_voicing" + juce::String (v) + "_tone_change", s, tone));

            // ...and so does what does change it.
            auto drive = s;
            drive.drive = 0.9f; drive.tight = 200.0f; drive.character = 0.8f;
            list.push_back (make<E> ("dist_voicing" + juce::String (v) + "_drive_change", s, drive));
        }

        E::Settings gated;
        gated.gateOn = true; gated.gateThresholdDb = -30.0f; gated.blend = 0.7f; gated.levelDb = -3.0f;
        list.push_back (make<E> ("dist_gate_blend", gated));

        E::Settings voicingChange, voicingChange2;
        voicingChange2.voicing = E::Voicing::fuzz;
        list.push_back (make<E> ("dist_voicing_change", voicingChange, voicingChange2));
        return list;
    }

    inline std::vector<Scenario> synthScenarios()
    {
        using E = SynthEngine;
        std::vector<Scenario> list;
        E::Settings base;
        list.push_back (make<E> ("synth_default", base));

        auto dry = base;
        dry.chorusOn = dry.delayOn = dry.reverbOn = false;
        list.push_back (make<E> ("synth_dry", dry));

        // Effects switched on while running: their delay lines have to start clean.
        auto effects = dry;
        effects.chorusOn = effects.delayOn = effects.reverbOn = true;
        list.push_back (make<E> ("synth_effects_change", dry, effects));

        auto thick = dry;
        thick.osc[0].unison = 7; thick.osc[1].unison = 7; thick.osc[1].bank = 3; thick.osc[1].octave = 1;
        thick.subLevel = 0.5f; thick.noiseLevel = 0.2f; thick.noiseColour = -0.5f; thick.filterSteep = true;
        thick.filterType = FilterEngine::Type::bandPass; thick.resonance = 0.6f; thick.filterDrive = 0.5f;
        list.push_back (make<E> ("synth_unison_steep", thick));

        auto modulated = dry;
        modulated.slots[0] = { E::Source::lfo1, E::Destination::cutoff, 0.6f };
        modulated.slots[1] = { E::Source::modEnv, E::Destination::positionA, 0.5f };
        modulated.slots[2] = { E::Source::lfo2, E::Destination::pan, 0.5f };
        modulated.lfo[0].rate = 4.0f; modulated.lfo[1].sync = true; modulated.lfo[1].retrigger = true;
        list.push_back (make<E> ("synth_modulated", modulated));

        auto mono = dry;
        mono.mode = E::VoiceMode::legato; mono.glide = 0.08f;
        list.push_back (make<E> ("synth_legato", mono));
        return list;
    }

    inline std::vector<Scenario> leadScenarios()
    {
        using E = LeadEngine;
        std::vector<Scenario> list;
        E::Settings base;
        list.push_back (make<E> ("lead_default", base));

        auto dry = base;
        dry.chorusOn = dry.delayOn = dry.reverbOn = false;
        list.push_back (make<E> ("lead_dry", dry));

        auto effects = dry;
        effects.chorusOn = effects.delayOn = effects.reverbOn = true;
        list.push_back (make<E> ("lead_effects_change", dry, effects));

        for (auto [name, type] : { std::pair { "soft", E::DriveType::soft }, std::pair { "tube", E::DriveType::tube }, std::pair { "fold", E::DriveType::fold } })
        {
            auto driven = dry;
            driven.driveType = type;
            driven.drive = 0.6f;
            list.push_back (make<E> (juce::String ("lead_drive_") + name, driven));
        }

        auto moved = dry;
        moved.drive = 0.3f;
        auto moved2 = moved;
        moved2.drive = 0.9f;
        moved2.slots[0] = { E::Source::lfo1, E::Destination::drive, 0.5f };
        moved2.lfo[0].rate = 6.0f;
        list.push_back (make<E> ("lead_drive_change", moved, moved2));

        auto metal = dry;
        metal.distOn = true; metal.distDrive = 0.7f;
        list.push_back (make<E> ("lead_dist", metal));

        auto poly = dry;
        poly.mode = E::VoiceMode::poly; poly.osc[0].unison = 7; poly.osc[1].wave = E::Wave::pulse; poly.subLevel = 0.5f; poly.noiseLevel = 0.1f;
        list.push_back (make<E> ("lead_poly_unison", poly));

        auto harsh = dry;
        harsh.sync = true; harsh.fm = 0.4f; harsh.ring = 0.3f; harsh.filterType = E::FilterType::lowPass;
        list.push_back (make<E> ("lead_sync_fm_ring", harsh));
        return list;
    }

    inline std::vector<Scenario> allScenarios()
    {
        std::vector<Scenario> all;

        for (auto& group : { compressorScenarios(), multibandScenarios(), reverbScenarios(), delayScenarios(), chorusScenarios(),
                             phaserScenarios(), filterScenarios(), esserScenarios(), distScenarios(), synthScenarios(), leadScenarios() })
            all.insert (all.end(), group.begin(), group.end());

        return all;
    }

    //==============================================================================
    /** 2.5 s of sweeping saw in pulses with noise on top, then silence for the tails. */
    inline juce::AudioBuffer<float> makeInput (double rate, double seconds)
    {
        juce::AudioBuffer<float> input (2, (int) (seconds * rate));
        input.clear();
        juce::Random random (20261102);
        double phaseL = 0.0, phaseR = 0.0;
        const auto end = (int) (juce::jmin (seconds, 2.5) * rate);

        for (int i = 0; i < end; ++i)
        {
            const auto t = i / rate;
            const auto freq = 110.0 * std::pow (16.0, t / 2.5);
            phaseL += freq / rate;
            phaseR += freq * 1.004 / rate;
            phaseL -= std::floor (phaseL);
            phaseR -= std::floor (phaseR);

            const auto pulse = 0.5 + 0.5 * std::sin (juce::MathConstants<double>::twoPi * 4.0 * t);
            const auto noise = (double) random.nextFloat() * 2.0 - 1.0;
            input.setSample (0, i, (float) (pulse * (0.5 * (2.0 * phaseL - 1.0)) + 0.03 * noise));
            input.setSample (1, i, (float) (pulse * (0.5 * (2.0 * phaseR - 1.0)) - 0.03 * noise));
        }

        return input;
    }

    /** The note script for the instruments, for the samples [pos, pos + n). */
    inline void fillMidi (juce::MidiBuffer& midi, int pos, int n, double rate)
    {
        struct Event { double seconds; juce::MidiMessage message; };

        static const std::vector<Event> events {
            { 0.0,  juce::MidiMessage::noteOn (1, 48, 0.8f) },
            { 0.1,  juce::MidiMessage::noteOn (1, 60, 0.6f) },
            { 0.25, juce::MidiMessage::noteOn (1, 67, 1.0f) },
            { 0.5,  juce::MidiMessage::controllerEvent (1, 1, 64) },
            { 0.75, juce::MidiMessage::pitchWheel (1, 10000) },
            { 0.9,  juce::MidiMessage::channelPressureChange (1, 80) },
            { 1.0,  juce::MidiMessage::noteOff (1, 60) },
            { 1.0,  juce::MidiMessage::noteOn (1, 72, 0.7f) },
            { 1.5,  juce::MidiMessage::noteOff (1, 48) },
            { 1.5,  juce::MidiMessage::noteOff (1, 67) },
            { 1.6,  juce::MidiMessage::noteOn (1, 55, 0.9f) },
            { 2.2,  juce::MidiMessage::noteOff (1, 72) },
            { 2.2,  juce::MidiMessage::noteOff (1, 55) },
        };

        for (auto& e : events)
            if (auto sample = (int) (e.seconds * rate); sample >= pos && sample < pos + n)
                midi.addEvent (e.message, sample - pos);
    }

    /** Renders the scenario with irregular block sizes, as hosts do. With `gapSeconds`, the input
        or the notes stop and come back that much later, for engines that rest on silence. */
    inline juce::AudioBuffer<float> render (const Scenario& scenario, double seconds = 4.0, double gapSeconds = 0.0)
    {
        auto buffer = makeInput (scenario.rate, seconds);
        const auto gapEnd = (int) ((1.0 + gapSeconds) * scenario.rate);

        if (gapSeconds > 0.0)
        {
            // One second, the gap, then that second again.
            const auto oneSecond = (int) scenario.rate;
            const auto input = makeInput (scenario.rate, 1.0);
            buffer.clear();

            for (int ch = 0; ch < 2; ++ch)
            {
                buffer.copyFrom (ch, 0, input, ch, 0, oneSecond);
                buffer.copyFrom (ch, gapEnd, input, ch, 0, oneSecond);
            }
        }

        if (scenario.instrument)
            buffer.clear();

        auto process = scenario.create (scenario.rate);
        static constexpr int sizes[] { 64, 128, 256, 480, 333, 512, 97 };
        int index = 0;

        for (int pos = 0; pos < buffer.getNumSamples();)
        {
            const auto n = juce::jmin (sizes[index++ % (int) std::size (sizes)], buffer.getNumSamples() - pos);
            juce::MidiBuffer midi;

            if (scenario.instrument)
            {
                fillMidi (midi, pos, n, scenario.rate);

                if (gapSeconds > 0.0)
                    fillMidi (midi, pos - gapEnd, n, scenario.rate);
            }

            const auto now = pos / scenario.rate;
            process ({ buffer, pos, n, midi, { true, now, now * bpm / 60.0 }, now >= switchSeconds });
            pos += n;
        }

        return buffer;
    }
}
