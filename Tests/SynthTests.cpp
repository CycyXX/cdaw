#include "TestEngine.h"
#include "Plugins/SynthPlugin.h"
#include "Shared/Synth/SynthPresets.h"
#include "Shared/Synth/SynthView.h"
#include "UI/LookAndFeel/CDAWLookAndFeel.h"
#include "UI/Plugins/SharedEffectEditor.h"

namespace cdaw::test
{
    /** C-Synth: pitch, envelopes, band-limited wavetables, the filter, unison, the mod matrix,
        voices, presets, the Tracktion plugin and the VST3 build. */
    struct SynthTests : public juce::UnitTest
    {
        SynthTests() : juce::UnitTest ("C-Synth", "CDAW") {}

        using P = SynthParameters::ID;
        using Settings = SynthEngine::Settings;
        static constexpr double rate = 48000.0;

        struct Note { int key; double on, off; int velocity = 100; };

        /** A plain patch to measure with: one sine, no filter, no movement, no effects, flat envelope. */
        static Settings plain()
        {
            auto values = SynthPresets::valuesOf ({});
            auto set = [&values] (int id, float v) { values[(size_t) id] = v; };
            set (P::aPosition, 0.0f);
            set (P::aUnison, 1.0f);
            set (P::bOn, 0.0f);
            set (P::noiseLevel, 0.0f);
            set (P::drift, 0.0f);
            set (P::cutoff, 20000.0f);
            set (P::resonance, 0.0f);
            set (P::keyTrack, 0.0f);
            set (P::filterEnv, 0.0f);
            set (P::ampAttack, 0.001f);
            set (P::ampDecay, 0.001f);
            set (P::ampSustain, 1.0f);
            set (P::ampRelease, 0.05f);
            set (P::ampVelocity, 0.0f);
            set (P::chorusOn, 0.0f);
            set (P::reverbOn, 0.0f);
            set (P::delayOn, 0.0f);
            set (P::volume, 0.0f);
            for (int k = 0; k < SynthEngine::numSlots; ++k)
                set (SynthParameters::slot (k, 0), 0.0f);
            return SynthParameters::toSettings ([&values] (int id) { return values[(size_t) id]; });
        }

        /** Plays notes through an engine, block by block; `each` runs after every block. */
        static juce::AudioBuffer<float> play (SynthEngine& engine, const Settings& s, const std::vector<Note>& notes, double seconds,
                                              std::function<void (int block, juce::MidiBuffer&)> extra = {},
                                              std::function<void (int block)> each = {}, int blockSize = 512)
        {
            engine.prepare (rate, blockSize);
            const int total = (int) (seconds * rate);
            juce::AudioBuffer<float> out (2, total);
            out.clear();
            juce::MidiBuffer midi;

            for (int start = 0, block = 0; start < total; start += blockSize, ++block)
            {
                const int n = juce::jmin (blockSize, total - start);
                midi.clear();
                for (auto& note : notes)
                {
                    const int on = (int) (note.on * rate), off = (int) (note.off * rate);
                    if (on >= start && on < start + n)
                        midi.addEvent (juce::MidiMessage::noteOn (1, note.key, (juce::uint8) note.velocity), on - start);
                    if (off >= start && off < start + n)
                        midi.addEvent (juce::MidiMessage::noteOff (1, note.key), off - start);
                }

                if (extra)
                    extra (block, midi);

                engine.process (out, start, n, midi, s, 120.0);

                if (each)
                    each (block);
            }

            return out;
        }

        /** The frequency between two times, from upward zero crossings (interpolated). */
        static double frequency (const juce::AudioBuffer<float>& b, double from, double to, int channel = 0)
        {
            const auto* d = b.getReadPointer (channel);
            double first = -1.0, last = -1.0;
            int crossings = 0;

            for (int i = (int) (from * rate); i < (int) (to * rate); ++i)
                if (d[i - 1] < 0.0f && d[i] >= 0.0f)
                {
                    const double at = i - 1 + d[i - 1] / (d[i - 1] - d[i]);
                    if (first < 0.0) first = at;
                    else             ++crossings;
                    last = at;
                }

            return crossings > 0 ? crossings * rate / (last - first) : 0.0;
        }

        static float rmsDb (const juce::AudioBuffer<float>& b, double from, double to, int channel = 0)
        {
            const int s = (int) (from * rate), e = juce::jmin (b.getNumSamples(), (int) (to * rate));
            return juce::Decibels::gainToDecibels (b.getRMSLevel (channel, s, e - s), -150.0f);
        }

