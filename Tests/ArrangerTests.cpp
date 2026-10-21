#include "TestEngine.h"
#include "Model/ArrangerOps.h"
#include "Model/AutomationOps.h"

namespace cdaw::test
{
    struct ArrangerTests : public juce::UnitTest
    {
        ArrangerTests() : juce::UnitTest ("Song sections", "CDAW") {}

        static te::TimeRange secs (double a, double b)
        {
            return { te::TimePosition::fromSeconds (a), te::TimePosition::fromSeconds (b) };
        }

        /** Three 2 s sections A, B, C, each with its own clip on the track. */
        struct Song
        {
            std::unique_ptr<te::Edit> edit;
            te::AudioTrack* track = nullptr;

            explicit Song (te::Engine& engine)
            {
                edit = te::test_utilities::createTestEdit (engine, 1);
                track = te::getAudioTracks (*edit)[0];

                const char* names[] = { "A", "B", "C" };
                for (int i = 0; i < 3; ++i)
                {
                    auto file = writeSineFile (juce::String ("sect_") + names[i], 2.0, 0.2f + 0.3f * (float) i);
                    auto clip = track->insertWaveClip (names[i], file, { secs (i * 2.0, i * 2.0 + 2.0), {} }, false);
                    clip->setName (names[i]);
                    ArrangerOps::addSection (*edit, secs (i * 2.0, i * 2.0 + 2.0), names[i]);
                }
            }

            juce::String sectionOrder() const
            {
                juce::String s;
                for (auto sec : ArrangerOps::getSections (*edit))
                    s << sec->getName();
                return s;
            }

            /** Name of the clip playing at `t`. */
            juce::String clipAt (double t) const
            {
                for (auto c : track->getClips())
                    if (c->getEditTimeRange().contains (te::TimePosition::fromSeconds (t)))
                        return c->getName();
                return "-";
            }
        };

        void runTest() override
        {
            auto& engine = getEngine();

            beginTest ("Sections are sorted, coloured by name and never overlap");
            {
                Song song (engine);
                expectEquals (song.sectionOrder(), juce::String ("ABC"));

                ArrangerOps::addSection (*song.edit, secs (1.0, 3.0), "Chorus");
                auto sections = ArrangerOps::getSections (*song.edit);
                expectEquals (sections.size(), 4);

                for (int i = 1; i < sections.size(); ++i)
                    expect (sections[i - 1]->getPosition().getEnd() <= sections[i]->getPosition().getStart() + te::TimeDuration::fromSeconds (1e-6),
                            "Sections overlap");

                expect (ArrangerOps::getColourForName ("Chorus 2") == ArrangerOps::getColourForName ("Chorus"));
            }

            beginTest ("Moving a section moves its material and closes the gap");
            {
                Song song (engine);
                auto sections = ArrangerOps::getSections (*song.edit);
                ArrangerOps::moveSection (*song.edit, *sections[2], te::TimePosition());   // C to the start

                expectEquals (song.sectionOrder(), juce::String ("CAB"));
                expectEquals (song.clipAt (1.0), juce::String ("C"));
                expectEquals (song.clipAt (3.0), juce::String ("A"));
                expectEquals (song.clipAt (5.0), juce::String ("B"));
                expectEquals (song.clipAt (7.0), juce::String ("-"));
                expectEquals (song.track->getClips().size(), 3);
            }

            beginTest ("Moving a section later works too");
            {
                Song song (engine);
                auto sections = ArrangerOps::getSections (*song.edit);
                ArrangerOps::moveSection (*song.edit, *sections[0], te::TimePosition::fromSeconds (6.0));   // A to the end

                expectEquals (song.sectionOrder(), juce::String ("BCA"));
                expectEquals (song.clipAt (1.0), juce::String ("B"));
                expectEquals (song.clipAt (5.0), juce::String ("A"));
            }

            beginTest ("Duplicating a section inserts a copy after it");
            {
                Song song (engine);
                ArrangerOps::duplicateSection (*song.edit, *ArrangerOps::getSections (*song.edit)[1]);

                expectEquals (song.sectionOrder(), juce::String ("ABBC"));
                expectEquals (song.clipAt (3.0), juce::String ("B"));
                expectEquals (song.clipAt (5.0), juce::String ("B"));
                expectEquals (song.clipAt (7.0), juce::String ("C"));
            }

            beginTest ("Deleting a section removes its material and closes the gap");
            {
                Song song (engine);
                ArrangerOps::deleteSection (*song.edit, *ArrangerOps::getSections (*song.edit)[0], nullptr);

                expectEquals (song.sectionOrder(), juce::String ("BC"));
                expectEquals (song.clipAt (1.0), juce::String ("B"));
                expectEquals (song.clipAt (3.0), juce::String ("C"));
                expectEquals (song.track->getClips().size(), 2);
            }

            beginTest ("A copied section brings its automation");
            {
                Song song (engine);
                auto* vol = AutomationOps::getVolumeParameter (*song.track);
                auto& curve = vol->getCurve();
                const float loud = te::decibelsToVolumeFaderPosition (0.0f);
                const float quiet = te::decibelsToVolumeFaderPosition (-30.0f);

                // B fades down inside its section; A and C stay loud.
                curve.addPoint (te::TimePosition::fromSeconds (2.0), loud, 0.0f, nullptr);
                curve.addPoint (te::TimePosition::fromSeconds (3.9), quiet, 0.0f, nullptr);
                curve.addPoint (te::TimePosition::fromSeconds (4.0), loud, 0.0f, nullptr);

                ArrangerOps::duplicateSection (*song.edit, *ArrangerOps::getSections (*song.edit)[1]);

                auto valueAt = [&] (double t) { return curve.getValueAt (te::TimePosition::fromSeconds (t), loud); };
                expectWithinAbsoluteError (valueAt (2.05), valueAt (4.05), 0.02f);   // start of B and its copy
                expectWithinAbsoluteError (valueAt (3.85), valueAt (5.85), 0.02f);   // end of B and its copy
                expectLessThan (valueAt (5.85), loud - 0.1f);
                expectWithinAbsoluteError (valueAt (6.5), loud, 0.02f);              // C after it is untouched
            }

            beginTest ("Resizing a section stops at its neighbours");
            {
                Song song (engine);
                auto b = ArrangerOps::getSections (*song.edit)[1];
                ArrangerOps::resizeSection (*b, secs (1.0, 5.0));
                expectWithinAbsoluteError (b->getPosition().getStart().inSeconds(), 2.0, 1e-6);
                expectWithinAbsoluteError (b->getPosition().getEnd().inSeconds(), 4.0, 1e-6);

                ArrangerOps::resizeSection (*b, secs (2.0, 3.0));
                expectWithinAbsoluteError (b->getPosition().getEnd().inSeconds(), 3.0, 1e-6);
                expectEquals (song.clipAt (3.5), juce::String ("B"));   // material stays put
            }
        }
    };

    static ArrangerTests arrangerTests;
}
