#include "TestEngine.h"
#include "Model/AutomationOps.h"

namespace cdaw::test
{
    struct AutomationTests : public juce::UnitTest
    {
        AutomationTests() : juce::UnitTest ("Automation", "CDAW") {}

        void runTest() override
        {
            auto& engine = getEngine();

            beginTest ("Volume automation ramp lowers the rendered level");
            {
                auto edit = te::test_utilities::createTestEdit (engine, 1);
                auto track = te::getAudioTracks (*edit)[0];
                track->insertWaveClip ("s", writeSineFile ("auto_src", 4.0, 0.5f),
                                       { { te::TimePosition(), te::TimeDuration::fromSeconds (4.0) }, {} }, false);

                auto* vol = AutomationOps::getVolumeParameter (*track);
                expect (vol != nullptr);

                auto& curve = vol->getCurve();
                curve.addPoint (te::TimePosition(), te::decibelsToVolumeFaderPosition (0.0f), 0.0f, nullptr);
                curve.addPoint (te::TimePosition::fromSeconds (4.0), te::decibelsToVolumeFaderPosition (-60.0f), 0.0f, nullptr);
                expectEquals (curve.getNumPoints(), 2);

                auto out = te::test_utilities::renderToAudioBuffer (*edit);
                auto early = te::test_utilities::getRMSLevel (out, { te::TimePosition::fromSeconds (0.1), te::TimePosition::fromSeconds (0.6) }, 0);
                auto late  = te::test_utilities::getRMSLevel (out, { te::TimePosition::fromSeconds (3.2), te::TimePosition::fromSeconds (3.7) }, 0);
                logMessage ("RMS early " + juce::String (early, 4) + "  late " + juce::String (late, 4));
                expectGreaterThan (early, late * 4.0f);
            }

            beginTest ("Lane keys round-trip and parameter lookup");
            {
                auto edit = te::test_utilities::createTestEdit (engine, 1);
                auto track = te::getAudioTracks (*edit)[0];
                auto* vol = AutomationOps::getVolumeParameter (*track);
                AutomationOps::showLane (*track, *vol);

                auto keys = AutomationOps::getLaneKeys (*track);
                expectEquals (keys.size(), 1);
                expect (AutomationOps::findParameter (*track, keys[0]) == vol);

                AutomationOps::hideLane (*track, keys[0]);
                expect (AutomationOps::getLaneKeys (*track).isEmpty());
            }
        }
    };

    static AutomationTests automationTests;
}
