#include "TestEngine.h"
#include "Plugins/LeadPlugin.h"
#include "Shared/Lead/LeadPresets.h"
#include "Shared/Lead/LeadView.h"
#include "Shared/LookAndFeel/CDAWLookAndFeel.h"
#include "UI/Plugins/SharedEffectEditor.h"
#include <complex>

namespace cdaw::test
{
    /** C-Lead inside CDAW: the Tracktion plugin, the VST3 build against it, and its pages.
        The engine's own tests are in the plugins' repository. */
    struct LeadTests : public juce::UnitTest
    {
        LeadTests() : juce::UnitTest ("C-Lead", "CDAW") {}

        using P = LeadParameters::ID;
        using Settings = LeadEngine::Settings;
        using Wave = LeadEngine::Wave;
        static constexpr double rate = 48000.0;

        /** The VST3 from this build tree: it plays MIDI exactly like the built-in plugin, opens its
            editor, exposes every parameter and restores its settings. Skipped if not built. */
        void checkVst3()
        {
            auto& engine = getEngine();
            auto bundle = findPluginVst3 ("CLead", "C-Lead");

            juce::OwnedArray<juce::PluginDescription> types;
            if (bundle.exists())
                for (auto* format : engine.getPluginManager().pluginFormatManager.getFormats())
                    if (format->getName() == "VST3")
                        format->findAllTypesForFile (types, bundle.getFullPathName());

            if (types.isEmpty())
            {
                logMessage ("C-Lead VST3 not built (" + bundle.getFullPathName() + "), skipping");
                return;
            }

            expectEquals (types[0]->name, juce::String ("C-Lead"));
            expect (types[0]->isInstrument, "an instrument");
            engine.getPluginManager().knownPluginList.addType (*types[0]);

            const auto& specs = LeadParameters::all();
            const std::vector<std::pair<int, float>> changes { { P::sync, 1.0f }, { P::bSemitones, 9.0f }, { P::cutoff, 1500.0f }, { P::voiceMode, 1.0f },
                                                               { P::distOn, 1.0f }, { P::distVoicing, 2.0f }, { P::distDrive, 0.7f },
                                                               { P::macro2, 0.6f }, { P::lfo2Retrigger, 0.0f }, { P::subLevel, 0.3f }, { P::glide, 0.15f } };

            auto render = [&] (bool useVst3, bool change)
            {
                auto edit = te::test_utilities::createTestEdit (engine, 1);
                edit->tempoSequence.getTempo (0)->setBpm (120.0);
                auto track = te::getAudioTracks (*edit)[0];
                auto clip = track->insertMIDIClip ({ te::TimePosition::fromSeconds (0.0), te::TimePosition::fromSeconds (4.0) }, nullptr);
                for (auto [key, beat, length] : { std::tuple (60, 0.0, 1.5), std::tuple (64, 1.0, 1.0), std::tuple (67, 1.75, 1.25), std::tuple (72, 3.0, 2.0) })
                    clip->getSequence().addNote (key, te::BeatPosition::fromBeats (beat), te::BeatDuration::fromBeats (length), 100, 0, nullptr);

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
                    auto plugin = edit->getPluginCache().createNewPlugin (LeadPlugin::xmlTypeName, {});
                    track->pluginList.insertPlugin (plugin, 0, nullptr);
                    if (change)
                        for (auto& [id, value] : changes)
                            dynamic_cast<LeadPlugin*> (plugin.get())->parameter (id).setParameter (value, juce::sendNotification);
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
            logMessage ("VST3 vs built-in C-Lead, difference below the signal: defaults " + juce::String (defaults, 1)
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
                expectEquals (editor->getWidth(), LeadView::defaultWidth, "the VST3 editor opens at the view's size");
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
            beginTest ("C-Lead in Tracktion plays a MIDI clip");
            {
                auto edit = te::test_utilities::createTestEdit (getEngine(), 1);
                edit->tempoSequence.getTempo (0)->setBpm (120.0);
                auto track = te::getAudioTracks (*edit)[0];
                auto plugin = edit->getPluginCache().createNewPlugin (LeadPlugin::xmlTypeName, {});
                track->pluginList.insertPlugin (plugin, 0, nullptr);
                auto* lead = dynamic_cast<LeadPlugin*> (plugin.get());
                expect (lead != nullptr && lead->isSynth() && lead->takesMidiInput());

                // The plain patch, through the plugin's parameters.
                for (auto [id, v] : std::initializer_list<std::pair<int, float>> {
                         { P::aWave, (float) Wave::sine }, { P::aUnison, 1.0f }, { P::bOn, 0.0f }, { P::drive, 0.0f },
                         { P::filterType, (float) LeadEngine::FilterType::lowPass }, { P::cutoff, 20000.0f }, { P::resonance, 0.0f },
                         { P::keyTrack, 0.0f }, { P::filterEnv, 0.0f }, { P::filterVelocity, 0.0f },
                         { P::ampAttack, 0.001f }, { P::ampDecay, 0.001f }, { P::ampSustain, 1.0f }, { P::ampRelease, 0.05f }, { P::ampVelocity, 0.0f },
                         { P::vibratoDepth, 0.0f }, { P::glide, 0.0f }, { P::chorusOn, 0.0f }, { P::delayOn, 0.0f }, { P::reverbOn, 0.0f }, { P::volume, 0.0f } })
                    lead->parameter (id).setParameter (v, juce::sendNotification);
                for (int k = 0; k < LeadEngine::numSlots; ++k)
                    lead->parameter (LeadParameters::slot (k, 0)).setParameter (0.0f, juce::sendNotification);

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
                expect (lead->getTailLength() >= 0.05);
            }

            beginTest ("The C-Lead VST3 plays the same as the built-in C-Lead");
            checkVst3();

            if (juce::SystemStats::getEnvironmentVariable ("CDAW_SNAPSHOTS", {}) != "1")
                return;

            beginTest ("Snapshots: C-Lead's pages");
            {
                CDAWLookAndFeel laf;
                juce::LookAndFeel::setDefaultLookAndFeel (&laf);
                auto edit = te::test_utilities::createTestEdit (getEngine(), 1);
                edit->tempoSequence.getTempo (0)->setBpm (120.0);
                auto track = te::getAudioTracks (*edit)[0];
                auto plugin = edit->getPluginCache().createNewPlugin (LeadPlugin::xmlTypeName, {});
                track->pluginList.insertPlugin (plugin, 0, nullptr);
                auto* lead = dynamic_cast<LeadPlugin*> (plugin.get());
                for (auto [id, v] : std::initializer_list<std::pair<int, float>> {
                         { P::sync, 1.0f }, { P::macro3, 0.4f }, { P::macro1, 0.25f }, { P::vibratoDepth, 35.0f },
                         { P::voiceMode, 1.0f }, { P::glide, 0.12f }, { P::distOn, 1.0f }, { P::distDrive, 0.4f } })
                    lead->parameter (id).setParameter (v, juce::sendNotification);

                SharedEffectEditor<LeadView, LeadEngine> editor (*lead, lead->engine);
                editor.setSize (LeadView::defaultWidth, LeadView::defaultHeight);
                auto& view = editor.getView();

                // A legato line gliding up and back down, a low key held under the last note, whose vibrato comes in.
                lead->engine.prepare (48000.0, 512);
                juce::AudioBuffer<float> buffer (2, 512);
                juce::MidiBuffer midi;
                const auto settings = lead->getSettings();
                const std::vector<std::tuple<int, int, bool>> events { { 0, 64, true }, { 60, 67, true }, { 90, 64, false }, { 140, 71, true },
                                                                       { 170, 67, false }, { 200, 60, true }, { 220, 69, true }, { 230, 71, false } };
                for (int block = 0; block < 400; ++block)
                {
                    midi.clear();
                    for (auto& [at, key, on] : events)
                        if (at == block)
                            midi.addEvent (on ? juce::MidiMessage::noteOn (1, key, (juce::uint8) 100) : juce::MidiMessage::noteOff (1, key), 0);

                    lead->engine.process (buffer, 0, 512, midi, settings, 120.0);
                    view.getStereoField().add (buffer.getReadPointer (0), buffer.getReadPointer (1), 512);
                    lead->engine.outputFifo->active = true;
                    if (block % 2 == 0)
                        view.tick();
                }

                auto dir = juce::SystemStats::getEnvironmentVariable ("CDAW_SNAPSHOT_DIR", {});
                auto folder = dir.isNotEmpty() ? juce::File (dir) : getTestDir().getChildFile ("snapshots");
                folder.createDirectory();

                for (auto [page, name] : { std::pair (0, "c-lead"), std::pair (1, "c-lead-mod"), std::pair (2, "c-lead-fx") })
                {
                    view.showPage (page);
                    for (int i = 0; i < (page == 0 ? 1 : 20); ++i)
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

    static LeadTests leadTests;
}
