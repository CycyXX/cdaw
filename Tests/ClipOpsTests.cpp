#include "TestEngine.h"
#include "Model/ClipOps.h"
#include "Model/TrackOps.h"
#include "Model/EditViewState.h"

namespace cdaw::test
{
    struct ClipOpsTests : public juce::UnitTest
    {
        ClipOpsTests() : juce::UnitTest ("Clip editing", "CDAW") {}

        static te::WaveAudioClip* addClip (te::AudioTrack& t, const juce::File& f, double start, double len)
        {
            return t.insertWaveClip ("c", f, { { te::TimePosition::fromSeconds (start), te::TimeDuration::fromSeconds (len) }, {} }, false).get();
        }

        void runTest() override
        {
            auto& engine = getEngine();
            auto file = writeSineFile ("clipops_src", 8.0, 0.5f);

            beginTest ("Moving a clip onto another trims the covered part");
            {
                auto edit = te::test_utilities::createTestEdit (engine, 1);
                auto track = te::getAudioTracks (*edit)[0];
                auto a = addClip (*track, file, 0.0, 4.0);
                auto b = addClip (*track, file, 6.0, 2.0);

                // Move b to start at 3s: a must end at 3s.
                ClipOps::moveClips (*edit, { b }, te::TimeDuration::fromSeconds (-3.0), 0, false);
                expectWithinAbsoluteError (b->getPosition().getStart().inSeconds(), 3.0, 1e-6);
                expectWithinAbsoluteError (a->getPosition().getEnd().inSeconds(), 3.0, 1e-6);
                expectEquals (track->getClips().size(), 2);
            }

            beginTest ("Moving a clip into the middle of another splits it");
            {
                auto edit = te::test_utilities::createTestEdit (engine, 1);
                auto track = te::getAudioTracks (*edit)[0];
                addClip (*track, file, 0.0, 6.0);
                auto b = addClip (*track, file, 7.0, 1.0);

                ClipOps::moveClips (*edit, { b }, te::TimeDuration::fromSeconds (-5.0), 0, false);   // b now 2..3
                expectEquals (track->getClips().size(), 3);

                // Nothing else may overlap b.
                for (auto c : track->getClips())
                    if (c != b)
                        expect (! c->getEditTimeRange().overlaps (b->getEditTimeRange()), "Overlap left behind");
            }

            beginTest ("Copy-drag leaves the original and moves to another track");
            {
                auto edit = te::test_utilities::createTestEdit (engine, 2);
                auto t1 = te::getAudioTracks (*edit)[0];
                auto t2 = te::getAudioTracks (*edit)[1];
                auto a = addClip (*t1, file, 1.0, 2.0);

                auto moved = ClipOps::moveClips (*edit, { a }, te::TimeDuration::fromSeconds (1.0), 1, true);
                expectEquals (moved.size(), 1);
                expectEquals (t1->getClips().size(), 1);
                expectEquals (t2->getClips().size(), 1);
                expectWithinAbsoluteError (t2->getClips()[0]->getPosition().getStart().inSeconds(), 2.0, 1e-6);
            }

            beginTest ("Split at time");
            {
                auto edit = te::test_utilities::createTestEdit (engine, 1);
                te::SelectionManager sm (engine);
                auto track = te::getAudioTracks (*edit)[0];
                addClip (*track, file, 0.0, 4.0);

                ClipOps::splitAt (*edit, sm, te::TimePosition::fromSeconds (1.5));
                expectEquals (track->getClips().size(), 2);
                expectWithinAbsoluteError (track->getClips()[1]->getPosition().getStart().inSeconds(), 1.5, 1e-6);
                expectWithinAbsoluteError (track->getClips()[1]->getPosition().getOffset().inSeconds(), 1.5, 1e-6);
            }

            beginTest ("Duplicate places the copy after the selection");
            {
                auto edit = te::test_utilities::createTestEdit (engine, 1);
                auto track = te::getAudioTracks (*edit)[0];
                auto a = addClip (*track, file, 1.0, 2.0);

                auto copies = ClipOps::duplicateClips (*edit, { a });
                expectEquals (copies.size(), 1);
                expectWithinAbsoluteError (copies[0]->getPosition().getStart().inSeconds(), 3.0, 1e-6);
            }

            beginTest ("Trim start can't go before the source material");
            {
                auto edit = te::test_utilities::createTestEdit (engine, 1);
                auto track = te::getAudioTracks (*edit)[0];
                auto a = addClip (*track, file, 2.0, 2.0);

                ClipOps::trimStart (*a, te::TimePosition::fromSeconds (0.5));
                expectWithinAbsoluteError (a->getPosition().getStart().inSeconds(), 2.0, 1e-6);

                ClipOps::trimStart (*a, te::TimePosition::fromSeconds (3.0));
                expectWithinAbsoluteError (a->getPosition().getStart().inSeconds(), 3.0, 1e-6);
                expectWithinAbsoluteError (a->getPosition().getOffset().inSeconds(), 1.0, 1e-6);
                expectWithinAbsoluteError (a->getPosition().getEnd().inSeconds(), 4.0, 1e-6);
            }

            beginTest ("Grid snapping at 120 bpm 4/4");
            {
                auto edit = te::test_utilities::createTestEdit (engine, 1);
                edit->tempoSequence.getTempo (0)->setBpm (120.0);
                te::SelectionManager sm (engine);
                EditViewState vs (*edit, sm);

                vs.gridIndex = 2;   // 1/4 = 0.5 s
                expectWithinAbsoluteError (vs.snapAlways (te::TimePosition::fromSeconds (0.74)).inSeconds(), 0.5, 1e-6);
                expectWithinAbsoluteError (vs.snapAlways (te::TimePosition::fromSeconds (0.76)).inSeconds(), 1.0, 1e-6);

                vs.gridIndex = 0;   // bar = 2 s
                expectWithinAbsoluteError (vs.snapAlways (te::TimePosition::fromSeconds (2.9)).inSeconds(), 2.0, 1e-6);
                expectWithinAbsoluteError (vs.snapAlways (te::TimePosition::fromSeconds (3.1)).inSeconds(), 4.0, 1e-6);
            }

            beginTest ("Track kinds");
            {
                auto edit = te::test_utilities::createTestEdit (engine, 1);
                auto fx = TrackOps::addFxTrack (*edit, nullptr);
                auto group = TrackOps::addFolderTrack (*edit, nullptr, true);
                auto inst = TrackOps::addInstrumentTrack (*edit, nullptr, nullptr);

                expect (TrackOps::getKind (*fx) == TrackKind::fx);
                expect (TrackOps::getKind (*group) == TrackKind::group);
                expect (TrackOps::getKind (*inst) == TrackKind::instrument);
                expect (TrackOps::getKind (*te::getAudioTracks (*edit)[0]) == TrackKind::audio);
            }
        }
    };

    static ClipOpsTests clipOpsTests;
}
