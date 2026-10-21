#include "TestEngine.h"
#include "Model/TempoOps.h"

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