        /** The level (dB) of a frequency in a stretch of audio (Goertzel, Hann window). */
        static double toneDb (const juce::AudioBuffer<float>& b, double hz, double from, double to)
        {
            const int s = (int) (from * rate), n = (int) ((to - from) * rate);
            const double w = juce::MathConstants<double>::twoPi * hz / rate;
            double re = 0.0, im = 0.0;
            for (int i = 0; i < n; ++i)
            {
                const double window = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * i / (n - 1));
                const double x = b.getSample (0, s + i) * window;
                re += x * std::cos (w * i);
                im -= x * std::sin (w * i);
            }
            return 20.0 * std::log10 (juce::jmax (1.0e-12, std::sqrt (re * re + im * im) / (n * 0.25)));
        }

        static bool finite (const juce::AudioBuffer<float>& b)
        {
            for (int ch = 0; ch < b.getNumChannels(); ++ch)
                for (int i = 0; i < b.getNumSamples(); ++i)
                    if (! std::isfinite (b.getSample (ch, i)))
                        return false;
            return true;
        }

        /** The VST3 from this build tree: it plays a MIDI chord exactly like the built-in plugin,
            opens its editor, exposes every parameter and restores its settings. Skipped if not built. */
        void checkVst3()
        {
            auto& engine = getEngine();
            auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
            auto config = exe.getParentDirectory();
            auto preset = config.getParentDirectory().getParentDirectory().getParentDirectory();
            auto bundle = preset.getChildFile ("VST3/CSynth/CSynth_artefacts").getChildFile (config.getFileName()).getChildFile ("VST3/C-Synth.vst3");

            juce::OwnedArray<juce::PluginDescription> types;
            if (bundle.exists())
                for (auto* format : engine.getPluginManager().pluginFormatManager.getFormats())
                    if (format->getName() == "VST3")
                        format->findAllTypesForFile (types, bundle.getFullPathName());

            if (types.isEmpty())
            {
                logMessage ("C-Synth VST3 not built (" + bundle.getFullPathName() + "), skipping");
                return;
            }

            expectEquals (types[0]->name, juce::String ("C-Synth"));
            expect (types[0]->isInstrument, "an instrument");
            engine.getPluginManager().knownPluginList.addType (*types[0]);

            const auto& specs = SynthParameters::all();
            const std::vector<std::pair<int, float>> changes { { P::aBank, 1.0f }, { P::aPosition, 0.3f }, { P::cutoff, 1500.0f }, { P::delayOn, 1.0f },
                                                               { P::macro1, 0.7f }, { P::lfo2Retrigger, 1.0f }, { P::subLevel, 0.3f } };

            auto render = [&] (bool useVst3, bool change)
            {
                auto edit = te::test_utilities::createTestEdit (engine, 1);
                edit->tempoSequence.getTempo (0)->setBpm (120.0);
                auto track = te::getAudioTracks (*edit)[0];
                auto clip = track->insertMIDIClip ({ te::TimePosition::fromSeconds (0.0), te::TimePosition::fromSeconds (3.0) }, nullptr);
                for (auto [key, beat] : { std::pair (48, 0.0), std::pair (55, 0.5), std::pair (60, 1.0), std::pair (64, 1.5) })
                    clip->getSequence().addNote (key, te::BeatPosition::fromBeats (beat), te::BeatDuration::fromBeats (3.0), 100, 0, nullptr);

                if (useVst3)
                {
                    auto plugin = edit->getPluginCache().createNewPlugin (te::ExternalPlugin::xmlTypeName, *types[0]);
                    track->pluginList.insertPlugin (plugin, 0, nullptr);
                    auto* instance = dynamic_cast<te::ExternalPlugin*> (plugin.get())->getAudioPluginInstance();
                    expect (instance != nullptr, "VST3 instance loads");

                    if (instance != nullptr && change)
                        for (auto& [id, value] : changes)
                            for (auto* param : instance->getParameters())
                                if (param->getName (64) == specs[(size_t) id].name)
                                    param->setValueNotifyingHost (specs[(size_t) id].getRange().convertTo0to1 (value));
                }
                else
                {
                    auto plugin = edit->getPluginCache().createNewPlugin (SynthPlugin::xmlTypeName, {});
                    track->pluginList.insertPlugin (plugin, 0, nullptr);
                    if (change)
                        for (auto& [id, value] : changes)
                            dynamic_cast<SynthPlugin*> (plugin.get())->parameter (id).setParameter (value, juce::sendNotification);
                }

                return te::test_utilities::renderToAudioBuffer (*edit);
            };

            auto compare = [&] (bool change)
            {
                auto vst3 = render (true, change), builtIn = render (false, change);
                const auto length = juce::jmin (vst3.buffer.getNumSamples(), builtIn.buffer.getNumSamples());
                expectGreaterThan (length, (int) builtIn.sampleRate, "both renders have audio");

                double signal = 0.0, difference = 0.0;
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < length; ++i)
                    {
                        auto a = builtIn.buffer.getSample (ch, i), b = vst3.buffer.getSample (ch, i);
                        signal += a * a;
                        difference += (a - b) * (a - b);
                    }

                expectGreaterThan (signal, 1.0, "it plays");
                return 10.0 * std::log10 (juce::jmax (1.0e-30, difference) / juce::jmax (1.0e-30, signal));
            };

