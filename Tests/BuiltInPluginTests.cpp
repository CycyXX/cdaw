#include "TestEngine.h"
#include "Engine/MasterAnalyser.h"
#include "Model/MixerOps.h"
#include "Plugins/BuiltInPlugins.h"
#include "Plugins/CompressorPlugin.h"
#include "Plugins/EqPlugin.h"
#include "Plugins/MultibandPlugin.h"
#include "Plugins/ReverbPlugin.h"
#include "Plugins/SimpleReverbPlugin.h"
#include "Shared/CVerb/CVerbView.h"
#include "UI/LookAndFeel/CDAWLookAndFeel.h"
#include "UI/Plugins/CompressorEditor.h"
#include "UI/Plugins/EqEditor.h"
#include "UI/Plugins/MultibandEditor.h"
#include "UI/Plugins/ReverbEditor.h"
#include "UI/Plugins/SimpleReverbEditor.h"

namespace cdaw::test
{
    struct BuiltInPluginTests : public juce::UnitTest
    {
        BuiltInPluginTests() : juce::UnitTest ("Compressor and EQ", "CDAW") {}

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

        /** RMS in dB of channel 0 between two times. */
        static float rmsDb (const te::test_utilities::BufferAndSampleRate& r, double from, double to)
        {
            auto s = (int) (from * r.sampleRate), e = juce::jmin (r.buffer.getNumSamples(), (int) (to * r.sampleRate));
            return juce::Decibels::gainToDecibels (r.buffer.getRMSLevel (0, s, e - s), -150.0f);
        }

        /** Renders a sine through the track's plugins and returns the gain change in dB. */
        float gainThrough (te::Engine& engine, double hz, std::function<void (te::AudioTrack&)> setUp)
        {
            auto edit = te::test_utilities::createTestEdit (engine, 1);
            auto track = te::getAudioTracks (*edit)[0];
            auto wav = writeSineFile ("eq_sine_" + juce::String ((int) hz), 1.0, 0.25f, hz);
            track->insertWaveClip ("s", wav, { secs (0.0, 1.0), {} }, false);

            auto dry = te::test_utilities::renderToAudioBuffer (*edit);
            setUp (*track);
            auto wet = te::test_utilities::renderToAudioBuffer (*edit);
            return rmsDb (wet, 0.3, 0.9) - rmsDb (dry, 0.3, 0.9);
        }

        /** A WAV with one click at the start, then silence. */
        static juce::File writeImpulseFile (double seconds)
        {
            auto file = getTestDir().getChildFile ("impulse.wav");
            file.deleteFile();
            juce::AudioBuffer<float> buffer (2, (int) (seconds * 48000.0));
            buffer.clear();
            buffer.setSample (0, 10, 1.0f);
            buffer.setSample (1, 10, 1.0f);

            juce::WavAudioFormat wav;
            if (auto out = std::unique_ptr<juce::OutputStream> (file.createOutputStream()))
                if (auto writer = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (out.get(), 48000.0, 2, 24, {}, 0)))
                {
                    out.release();
                    writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
                }

            return file;
        }

        /** Decay rate (dB per second) between two times: a straight-line fit to 50 ms energies. */
        static double decaySlope (const te::test_utilities::BufferAndSampleRate& r, double from, double to)
        {
            std::vector<std::pair<double, double>> points;
            const int window = (int) (0.05 * r.sampleRate);

            for (double t = from; t + 0.05 <= to; t += 0.05)
            {
                auto start = (int) (t * r.sampleRate);
                auto rms = juce::jmax (r.buffer.getRMSLevel (0, start, window), r.buffer.getRMSLevel (1, start, window));
                points.push_back ({ t, 20.0 * std::log10 (juce::jmax (1.0e-9f, rms)) });
            }

            double mx = 0, my = 0;
            for (auto& [x, y] : points) { mx += x; my += y; }
            mx /= (double) points.size();
            my /= (double) points.size();

            double num = 0, den = 0;
            for (auto& [x, y] : points) { num += (x - mx) * (y - my); den += (x - mx) * (x - mx); }
            return num / den;
        }

        static ReverbPlugin* plainReverb (te::AudioTrack& t)
        {
            auto rv = insert<ReverbPlugin> (t);
            rv->mix.setParameter (1.0f, juce::sendNotification);
            rv->preDelay.setParameter (0.0f, juce::sendNotification);
            rv->early.setParameter (0.0f, juce::sendNotification);
            rv->modulation.setParameter (0.0f, juce::sendNotification);
            rv->lowDecay.setParameter (1.0f, juce::sendNotification);
            rv->highDecay.setParameter (1.0f, juce::sendNotification);
            rv->postLowCut.setParameter (ReverbPlugin::lowCutOff, juce::sendNotification);
            rv->postHighCut.setParameter (ReverbPlugin::highCutOff, juce::sendNotification);
            rv->decay.setParameter (2.0f, juce::sendNotification);
            return rv;
        }

