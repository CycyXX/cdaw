#include "TestEngine.h"
#include "Plugins/DistPlugin.h"
#include "Shared/Dist/DistView.h"
#include "Shared/LookAndFeel/CDAWLookAndFeel.h"
#include "UI/Plugins/SharedEffectEditor.h"

namespace cdaw::test
{
    /** C-Dist inside CDAW: the Tracktion plugin and its latency, the VST3 build against it, and its view.
        The engine's own tests are in the plugins' repository. */
    struct DistTests : public juce::UnitTest
    {
        DistTests() : juce::UnitTest ("C-Dist", "CDAW") {}

        using P = DistParameters::ID;
        using Settings = DistEngine::Settings;
        using Voicing = DistEngine::Voicing;
        static constexpr double rate = 48000.0;
        static constexpr double twoPi = juce::MathConstants<double>::twoPi;

        /** A plucked note: a bright, decaying, harmonic-rich tone every half second, peaking near -12 dBFS. */
        static juce::AudioBuffer<float> plucks (double seconds, double hz = 110.0)
        {
            juce::AudioBuffer<float> b (2, (int) (seconds * rate));
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const double t = std::fmod (i / rate, 0.5);
                double x = 0.0;
                for (int h = 1; h <= 12; ++h)
                    x += std::sin (twoPi * hz * h * t) / h * std::exp (-t * (2.0 + h * 0.8));
                const auto v = (float) (0.13 * x);
                b.setSample (0, i, v);
                b.setSample (1, i, v);
            }
            return b;
        }

        static bool finite (const juce::AudioBuffer<float>& b)
        {
            for (int ch = 0; ch < b.getNumChannels(); ++ch)
                for (int i = 0; i < b.getNumSamples(); ++i)
                    if (! std::isfinite (b.getSample (ch, i)))
                        return false;
            return true;
        }

