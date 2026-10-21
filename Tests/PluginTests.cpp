#include "TestEngine.h"
#include "Model/MixerOps.h"
#include "Model/TrackOps.h"
#include "Model/ProjectSession.h"
#include "UI/Plugins/PluginMenus.h"

namespace cdaw::test
{
    struct PluginTests : public juce::UnitTest
    {
        PluginTests() : juce::UnitTest ("Plugins", "CDAW") {}

        static std::unique_ptr<juce::PluginDescription> findVst3 (te::Engine& engine, const juce::File& file)
        {
            juce::OwnedArray<juce::PluginDescription> types;

            for (auto* format : engine.getPluginManager().pluginFormatManager.getFormats())
                if (format->getName() == "VST3")
                    format->findAllTypesForFile (types, file.getFullPathName());

            if (types.isEmpty())
                return {};

            // Tracktion only instantiates plugins present in the known-plugin list.
            engine.getPluginManager().knownPluginList.addType (*types[0]);

            return std::make_unique<juce::PluginDescription> (*types[0]);
        }

        void runTest() override
        {
            auto& engine = getEngine();

            beginTest ("Built-in EQ and send survive save / reload");
            {
                auto dir = getTestDir().getChildFile ("PluginProjects");
                dir.deleteRecursively();
                dir.createDirectory();
                auto editFile = ProjectSession::createProjectFolder (dir, "Plug");

                {
                    ProjectSession s (engine, editFile);
                    auto& edit = s.getEdit();
                    auto track = te::getAudioTracks (edit)[0];

                    auto eq = PluginMenus::createPlugin (edit, te::PluginManager::createBuiltInPluginDescription<te::EqualiserPlugin>());
                    expect (eq != nullptr);
                    track->pluginList.insertPlugin (eq, PluginMenus::getPreFaderInsertIndex (*track), nullptr);
                    eq->getAutomatableParameter (0)->setParameter (0.3f, juce::sendNotification);

                    auto fx = TrackOps::addFxTrack (edit, nullptr);
                    auto buses = MixerOps::getFxBuses (edit);
                    expectEquals (buses.size(), 1);
                    MixerOps::addSend (*track, buses[0].number, true, -10.0f);
                    expect (s.save());
                }

                {
                    ProjectSession s (engine, editFile);
                    auto track = te::getAudioTracks (s.getEdit())[0];
                    auto inserts = MixerOps::getInserts (*track);
                    expectEquals (inserts.size(), 1);
                    expect (dynamic_cast<te::EqualiserPlugin*> (inserts[0]) != nullptr);

                    auto sends = MixerOps::getSends (*track);
                    expectEquals (sends.size(), 1);
                    if (sends.size() == 1)
                    {
                        expectWithinAbsoluteError (sends[0]->getGainDb(), -10.0f, 0.1f);
                        expect (MixerOps::isPostFader (*sends[0]));
                    }
                }
            }

            // Real third-party plugins: only when explicitly enabled.
            if (juce::SystemStats::getEnvironmentVariable ("CDAW_LOCAL_PLUGIN_TESTS", {}) != "1")
                return;

            beginTest ("Local: load Archetype Nolly X and process audio");
            {
                juce::File vst3 ("C:\\Program Files\\Common Files\\VST3\\Archetype Nolly X.vst3");
                if (! vst3.exists())
                {
                    logMessage ("Nolly X not installed, skipping");
                }
                else
                {
                    auto desc = findVst3 (engine, vst3);
                    expect (desc != nullptr, "No VST3 found in the bundle");

                    if (desc != nullptr)
                    {
                        logMessage ("Found: " + desc->name + " by " + desc->manufacturerName);
                        auto edit = te::test_utilities::createTestEdit (engine, 1);
                        auto track = te::getAudioTracks (*edit)[0];
                        auto src = writeSineFile ("nolly_in", 2.0, 0.3f, 110.0);
                        track->insertWaveClip ("di", src, { { te::TimePosition(), te::TimeDuration::fromSeconds (2.0) }, {} }, false);

                        auto plugin = PluginMenus::createPlugin (*edit, *desc);
                        expect (plugin != nullptr);
                        track->pluginList.insertPlugin (plugin, 0, nullptr);

                        if (auto ext = dynamic_cast<te::ExternalPlugin*> (plugin.get()))
                        {
                            expect (ext->getAudioPluginInstance() != nullptr, "Plugin instance failed to load: " + ext->getLoadError());
                            logMessage ("Latency: " + juce::String (ext->getLatencySeconds() * 1000.0, 2) + " ms");
                        }

                        auto out = te::test_utilities::renderToAudioBuffer (*edit);
                        auto peak = out.buffer.getMagnitude (0, out.buffer.getNumSamples());
                        logMessage ("Output peak: " + juce::String (peak, 4));
                        expect (peak > 0.0001f, "Nolly X produced silence");
                    }
                }
            }
        }
    };

    static PluginTests pluginTests;
}