            auto defaults = compare (false), changed = compare (true);
            logMessage ("VST3 vs built-in C-Synth, difference below the signal: defaults " + juce::String (defaults, 1)
                        + " dB, changed settings " + juce::String (changed, 1) + " dB");
            expectLessThan (defaults, -80.0, "the same audio with the same settings");
            expectLessThan (changed, -80.0, "settings sent through the host arrive with the first block");

            juce::String error;
            auto& formats = engine.getPluginManager().pluginFormatManager;
            auto first = formats.createPluginInstance (*types[0], 48000.0, 512, error);
            auto second = formats.createPluginInstance (*types[0], 48000.0, 512, error);
            expect (first != nullptr && second != nullptr, "instances: " + error);
            if (first == nullptr || second == nullptr)
                return;

            if (auto* editor = first->createEditorIfNeeded())
            {
                expectEquals (editor->getWidth(), SynthView::defaultWidth, "the VST3 editor opens at the view's size");
                delete editor;
            }
            else
            {
                expect (false, "the VST3 has an editor");
            }

            // Every parameter is automatable in the host. (An instrument also gets JUCE's hidden MIDI CC
            // parameters: how a VST3 host's mod wheel, pitch bend and pedal reach it.)
            juce::StringArray hostNames;
            for (auto* param : first->getParameters())
                hostNames.add (param->getName (64));
            for (auto& spec : specs)
                expect (hostNames.contains (spec.name), juce::String (spec.name) + " is a host parameter");

            for (auto* param : first->getParameters())
                if (param->getName (64) == specs[P::cutoff].name)
                    param->setValueNotifyingHost (specs[P::cutoff].getRange().convertTo0to1 (3210.0f));

