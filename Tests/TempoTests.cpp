#include "TestEngine.h"
#include "Model/TempoOps.h"
#include "Plugins/DelayPlugin.h"

namespace cdaw::test
{
    struct TempoTests : public juce::UnitTest
    {
        TempoTests() : juce::UnitTest ("Tempo and markers", "CDAW") {}

        void runTest() override
        {
            auto& engine = getEngine();

            beginTest ("Tempo change and time signature");
            {
                auto edit = te::test_utilities::createTestEdit (engine, 1);
                edit->tempoSequence.getTempo (0)->setBpm (120.0);

                // Bar 3 starts at 4 s at 120 bpm 4/4.
                TempoOps::setTempoAt (*edit, te::TimePosition::fromSeconds (4.0), 60.0);
                expectEquals (edit->tempoSequence.getNumTempos(), 2);
                expectWithinAbsoluteError (edit->tempoSequence.getTempoAt (te::TimePosition::fromSeconds (5.0)).getBpm(), 60.0, 0.01);

                // Editing the same spot doesn't stack another change.
                TempoOps::setTempoAt (*edit, te::TimePosition::fromSeconds (4.0), 90.0);
                expectEquals (edit->tempoSequence.getNumTempos(), 2);

                TempoOps::setTimeSigAt (*edit, te::TimePosition::fromSeconds (10.0), "7/8");
                expectEquals (edit->tempoSequence.getNumTimeSigs(), 2);
                expectEquals (edit->tempoSequence.getTimeSigAt (te::TimePosition::fromSeconds (12.0)).getStringTimeSig(), juce::String ("7/8"));
            }

            beginTest ("Positions typed as bar.beat.tick or as times, and shown as bar.beat.tick");
            {
                auto edit = te::test_utilities::createTestEdit (engine, 1);
                edit->tempoSequence.getTempo (0)->setBpm (120.0);
                auto at = [&] (const char* text) { return TempoOps::parsePosition (*edit, text); };
                auto seconds = [] (std::optional<te::TimePosition> t) { return t ? t->inSeconds() : -1.0; };

                // 120 bpm in 4/4: a beat is 0.5 s, a bar 2 s.
                expectWithinAbsoluteError (seconds (at ("1")), 0.0, 1.0e-9);
                expectWithinAbsoluteError (seconds (at ("2")), 2.0, 1.0e-9);
                expectWithinAbsoluteError (seconds (at ("3.2")), 4.5, 1.0e-9);
                expectWithinAbsoluteError (seconds (at ("1.1.480")), 0.25, 1.0e-9);
                expectWithinAbsoluteError (seconds (at ("1:30.5")), 90.5, 1.0e-9);
                expectWithinAbsoluteError (seconds (at ("12.5 s")), 12.5, 1.0e-9);
                for (auto* bad : { "", "abc", "0", "2.0", "1.1.960", "1.2.3.4", "1:x" })
                    expect (! at (bad).has_value(), juce::String ("not a position: \"") + bad + "\"");

                expectEquals (TempoOps::formatBarsBeats (*edit, te::TimePosition::fromSeconds (4.5)), juce::String ("3.2.000"));
                expectEquals (TempoOps::formatBarsBeats (*edit, te::TimePosition::fromSeconds (0.25)), juce::String ("1.1.480"));
                expectEquals (TempoOps::formatTime (te::TimePosition::fromSeconds (90.5)), juce::String ("1:30.500"));

                // What's shown reads back as the same place (to a tick), across a tempo change.
                TempoOps::setTempoAt (*edit, te::TimePosition::fromSeconds (8.0), 90.0);
                for (double t : { 0.0, 1.234, 7.99, 8.0, 13.37, 60.0 })
                {
                    const auto shown = TempoOps::formatBarsBeats (*edit, te::TimePosition::fromSeconds (t));
                    const auto back = seconds (at (shown.toRawUTF8()));
                    expectWithinAbsoluteError (back, t, 0.5 / 960.0 + 1.0e-6, shown);
                }
            }

            beginTest ("Plugins read the tempo without touching the model, as they run on audio threads");
            {
                auto edit = te::test_utilities::createTestEdit (engine, 1);
                auto& ts = edit->tempoSequence;
                ts.getTempo (0)->setBpm (120.0);
                TempoOps::setTempoAt (*edit, te::TimePosition::fromSeconds (4.0), 60.0);
                ts.getBpmAt ({});   // applies the changes, as the message thread soon would

                auto plugin = edit->getPluginCache().createNewPlugin (DelayPlugin::xmlTypeName, {});
                te::getAudioTracks (*edit)[0]->pluginList.insertPlugin (plugin, 0, nullptr);
                auto delay = dynamic_cast<DelayPlugin*> (plugin.get());
                expect (delay != nullptr);

                juce::AudioBuffer<float> buffer (2, 128);

                auto contextAt = [&] (double seconds)
                {
                    const auto start = te::TimePosition::fromSeconds (seconds);
                    return te::PluginRenderContext (&buffer, 0, 128, nullptr, 0.0,
                                                    { start, start + te::TimeDuration::fromSeconds (128.0 / 48000.0) },
                                                    true, false, false, false);
                };

                // The same answers as asking the sequence, on either side of the change.
                for (double seconds : { 0.0, 1.0, 3.99, 4.0, 4.5, 30.0 })
                {
                    const auto time = te::TimePosition::fromSeconds (seconds);
                    expectWithinAbsoluteError (delay->getBpm (contextAt (seconds)), ts.getBpmAt (time), 1.0e-9);
                    expectWithinAbsoluteError (delay->getSongPosition (contextAt (seconds)).beats, ts.toBeats (time).inBeats(), 1.0e-9);
                }

                // A change still waiting to be applied isn't applied by the plugin: that would
                // allocate and take the audio callback lock on an audio thread. (Changing the tree
                // is what undo does; setBpm applies it at once.)
                ts.getTempo (0)->state.setProperty (te::IDs::bpm, 90.0, nullptr);
                expectWithinAbsoluteError (delay->getBpm (contextAt (1.0)), 120.0, 1.0e-9, "The plugin applied the tempo change itself");

                ts.getBpmAt ({});
                expectWithinAbsoluteError (delay->getBpm (contextAt (1.0)), 90.0, 1.0e-9, "The plugin didn't get the new tempo");
            }

            beginTest ("Markers and navigation");
            {
                auto edit = te::test_utilities::createTestEdit (engine, 1);
                TempoOps::addMarker (*edit, te::TimePosition::fromSeconds (2.0), "Verse");
                TempoOps::addMarker (*edit, te::TimePosition::fromSeconds (8.0), "Chorus");
                expectEquals (TempoOps::getMarkers (*edit).size(), 2);

                expectWithinAbsoluteError (TempoOps::getNextMarkerTime (*edit, te::TimePosition(), true).inSeconds(), 2.0, 1e-3);
                expectWithinAbsoluteError (TempoOps::getNextMarkerTime (*edit, te::TimePosition::fromSeconds (2.0), true).inSeconds(), 8.0, 1e-3);
                expectWithinAbsoluteError (TempoOps::getNextMarkerTime (*edit, te::TimePosition::fromSeconds (8.0), false).inSeconds(), 2.0, 1e-3);
            }
        }
    };

    static TempoTests tempoTests;
}