        /** The C-Verb VST3 from this build tree, if it has been built. */
        static juce::File findCVerbVst3()
        {
            // .../build/<preset>/Tests/CDAW_Tests_artefacts/<config>/CDAW_Tests.exe
            auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
            auto config = exe.getParentDirectory();
            auto preset = config.getParentDirectory().getParentDirectory().getParentDirectory();
            return preset.getChildFile ("VST3/CVerb/CVerb_artefacts").getChildFile (config.getFileName()).getChildFile ("VST3/C-Verb.vst3");
        }

        static void saveSnapshot (juce::Component& c, const juce::String& name)
        {
            auto dir = getTestDir().getChildFile ("snapshots");
            dir.createDirectory();
            auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
            auto file = dir.getChildFile (name + ".png");
            file.deleteFile();
            juce::FileOutputStream out (file);
            juce::PNGImageFormat().writeImageToStream (img, out);
        }

        void runTest() override
        {
            auto& engine = getEngine();

            beginTest ("EQ filter maths: peak, shelf and steep cuts");
            {
                using T = EqMath::BandType;
                const double sr = 48000.0;
                expectWithinAbsoluteError (EqMath::design (T::peak, 0, 1000.0, 6.0, 1.0, sr).magnitudeDb (1000.0, sr), 6.0, 0.01);
                expectWithinAbsoluteError (EqMath::design (T::peak, 0, 1000.0, 6.0, 1.0, sr).magnitudeDb (50.0, sr), 0.0, 0.1);
                expectWithinAbsoluteError (EqMath::design (T::lowShelf, 0, 200.0, -4.0, 0.71, sr).magnitudeDb (20.0, sr), -4.0, 0.15);

                // 8th-order Butterworth: -3 dB at the corner, -48 dB an octave below.
                auto cut48 = EqMath::design (T::lowCut, 3, 1000.0, 0.0, 0.71, sr);
                expectWithinAbsoluteError (cut48.magnitudeDb (1000.0, sr), -3.01, 0.1);
                expectWithinAbsoluteError (cut48.magnitudeDb (500.0, sr), -48.2, 0.5);
                expectWithinAbsoluteError (cut48.magnitudeDb (8000.0, sr), 0.0, 0.05);

                auto cut6 = EqMath::design (T::highCut, 0, 1000.0, 0.0, 0.71, sr);
                expectWithinAbsoluteError (cut6.magnitudeDb (1000.0, sr), -3.01, 0.15);
                expectLessThan (EqMath::design (T::notch, 0, 1000.0, 0.0, 4.0, sr).magnitudeDb (1000.0, sr), -60.0);
            }

            beginTest ("EQ processes audio as its curve says");
            {
                auto boost = gainThrough (engine, 1000.0, [] (te::AudioTrack& t)
                {
                    auto eq = insert<EqPlugin> (t);
                    eq->setType (3, EqMath::BandType::peak);
                    eq->getBand (3).freq.setParameter (1000.0f, juce::sendNotification);
                    eq->getBand (3).gain.setParameter (6.0f, juce::sendNotification);
                    eq->getBand (3).q.setParameter (1.0f, juce::sendNotification);
                    eq->setOn (3, true);
                });
                logMessage ("1 kHz sine through +6 dB peak at 1 kHz: " + juce::String (boost, 2) + " dB");
                expectWithinAbsoluteError (boost, 6.0f, 0.2f);

                auto cut = gainThrough (engine, 100.0, [] (te::AudioTrack& t)
                {
                    auto eq = insert<EqPlugin> (t);
                    eq->setType (0, EqMath::BandType::lowCut);
                    eq->setSlope (0, 2);   // 24 dB/oct
                    eq->getBand (0).freq.setParameter (400.0f, juce::sendNotification);
                    eq->setOn (0, true);
                });
                logMessage ("100 Hz sine through a 24 dB/oct low cut at 400 Hz: " + juce::String (cut, 2) + " dB");
                expectWithinAbsoluteError (cut, -48.0f, 1.5f);

                auto bypassed = gainThrough (engine, 1000.0, [] (te::AudioTrack& t) { insert<EqPlugin> (t); });
                expectWithinAbsoluteError (bypassed, 0.0f, 0.01f, "all bands off = transparent");
            }

            beginTest ("Compressor curve: threshold, ratio and knee");
            {
                expectWithinAbsoluteError (CompressorPlugin::transfer (-6.0f, -20.0f, 4.0f, 0.0f), -16.5f, 0.001f);
                expectWithinAbsoluteError (CompressorPlugin::transfer (-30.0f, -20.0f, 4.0f, 0.0f), -30.0f, 0.001f);
                expectWithinAbsoluteError (CompressorPlugin::transfer (0.0f, -20.0f, 1000.0f, 0.0f), -20.0f, 0.05f, "limiter holds the threshold");

                // Soft knee meets the straight lines at its edges.
                expectWithinAbsoluteError (CompressorPlugin::transfer (-23.0f, -20.0f, 4.0f, 6.0f), -23.0f, 0.001f);
                expectWithinAbsoluteError (CompressorPlugin::transfer (-17.0f, -20.0f, 4.0f, 6.0f), -19.25f, 0.001f);
            }

            beginTest ("Compressor reduces a steady tone by the expected amount and meters it");
            {
                CompressorPlugin* comp = nullptr;
                auto change = gainThrough (engine, 1000.0, [&] (te::AudioTrack& t)
                {
                    comp = insert<CompressorPlugin> (t);
                    comp->thresholdGain.setParameter (juce::Decibels::decibelsToGain (-24.0f), juce::sendNotification);
                    comp->ratio.setParameter (0.25f, juce::sendNotification);     // 4:1
                    comp->knee.setParameter (0.0f, juce::sendNotification);
                    comp->attackMs.setParameter (1.0f, juce::sendNotification);
                    comp->releaseMs.setParameter (100.0f, juce::sendNotification);
                });

                // Sine at 0.25 peaks at -12 dB: 12 dB over a -24 dB threshold at 4:1 -> 9 dB less.
                logMessage ("Gain change: " + juce::String (change, 2) + " dB (expected about -9)");
                expectWithinAbsoluteError (change, -9.0f, 1.0f);

                auto gr = comp->gainReduction.take();
                logMessage ("Metered gain reduction: " + juce::String (gr, 2) + " dB");
                expectWithinAbsoluteError (gr, 9.0f, 1.0f);
                expectGreaterThan (comp->inputPeak[0].take(), 0.2f);
            }

            beginTest ("CDAW compressor keeps the sidechain");
            {
                auto edit = te::test_utilities::createTestEdit (engine, 2);
                auto kick = te::getAudioTracks (*edit)[0];
                auto bass = te::getAudioTracks (*edit)[1];
                kick->insertWaveClip ("k", writeSineFile ("cc_key", 1.0, 0.8f, 60.0), { secs (0.0, 1.0), {} }, false);
                bass->insertWaveClip ("b", writeSineFile ("cc_bass", 2.0, 0.1f, 110.0), { secs (0.0, 2.0), {} }, false);

                auto comp = insert<CompressorPlugin> (*bass);
                // Above the bass's own level (-20 dB), well below the kick's: only the key compresses.
                comp->thresholdGain.setParameter (juce::Decibels::decibelsToGain (-15.0f), juce::sendNotification);
                comp->ratio.setParameter (0.1f, juce::sendNotification);
                comp->attackMs.setParameter (1.0f, juce::sendNotification);
                comp->releaseMs.setParameter (30.0f, juce::sendNotification);
                expect (MixerOps::canSidechain (*comp));

                auto without = te::test_utilities::renderToAudioBuffer (*edit);
                MixerOps::setSidechainSource (*comp, kick);
                auto with = te::test_utilities::renderToAudioBuffer (*edit);

                juce::AudioBuffer<float> diff (without.buffer);
                diff.addFrom (0, 0, with.buffer, 0, 0, juce::jmin (diff.getNumSamples(), with.buffer.getNumSamples()), -1.0f);
                auto between = [&] (double a, double b) { auto s = (int) (a * with.sampleRate); return diff.getRMSLevel (0, s, (int) (b * with.sampleRate) - s); };
                logMessage ("Bass removed while keyed: " + juce::String (between (0.3, 0.9), 4) + ", after: " + juce::String (between (1.4, 1.9), 4));
                expectGreaterThan (between (0.3, 0.9), 0.04f);
                expectLessThan (between (1.4, 1.9), 0.01f);
            }

            beginTest ("Multiband: crossovers add back up flat");
            {
                {
                    auto e = te::test_utilities::createTestEdit (engine, 1);
                    auto mb = insert<MultibandPlugin> (*te::getAudioTracks (*e)[0]);
                    for (int b = 0; b < MultibandPlugin::numBands; ++b)
                        expect (! mb->isOn (b), "a new multiband starts with its bands off");
                }

                for (double hz : { 40.0, 120.0, 700.0, 1000.0, 3000.0, 6000.0, 12000.0 })
                {
                    auto change = gainThrough (engine, hz, [] (te::AudioTrack& t)
                    {
                        auto mb = insert<MultibandPlugin> (t);
                        for (int b = 0; b < MultibandPlugin::numBands; ++b)
                        {
                            mb->getBand (b).on.setParameter (1.0f, juce::sendNotification);
                            mb->getBand (b).threshold.setParameter (0.0f, juce::sendNotification);    // never reached
                        }
                    });
                    logMessage (juce::String (hz) + " Hz through the uncompressed bands: " + juce::String (change, 3) + " dB");
                    expectWithinAbsoluteError (change, 0.0f, 0.05f);
                }
            }

            beginTest ("Multiband: each band compresses only its own range");
            {
                auto setUp = [] (te::AudioTrack& t)
                {
                    auto mb = insert<MultibandPlugin> (t);
                    for (int b = 0; b < MultibandPlugin::numBands; ++b)
                        mb->getBand (b).threshold.setParameter (0.0f, juce::sendNotification);

                    auto& low = mb->getBand (0);
                    low.on.setParameter (1.0f, juce::sendNotification);
                    low.threshold.setParameter (-30.0f, juce::sendNotification);
                    low.ratio.setParameter (4.0f, juce::sendNotification);
                    low.attack.setParameter (1.0f, juce::sendNotification);
                };

                // A -12 dB peak at 50 Hz: 18 dB over -30 at 4:1 -> about 13.5 dB less.
                auto low = gainThrough (engine, 50.0, setUp);
                auto high = gainThrough (engine, 5000.0, setUp);
                logMessage ("50 Hz: " + juce::String (low, 2) + " dB, 5 kHz: " + juce::String (high, 2) + " dB");
                expectWithinAbsoluteError (low, -13.3f, 1.5f);
                expectWithinAbsoluteError (high, 0.0f, 0.05f);

                auto soloed = gainThrough (engine, 50.0, [] (te::AudioTrack& t)
                {
                    auto mb = insert<MultibandPlugin> (t);
                    mb->getBand (3).solo.setParameter (1.0f, juce::sendNotification);
                });
                logMessage ("50 Hz with only the high band soloed: " + juce::String (soloed, 1) + " dB");
                expectLessThan (soloed, -60.0f);
            }

            beginTest ("Reverb: the tail decays at the set RT60");
            {
                auto edit = te::test_utilities::createTestEdit (engine, 1);
                auto track = te::getAudioTracks (*edit)[0];
                track->insertWaveClip ("click", writeImpulseFile (4.0), { secs (0.0, 4.0), {} }, false);
                plainReverb (*track);

                auto r = te::test_utilities::renderToAudioBuffer (*edit);
                auto rt60 = -60.0 / decaySlope (r, 0.3, 1.5);
                logMessage ("Measured RT60: " + juce::String (rt60, 2) + " s (set to 2.0 s)");
                expectWithinAbsoluteError (rt60, 2.0, 0.3);
            }

            beginTest ("Reverb: level, cuts, mix");
            {
                auto wet = gainThrough (engine, 1000.0, [] (te::AudioTrack& t) { plainReverb (t); });
                logMessage ("1 kHz tone, 100 % wet: " + juce::String (wet, 1) + " dB relative to dry");
                expectWithinAbsoluteError (wet, -3.0f, 6.0f);

                auto open = gainThrough (engine, 80.0, [] (te::AudioTrack& t) { plainReverb (t); });
                auto cut = gainThrough (engine, 80.0, [] (te::AudioTrack& t)
                {
                    plainReverb (t)->postLowCut.setParameter (800.0f, juce::sendNotification);
                });
                auto preCut = gainThrough (engine, 80.0, [] (te::AudioTrack& t)
                {
                    plainReverb (t)->preLowCut.setParameter (800.0f, juce::sendNotification);
                });
                logMessage ("80 Hz into the reverb: " + juce::String (open, 1) + " dB, post cut at 800 Hz: " + juce::String (cut, 1)
                            + " dB, pre cut at 800 Hz: " + juce::String (preCut, 1) + " dB");
                expectLessThan (cut, open - 30.0f);

                // The reverb is linear: a cut before it and the same cut after it come out the same.
                expectWithinAbsoluteError (cut, preCut, 0.5f, "pre and post cuts agree");
                expectLessThan (preCut, open - 30.0f);

                auto dry = gainThrough (engine, 1000.0, [] (te::AudioTrack& t) { plainReverb (t)->mix.setParameter (0.0f, juce::sendNotification); });
                expectWithinAbsoluteError (dry, 0.0f, 0.01f, "mix 0 = dry");
            }

            beginTest ("Reverb: freeze holds the tail");
            {
                auto render = [&] (bool freezeAfterOneSecond)
                {
                    auto edit = te::test_utilities::createTestEdit (engine, 1);
                    auto track = te::getAudioTracks (*edit)[0];
                    track->insertWaveClip ("s", writeSineFile ("rv_burst", 1.0, 0.25f, 440.0), { secs (0.0, 1.0), {} }, false);
                    track->insertWaveClip ("pad", writeSineFile ("rv_silence", 4.0, 0.0f), { secs (1.0, 5.0), {} }, false);
                    auto rv = plainReverb (*track);
                    rv->decay.setParameter (0.8f, juce::sendNotification);

                    if (freezeAfterOneSecond)
                    {
                        auto& curve = rv->freeze.getParameter().getCurve();
                        curve.addPoint (te::TimePosition::fromSeconds (0.0), 0.0f, 0.0f, nullptr);
                        curve.addPoint (te::TimePosition::fromSeconds (0.95), 0.0f, 0.0f, nullptr);
                        curve.addPoint (te::TimePosition::fromSeconds (0.96), 1.0f, 0.0f, nullptr);
                    }

                    auto r = te::test_utilities::renderToAudioBuffer (*edit);
                    return rmsDb (r, 3.5, 4.5);
                };

                auto normal = render (false), frozen = render (true);
                logMessage ("Level 3 s after the sound stops: " + juce::String (normal, 1) + " dB normally, " + juce::String (frozen, 1) + " dB frozen");
                expectGreaterThan (frozen, normal + 40.0f);
                expectGreaterThan (frozen, -30.0f);
            }

            beginTest ("Reverb (simple): decay follows the knob, tone darkens the tail");
            {
                {
                    auto edit = te::test_utilities::createTestEdit (engine, 1);
                    auto track = te::getAudioTracks (*edit)[0];
                    track->insertWaveClip ("click", writeImpulseFile (4.0), { secs (0.0, 4.0), {} }, false);
                    auto rv = insert<SimpleReverbPlugin> (*track);
                    rv->mix.setParameter (1.0f, juce::sendNotification);
                    rv->preDelay.setParameter (0.0f, juce::sendNotification);
                    rv->tone.setParameter (1.0f, juce::sendNotification);     // highs ring almost as long as the body
                    rv->decay.setParameter (2.0f, juce::sendNotification);

                    auto rt60 = -60.0 / decaySlope (te::test_utilities::renderToAudioBuffer (*edit), 0.3, 1.5);
                    logMessage ("Reverb measured RT60: " + juce::String (rt60, 2) + " s (set to 2.0 s)");
                    expectWithinAbsoluteError (rt60, 2.0, 0.4);
                }

                auto withTone = [&] (float tone)
                {
                    return gainThrough (engine, 6000.0, [tone] (te::AudioTrack& t)
                    {
                        auto rv = insert<SimpleReverbPlugin> (t);
                        rv->mix.setParameter (1.0f, juce::sendNotification);
                        rv->tone.setParameter (tone, juce::sendNotification);
                    });
                };

                auto dark = withTone (-1.0f), bright = withTone (1.0f);
                logMessage ("6 kHz tone through the reverb: dark " + juce::String (dark, 1) + " dB, bright " + juce::String (bright, 1) + " dB");
                expectLessThan (dark, bright - 10.0f);

                auto dry = gainThrough (engine, 1000.0, [] (te::AudioTrack& t) { insert<SimpleReverbPlugin> (t)->mix.setParameter (0.0f, juce::sendNotification); });
                expectWithinAbsoluteError (dry, 0.0f, 0.01f, "mix 0 = dry");
            }

            beginTest ("The C-Verb VST3 sounds the same as the built-in C-Verb");
            {
                auto bundle = findCVerbVst3();
                juce::OwnedArray<juce::PluginDescription> types;

                if (bundle.exists())
                    for (auto* format : engine.getPluginManager().pluginFormatManager.getFormats())
                        if (format->getName() == "VST3")
                            format->findAllTypesForFile (types, bundle.getFullPathName());

                if (types.isEmpty())
                {
                    logMessage ("C-Verb VST3 not built (" + bundle.getFullPathName() + "), skipping");
                }
                else
                {
                    logMessage ("Found " + types[0]->name + " by " + types[0]->manufacturerName + ", " + juce::String (types[0]->numInputChannels)
                                + " in / " + juce::String (types[0]->numOutputChannels) + " out");
                    expectEquals (types[0]->name, juce::String ("C-Verb"));
                    engine.getPluginManager().knownPluginList.addType (*types[0]);

                    // The same settings on both: the shared defaults, plus a different decay and mix.
                    const float decaySeconds = 1.3f, mixAmount = 0.6f;
                    auto noise = writeSineFile ("cverb_vst3_in", 2.0, 0.3f, 330.0);

                    auto render = [&] (bool useVst3, bool changeSettings)
                    {
                        auto edit = te::test_utilities::createTestEdit (engine, 1);
                        auto track = te::getAudioTracks (*edit)[0];
                        track->insertWaveClip ("in", noise, { secs (0.0, 2.0), {} }, false);
                        track->insertWaveClip ("pad", writeSineFile ("cverb_vst3_pad", 2.0, 0.0f), { secs (2.0, 4.0), {} }, false);

                        if (useVst3)
                        {
                            auto plugin = edit->getPluginCache().createNewPlugin (te::ExternalPlugin::xmlTypeName, *types[0]);
                            track->pluginList.insertPlugin (plugin, 0, nullptr);
                            auto* instance = dynamic_cast<te::ExternalPlugin*> (plugin.get())->getAudioPluginInstance();
                            expect (instance != nullptr, "VST3 instance loads");

                            if (instance != nullptr)
                            {
                                auto set = [&] (CVerbParameters::ID id, float value)
                                {
                                    auto& spec = CVerbParameters::get (id);
                                    for (auto* param : instance->getParameters())
                                        if (param->getName (64) == spec.name)
                                            param->setValueNotifyingHost (spec.getRange().convertTo0to1 (value));
                                };


                                if (changeSettings)
                                {
                                    set (CVerbParameters::decay, decaySeconds);
                                    set (CVerbParameters::mix, mixAmount);
                                }
                            }
                        }
                        else
                        {
                            auto rv = insert<ReverbPlugin> (*track);
                            if (changeSettings)
                            {
                                rv->decay.setParameter (decaySeconds, juce::sendNotification);
                                rv->mix.setParameter (mixAmount, juce::sendNotification);
                            }
                        }

                        return te::test_utilities::renderToAudioBuffer (*edit);
                    };

                    auto compare = [&] (bool changeSettings)
                    {
                        auto vst3 = render (true, changeSettings), builtIn = render (false, changeSettings);
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
                    logMessage ("VST3 vs built-in C-Verb, difference below the signal (tail included): defaults " + juce::String (defaults, 1)
                                + " dB, changed decay and mix " + juce::String (changed, 1) + " dB");
                    expectLessThan (defaults, -80.0, "the same audio with the same settings");
                    expectLessThan (changed, -50.0, "settings sent through the host arrive with the first block");

                    // What a host does besides playing: open the editor, save and restore the settings.
                    juce::String error;
                    auto& formats = engine.getPluginManager().pluginFormatManager;
                    auto first = formats.createPluginInstance (*types[0], 48000.0, 512, error);
                    auto second = formats.createPluginInstance (*types[0], 48000.0, 512, error);
                    expect (first != nullptr && second != nullptr, "instances: " + error);

                    if (first != nullptr && second != nullptr)
                    {
                        if (auto* editor = first->createEditorIfNeeded())
                        {
                            logMessage ("VST3 editor opens at " + juce::String (editor->getWidth()) + " x " + juce::String (editor->getHeight()));
                            expectEquals (editor->getWidth(), CVerbView::defaultWidth);
                            delete editor;
                        }
                        else
                        {
                            expect (false, "the VST3 has an editor");
                        }

                        auto& decaySpec = CVerbParameters::get (CVerbParameters::decay);
                        juce::AudioProcessorParameter* decayParam = nullptr;
                        for (auto* param : first->getParameters())
                            if (param->getName (64) == decaySpec.name)
                                decayParam = param;

                        expect (decayParam != nullptr, "Decay is a host parameter");
                        // Every C-Verb parameter, plus the Bypass switch VST3 hosts get from every plugin.
                        expectEquals (first->getParameters().size(), (int) CVerbParameters::count + 1, "every C-Verb parameter is automatable in the host");

                        if (decayParam != nullptr)
                        {
                            decayParam->setValueNotifyingHost (decaySpec.getRange().convertTo0to1 (4.5f));
                            juce::MemoryBlock saved;
                            first->getStateInformation (saved);
                            second->setStateInformation (saved.getData(), (int) saved.getSize());

                            for (auto* param : second->getParameters())
                                if (param->getName (64) == decaySpec.name)
                                    expectWithinAbsoluteError (decaySpec.getRange().convertFrom0to1 (param->getValue()), 4.5f, 0.01f, "settings restore");

                            logMessage ("Decay shown by the host: " + decayParam->getCurrentValueAsText());
                        }
                    }
                }
            }

            beginTest ("Projects from earlier builds get CDAW's compressor and EQ");
            {
                juce::ValueTree edit (te::IDs::EDIT);
                juce::ValueTree comp (te::IDs::PLUGIN);
                comp.setProperty (te::IDs::type, "compressor", nullptr);
                comp.setProperty ("threshold", 0.25f, nullptr);
                juce::ValueTree eq (te::IDs::PLUGIN);
                eq.setProperty (te::IDs::type, "4bandEq", nullptr);
                eq.setProperty ("midFreq1", 2500.0f, nullptr);
                eq.setProperty ("midGain1", 3.0f, nullptr);
                eq.appendChild (juce::ValueTree (te::IDs::AUTOMATIONCURVE), nullptr);
                juce::ValueTree track (te::IDs::TRACK);
                track.appendChild (comp, nullptr);
                track.appendChild (eq, nullptr);
                edit.appendChild (track, nullptr);

                juce::ValueTree oldReverb (te::IDs::PLUGIN);
                oldReverb.setProperty (te::IDs::type, "reverb", nullptr);
                oldReverb.setProperty ("roomSize", 0.8f, nullptr);
                track.appendChild (oldReverb, nullptr);

                expectEquals (BuiltInPlugins::migrateEditState (edit), 3);
                expectEquals (oldReverb[te::IDs::type].toString(), juce::String (SimpleReverbPlugin::xmlTypeName), "old reverbs become the simple Reverb");
                expectEquals ((float) oldReverb["size"], 0.8f);
                expectEquals (comp[te::IDs::type].toString(), juce::String (CompressorPlugin::xmlTypeName));
                expectEquals ((float) comp["threshold"], 0.25f, "settings untouched");
                expectEquals (eq[te::IDs::type].toString(), juce::String (EqPlugin::xmlTypeName));
                expectEquals ((float) eq["b3freq"], 2500.0f);
                expectEquals ((float) eq["b3gain"], 3.0f);
                expectEquals (eq.getNumChildren(), 0, "old automation dropped");

                // The converted state loads into the EQ.
                auto e = te::test_utilities::createTestEdit (engine, 1);
                auto plugin = e->getPluginCache().createNewPlugin (EqPlugin::xmlTypeName, {});
                auto* p = dynamic_cast<EqPlugin*> (plugin.get());
                p->restorePluginStateFromValueTree (eq);
                expect (p->isOn (2));
                expectWithinAbsoluteError (p->getBand (2).gain.getCurrentValue(), 3.0f, 0.001f);
                expect (p->getType (2) == EqMath::BandType::peak);
            }

            if (juce::SystemStats::getEnvironmentVariable ("CDAW_SNAPSHOTS", {}) != "1")
                return;

            beginTest ("Snapshots: compressor and EQ editors");
            {
                CDAWLookAndFeel laf;
                juce::LookAndFeel::setDefaultLookAndFeel (&laf);
                auto edit = te::test_utilities::createTestEdit (engine, 1);
                auto track = te::getAudioTracks (*edit)[0];

                {
                    auto comp = insert<CompressorPlugin> (*track);
                    comp->thresholdGain.setParameter (juce::Decibels::decibelsToGain (-22.0f), juce::sendNotification);
                    comp->ratio.setParameter (0.25f, juce::sendNotification);
                    CompressorEditor editor (*comp);

                    // Drum-like hits: fast rise, decay, compressed by up to ~9 dB.
                    juce::Random rng (3);
                    for (int i = 0; i < 400; ++i)
                    {
                        auto phase = (i % 30) / 30.0f;
                        auto level = juce::Decibels::decibelsToGain (-4.0f - 40.0f * phase + rng.nextFloat() * 2.0f);
                        auto inDb = juce::Decibels::gainToDecibels (level);
                        auto gr = juce::jmax (0.0f, (inDb - comp->getThresholdDb()) * 0.75f);
                        auto out = level * juce::Decibels::decibelsToGain (-gr);
                        editor.tick (level, level * 0.9f, out, out * 0.9f, level, gr);
                    }

                    saveSnapshot (editor, "compressor");
                }

                {
                    auto eq = insert<EqPlugin> (*track);
                    auto set = [eq] (int b, EqMath::BandType t, float f, float g, float q, int slope = 2)
                    {
                        eq->setType (b, t);
                        eq->setSlope (b, slope);
                        eq->getBand (b).freq.setParameter (f, juce::sendNotification);
                        eq->getBand (b).gain.setParameter (g, juce::sendNotification);
                        eq->getBand (b).q.setParameter (q, juce::sendNotification);
                        eq->setOn (b, true);
                    };
                    set (0, EqMath::BandType::lowCut, 60.0f, 0.0f, 0.71f, 2);
                    set (2, EqMath::BandType::peak, 320.0f, -4.5f, 1.4f);
                    set (4, EqMath::BandType::peak, 2200.0f, 3.0f, 0.9f);
                    set (6, EqMath::BandType::highShelf, 9000.0f, 2.5f, 0.71f);

                    EqEditor editor (*eq);
                    editor.setSize (1000, 600);

                    // Pinkish noise into the analyser, through the EQ's own curve, so the spectrum has a shape.
                    eq->postFifo->active = true;
                    juce::Random rng (7);
                    juce::AudioBuffer<float> noise (1, 4096);
                    float b0 = 0, b1 = 0, b2 = 0;
                    for (int block = 0; block < 12; ++block)
                    {
                        for (int i = 0; i < noise.getNumSamples(); ++i)
                        {
                            auto white = rng.nextFloat() * 2.0f - 1.0f;
                            b0 = 0.99765f * b0 + white * 0.0990460f;
                            b1 = 0.96300f * b1 + white * 0.2965164f;
                            b2 = 0.57000f * b2 + white * 1.0526913f;
                            noise.setSample (0, i, (b0 + b1 + b2 + white * 0.1848f) * 0.08f);
                        }

                        eq->postFifo->push (noise);
                        editor.updateSpectrum();
                    }

                    saveSnapshot (editor, "eq");
                }

                {
                    auto mb = insert<MultibandPlugin> (*track);
                    for (int b = 0; b < 3; ++b)
                        mb->getBand (b).on.setParameter (1.0f, juce::sendNotification);
                    mb->getBand (0).threshold.setParameter (-24.0f, juce::sendNotification);
                    mb->getBand (1).solo.setParameter (0.0f, juce::sendNotification);
                    mb->getBand (2).ratio.setParameter (6.0f, juce::sendNotification);
                    mb->getBand (3).on.setParameter (0.0f, juce::sendNotification);
                    MultibandEditor editor (*mb);
                    editor.setSize (1080, 660);

                    MultibandEditor::Levels levels {};
                    for (int i = 0; i < 120; ++i)
                    {
                        const float in[4] { 0.5f, 0.3f, 0.2f, 0.08f }, gr[4] { 7.5f, 3.2f, 1.4f, 0.0f };
                        for (int b = 0; b < 4; ++b)
                        {
                            levels.bandIn[b] = in[b];
                            levels.reduction[b] = gr[b];
                            levels.bandOut[b] = in[b] * juce::Decibels::decibelsToGain (-gr[b]);
                        }
                        levels.in[0] = levels.in[1] = 0.7f;
                        levels.out[0] = levels.out[1] = 0.45f;
                        editor.tick (levels);
                    }

                    mb->postFifo->active = true;
                    juce::Random rng (11);
                    juce::AudioBuffer<float> noise (1, 4096);
                    float b0 = 0, b1 = 0, b2 = 0;
                    for (int block = 0; block < 12; ++block)
                    {
                        for (int i = 0; i < noise.getNumSamples(); ++i)
                        {
                            auto white = rng.nextFloat() * 2.0f - 1.0f;
                            b0 = 0.99765f * b0 + white * 0.0990460f;
                            b1 = 0.96300f * b1 + white * 0.2965164f;
                            b2 = 0.57000f * b2 + white * 1.0526913f;
                            noise.setSample (0, i, (b0 + b1 + b2 + white * 0.1848f) * 0.08f);
                        }
                        mb->postFifo->push (noise);
                        editor.updateSpectrum();
                    }

                    saveSnapshot (editor, "multiband");
                }

                {
                    auto rv = insert<ReverbPlugin> (*track);
                    rv->preLowCut.setParameter (180.0f, juce::sendNotification);
                    ReverbEditor editor (*rv);
                    editor.setSize (1140, 680);

                    // Hits every 1.2 s with the reverb ringing on after each.
                    for (int i = 0; i < 360; ++i)
                    {
                        auto t = (float) (i % 72) / 60.0f;
                        auto dry = t < 0.15f ? 0.6f * (1.0f - t / 0.15f) + 0.01f : 0.0f;
                        auto wet = 0.3f * std::exp (-t * 2.4f) * (1.0f - std::exp (-t * 40.0f)) + 0.002f;
                        editor.tick (dry, wet);
                    }

                    // Pinkish noise as the reverb's output, for the faint spectrum.
                    rv->engine.wetFifo->active = true;
                    juce::Random rng (5);
                    juce::AudioBuffer<float> noise (1, 4096);
                    float n0 = 0, n1 = 0;
                    for (int block = 0; block < 12; ++block)
                    {
                        for (int i = 0; i < noise.getNumSamples(); ++i)
                        {
                            auto white = rng.nextFloat() * 2.0f - 1.0f;
                            n0 = 0.997f * n0 + white * 0.03f;
                            n1 = 0.6f * n1 + white * 0.4f;
                            noise.setSample (0, i, (n0 + n1) * 0.1f);
                        }
                        rv->engine.wetFifo->push (noise);
                        editor.updateSpectrum();
                    }

                    saveSnapshot (editor, "reverb");
                }

                {
                    auto rv = insert<SimpleReverbPlugin> (*track);
                    rv->tone.setParameter (-0.4f, juce::sendNotification);
                    SimpleReverbEditor editor (*rv);
                    editor.setSize (600, 400);
                    rv->engine.wetFifo->active = true;
                    juce::Random rng (6);
                    juce::AudioBuffer<float> noise (1, 4096);
                    for (int block = 0; block < 12; ++block)
                    {
                        for (int i = 0; i < noise.getNumSamples(); ++i)
                            noise.setSample (0, i, (rng.nextFloat() * 2.0f - 1.0f) * 0.05f);
                        rv->engine.wetFifo->push (noise);
                        editor.updateSpectrum();
                    }
                    editor.tick (0.2f);
                    saveSnapshot (editor, "reverb_simple");
                }

                juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
            }
        }
    };

    static BuiltInPluginTests builtInPluginTests;
}