            juce::MemoryBlock saved;
            first->getStateInformation (saved);
            second->setStateInformation (saved.getData(), (int) saved.getSize());
            for (auto* param : second->getParameters())
                if (param->getName (64) == specs[P::cutoff].name)
                    expectWithinAbsoluteError (specs[P::cutoff].getRange().convertFrom0to1 (param->getValue()), 3210.0f, 1.0f, "settings restore");
        }

        void runTest() override
        {
            beginTest ("A sine plays at the note's pitch and level; octave and semitone shifts");
            {
                SynthEngine engine;
                auto s = plain();
                auto out = play (engine, s, { { 69, 0.0, 1.0 } }, 1.2);
                const auto hz = frequency (out, 0.2, 0.9);
                logMessage ("A4: " + juce::String (hz, 3) + " Hz, peak " + juce::String (out.getMagnitude (0, (int) (0.2 * rate), (int) (0.5 * rate)), 3));
                expectWithinAbsoluteError (hz, 440.0, 0.05);
                expectWithinAbsoluteError (out.getMagnitude (0, (int) (0.2 * rate), (int) (0.5 * rate)), 0.16f, 0.005f, "0.8 level through the voice headroom");
                expectWithinAbsoluteError (rmsDb (out, 1.3 - 0.1, 1.2), -150.0f, 1.0f, "released and silent");

                s.osc[0].octave = 1;
                s.osc[0].semitones = 7;
                expectWithinAbsoluteError (frequency (play (engine, s, { { 69, 0.0, 1.0 } }, 1.0), 0.2, 0.9), 440.0 * 2.0 * std::pow (2.0, 7.0 / 12.0), 0.2);
            }

            beginTest ("Envelopes: a linear attack, an exponential release reaching -60 dB at its time");
            {
                SynthEngine engine;
                auto s = plain();
                s.ampEnv.attack = 0.5f;
                s.ampEnv.release = 1.0f;
                auto out = play (engine, s, { { 57, 0.0, 1.0 } }, 2.2);

                const float full = out.getMagnitude (0, (int) (0.7 * rate), (int) (0.2 * rate));
                const float half = out.getMagnitude (0, (int) (0.24 * rate), (int) (0.02 * rate));
                const float released = rmsDb (out, 1.45, 1.55) - rmsDb (out, 0.7, 0.9);
                logMessage ("Halfway through the attack: " + juce::String (half / full, 3) + " of full; half the release on: "
                            + juce::String (released, 1) + " dB");
                expectWithinAbsoluteError (half / full, 0.5f, 0.03f);
                expectWithinAbsoluteError (released, -30.0f, 2.0f);
                expectWithinAbsoluteError (SynthEngine::envelopeAt (s.ampEnv, 0.25, -1.0), 0.5f, 0.001f, "the drawn envelope agrees");
                expectWithinAbsoluteError (juce::Decibels::gainToDecibels (SynthEngine::envelopeAt (s.ampEnv, 2.0, 1.0)), -60.0f, 0.5f);
            }

            beginTest ("Wavetables are band-limited: a high saw doesn't alias");
            {
                SynthEngine engine;
                auto s = plain();
                s.osc[0].position = 10.0f / 15.0f;      // the analog bank's saw frame
                auto out = play (engine, s, { { 100, 0.0, 1.0 } }, 1.0);      // E7, 2637 Hz

                // Blackman-Harris: its leakage is far below anything aliasing would leave.
                constexpr int order = 14, size = 1 << order;
                juce::dsp::FFT fft (order);
                juce::dsp::WindowingFunction<float> window ((size_t) size, juce::dsp::WindowingFunction<float>::blackmanHarris, false);
                std::vector<float> data ((size_t) size * 2, 0.0f);
                for (int i = 0; i < size; ++i)
                    data[(size_t) i] = out.getSample (0, (int) (0.3 * rate) + i);
                window.multiplyWithWindowingTable (data.data(), (size_t) size);
                fft.performFrequencyOnlyForwardTransform (data.data());

                const double f0 = 440.0 * std::pow (2.0, (100 - 69) / 12.0);
                double harmonic = 0.0, other = 0.0;
                for (int k = 2; k < size / 2; ++k)
                {
                    const double hz = k * rate / size;
                    const double nearest = std::round (hz / f0) * f0;
                    const double power = (double) data[(size_t) k] * data[(size_t) k];
                    (std::abs (hz - nearest) < 4.0 * rate / size ? harmonic : other) += power;
                }

                const auto aliasDb = 10.0 * std::log10 (other / harmonic);
                logMessage ("E7 saw: everything off the harmonics is " + juce::String (aliasDb, 1) + " dB");
                expectLessThan (aliasDb, -70.0);
                expectEquals (Wavetables::levelFor (f0, rate), 7, "eight harmonics fit below 0.45 x 48 kHz");
            }

            beginTest ("The filter cuts as its curve says; key tracking; unison spreads the voices");
            {
                SynthEngine engine;
                auto s = plain();
                s.osc[0].position = 10.0f / 15.0f;
                auto open = play (engine, s, { { 48, 0.0, 1.0 } }, 1.0);
                s.cutoff = 600.0f;
                s.filterSteep = true;
                auto cut = play (engine, s, { { 48, 0.0, 1.0 } }, 1.0);

                const double f0 = 440.0 * std::pow (2.0, (48 - 69) / 12.0);
                for (int h : { 2, 6, 12 })
                {
                    const auto measured = toneDb (cut, f0 * h, 0.3, 0.9) - toneDb (open, f0 * h, 0.3, 0.9);
                    const auto drawn = FilterEngine::getResponseDb (SynthEngine::filterSettings (s), 600.0, f0 * h, rate);
                    logMessage ("Harmonic " + juce::String (h) + " (" + juce::String (f0 * h, 0) + " Hz) through a 600 Hz 24 dB low pass: "
                                + juce::String (measured, 2) + " dB (drawn " + juce::String (drawn, 2) + ")");
                    expectWithinAbsoluteError (measured, drawn, 0.5);
                }

                // Full key tracking an octave above C4 is the same as a cutoff an octave higher.
                s.keyTrack = 1.0f;
                const double f72 = f0 * 4.0;
                auto tracked = toneDb (play (engine, s, { { 72, 0.0, 1.0 } }, 1.0), f72 * 3.0, 0.3, 0.9);
                s.keyTrack = 0.0f;
                s.cutoff = 1200.0f;
                auto moved = toneDb (play (engine, s, { { 72, 0.0, 1.0 } }, 1.0), f72 * 3.0, 0.3, 0.9);
                expectWithinAbsoluteError (tracked, moved, 0.2, "key tracking moves the cutoff with the note");

                auto spread = plain();
                spread.osc[0].unison = 5;
                spread.osc[0].detune = 0.5f;
                spread.osc[0].spread = 1.0f;
                auto wide = play (engine, spread, { { 60, 0.0, 1.0 } }, 1.0);
                auto narrow = play (engine, plain(), { { 60, 0.0, 1.0 } }, 1.0);

                auto sideRatio = [] (const juce::AudioBuffer<float>& b)
                {
                    double side = 0.0, mid = 0.0;
                    for (int i = (int) (0.2 * rate); i < (int) (0.9 * rate); ++i)
                    {
                        const double l = b.getSample (0, i), r = b.getSample (1, i);
                        side += (l - r) * (l - r);
                        mid += (l + r) * (l + r);
                    }
                    return std::sqrt (side / juce::jmax (1.0e-20, mid));
                };

                logMessage ("Side / mid: one voice " + juce::String (sideRatio (narrow), 4) + ", five spread " + juce::String (sideRatio (wide), 3));
                expectLessThan (sideRatio (narrow), 1.0e-6);
                expectGreaterThan (sideRatio (wide), 0.3);
            }

            beginTest ("The mod matrix: an LFO on pitch makes vibrato, a macro opens the filter");
            {
                SynthEngine engine;
                auto s = plain();
                s.lfo[0].rate = 4.0f;
                s.slots[0] = { SynthEngine::Source::lfo1, SynthEngine::Destination::pitch, 1.0f / 12.0f };   // +-1 semitone
                auto out = play (engine, s, { { 69, 0.0, 2.0 } }, 2.0);

                double lowest = 1.0e9, highest = 0.0;
                for (double t = 0.2; t + 0.02 < 1.9; t += 0.01)
                {
                    const auto hz = frequency (out, t, t + 0.02);
                    lowest = juce::jmin (lowest, hz);
                    highest = juce::jmax (highest, hz);
                }
                logMessage ("Vibrato from " + juce::String (lowest, 1) + " to " + juce::String (highest, 1) + " Hz");
                expectWithinAbsoluteError (highest / lowest, std::pow (2.0, 2.0 / 12.0), 0.01, "two semitones from bottom to top");

                auto macro = plain();
                macro.osc[0].position = 10.0f / 15.0f;
                macro.cutoff = 300.0f;
                macro.slots[0] = { SynthEngine::Source::macro2, SynthEngine::Destination::cutoff, 0.6f };      // up to 3 octaves
                const double f0 = 440.0 * std::pow (2.0, (48 - 69) / 12.0);
                auto closed = toneDb (play (engine, macro, { { 48, 0.0, 1.0 } }, 1.0), f0 * 12.0, 0.3, 0.9);
                macro.macros[1] = 1.0f;
                auto opened = toneDb (play (engine, macro, { { 48, 0.0, 1.0 } }, 1.0), f0 * 12.0, 0.3, 0.9);
                logMessage ("Harmonic 12 with the Brightness macro at 0 / 1: " + juce::String (closed, 1) + " / " + juce::String (opened, 1) + " dB");
                expectGreaterThan (opened, closed + 20.0);
            }

            beginTest ("Voices: polyphony is capped, the oldest are stolen, the pedal holds, legato glides");
            {
                SynthEngine engine;
                auto s = plain();
                s.polyphony = 16;
                std::vector<Note> cluster;
                for (int i = 0; i < 24; ++i)
                    cluster.push_back ({ 40 + i * 2, 0.01 * i, 2.0 });

                int most = 0;
                auto out = play (engine, s, cluster, 1.0, {}, [&] (int) { most = juce::jmax (most, engine.activeVoices.load()); });
                expectEquals (most, 16);
                expect (finite (out));
                bool newestPlaying = false, oldestGone = true;
                for (auto& v : engine.voiceViews)
                {
                    newestPlaying = newestPlaying || v.note.load() == 40 + 23 * 2;
                    oldestGone = oldestGone && v.note.load() != 40;
                }
                expect (newestPlaying && oldestGone, "the last note took the first one's voice");
                expectLessThan (out.getMagnitude (0, 0, out.getNumSamples()), 16.0f * 0.16f, "no more than 16 voices' worth");

                // The pedal: the key is released at 0.3 s, the pedal at 1 s.
                auto pedal = [] (int block, juce::MidiBuffer& midi)
                {
                    if (block == 0)                         midi.addEvent (juce::MidiMessage::controllerEvent (1, 64, 127), 0);
                    if (block == (int) (1.0 * rate) / 512)  midi.addEvent (juce::MidiMessage::controllerEvent (1, 64, 0), 0);
                };
                auto held = play (engine, s, { { 60, 0.05, 0.3 } }, 1.6, pedal);
                logMessage ("Pedal: " + juce::String (rmsDb (held, 0.8, 0.9), 1) + " dB while held after the key is up, "
                            + juce::String (rmsDb (held, 1.4, 1.5), 1) + " dB after the pedal");
                expectGreaterThan (rmsDb (held, 0.8, 0.9), -20.0f);
                expectLessThan (rmsDb (held, 1.4, 1.5), -100.0f);

                auto legato = plain();
                legato.mode = SynthEngine::VoiceMode::legato;
                legato.glide = 0.05f;
                int voicesInLegato = 0;
                auto glide = play (engine, legato, { { 57, 0.0, 1.0 }, { 69, 0.5, 1.0 } }, 1.0, {},
                                   [&] (int) { voicesInLegato = juce::jmax (voicesInLegato, engine.activeVoices.load()); });
                expectEquals (voicesInLegato, 1, "legato plays one voice");
                expectWithinAbsoluteError (frequency (glide, 0.2, 0.45), 220.0, 0.1);
                expectWithinAbsoluteError (frequency (glide, 0.8, 0.95), 440.0, 0.2, "it glided up to the second key");
                const auto during = frequency (glide, 0.5, 0.53);
                expect (during > 225.0 && during < 435.0, "on the way: " + juce::String (during, 1) + " Hz");
            }

            beginTest ("Renders repeat exactly, whatever the block size");
            {
                SynthEngine a, b;
                auto s = SynthParameters::toSettings ([] (int id) { return SynthParameters::get (id).defaultValue; });
                const std::vector<Note> chord { { 48, 0.0, 1.5 }, { 55, 0.1, 1.5 }, { 60, 0.2, 1.5 }, { 64, 0.3, 1.5 } };
                auto first = play (a, s, chord, 2.5, {}, {}, 512);
                auto second = play (b, s, chord, 2.5, {}, {}, 160);

                double signal = 0.0, difference = 0.0;
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < first.getNumSamples(); ++i)
                    {
                        signal += juce::square (first.getSample (ch, i));
                        difference += juce::square (first.getSample (ch, i) - second.getSample (ch, i));
                    }
                const auto db = 10.0 * std::log10 (juce::jmax (1.0e-30, difference) / signal);
                logMessage ("The default pad in blocks of 512 and 160: difference " + juce::String (db, 1) + " dB");
                expectLessThan (db, -100.0);

                // Wide, but it still holds together in mono: the sides stay more alike than not.
                double lr = 0.0, ll = 0.0, rr = 0.0;
                for (int i = (int) (0.5 * rate); i < (int) (2.5 * rate); ++i)
                {
                    const double l = first.getSample (0, i), r = first.getSample (1, i);
                    lr += l * r;
                    ll += l * l;
                    rr += r * r;
                }
                const auto correlation = lr / std::sqrt (ll * rr);
                logMessage ("The default pad's left / right correlation: " + juce::String (correlation, 2));
                expectGreaterThan (correlation, 0.1);
            }

            beginTest ("Every factory preset loads and plays a chord within range");
            {
                const std::vector<Note> chord { { 48, 0.0, 2.0, 110 }, { 52, 0.0, 2.0, 110 }, { 55, 0.0, 2.0, 110 }, { 59, 0.0, 2.0, 110 }, { 64, 0.0, 2.0, 110 } };
                juce::StringArray names;

                for (auto& preset : SynthPresets::factory())
                {
                    expect (! names.contains (preset.name), preset.name + " twice");
                    names.add (preset.name);
                    expectEquals (preset.macroNames.size(), SynthEngine::numMacros);

                    const auto values = SynthPresets::valuesOf (preset);
                    auto s = SynthParameters::toSettings ([&values] (int id) { return values[(size_t) id]; });
                    SynthEngine engine;
                    auto out = play (engine, s, chord, 3.0);
                    const auto peak = out.getMagnitude (0, out.getNumSamples());
                    const auto level = juce::jmax (rmsDb (out, 0.0, 3.0, 0), rmsDb (out, 0.0, 3.0, 1));
                    logMessage (preset.category + " / " + preset.name + ": peak " + juce::String (juce::Decibels::gainToDecibels (peak), 1)
                                + " dB, level " + juce::String (level, 1) + " dB");
                    expect (finite (out), preset.name + " stays finite");
                    expectLessThan (peak, 1.0f, preset.name + " stays below full scale");
                    expectGreaterThan (level, -45.0f, preset.name + " is audible");
                }

                expectGreaterThan ((int) SynthPresets::factory().size(), 18);
            }

            beginTest ("User presets save and load");
            {
                auto folder = getTestDir().getChildFile ("synth_presets");
                folder.deleteRecursively();
                SynthPresets::setUserFolder (folder);

                auto values = SynthPresets::valuesOf (SynthPresets::factory()[3]);
                values[(size_t) P::cutoff] = 1234.0f;
                const juce::StringArray macros { "One", "Two", "Three", "Four" };
                expect (SynthPresets::saveUser ("My Pad", [&values] (int id) { return values[(size_t) id]; }, macros).wasOk());

                auto loaded = SynthPresets::loadUser();
                expectEquals ((int) loaded.size(), 1);
                if (! loaded.empty())
                {
                    expectEquals (loaded[0].name, juce::String ("My Pad"));
                    expect (loaded[0].isUser());
                    expectEquals (loaded[0].macroNames[2], juce::String ("Three"));
                    auto back = SynthPresets::valuesOf (loaded[0]);
                    for (int i = 0; i < P::count; ++i)
                        expectWithinAbsoluteError (back[(size_t) i], values[(size_t) i], 1.0e-4f, SynthParameters::get (i).id);
                }

                expect (SynthPresets::saveUser ("  ", [&values] (int id) { return values[(size_t) id]; }, macros).failed(), "a preset needs a name");
                expectEquals ((int) SynthPresets::everything().size(), (int) SynthPresets::factory().size() + 1);
                SynthPresets::setUserFolder ({});
            }

            beginTest ("C-Synth in Tracktion plays a MIDI clip");
            {
                auto edit = te::test_utilities::createTestEdit (getEngine(), 1);
                edit->tempoSequence.getTempo (0)->setBpm (120.0);
                auto track = te::getAudioTracks (*edit)[0];
                auto plugin = edit->getPluginCache().createNewPlugin (SynthPlugin::xmlTypeName, {});
                track->pluginList.insertPlugin (plugin, 0, nullptr);
                auto* synth = dynamic_cast<SynthPlugin*> (plugin.get());
                expect (synth != nullptr && synth->isSynth() && synth->takesMidiInput());

                // The plain patch, through the plugin's parameters.
                for (auto [id, v] : std::initializer_list<std::pair<int, float>> {
                         { P::aPosition, 0.0f }, { P::aUnison, 1.0f }, { P::bOn, 0.0f }, { P::noiseLevel, 0.0f }, { P::drift, 0.0f },
                         { P::cutoff, 20000.0f }, { P::resonance, 0.0f }, { P::keyTrack, 0.0f }, { P::filterEnv, 0.0f },
                         { P::ampAttack, 0.001f }, { P::ampDecay, 0.001f }, { P::ampSustain, 1.0f }, { P::ampRelease, 0.05f }, { P::ampVelocity, 0.0f },
                         { P::chorusOn, 0.0f }, { P::reverbOn, 0.0f }, { P::volume, 0.0f } })
                    synth->parameter (id).setParameter (v, juce::sendNotification);
                for (int k = 0; k < SynthEngine::numSlots; ++k)
                    synth->parameter (SynthParameters::slot (k, 0)).setParameter (0.0f, juce::sendNotification);

                auto clip = track->insertMIDIClip ({ te::TimePosition::fromSeconds (0.0), te::TimePosition::fromSeconds (2.0) }, nullptr);
                clip->getSequence().addNote (69, te::BeatPosition::fromBeats (1.0), te::BeatDuration::fromBeats (2.0), 100, 0, nullptr);     // 0.5 s to 1.5 s

                auto r = te::test_utilities::renderToAudioBuffer (*edit);
                auto onset = -1;
                for (int i = 0; i < r.buffer.getNumSamples() && onset < 0; ++i)
                    if (std::abs (r.buffer.getSample (0, i)) > 0.01f)
                        onset = i;

                logMessage ("Rendered at " + juce::String (r.sampleRate) + " Hz: the note starts at " + juce::String (onset / r.sampleRate, 4) + " s");
                expectWithinAbsoluteError (onset / r.sampleRate, 0.5, 0.002, "on the beat");

                double first = -1.0, last = -1.0;
                int crossings = 0;
                for (int i = (int) (0.7 * r.sampleRate); i < (int) (1.3 * r.sampleRate); ++i)
                {
                    const float a = r.buffer.getSample (0, i - 1), b = r.buffer.getSample (0, i);
                    if (a < 0.0f && b >= 0.0f)
                    {
                        const double at = i - 1 + a / (a - b);
                        if (first < 0.0) first = at; else ++crossings;
                        last = at;
                    }
                }
                expectWithinAbsoluteError (crossings * r.sampleRate / (last - first), 440.0, 0.1);
                expectLessThan (r.buffer.getMagnitude (0, (int) (1.8 * r.sampleRate), (int) (0.1 * r.sampleRate)), 1.0e-4f, "and it stops");
                expect (synth->getTailLength() >= 0.05);
            }

            beginTest ("The C-Synth VST3 plays the same as the built-in C-Synth");
            checkVst3();

            if (juce::SystemStats::getEnvironmentVariable ("CDAW_SNAPSHOTS", {}) != "1")
                return;

            beginTest ("Snapshots: C-Synth's pages");
            {
                CDAWLookAndFeel laf;
                juce::LookAndFeel::setDefaultLookAndFeel (&laf);
                auto edit = te::test_utilities::createTestEdit (getEngine(), 1);
                edit->tempoSequence.getTempo (0)->setBpm (120.0);
                auto track = te::getAudioTracks (*edit)[0];
                auto plugin = edit->getPluginCache().createNewPlugin (SynthPlugin::xmlTypeName, {});
                track->pluginList.insertPlugin (plugin, 0, nullptr);
                auto* synth = dynamic_cast<SynthPlugin*> (plugin.get());
                synth->parameter (P::delayOn).setParameter (1.0f, juce::sendNotification);
                synth->parameter (P::macro2).setParameter (0.4f, juce::sendNotification);

                SharedEffectEditor<SynthView, SynthEngine> editor (*synth, synth->engine);
                editor.setSize (SynthView::defaultWidth, SynthView::defaultHeight);
                auto& view = editor.getView();

                // A chord held for a while, one note already released, playing through the engine.
                synth->engine.prepare (48000.0, 512);
                juce::AudioBuffer<float> buffer (2, 512);
                juce::MidiBuffer midi;
                const auto settings = synth->getSettings();
                for (int block = 0; block < 300; ++block)
                {
                    midi.clear();
                    if (block == 0)
                        for (int note : { 48, 55, 60, 64, 71 })
                            midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) (80 + note % 30)), 0);
                    if (block == 240)
                        midi.addEvent (juce::MidiMessage::noteOff (1, 71), 0);

                    synth->engine.process (buffer, 0, 512, midi, settings, 120.0);
                    view.getStereoField().add (buffer.getReadPointer (0), buffer.getReadPointer (1), 512);
                    synth->engine.outputFifo->active = true;
                    if (block % 3 == 0)
                        view.tick();
                }

                auto dir = juce::SystemStats::getEnvironmentVariable ("CDAW_SNAPSHOT_DIR", {});
                auto folder = dir.isNotEmpty() ? juce::File (dir) : getTestDir().getChildFile ("snapshots");
                folder.createDirectory();

                for (auto [page, name] : { std::pair (0, "c-synth"), std::pair (1, "c-synth-mod"), std::pair (2, "c-synth-fx") })
                {
                    view.showPage (page);
                    for (int i = 0; i < 20; ++i)
                        view.tick();

                    auto image = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f);
                    auto file = folder.getChildFile (juce::String (name) + ".png");
                    file.deleteFile();
                    juce::FileOutputStream out (file);
                    juce::PNGImageFormat().writeImageToStream (image, out);
                }

                juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
            }
        }
    };

    static SynthTests synthTests;
}
