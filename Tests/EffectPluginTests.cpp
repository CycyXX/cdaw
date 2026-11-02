#include "TestEngine.h"
#include "Plugins/ChorusPlugin.h"
#include "Plugins/DelayPlugin.h"
#include "Plugins/EsserPlugin.h"
#include "Plugins/FilterPlugin.h"
#include "Plugins/PhaserPlugin.h"
#include "Shared/Dist/DistParameters.h"
#include "Shared/Lead/LeadParameters.h"
#include "Shared/Synth/SynthParameters.h"
#include "Shared/Chorus/ChorusView.h"
#include "Shared/Delay/DelayView.h"
#include "Shared/Esser/EsserView.h"
#include "Shared/DSP/TempoSync.h"
#include "Shared/Filter/FilterView.h"
#include "Shared/Phaser/PhaserView.h"
#include "UI/LookAndFeel/CDAWLookAndFeel.h"
#include "UI/Plugins/SharedEffectEditor.h"

namespace cdaw::test
{
    /** C-Delay, C-Chorus, C-Phaser, C-Filter and C-Esser: their sound, their displays' maths, the VST3
        builds and the conversion of Tracktion's simple effects in old projects. */
    struct EffectPluginTests : public juce::UnitTest
    {
        EffectPluginTests() : juce::UnitTest ("Delay, chorus, phaser and filter", "CDAW") {}

        static te::TimeRange secs (double a, double b)
        {
            return { te::TimePosition::fromSeconds (a), te::TimePosition::fromSeconds (b) };
        }

        template <typename PluginType>
        static PluginType* insert (te::AudioTrack& track)
        {
            auto p = track.edit.getPluginCache().createNewPlugin (PluginType::xmlTypeName, {});
            track.pluginList.insertPlugin (p, 0, nullptr);
            return dynamic_cast<PluginType*> (p.get());
        }

        template <typename PluginType>
        static void set (PluginType& plugin, int id, float value)
        {
            plugin.parameter (id).setParameter (value, juce::sendNotification);
        }

        /** A WAV with one click at sample 10 on the chosen channels, then silence. */
        static juce::File writeClickFile (const juce::String& name, double seconds, bool left = true, bool right = true)
        {
            auto file = getTestDir().getChildFile (name + ".wav");
            file.deleteFile();
            juce::AudioBuffer<float> buffer (2, (int) (seconds * 48000.0));
            buffer.clear();
            if (left)  buffer.setSample (0, 10, 1.0f);
            if (right) buffer.setSample (1, 10, 1.0f);

            juce::WavAudioFormat wav;
            if (auto out = std::unique_ptr<juce::OutputStream> (file.createOutputStream()))
                if (auto writer = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (out.get(), 48000.0, 2, 24, {}, 0)))
                {
                    out.release();
                    writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
                }

            return file;
        }

        /** RMS in dB of a channel between two times. */
        static float rmsDb (const te::test_utilities::BufferAndSampleRate& r, double from, double to, int channel = 0)
        {
            auto s = (int) (from * r.sampleRate), e = juce::jmin (r.buffer.getNumSamples(), (int) (to * r.sampleRate));
            return juce::Decibels::gainToDecibels (r.buffer.getRMSLevel (channel, s, e - s), -150.0f);
        }

        /** The loudest sample (and where) within +-`window` samples of `around`. */
        static std::pair<float, int> peakNear (const te::test_utilities::BufferAndSampleRate& r, int channel, int around, int window = 200)
        {
            float best = 0.0f;
            int at = -1;
            for (int i = juce::jmax (0, around - window); i < juce::jmin (r.buffer.getNumSamples(), around + window); ++i)
                if (std::abs (r.buffer.getSample (channel, i)) > best)
                {
                    best = std::abs (r.buffer.getSample (channel, i));
                    at = i;
                }
            return { best, at };
        }

        /** Into CDAW_SNAPSHOT_DIR if set, else the test folder's "snapshots". */
        static void saveSnapshot (juce::Component& c, const juce::String& name)
        {
            auto chosen = juce::SystemStats::getEnvironmentVariable ("CDAW_SNAPSHOT_DIR", {});
            auto dir = chosen.isNotEmpty() ? juce::File (chosen) : getTestDir().getChildFile ("snapshots");
            dir.createDirectory();
            auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
            auto file = dir.getChildFile (name + ".png");
            file.deleteFile();
            juce::FileOutputStream out (file);
            juce::PNGImageFormat().writeImageToStream (img, out);
        }

        /** Renders a click through a C-Delay set up by `setUp`. */
        te::test_utilities::BufferAndSampleRate renderClickThroughDelay (std::function<void (DelayPlugin&)> setUp, double seconds = 2.0,
                                                                         bool left = true, bool right = true)
        {
            auto edit = te::test_utilities::createTestEdit (getEngine(), 1);
            edit->tempoSequence.getTempo (0)->setBpm (120.0);
            auto track = te::getAudioTracks (*edit)[0];
            track->insertWaveClip ("click", writeClickFile ("delay_click", seconds, left, right), { secs (0.0, seconds), {} }, false);

            auto delay = insert<DelayPlugin> (*track);
            using P = DelayParameters::ID;
            set (*delay, P::mix, 1.0f);
            set (*delay, P::lowCut, DelayEngine::lowCutOff);
            set (*delay, P::highCut, DelayEngine::highCutOff);
            set (*delay, P::sync, 0.0f);
            setUp (*delay);
            return te::test_utilities::renderToAudioBuffer (*edit);
        }

        /** Renders a sine (1 s, -12 dB) through what `setUp` puts on the track. */
        te::test_utilities::BufferAndSampleRate renderSine (double hz, std::function<void (te::AudioTrack&)> setUp, double seconds = 1.0, float amplitude = 0.25f)
        {
            auto edit = te::test_utilities::createTestEdit (getEngine(), 1);
            edit->tempoSequence.getTempo (0)->setBpm (120.0);
            auto track = te::getAudioTracks (*edit)[0];
            auto wav = writeSineFile ("fx_sine_" + juce::String ((int) hz) + "_" + juce::String (seconds) + "_" + juce::String (amplitude), seconds, amplitude, hz);
            track->insertWaveClip ("s", wav, { secs (0.0, seconds), {} }, false);
            setUp (*track);
            return te::test_utilities::renderToAudioBuffer (*edit);
        }

        /** The gain (dB) a set-up applies to a steady sine. */
        float gainThrough (double hz, std::function<void (te::AudioTrack&)> setUp, float amplitude = 0.25f)
        {
            auto dry = renderSine (hz, [] (te::AudioTrack&) {}, 1.0, amplitude);
            auto wet = renderSine (hz, setUp, 1.0, amplitude);
            return rmsDb (wet, 0.3, 0.9) - rmsDb (dry, 0.3, 0.9);
        }