        void runTest() override
        {
            beginTest ("C-Dist in Tracktion: distorts a track and reports its latency, so it stays in time");
            {
                // A sine, and later a click: through C-Dist, the click (dry, Blend 0) lands where it was.
                juce::AudioBuffer<float> source (2, (int) (2.0 * rate));
                source.clear();
                for (int i = 0; i < (int) (1.0 * rate); ++i)
                    for (int ch = 0; ch < 2; ++ch)
                        source.setSample (ch, i, 0.25f * (float) std::sin (twoPi * 220.0 * i / rate));
                const int click = (int) (1.5 * rate);
                source.setSample (0, click, 0.5f);
                source.setSample (1, click, 0.5f);
                auto wav = getTestDir().getChildFile ("dist_source.wav");
                wav.deleteFile();
                {
                    juce::WavAudioFormat format;
                    std::unique_ptr<juce::OutputStream> stream (wav.createOutputStream());
                    if (auto writer = std::unique_ptr<juce::AudioFormatWriter> (format.createWriterFor (stream.get(), rate, 2, 32, {}, 0)))
                    {
                        stream.release();
                        writer->writeFromAudioSampleBuffer (source, 0, source.getNumSamples());
                    }
                }

                auto render = [&] (float blendValue)
                {
                    auto edit = te::test_utilities::createTestEdit (getEngine(), 1);
                    auto track = te::getAudioTracks (*edit)[0];
                    track->insertWaveClip ("source", wav, { { te::TimePosition(), te::TimePosition::fromSeconds (2.0) }, {} }, false);
                    auto plugin = edit->getPluginCache().createNewPlugin (DistPlugin::xmlTypeName, {});
                    track->pluginList.insertPlugin (plugin, 0, nullptr);
                    auto* dist = dynamic_cast<DistPlugin*> (plugin.get());
                    dist->parameter (P::voicing).setParameter ((float) Voicing::modern, juce::sendNotification);
                    dist->parameter (P::drive).setParameter (0.8f, juce::sendNotification);
                    dist->parameter (P::blend).setParameter (blendValue, juce::sendNotification);
                    expectWithinAbsoluteError (dist->getLatencySeconds(), DistEngine::latencySamples / 44100.0, 0.0005, "it reports the oversampling's latency");
                    return te::test_utilities::renderToAudioBuffer (*edit);
                };

                auto wet = render (1.0f);
                expect (finite (wet.buffer));
                const auto rendered = wet.buffer.getRMSLevel (0, (int) (0.3 * wet.sampleRate), (int) (0.5 * wet.sampleRate));
                logMessage ("Rendered at " + juce::String (wet.sampleRate) + " Hz: the sine through Modern at " + juce::String (juce::Decibels::gainToDecibels (rendered), 1) + " dB RMS");
                expectGreaterThan (rendered, 0.01f, "it plays");

                auto dry = render (0.0f);
                int at = -1;
                for (int i = (int) (1.2 * dry.sampleRate); i < dry.buffer.getNumSamples(); ++i)
                    if (std::abs (dry.buffer.getSample (0, i)) > 0.2f)
                    {
                        at = i;
                        break;
                    }
                const auto expected = (int) std::round (1.5 * dry.sampleRate);
                logMessage ("The click comes out at sample " + juce::String (at) + " (in at " + juce::String (expected) + ")");
                expectWithinAbsoluteError (at, expected, 2, "Tracktion lines up the reported latency");
            }

            beginTest ("The C-Dist VST3 sounds the same as the built-in C-Dist");
            checkVst3();

            if (juce::SystemStats::getEnvironmentVariable ("CDAW_SNAPSHOTS", {}) != "1")
                return;

            beginTest ("Snapshot: C-Dist");
            {
                CDAWLookAndFeel laf;
                juce::LookAndFeel::setDefaultLookAndFeel (&laf);
                auto edit = te::test_utilities::createTestEdit (getEngine(), 1);
                auto track = te::getAudioTracks (*edit)[0];
                auto plugin = edit->getPluginCache().createNewPlugin (DistPlugin::xmlTypeName, {});
                track->pluginList.insertPlugin (plugin, 0, nullptr);
                auto* dist = dynamic_cast<DistPlugin*> (plugin.get());
                dist->parameter (P::gateOn).setParameter (1.0f, juce::sendNotification);
                dist->parameter (P::gateThreshold).setParameter (-58.0f, juce::sendNotification);
                dist->parameter (P::mid).setParameter (2.0f, juce::sendNotification);

                SharedEffectEditor<DistView, DistEngine> editor (*dist, dist->engine);
                editor.setSize (DistView::defaultWidth, DistView::defaultHeight);
                auto& view = editor.getView();

                // Plucked notes with a little hum between them, for the gate to shut on.
                auto in = plucks (3.0, 110.0);
                juce::Random random (5);
                for (int i = 0; i < in.getNumSamples(); ++i)
                    for (int ch = 0; ch < 2; ++ch)
                        in.setSample (ch, i, in.getSample (ch, i) * (std::fmod (i / rate, 1.0) < 0.62 ? 1.0f : 0.0f) + 0.0004f * (random.nextFloat() - 0.5f));

                dist->engine.prepare (rate, 512);
                const auto s = dist->getSettings();
                dist->engine.inputFifo->active = dist->engine.outputFifo->active = true;
                juce::AudioBuffer<float> block (2, 512);
                const int stop = (int) (2.45 * rate);
                for (int start = 0, n = 0; start + 512 <= stop; start += 512, ++n)
                {
                    for (int ch = 0; ch < 2; ++ch)
                        block.copyFrom (ch, 0, in, ch, start, 512);
                    dist->engine.process (block, 0, 512, s);
                    if (n % 2 == 0)
                        view.tick();
                }

                auto dir = juce::SystemStats::getEnvironmentVariable ("CDAW_SNAPSHOT_DIR", {});
                auto folder = dir.isNotEmpty() ? juce::File (dir) : getTestDir().getChildFile ("snapshots");
                folder.createDirectory();
                auto image = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f);
                auto file = folder.getChildFile ("c-dist.png");
                file.deleteFile();
                juce::FileOutputStream out (file);
                juce::PNGImageFormat().writeImageToStream (image, out);
                juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
            }
        }

        /** The VST3 from this build tree: the same audio as the built-in plugin (latency included),
            its editor, every parameter and its state. Skipped if not built. */
        void checkVst3()
        {
            auto& engine = getEngine();
            auto bundle = findPluginVst3 ("CDist", "C-Dist");

            juce::OwnedArray<juce::PluginDescription> types;
            if (bundle.exists())
                for (auto* format : engine.getPluginManager().pluginFormatManager.getFormats())
                    if (format->getName() == "VST3")
                        format->findAllTypesForFile (types, bundle.getFullPathName());

            if (types.isEmpty())
            {
                logMessage ("C-Dist VST3 not built (" + bundle.getFullPathName() + "), skipping");
                return;
            }

            expectEquals (types[0]->name, juce::String ("C-Dist"));
            expect (! types[0]->isInstrument, "an effect");
            engine.getPluginManager().knownPluginList.addType (*types[0]);

            const auto& specs = DistParameters::all();
            const std::vector<std::pair<int, float>> changes { { P::voicing, (float) Voicing::fuzz }, { P::drive, 0.7f }, { P::tight, 200.0f },
                                                               { P::treble, 4.0f }, { P::blend, 0.7f }, { P::gateOn, 1.0f } };

            auto render = [&] (bool useVst3, bool change)
            {
                // The sine and click the Tracktion test wrote.
                auto edit = te::test_utilities::createTestEdit (engine, 1);
                auto track = te::getAudioTracks (*edit)[0];
                track->insertWaveClip ("source", getTestDir().getChildFile ("dist_source.wav"), { { te::TimePosition(), te::TimePosition::fromSeconds (2.0) }, {} }, false);

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
                    auto plugin = edit->getPluginCache().createNewPlugin (DistPlugin::xmlTypeName, {});
                    track->pluginList.insertPlugin (plugin, 0, nullptr);
                    if (change)
                        for (auto& [id, value] : changes)
                            dynamic_cast<DistPlugin*> (plugin.get())->parameter (id).setParameter (value, juce::sendNotification);
                }

                return te::test_utilities::renderToAudioBuffer (*edit);
            };

            auto compare = [&] (bool change)
            {
                auto vst3 = render (true, change), builtIn = render (false, change);
                const auto length = juce::jmin (vst3.buffer.getNumSamples(), builtIn.buffer.getNumSamples());
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
            logMessage ("VST3 vs built-in C-Dist, difference below the signal: defaults " + juce::String (defaults, 1)
                        + " dB, changed settings " + juce::String (changed, 1) + " dB");
            expectLessThan (defaults, -80.0, "the same audio (and latency) with the same settings");
            expectLessThan (changed, -80.0, "settings sent through the host arrive with the first block");

            juce::String error;
            auto& formats = engine.getPluginManager().pluginFormatManager;
            auto first = formats.createPluginInstance (*types[0], 48000.0, 512, error);
            auto second = formats.createPluginInstance (*types[0], 48000.0, 512, error);
            expect (first != nullptr && second != nullptr, "instances: " + error);
            if (first == nullptr || second == nullptr)
                return;

            first->prepareToPlay (48000.0, 512);
            expectEquals (first->getLatencySamples(), DistEngine::latencySamples, "the VST3 reports its latency");

            if (auto* editor = first->createEditorIfNeeded())
            {
                expectEquals (editor->getWidth(), DistView::defaultWidth, "the VST3 editor opens at the view's size");
                delete editor;
            }
            else
            {
                expect (false, "the VST3 has an editor");
            }

            juce::StringArray hostNames;
            for (auto* param : first->getParameters())
                hostNames.add (param->getName (64));
            for (auto& spec : specs)
                expect (hostNames.contains (spec.name), juce::String (spec.name) + " is a host parameter");

            for (auto* param : first->getParameters())
                if (param->getName (64) == specs[P::drive].name)
                    param->setValueNotifyingHost (specs[P::drive].getRange().convertTo0to1 (0.37f));

            juce::MemoryBlock saved;
            first->getStateInformation (saved);
            second->setStateInformation (saved.getData(), (int) saved.getSize());
            for (auto* param : second->getParameters())
                if (param->getName (64) == specs[P::drive].name)
                    expectWithinAbsoluteError (specs[P::drive].getRange().convertFrom0to1 (param->getValue()), 0.37f, 0.001f, "settings restore");
        }
    };

    static DistTests distTests;
}
