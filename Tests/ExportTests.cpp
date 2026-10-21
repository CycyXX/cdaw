#include "TestEngine.h"
#include "Render/ExportService.h"

namespace cdaw::test
{
    struct ExportTests : public juce::UnitTest
    {
        ExportTests() : juce::UnitTest ("Export", "CDAW") {}

        static bool runJob (std::vector<ExportService::Job::Item> items, juce::StringArray& results)
        {
            std::atomic<bool> done { false };
            bool success = false;

            ExportService::Job job (std::move (items), [&] (bool ok, juce::StringArray r)
            {
                success = ok;
                results = r;
                done = true;
            });

            te::test_utilities::runDispatchLoopUntilTrue (done);
            return success;
        }

        void runTest() override
        {
            auto& engine = getEngine();
            auto outDir = getTestDir().getChildFile ("exports");
            outDir.deleteRecursively();
            outDir.createDirectory();

            auto edit = te::test_utilities::createTestEdit (engine, 2, te::Edit::forRendering);
            auto t1 = te::getAudioTracks (*edit)[0];
            auto t2 = te::getAudioTracks (*edit)[1];
            t1->insertWaveClip ("a", writeSineFile ("exp_a", 3.0, 0.25f), { { te::TimePosition(), te::TimeDuration::fromSeconds (3.0) }, {} }, false);
            t2->insertWaveClip ("b", writeSineFile ("exp_b", 2.0, 0.25f, 500.0), { { te::TimePosition::fromSeconds (1.0), te::TimeDuration::fromSeconds (2.0) }, {} }, false);

            beginTest ("Mixdown: 24-bit / 48 kHz WAV with the song length");
            {
                ExportService::Options o;
                o.bitDepth = 24;
                o.sampleRate = 48000.0;
                o.tailSeconds = 0.0;
                o.dither = false;
                auto dest = outDir.getChildFile ("mix.wav");

                juce::StringArray results;
                expect (runJob ({ { ExportService::makeParameters (*edit, o, dest, {}), "Mixdown" } }, results), results.joinIntoString ("; "));
                expect (dest.existsAsFile());

                juce::AudioFormatManager fm;
                fm.registerBasicFormats();

                if (auto reader = std::unique_ptr<juce::AudioFormatReader> (fm.createReaderFor (dest)))
                {
                    expectEquals ((int) reader->bitsPerSample, 24);
                    expectWithinAbsoluteError (reader->sampleRate, 48000.0, 0.1);
                    expectWithinAbsoluteError ((double) reader->lengthInSamples / reader->sampleRate, 3.0, 0.05);
                }
                else
                {
                    expect (false, "Couldn't read the exported file");
                }
            }

            beginTest ("Stems: one file per track");
            {
                ExportService::Options o;
                o.tailSeconds = 0.0;
                std::vector<ExportService::Job::Item> items;
                items.push_back ({ ExportService::makeParameters (*edit, o, outDir.getChildFile ("stem1.wav"), { t1 }), "1" });
                items.push_back ({ ExportService::makeParameters (*edit, o, outDir.getChildFile ("stem2.wav"), { t2 }), "2" });

                juce::StringArray results;
                expect (runJob (std::move (items), results), results.joinIntoString ("; "));
                expect (outDir.getChildFile ("stem1.wav").existsAsFile());
                expect (outDir.getChildFile ("stem2.wav").existsAsFile());
            }

            beginTest ("MIDI export round trip");
            {
                auto midiEdit = te::test_utilities::createTestEdit (engine, 1);
                auto track = te::getAudioTracks (*midiEdit)[0];
                auto mc = track->insertMIDIClip ({ te::TimePosition(), te::TimePosition::fromSeconds (10.0) }, nullptr);   // 60 bpm: 10 beats

                for (int i = 0; i < 8; ++i)
                    mc->getSequence().addNote (36 + i, te::BeatPosition::fromBeats (i), te::BeatDuration::fromBeats (0.5), 100, 0, nullptr);

                auto dest = outDir.getChildFile ("song.mid");
                expect (ExportService::exportMidi (*midiEdit, {}, dest));

                juce::MidiFile mf;
                juce::FileInputStream in (dest);
                expect (mf.readFrom (in));
                expectEquals (mf.getNumTracks(), 2);

                int noteOns = 0;
                if (auto* seq = mf.getTrack (1))
                    for (auto* ev : *seq)
                        if (ev->message.isNoteOn())
                            ++noteOns;

                expectEquals (noteOns, 8);
            }
        }
    };

    static ExportTests exportTests;
}