        /** How much the level of a steady sine moves (dB, loudest minus quietest 20 ms window). */
        static float levelSwing (const te::test_utilities::BufferAndSampleRate& r, double from, double to, int channel = 0)
        {
            float lo = 1000.0f, hi = -1000.0f;
            for (double t = from; t + 0.02 <= to; t += 0.02)
            {
                auto db = rmsDb (r, t, t + 0.02, channel);
                lo = juce::jmin (lo, db);
                hi = juce::jmax (hi, db);
            }
            return hi - lo;
        }

        /** A VST3 from this build tree (built or not). */
        static juce::File findVst3 (const juce::String& folder, const juce::String& bundle)
        {
            // .../build/<preset>/Tests/CDAW_Tests_artefacts/<config>/CDAW_Tests.exe
            auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
            auto config = exe.getParentDirectory();
            auto preset = config.getParentDirectory().getParentDirectory().getParentDirectory();
            return preset.getChildFile ("VST3/" + folder + "/" + folder + "_artefacts").getChildFile (config.getFileName())
                         .getChildFile ("VST3/" + bundle + ".vst3");
        }

        struct Vst3Check
        {
            juce::String folder, name;
            const char* builtInType;
            const ParameterSpecs& specs;
            std::vector<std::pair<int, float>> changes;     // made on both before comparing again
            int editorWidth;
            std::pair<int, float> saved;                    // set, saved and restored in a second instance
        };

        /** The VST3 build sounds like the built-in plugin, opens its editor, exposes every
            parameter and restores its settings. Skipped when the VST3 hasn't been built. */
        void checkVst3 (const Vst3Check& c)
        {
            auto& engine = getEngine();
            auto bundle = findVst3 (c.folder, c.name);
            juce::OwnedArray<juce::PluginDescription> types;

            if (bundle.exists())
                for (auto* format : engine.getPluginManager().pluginFormatManager.getFormats())
                    if (format->getName() == "VST3")
                        format->findAllTypesForFile (types, bundle.getFullPathName());

            if (types.isEmpty())
            {
                logMessage (c.name + " VST3 not built (" + bundle.getFullPathName() + "), skipping");
                return;
            }

            expectEquals (types[0]->name, c.name);
            engine.getPluginManager().knownPluginList.addType (*types[0]);

            auto input = writeSineFile ("vst3_in", 2.0, 0.3f, 330.0);
            auto pad = writeSineFile ("vst3_pad", 2.0, 0.0f);

            auto findParameter = [] (juce::AudioPluginInstance& instance, const ParameterSpec& spec) -> juce::AudioProcessorParameter*
            {
                for (auto* param : instance.getParameters())
                    if (param->getName (64) == spec.name)
                        return param;
                return nullptr;
            };

            auto render = [&] (bool useVst3, bool change)
            {
                auto edit = te::test_utilities::createTestEdit (engine, 1);
                edit->tempoSequence.getTempo (0)->setBpm (120.0);
                auto track = te::getAudioTracks (*edit)[0];
                track->insertWaveClip ("in", input, { secs (0.0, 2.0), {} }, false);
                track->insertWaveClip ("pad", pad, { secs (2.0, 4.0), {} }, false);

                if (useVst3)
                {
                    auto plugin = edit->getPluginCache().createNewPlugin (te::ExternalPlugin::xmlTypeName, *types[0]);
                    track->pluginList.insertPlugin (plugin, 0, nullptr);
                    auto* instance = dynamic_cast<te::ExternalPlugin*> (plugin.get())->getAudioPluginInstance();
                    expect (instance != nullptr, "VST3 instance loads");

                    if (instance != nullptr && change)
                        for (auto& [id, value] : c.changes)
                            if (auto* param = findParameter (*instance, c.specs[(size_t) id]))
                                param->setValueNotifyingHost (c.specs[(size_t) id].getRange().convertTo0to1 (value));
                }
                else
                {
                    auto plugin = edit->getPluginCache().createNewPlugin (c.builtInType, {});
                    track->pluginList.insertPlugin (plugin, 0, nullptr);
                    auto* shared = dynamic_cast<SharedEffectPlugin*> (plugin.get());

                    if (change)
                        for (auto& [id, value] : c.changes)
                            shared->parameter (id).setParameter (value, juce::sendNotification);
                }

                return te::test_utilities::renderToAudioBuffer (*edit);
            };

            auto compare = [&] (bool change)
            {
                auto vst3 = render (true, change), builtIn = render (false, change);
                const auto start = (int) (0.1 * builtIn.sampleRate);
                const auto length = juce::jmin (vst3.buffer.getNumSamples(), builtIn.buffer.getNumSamples()) - start;
                expectGreaterThan (length, (int) builtIn.sampleRate, "both renders have audio");

                double signal = 0.0, difference = 0.0;
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = start; i < start + length; ++i)
                    {
                        auto a = builtIn.buffer.getSample (ch, i), b = vst3.buffer.getSample (ch, i);
                        signal += a * a;
                        difference += (a - b) * (a - b);
                    }

                return 10.0 * std::log10 (juce::jmax (1.0e-30, difference) / juce::jmax (1.0e-30, signal));
            };

            auto defaults = compare (false), changed = compare (true);
            logMessage ("VST3 vs built-in " + c.name + ", difference below the signal: defaults " + juce::String (defaults, 1)
                        + " dB, changed settings " + juce::String (changed, 1) + " dB");
            expectLessThan (defaults, -80.0, "the same audio with the same settings");
            expectLessThan (changed, -80.0, "settings sent through the host arrive with the first block");

            // What a host does besides playing: open the editor, save and restore the settings.
            juce::String error;
            auto& formats = engine.getPluginManager().pluginFormatManager;
            auto first = formats.createPluginInstance (*types[0], 48000.0, 512, error);
            auto second = formats.createPluginInstance (*types[0], 48000.0, 512, error);
            expect (first != nullptr && second != nullptr, "instances: " + error);

            if (first == nullptr || second == nullptr)
                return;

            if (auto* editor = first->createEditorIfNeeded())
            {
                expectEquals (editor->getWidth(), c.editorWidth, "the VST3 editor opens at the view's size");
                delete editor;
            }
            else
            {
                expect (false, "the VST3 has an editor");
            }

            // Every parameter, plus the Bypass switch VST3 hosts get from every plugin.
            expectEquals (first->getParameters().size(), (int) c.specs.size() + 1, "every parameter is automatable in the host");

            auto& spec = c.specs[(size_t) c.saved.first];
            if (auto* param = findParameter (*first, spec))
            {
                param->setValueNotifyingHost (spec.getRange().convertTo0to1 (c.saved.second));
                juce::MemoryBlock saved;
                first->getStateInformation (saved);
                second->setStateInformation (saved.getData(), (int) saved.getSize());

                if (auto* restored = findParameter (*second, spec))
                    expectWithinAbsoluteError (spec.getRange().convertFrom0to1 (restored->getValue()), c.saved.second,
                                               (spec.maximum - spec.minimum) * 0.001f, "settings restore");

                logMessage (juce::String (spec.name) + " shown by the host: " + param->getCurrentValueAsText());
            }
            else
            {
                expect (false, juce::String (spec.name) + " is a host parameter");
            }
        }

        void runTest() override
        {
            auto& engine = getEngine();
            using DP = DelayParameters::ID;
            using CP = ChorusParameters::ID;
            using PP = PhaserParameters::ID;
            using FP = FilterParameters::ID;
            using EP = EsserParameters::ID;
            using Type = FilterEngine::Type;

            // The click as it comes out of the render (resampled to the render's rate): where, how loud.
            auto dry = renderClickThroughDelay ([] (DelayPlugin& d) { set (d, DP::mix, 0.0f); });
            const auto [click, clickAt] = peakNear (dry, 0, 10, 20);

            beginTest ("C-Delay: echoes arrive on time and fade by the feedback");
            {
                auto r = renderClickThroughDelay ([] (DelayPlugin& d)
                {
                    set (d, DP::time, 250.0f);
                    set (d, DP::feedback, 0.5f);
                });

                const int step = (int) std::round (0.25 * r.sampleRate);
                auto [first, firstAt] = peakNear (r, 0, clickAt + step);
                auto [second, secondAt] = peakNear (r, 0, clickAt + 2 * step);
                auto [third, thirdAt] = peakNear (r, 0, clickAt + 3 * step);
                logMessage ("Click at " + juce::String (clickAt) + " (" + juce::String (click, 3) + "), echoes at " + juce::String (firstAt) + ", "
                            + juce::String (secondAt) + ", " + juce::String (thirdAt) + ": " + juce::String (first / click, 3) + ", "
                            + juce::String (second / click, 3) + ", " + juce::String (third / click, 3) + " of the click");

                expectWithinAbsoluteError (firstAt, clickAt + step, 1);
                expectWithinAbsoluteError (secondAt, clickAt + 2 * step, 1);
                expectWithinAbsoluteError (first / click, 1.0f, 0.02f);
                expectWithinAbsoluteError (second / click, 0.5f, 0.02f);
                expectWithinAbsoluteError (third / click, 0.25f, 0.02f);
                expectLessThan (std::abs (r.buffer.getSample (0, clickAt)), 1.0e-4f, "mix 100 % has no dry signal");
            }

            beginTest ("C-Delay: synced to the tempo, the display's echoes match the sound");
            {
                DelayEngine::Settings settings;
                auto r = renderClickThroughDelay ([this, &settings] (DelayPlugin& d)
                {
                    set (d, DP::sync, 1.0f);
                    set (d, DP::division, 9.0f);     // 1/4: half a second at 120 bpm
                    set (d, DP::feedback, 0.6f);
                    settings = d.getSettings();
                    expectWithinAbsoluteError (d.getBpmNow(), 120.0, 0.001);
                }, 3.0);

                const int quarter = (int) std::round (0.5 * r.sampleRate);
                expectWithinAbsoluteError (peakNear (r, 0, clickAt + quarter).second, clickAt + quarter, 1, "a quarter note at 120 bpm");

                auto echoes = DelayEngine::getEchoes (settings, 120.0, r.sampleRate);
                expect (echoes.size() >= 3);
                for (int i = 0; i < juce::jmin (3, (int) echoes.size()); ++i)
                {
                    auto& e = echoes[(size_t) i];
                    auto level = peakNear (r, 0, clickAt + (int) std::round (e.seconds * r.sampleRate)).first / click;
                    expectWithinAbsoluteError (juce::Decibels::gainToDecibels (level), e.gainDb, 0.3f, "echo " + juce::String (i + 1) + " as loud as drawn");
                }
            }

            beginTest ("C-Delay: ping-pong bounces from left to right");
            {
                auto r = renderClickThroughDelay ([] (DelayPlugin& d)
                {
                    set (d, DP::time, 200.0f);
                    set (d, DP::feedback, 0.5f);
                    set (d, DP::mode, 1.0f);
                });

                const int step = (int) std::round (0.2 * r.sampleRate);
                auto leftFirst = peakNear (r, 0, clickAt + step).first / click, rightFirst = peakNear (r, 1, clickAt + step).first / click;
                auto leftSecond = peakNear (r, 0, clickAt + 2 * step).first / click, rightSecond = peakNear (r, 1, clickAt + 2 * step).first / click;
                logMessage ("First echo L " + juce::String (leftFirst, 3) + " R " + juce::String (rightFirst, 3)
                            + ", second L " + juce::String (leftSecond, 3) + " R " + juce::String (rightSecond, 3));
                expectWithinAbsoluteError (leftFirst, 1.0f, 0.02f);
                expectLessThan (rightFirst, 0.01f);
                expectLessThan (leftSecond, 0.01f);
                expectWithinAbsoluteError (rightSecond, 0.5f, 0.02f);
            }

            beginTest ("C-Delay: mix 0 is dry, full feedback stays bounded, cuts darken the repeats");
            {
                expectGreaterThan (click, 0.5f, "the dry click comes through");
                expectLessThan (peakNear (dry, 0, clickAt + (int) (0.375 * dry.sampleRate), 2000).first, 1.0e-5f, "no echoes at mix 0");

                auto repeatsAfter3s = [&] (float drive)
                {
                    auto r = renderClickThroughDelay ([drive] (DelayPlugin& d)
                    {
                        set (d, DP::time, 50.0f);
                        set (d, DP::feedback, 1.0f);
                        set (d, DP::drive, drive);
                    }, 4.0);
                    return r.buffer.getMagnitude (0, (int) (3.0 * r.sampleRate), (int) (0.5 * r.sampleRate));
                };

                auto clean = repeatsAfter3s (0.0f), driven = repeatsAfter3s (1.0f);
                logMessage ("100 % feedback, 3 s on: peak " + juce::String (clean / click, 3) + " of the click clean, "
                            + juce::String (driven / click, 3) + " with full drive");
                expectWithinAbsoluteError (clean / click, 1.0f, 0.01f, "clean repeats go on forever, at the same level");
                expectGreaterThan (driven, 0.05f, "driven repeats still go on");
                expectLessThan (driven, clean, "but the drive wears them down");

                // A high cut in the loop: each repeat loses more of the top.
                auto bright = DelayEngine::Settings {};
                bright.lowCut = DelayEngine::lowCutOff;
                bright.highCut = 3000.0f;
                const double onePass = DelayEngine::getLoopFilterDb (bright, 8000.0, 48000.0);
                expectLessThan (onePass, -6.0, "8 kHz is cut by a 3 kHz high cut");
                expectWithinAbsoluteError (DelayEngine::getLoopFilterDb (bright, 200.0, 48000.0), 0.0, 0.1);
            }

            beginTest ("C-Chorus: a still voice is a delay, and the comb it makes is the one drawn");
            {
                ChorusEngine::Settings settings;
                auto comb = [&] (te::AudioTrack& t)
                {
                    auto c = insert<ChorusPlugin> (t);
                    set (*c, CP::voices, 1.0f);
                    set (*c, CP::depth, 0.0f);
                    set (*c, CP::delay, 1.0f);
                    set (*c, CP::feedback, 0.0f);
                    set (*c, CP::spread, 0.0f);
                    set (*c, CP::tone, ChorusEngine::toneOff);
                    set (*c, CP::mix, 0.5f);
                    settings = c->getSettings();
                };

                const float delays[ChorusEngine::maxVoices] { 1.0f, 1.0f, 1.0f, 1.0f };
                const double rate = renderSine (1000.0, [] (te::AudioTrack&) {}).sampleRate;
                for (double hz : { 500.0, 750.0, 1000.0, 1250.0 })
                {
                    auto measured = gainThrough (hz, comb);
                    auto predicted = (float) ChorusEngine::getResponseDb (settings, delays, hz, rate);
                    logMessage (juce::String (hz) + " Hz through a 1 ms voice at 50 %: " + juce::String (measured, 2) + " dB (drawn "
                                + juce::String (juce::jmax (-99.0f, predicted), 2) + " dB)");
                    if (predicted > -20.0f)
                        expectWithinAbsoluteError (measured, predicted, 0.5f);
                    else
                        expectLessThan (measured, -25.0f, "the notch");
                }

                auto dry = gainThrough (1000.0, [] (te::AudioTrack& t) { set (*insert<ChorusPlugin> (t), CP::mix, 0.0f); });
                expectWithinAbsoluteError (dry, 0.0f, 0.01f, "mix 0 = dry");
            }

            beginTest ("C-Chorus: voices move and spread the sound apart");
            {
                auto r = renderSine (2000.0, [] (te::AudioTrack& t)
                {
                    auto c = insert<ChorusPlugin> (t);
                    set (*c, CP::voices, 2.0f);
                    set (*c, CP::spread, 1.0f);
                    set (*c, CP::depth, 0.8f);
                    set (*c, CP::rate, 2.0f);
                    set (*c, CP::mix, 0.5f);
                });

                const int start = (int) (0.2 * r.sampleRate), n = (int) (0.7 * r.sampleRate);
                juce::AudioBuffer<float> difference (1, n);
                difference.copyFrom (0, 0, r.buffer, 0, start, n);
                difference.addFrom (0, 0, r.buffer, 1, start, n, -1.0f);
                const auto ratio = difference.getRMSLevel (0, 0, n) / juce::jmax (1.0e-9f, r.buffer.getRMSLevel (0, start, n));
                logMessage ("Two spread voices: left minus right is " + juce::String (ratio, 2) + " of the left");
                expectGreaterThan (ratio, 0.2f, "the sides differ");
                expectGreaterThan (levelSwing (r, 0.2, 0.9), 3.0f, "the comb moves, so the level of a tone does");
            }

            beginTest ("C-Phaser: with the sweep still, it sounds as the response is drawn; the LFO moves it");
            {
                PhaserEngine::Settings settings;
                auto still = [&] (te::AudioTrack& t)
                {
                    auto p = insert<PhaserPlugin> (t);
                    set (*p, PP::depth, 0.0f);
                    set (*p, PP::centre, 1000.0f);
                    set (*p, PP::stages, 4.0f);
                    set (*p, PP::feedback, 0.4f);
                    set (*p, PP::mix, 0.5f);
                    settings = p->getSettings();
                };

                const double rate = renderSine (1000.0, [] (te::AudioTrack&) {}).sampleRate;
                for (double hz : { 300.0, 1000.0, 2414.0, 6000.0 })
                {
                    auto measured = gainThrough (hz, still);
                    auto predicted = (float) PhaserEngine::getResponseDb (settings, 1000.0, hz, rate);
                    logMessage (juce::String (hz) + " Hz, 4 stages at 1 kHz: " + juce::String (measured, 2) + " dB (drawn " + juce::String (predicted, 2) + " dB)");
                    if (predicted > -20.0f)
                        expectWithinAbsoluteError (measured, predicted, 0.6f);
                    else
                        expectLessThan (measured, -18.0f, "the notch");
                }

                auto moving = renderSine (1000.0, [] (te::AudioTrack& t)
                {
                    auto p = insert<PhaserPlugin> (t);
                    set (*p, PP::depth, 0.8f);
                    set (*p, PP::rate, 2.0f);
                });
                auto swing = levelSwing (moving, 0.1, 0.95);
                logMessage ("1 kHz through a sweeping phaser: the level swings " + juce::String (swing, 1) + " dB");
                expectGreaterThan (swing, 10.0f);

                PhaserEngine::Settings synced;
                synced.sync = true;
                synced.division = 9;    // 1/4
                expectWithinAbsoluteError (PhaserEngine::getRateHz (synced, 120.0), 2.0, 1.0e-9, "a quarter note at 120 bpm is 2 Hz");
                expectEquals (TempoSync::nearest (0.375, 120.0), 8, "0.375 s is a dotted eighth at 120 bpm");
            }

            beginTest ("LFOs keep in step with the song");
            {
                TempoSync::LfoClock clock;
                const double rate = 48000.0;
                expect (! clock.shouldLock ({ false, 1.0, 2.0 }, 512, rate), "stopped: runs freely");
                expect (clock.shouldLock ({ true, 1.0, 2.0 }, 512, rate), "playback starts: picks up the song position");
                expect (! clock.shouldLock ({ true, 1.0 + 512 / rate, 2.0 }, 512, rate), "playing on: runs on");
                expect (clock.shouldLock ({ true, 5.0, 10.0 }, 512, rate), "a jump: picks it up again");

                expectWithinAbsoluteError (TempoSync::LfoClock::syncedPhaseAt ({ true, 0.0, 9.5 }, 9), 0.5, 1.0e-9, "a 1/4 LFO halfway through beat 10");
                expectWithinAbsoluteError (TempoSync::LfoClock::syncedPhaseAt ({ true, 0.0, 6.0 }, 15), 0.5, 1.0e-9, "a 1/1 LFO halfway through bar 2");
                expectWithinAbsoluteError (TempoSync::LfoClock::phaseAt ({ true, 2.25, 0.0 }, 2.0), 0.5, 1.0e-9, "2 Hz, 2.25 s in");
            }

            beginTest ("C-Filter: each type cuts where its curve says; resonance, drive, mix, output");
            {
                FilterEngine::Settings settings;
                auto filter = [&] (Type type, bool steep, float resonance)
                {
                    return [&settings, type, steep, resonance] (te::AudioTrack& t)
                    {
                        auto f = insert<FilterPlugin> (t);
                        set (*f, FP::type, (float) type);
                        set (*f, FP::slope, steep ? 1.0f : 0.0f);
                        set (*f, FP::cutoff, 1000.0f);
                        set (*f, FP::resonance, resonance);
                        settings = f->getSettings();
                    };
                };

                const double rate = renderSine (1000.0, [] (te::AudioTrack&) {}).sampleRate;
                struct Case { Type type; bool steep; float resonance; double hz; };
                for (auto c : { Case { Type::lowPass, false, 0.0f, 1000.0 }, Case { Type::lowPass, false, 0.0f, 4000.0 },
                                Case { Type::highPass, true, 0.0f, 250.0 }, Case { Type::highPass, true, 0.0f, 4000.0 },
                                Case { Type::bandPass, false, 0.5f, 1000.0 }, Case { Type::bandPass, true, 0.5f, 2000.0 },
                                Case { Type::notch, false, 0.0f, 1000.0 }, Case { Type::lowPass, false, 1.0f, 1000.0 } })
                {
                    // A full resonance peak would take a -12 dB tone past full scale: test it quieter.
                    auto measured = gainThrough (c.hz, filter (c.type, c.steep, c.resonance), c.resonance > 0.9f ? 0.02f : 0.25f);
                    auto predicted = (float) FilterEngine::getResponseDb (settings, 1000.0, c.hz, rate);
                    logMessage (FilterParameters::getTypeNames()[(int) c.type] + (c.steep ? " 24" : " 12") + " at 1 kHz, resonance "
                                + juce::String (c.resonance, 1) + ", " + juce::String (c.hz) + " Hz: " + juce::String (measured, 2)
                                + " dB (drawn " + juce::String (juce::jmax (-99.0f, predicted), 2) + " dB)");
                    if (predicted > -40.0f)
                        expectWithinAbsoluteError (measured, predicted, 0.3f);
                    else
                        expectLessThan (measured, -35.0f);
                }

                expectWithinAbsoluteError (FilterEngine::getResponseDb (settings, 1000.0, 1000.0, rate), 20.0 * std::log10 (FilterEngine::getQ (settings)), 0.1,
                                           "full resonance peaks by Q");

                auto dry = gainThrough (1000.0, [] (te::AudioTrack& t)
                {
                    auto f = insert<FilterPlugin> (t);
                    set (*f, FP::cutoff, 100.0f);
                    set (*f, FP::mix, 0.0f);
                    set (*f, FP::output, 6.0f);
                });
                expectWithinAbsoluteError (dry, 6.0f, 0.05f, "mix 0 passes the input, output adds its gain");

                auto driven = gainThrough (100.0, [] (te::AudioTrack& t)
                {
                    auto f = insert<FilterPlugin> (t);
                    set (*f, FP::cutoff, 20000.0f);
                    set (*f, FP::drive, 1.0f);
                });
                logMessage ("A -12 dB sine through full drive: " + juce::String (driven, 1) + " dB");
                expectLessThan (driven, -1.0f, "drive squashes loud signals");
            }

            beginTest ("C-Filter: the LFO and the envelope move the cutoff");
            {
                auto withEnvelope = [&] (float amount)
                {
                    return gainThrough (2000.0, [amount] (te::AudioTrack& t)
                    {
                        auto f = insert<FilterPlugin> (t);
                        set (*f, FP::cutoff, 200.0f);
                        set (*f, FP::envAmount, amount);
                    });
                };

                auto closed = withEnvelope (0.0f), opened = withEnvelope (1.0f);
                logMessage ("2 kHz into a 200 Hz low pass: " + juce::String (closed, 1) + " dB, with the envelope opening it " + juce::String (opened, 1) + " dB");
                expectGreaterThan (opened, closed + 15.0f);

                auto wobble = renderSine (2000.0, [] (te::AudioTrack& t)
                {
                    auto f = insert<FilterPlugin> (t);
                    set (*f, FP::cutoff, 1000.0f);
                    set (*f, FP::lfoAmount, 0.5f);
                    set (*f, FP::lfoRate, 2.0f);
                });
                auto swing = levelSwing (wobble, 0.1, 0.95);
                logMessage ("2 kHz through a 1 kHz low pass with the LFO at 2 octaves: the level swings " + juce::String (swing, 1) + " dB");
                expectGreaterThan (swing, 10.0f);

                for (int shape = 0; shape < (int) FilterEngine::Shape::numShapes; ++shape)
                    for (double phase : { 0.0, 0.13, 0.5, 0.77 })
                    {
                        auto v = FilterEngine::lfoValue ((FilterEngine::Shape) shape, phase, 0.3f);
                        expect (v >= -1.0f && v <= 1.0f, "LFO shapes stay within -1..1");
                    }
            }

            beginTest ("C-Esser: the band is turned down by the range above the threshold, left alone below it");
            {
                // A steady sine straight through the engine: the gain (dB) over its last half.
                auto through = [] (double hz, std::function<void (EsserEngine::Settings&)> change, float amplitude = 0.25f)
                {
                    EsserEngine::Settings s;
                    s.thresholdDb = -40.0f;
                    s.rangeDb = 12.0f;
                    change (s);

                    EsserEngine e;
                    e.prepare (48000.0, 4800);
                    juce::AudioBuffer<float> buffer (2, 48000);
                    for (int i = 0; i < buffer.getNumSamples(); ++i)
                        for (int ch = 0; ch < 2; ++ch)
                            buffer.setSample (ch, i, amplitude * (float) std::sin (juce::MathConstants<double>::twoPi * hz * i / 48000.0));

                    for (int start = 0; start < buffer.getNumSamples(); start += 480)
                        e.process (buffer, start, 480, s);

                    const float rms = buffer.getRMSLevel (0, 24000, 24000);
                    return juce::Decibels::gainToDecibels (rms / (amplitude * juce::MathConstants<float>::sqrt2 * 0.5f), -150.0f);
                };

                EsserEngine::Settings defaults;
                const auto centre = EsserEngine::getCentreHz (defaults);
                expectWithinAbsoluteError ((float) centre, 6325.0f, 1.0f, "the band's centre is between its edges");

                expectWithinAbsoluteError (through (centre, [] (auto& s) { s.thresholdDb = 0.0f; }), 0.0f, 0.01f, "below the threshold: untouched");
                expectWithinAbsoluteError (through (centre, [] (auto&) {}), -12.0f, 0.5f, "far above: turned down by the range");
                expectWithinAbsoluteError (through (centre, [] (auto& s) { s.rangeDb = 6.0f; }), -6.0f, 0.5f, "a smaller range reduces less");
                expectGreaterThan (through (500.0, [] (auto&) {}), -0.3f, "below the band: the detector doesn't hear it");
                expectWithinAbsoluteError (through (centre, [] (auto& s) { s.mode = EsserEngine::Mode::wideband; }), -12.0f, 0.5f, "wideband");

                // Only a little above the threshold: the soft 4:1 slope, not the range.
                const float level = through (centre, [] (auto& s) { s.listen = EsserEngine::Listen::band; s.thresholdDb = 0.0f; }) - 12.0f;
                const float slight = through (centre, [level] (auto& s) { s.thresholdDb = level - 4.0f; });
                expectWithinAbsoluteError (slight, -EsserEngine::getReductionDb ({}, level, level - 4.0f), 0.6f, "a soft 4:1 just above the threshold");
                expectGreaterThan (slight, -4.0f);

                expectWithinAbsoluteError (through (centre, [] (auto& s) { s.listen = EsserEngine::Listen::removed; }),
                                           juce::Decibels::gainToDecibels (1.0f - juce::Decibels::decibelsToGain (-12.0f)), 0.5f,
                                           "listening to what is removed");

                // Auto: the threshold sits below the programme's level, so a quiet sibilant is reduced as much as a loud one.
                const float autoLoud = through (centre, [] (auto& s) { s.autoThreshold = true; s.thresholdDb = 0.0f; });
                const float autoQuiet = through (centre, [] (auto& s) { s.autoThreshold = true; s.thresholdDb = 0.0f; }, 0.025f);
                expect (autoLoud < -3.0f && autoLoud > -11.0f, "auto reduces a sibilant: " + juce::String (autoLoud));
                expectWithinAbsoluteError (autoQuiet, autoLoud, 0.5f, "auto follows the level: 20 dB quieter, the same reduction");

                // The drawn response is what it does.
                EsserEngine::Settings split;
                expectWithinAbsoluteError ((float) EsserEngine::getResponseDb (split, 12.0f, centre, 48000.0), -12.0f, 0.01f);
                expectGreaterThan ((float) EsserEngine::getResponseDb (split, 12.0f, 500.0, 48000.0), -0.5f, "split: the lows stay");
                expectWithinAbsoluteError ((float) EsserEngine::getResponseDb (split, 0.0f, 2000.0, 48000.0), 0.0f, 0.001f, "flat when not reducing");

                // And the built-in plugin does it in an edit.
                const auto inEdit = gainThrough (centre, [] (te::AudioTrack& t)
                {
                    auto p = insert<EsserPlugin> (t);
                    set (*p, EP::threshold, -40.0f);
                    set (*p, EP::range, 9.0f);
                });
                expectWithinAbsoluteError (inEdit, -9.0f, 0.6f, "the built-in C-Esser");
            }

            beginTest ("The C-Delay VST3 sounds the same as the built-in C-Delay");
            checkVst3 ({ "CDelay", "C-Delay", DelayPlugin::xmlTypeName, DelayParameters::all(),
                         { { DP::sync, 0.0f }, { DP::time, 230.0f }, { DP::feedback, 0.6f }, { DP::mode, 1.0f }, { DP::lowCut, 300.0f },
                           { DP::highCut, 3000.0f }, { DP::drive, 0.3f }, { DP::duck, 0.5f }, { DP::spread, 0.4f }, { DP::width, 1.5f } },
                         DelayView::defaultWidth, { DP::feedback, 0.77f } });

            beginTest ("The C-Chorus VST3 sounds the same as the built-in C-Chorus");
            checkVst3 ({ "CChorus", "C-Chorus", ChorusPlugin::xmlTypeName, ChorusParameters::all(),
                         { { CP::voices, 3.0f }, { CP::rate, 1.3f }, { CP::depth, 0.6f }, { CP::feedback, 0.4f }, { CP::tone, 6000.0f }, { CP::mix, 0.7f } },
                         ChorusView::defaultWidth, { CP::depth, 0.33f } });

            beginTest ("The C-Phaser VST3 sounds the same as the built-in C-Phaser");
            checkVst3 ({ "CPhaser", "C-Phaser", PhaserPlugin::xmlTypeName, PhaserParameters::all(),
                         { { PP::stages, 10.0f }, { PP::rate, 0.8f }, { PP::feedback, -0.5f }, { PP::spread, 0.7f }, { PP::centre, 1500.0f } },
                         PhaserView::defaultWidth, { PP::centre, 2500.0f } });

            beginTest ("The C-Filter VST3 sounds the same as the built-in C-Filter");
            checkVst3 ({ "CFilter", "C-Filter", FilterPlugin::xmlTypeName, FilterParameters::all(),
                         { { FP::type, 2.0f }, { FP::slope, 1.0f }, { FP::cutoff, 700.0f }, { FP::resonance, 0.6f }, { FP::drive, 0.4f },
                           { FP::lfoAmount, 0.4f }, { FP::lfoSync, 1.0f }, { FP::lfoDivision, 6.0f }, { FP::lfoShape, 1.0f }, { FP::envAmount, 0.3f } },
                         FilterView::defaultWidth, { FP::cutoff, 3300.0f } });

            beginTest ("The C-Esser VST3 sounds the same as the built-in C-Esser");
            checkVst3 ({ "CEsser", "C-Esser", EsserPlugin::xmlTypeName, EsserParameters::all(),
                         { { EP::threshold, -50.0f }, { EP::lowfreq, 1000.0f }, { EP::highfreq, 2500.0f }, { EP::range, 8.0f },
                           { EP::release, 40.0f }, { EP::output, -2.0f } },
                         EsserView::defaultWidth, { EP::threshold, -33.0f } });

            beginTest ("Parameter IDs are unique and leave Tracktion's own plugin properties alone");
            {
                // Each parameter is stored as a property of the plugin's state, next to these.
                const juce::StringArray reserved { "type", "id", "uid", "enabled", "process", "frozen", "quickParamName", "customName",
                                                   "masterPluginID", "sidechainSourceID", "windowX", "windowY", "windowLocked",
                                                   "manufacturer", "name", "state", "filename", "programNum", "parameters" };

                for (auto* specs : { &DelayParameters::all(), &ChorusParameters::all(), &PhaserParameters::all(), &FilterParameters::all(),
                                     &SynthParameters::all(), &LeadParameters::all(), &DistParameters::all(), &EsserParameters::all() })
                {
                    juce::StringArray ids, names;
                    for (auto& spec : *specs)
                    {
                        expect (! reserved.contains (spec.id), juce::String (spec.id) + " is one of Tracktion's plugin properties");
                        expect (! ids.contains (spec.id), juce::String (spec.id) + " is used twice");
                        expect (! names.contains (spec.name), juce::String (spec.name) + " is a name used twice (hosts list parameters by name)");
                        names.add (spec.name);
                        expect (spec.defaultValue >= spec.minimum && spec.defaultValue <= spec.maximum, juce::String (spec.id) + "'s default is in range");
                        expectWithinAbsoluteError (spec.fromText (spec.toText (spec.defaultValue)), spec.defaultValue, (spec.maximum - spec.minimum) * 0.01f,
                                                   juce::String (spec.id) + "'s text (" + spec.toText (spec.defaultValue) + ") reads back");
                        ids.add (spec.id);
                    }
                }
            }

            beginTest ("Projects from earlier builds get C-Delay, C-Chorus, C-Phaser and C-Filter");
            {
                juce::ValueTree edit (te::IDs::EDIT), track (te::IDs::TRACK);
                edit.appendChild (track, nullptr);

                auto oldPlugin = [&track] (const char* type, std::initializer_list<std::pair<const char*, juce::var>> properties)
                {
                    juce::ValueTree p (te::IDs::PLUGIN);
                    p.setProperty (te::IDs::type, type, nullptr);
                    for (auto& [id, value] : properties)
                        p.setProperty (id, value, nullptr);
                    track.appendChild (p, nullptr);
                    return p;
                };

                auto delay = oldPlugin ("delay", { { "length", 300 }, { "feedback", -6.0f }, { "mix", 0.5f } });
                auto chorus = oldPlugin ("chorus", { { "depthMs", 3.5f }, { "speedHz", 0.8f }, { "width", 0.5f }, { "mixProportion", 0.5f } });
                auto phaser = oldPlugin ("phaser", { { "depth", 4.0f }, { "rate", 0.3f }, { "feedback", 0.6f } });
                auto lowPass = oldPlugin ("lowpass", { { "frequency", 2500.0f }, { "mode", "highpass" } });

                juce::ValueTree frequencyCurve (te::IDs::AUTOMATIONCURVE), otherCurve (te::IDs::AUTOMATIONCURVE);
                frequencyCurve.setProperty (te::IDs::paramID, "frequency", nullptr);
                lowPass.appendChild (frequencyCurve, nullptr);
                otherCurve.setProperty (te::IDs::paramID, "feedback", nullptr);
                delay.appendChild (otherCurve, nullptr);

                expectEquals (BuiltInPlugins::migrateEditState (edit), 4);
                expectEquals (delay[te::IDs::type].toString(), juce::String (DelayPlugin::xmlTypeName));
                expectEquals (chorus[te::IDs::type].toString(), juce::String (ChorusPlugin::xmlTypeName));
                expectEquals (phaser[te::IDs::type].toString(), juce::String (PhaserPlugin::xmlTypeName));
                expectEquals (lowPass[te::IDs::type].toString(), juce::String (FilterPlugin::xmlTypeName));
                expectEquals (delay.getNumChildren(), 0, "the delay's dB feedback automation is dropped");
                expectEquals (lowPass.getNumChildren(), 1, "the filter's frequency automation stays");

                auto e = te::test_utilities::createTestEdit (engine, 1);
                auto load = [&] (const char* type, const juce::ValueTree& state)
                {
                    auto p = e->getPluginCache().createNewPlugin (type, {});
                    p->restorePluginStateFromValueTree (state);
                    return p;
                };

                auto d = load (DelayPlugin::xmlTypeName, delay);
                auto ds = dynamic_cast<DelayPlugin*> (d.get())->getSettings();
                expect (! ds.sync);
                expectWithinAbsoluteError (ds.timeMs, 300.0f, 0.01f);
                expectWithinAbsoluteError (ds.feedback, 0.501f, 0.002f, "-6 dB feedback");
                expectWithinAbsoluteError (ds.mix, 0.5f, 0.001f, "an equal mix stays equal");
                expectEquals (ds.highCut, DelayEngine::highCutOff, "unfiltered, as before");

                auto c = load (ChorusPlugin::xmlTypeName, chorus);
                auto cs = dynamic_cast<ChorusPlugin*> (c.get())->getSettings();
                expectEquals (cs.voices, 1);
                expectWithinAbsoluteError (cs.delayMs, 20.0f, 0.01f);
                expectWithinAbsoluteError (cs.depth * ChorusEngine::maxModulationMs, 3.5f, 0.01f);
                expectWithinAbsoluteError (cs.rateHz, 0.8f, 0.001f);

                auto ph = load (PhaserPlugin::xmlTypeName, phaser);
                auto ps = dynamic_cast<PhaserPlugin*> (ph.get())->getSettings();
                expectEquals (ps.stages, 4);
                expectWithinAbsoluteError (ps.centreHz, 400.0f, 0.5f, "100 Hz up 4 octaves sweeps around 400 Hz");
                expectWithinAbsoluteError (ps.depth * PhaserEngine::maxOctaves, 2.0f, 0.01f);

                auto f = load (FilterPlugin::xmlTypeName, lowPass);
                auto fs = dynamic_cast<FilterPlugin*> (f.get())->getSettings();
                expect (fs.type == FilterEngine::Type::highPass);
                expect (! fs.steep);
                expectWithinAbsoluteError (fs.cutoff, 2500.0f, 0.01f);
                expectWithinAbsoluteError ((float) FilterEngine::getQ (fs), 0.7071f, 0.001f, "the same Butterworth response");
            }

            if (juce::SystemStats::getEnvironmentVariable ("CDAW_SNAPSHOTS", {}) != "1")
                return;

            beginTest ("Snapshots: C-Delay, C-Chorus, C-Phaser, C-Filter and C-Esser");
            {
                CDAWLookAndFeel laf;
                juce::LookAndFeel::setDefaultLookAndFeel (&laf);
                auto edit = te::test_utilities::createTestEdit (engine, 1);
                edit->tempoSequence.getTempo (0)->setBpm (120.0);
                auto track = te::getAudioTracks (*edit)[0];

                {
                    auto d = insert<DelayPlugin> (*track);
                    set (*d, DP::division, 8.0f);
                    set (*d, DP::feedback, 0.55f);
                    set (*d, DP::mode, 1.0f);
                    set (*d, DP::lowCut, 180.0f);
                    set (*d, DP::highCut, 4500.0f);
                    set (*d, DP::drive, 0.35f);
                    set (*d, DP::wobble, 0.3f);
                    set (*d, DP::duck, 0.4f);
                    d->engine.prepare (48000.0, 512);

                    SharedEffectEditor<DelayView, DelayEngine> editor (*d, d->engine);
                    editor.setSize (DelayView::defaultWidth, DelayView::defaultHeight);

                    // Hits every 0.75 s, the last one 375 ms ago: on the echo grid, so echoes light up.
                    for (int i = 0; i < 400; ++i)
                    {
                        const float t = std::fmod ((float) i / 60.0f + 0.475f, 0.75f);
                        const float level = t < 0.12f ? 0.8f * (1.0f - t / 0.12f) + 0.0005f : 0.0005f;
                        editor.getView().tick (level, level * 0.85f, level * 0.5f, level * 0.4f, level > 0.1f ? 8.0f : 1.0f);
                    }

                    saveSnapshot (editor, "c-delay");
                }

                {
                    auto c = insert<ChorusPlugin> (*track);
                    set (*c, CP::voices, 3.0f);
                    set (*c, CP::feedback, 0.25f);
                    c->engine.prepare (48000.0, 512);
                    SharedEffectEditor<ChorusView, ChorusEngine> editor (*c, c->engine);
                    editor.setSize (ChorusView::defaultWidth, ChorusView::defaultHeight);

                    const double phases[] { 0.12, 0.47, 0.81, 0.3 };
                    for (int v = 0; v < ChorusEngine::maxVoices; ++v)
                        c->engine.voicePhase[(size_t) v].store (phases[v]);

                    for (int i = 0; i < 30; ++i)
                        editor.getView().tick (0.4f, 0.3f);

                    saveSnapshot (editor, "c-chorus");
                }

                {
                    auto p = insert<PhaserPlugin> (*track);
                    set (*p, PP::stages, 8.0f);
                    set (*p, PP::rate, 0.5f);
                    set (*p, PP::feedback, 0.6f);
                    set (*p, PP::spread, 0.35f);
                    p->engine.prepare (48000.0, 512);
                    SharedEffectEditor<PhaserView, PhaserEngine> editor (*p, p->engine);
                    editor.setSize (PhaserView::defaultWidth, PhaserView::defaultHeight);

                    // Four seconds of sweep at 0.5 Hz, with sound coming and going.
                    for (int i = 0; i < 260; ++i)
                    {
                        p->engine.lfoPhase.store (std::fmod (i * 0.5 / 60.0, 1.0));
                        editor.getView().tick (0.3f, (i / 50) % 3 == 2 ? 0.01f : 0.3f);
                    }

                    saveSnapshot (editor, "c-phaser");
                }

                {
                    auto f = insert<FilterPlugin> (*track);
                    set (*f, FP::cutoff, 900.0f);
                    set (*f, FP::resonance, 0.55f);
                    set (*f, FP::slope, 1.0f);
                    set (*f, FP::lfoAmount, 0.35f);
                    set (*f, FP::lfoStereo, 0.4f);
                    set (*f, FP::envAmount, 0.25f);
                    f->engine.prepare (48000.0, 512);
                    SharedEffectEditor<FilterView, FilterEngine> editor (*f, f->engine);
                    editor.setSize (FilterView::defaultWidth, FilterView::defaultHeight);

                    // A 1 Hz LFO with hits pushing the envelope up now and then.
                    const auto s = f->getSettings();
                    for (int i = 0; i < 260; ++i)
                    {
                        const double phase = i / 60.0;
                        const float env = (i % 45) < 8 ? 1.0f - (float) (i % 45) / 8.0f : 0.0f;
                        auto at = [&] (double p) { return (float) FilterEngine::modulatedCutoff (s, FilterEngine::lfoValue (s.lfoShape, p), env, 48000.0); };
                        editor.getView().tick (0.3f, 0.25f, at (phase), at (phase + 0.2), env);
                    }

                    f->engine.outputFifo->active = true;
                    juce::Random rng (9);
                    juce::AudioBuffer<float> noise (1, 4096);
                    float b0 = 0, b1 = 0;
                    for (int block = 0; block < 12; ++block)
                    {
                        for (int i = 0; i < noise.getNumSamples(); ++i)
                        {
                            auto white = rng.nextFloat() * 2.0f - 1.0f;
                            b0 = 0.995f * b0 + white * 0.05f;
                            b1 = 0.6f * b1 + white * 0.3f;
                            noise.setSample (0, i, (b0 + b1) * 0.15f);
                        }
                        f->engine.outputFifo->push (noise);
                        editor.getView().updateSpectrum();
                    }

                    saveSnapshot (editor, "c-filter");
                }

                {
                    auto d = insert<EsserPlugin> (*track);
                    set (*d, EP::threshold, -30.0f);
                    set (*d, EP::range, 10.0f);
                    d->engine.prepare (48000.0, 512);
                    SharedEffectEditor<EsserView, EsserEngine> editor (*d, d->engine);
                    editor.setSize (EsserView::defaultWidth, EsserView::defaultHeight);

                    // Speech-like: the band's level wandering below the threshold, with an "s" now and then.
                    const auto s = d->getSettings();
                    for (int i = 0; i < 900; ++i)
                    {
                        const int t = i % 110;
                        const float levelDb = t < 22 ? -18.0f + 4.0f * std::sin ((float) t * 0.4f) : -44.0f + 8.0f * std::sin ((float) i * 0.13f);
                        editor.getView().tick (juce::Decibels::decibelsToGain (levelDb), s.thresholdDb,
                                               EsserEngine::getReductionDb (s, levelDb, s.thresholdDb));
                    }

                    saveSnapshot (editor, "c-esser");
                }

                juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
            }
        }
    };

    static EffectPluginTests effectPluginTests;
}
