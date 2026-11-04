// Timings for the efficiency work. They only print: nothing here can fail on a slow machine.
// Enabled with CDAW_PERF=1. Compare numbers from the same machine and build configuration only.
// Painting here uses the software renderer, not Direct2D, so it shows CDAW's own work per paint
// and not the graphics card's.

#include "TestEngine.h"
#include "DspScenarios.h"
#include "Model/AutomationOps.h"
#include "Model/ClipOps.h"
#include "Model/CompOps.h"
#include "Model/EditViewState.h"
#include "Model/ProjectSession.h"
#include "Model/TrackOps.h"
#include "Plugins/CompressorPlugin.h"
#include "Plugins/DistPlugin.h"
#include "Plugins/EqPlugin.h"
#include "Plugins/MultibandPlugin.h"
#include "Plugins/ReverbPlugin.h"
#include "UI/Arrange/ArrangeView.h"
#include "UI/LookAndFeel/CDAWLookAndFeel.h"
#include "UI/Mixer/MixerView.h"
#include "UI/PianoRoll/PianoRoll.h"
#include <tracktion_graph/tracktion_graph.h>
#include <tracktion_engine/playback/graph/tracktion_TracktionEngineNode.h>
#include <tracktion_engine/playback/graph/tracktion_EditNodeBuilder.h>

namespace cdaw::test
{
    struct PerformanceTests : public juce::UnitTest
    {
        PerformanceTests() : juce::UnitTest ("Performance", "CDAW") {}

        static constexpr int numAudioTracks = 48, clipsPerTrack = 25, numTakeTracks = 8, passesPerTake = 6;
        static constexpr int numMidiTracks = 16, notesPerClip = 2000, automationPoints = 500;

        static double now()     { return juce::Time::getMillisecondCounterHiRes(); }

        static void pump (int ms)
        {
            juce::MessageManager::getInstance()->runDispatchLoopUntil (ms);
        }

        /** Runs what's queued, and what that queues in turn, without waiting any longer than that. */
        static void settle()
        {
            for (int round = 0; round < 3; ++round)
            {
                auto done = std::make_shared<bool> (false);
                juce::MessageManager::callAsync ([done] { *done = true; });

                while (! *done)
                    pump (0);
            }
        }

        /** Median of the runs, so one interruption doesn't count. */
        template <typename Work>
        double timed (const juce::String& label, int runs, Work&& work)
        {
            std::vector<double> times;

            for (int i = 0; i < runs; ++i)
            {
                auto start = now();
                work (i);
                times.push_back (now() - start);
            }

            std::sort (times.begin(), times.end());
            auto median = times[times.size() / 2];
            logMessage ("PERF " + label.paddedRight (' ', 44) + juce::String (median, 2).paddedLeft (' ', 10) + " ms");
            return median;
        }

        void runTest() override
        {
            if (juce::SystemStats::getEnvironmentVariable ("CDAW_PERF", {}) != "1")
                return;

            beginTest ("DSP engines");
            measureDsp();

            beginTest ("Project and UI");
            measureProject();
            expect (true);
        }

        //==============================================================================
        void measureDsp()
        {
            logMessage ("PERF DSP, one core at 48 kHz, 128-sample blocks: % of real time, on signal / on silence");
            constexpr double seconds = 3.0;
            constexpr int blockSize = 128;

            for (auto& scenario : dsp::allScenarios())
            {
                auto cost = [&] (bool silent)
                {
                    auto buffer = dsp::makeInput (scenario.rate, seconds);

                    if (silent || scenario.instrument)
                        buffer.clear();

                    auto process = scenario.create (scenario.rate);

                    // Silence as it is when nothing has played for a while: some engines rest by then.
                    if (silent)
                    {
                        for (int run = 0; run < 3; ++run)
                            for (int pos = 0; pos + blockSize <= buffer.getNumSamples(); pos += blockSize)
                            {
                                juce::MidiBuffer none;
                                const auto time = pos / scenario.rate;
                                process ({ buffer, pos, blockSize, none, { false, time, time * dsp::bpm / 60.0 }, false });
                            }
                    }

                    auto start = now();

                    for (int pos = 0; pos + blockSize <= buffer.getNumSamples(); pos += blockSize)
                    {
                        juce::MidiBuffer midi;

                        if (! silent && scenario.instrument)
                            dsp::fillMidi (midi, pos, blockSize, scenario.rate);

                        const auto time = pos / scenario.rate;
                        process ({ buffer, pos, blockSize, midi, { false, time, time * dsp::bpm / 60.0 }, false });
                    }

                    return (now() - start) / (seconds * 10.0);
                };

                // The best of three: anything slower was interrupted.
                auto signal = juce::jmin (cost (false), cost (false), cost (false));
                auto silence = juce::jmin (cost (true), cost (true), cost (true));
                logMessage ("PERF " + scenario.name.paddedRight (' ', 34) + juce::String (signal, 2).paddedLeft (' ', 8) + " %"
                            + juce::String (silence, 2).paddedLeft (' ', 8) + " %");
            }
        }

        //==============================================================================
        static te::TimePosition at (double seconds)     { return te::TimePosition::fromSeconds (seconds); }

        void buildProject (ProjectSession& session)
        {
            auto& edit = session.getEdit();
            auto wave = writeSineFile ("perf_wave", 40.0, 0.4f, 220.0);

            // The saved project also serves to measure the app itself: it plays, but silently.
            edit.getMasterVolumePlugin()->setVolumeDb (-100.0f);

            // Comped tracks first, so they're on screen: each clip holds several recorded passes.
            for (int t = 0; t < numTakeTracks; ++t)
            {
                auto track = t == 0 ? te::getAudioTracks (edit)[0]
                                    : TrackOps::addAudioTrack (edit, nullptr, nullptr, "Takes " + juce::String (t + 1)).get();

                for (int pass = 0; pass < passesPerTake; ++pass)
                {
                    auto file = writeSineFile ("perf_take_" + juce::String (t) + "_" + juce::String (pass), 20.0, 0.3f, 110.0 * (pass + 1));
                    auto clip = track->insertWaveClip ("Take", file, { { at (0.0), te::TimeDuration::fromSeconds (20.0) }, {} }, false);
                    CompOps::absorbRecording (*clip);
                }
            }

            for (int t = 0; t < numAudioTracks; ++t)
            {
                auto track = TrackOps::addAudioTrack (edit, nullptr, nullptr, "Audio " + juce::String (t + 1));

                for (int c = 0; c < clipsPerTrack; ++c)
                    track->insertWaveClip ("Clip", wave, { { at (c * 9.0), te::TimeDuration::fromSeconds (8.0) },
                                                           te::TimeDuration::fromSeconds ((double) (c % 20)) }, false);

                auto eq = edit.getPluginCache().createNewPlugin (EqPlugin::xmlTypeName, {});
                track->pluginList.insertPlugin (eq, 0, nullptr);
                auto comp = edit.getPluginCache().createNewPlugin (CompressorPlugin::xmlTypeName, {});
                track->pluginList.insertPlugin (comp, 1, nullptr);

                if (t < 4)
                {
                    auto* volume = AutomationOps::getVolumeParameter (*track);
                    auto& curve = volume->getCurve();

                    for (int p = 0; p < automationPoints; ++p)
                        curve.addPoint (at (p * 0.4), (p % 2) == 0 ? 0.3f : 0.7f, 0.0f, nullptr);

                    AutomationOps::showLane (*track, *volume);
                }
            }

            for (int t = 0; t < numMidiTracks; ++t)
            {
                auto track = TrackOps::addInstrumentTrack (edit, nullptr, nullptr);
                auto clip = track->insertMIDIClip ({ at (0.0), at (200.0) }, nullptr);
                auto& sequence = clip->getSequence();

                for (int n = 0; n < notesPerClip; ++n)
                    sequence.addNote (36 + (n * 7) % 48, te::BeatPosition::fromBeats (n * 0.2), te::BeatDuration::fromBeats (0.15), 90, 0, nullptr);
            }
        }

        void measureProject()
        {
            CDAWLookAndFeel laf;
            juce::LookAndFeel::setDefaultLookAndFeel (&laf);

            auto& engine = getEngine();
            auto dir = getTestDir().getChildFile ("PerfProject");
            dir.deleteRecursively();
            dir.createDirectory();
            auto editFile = ProjectSession::createProjectFolder (dir, "Perf");

            {
                auto buildStart = now();
                ProjectSession session (engine, editFile);
                buildProject (session);
                pump (500);
                logMessage ("PERF project built in " + juce::String (now() - buildStart, 0) + " ms: "
                            + juce::String (te::getAudioTracks (session.getEdit()).size()) + " tracks");

                measureModel (session);
                measureViews (session);
            }

            timed ("Check a project is readable", 3, [&] (int) { ProjectSession::isReadable (engine, editFile, {}); });
            timed ("Open a project", 3, [&] (int) { ProjectSession reopened (engine, editFile); });

            juce::LookAndFeel::setDefaultLookAndFeel (nullptr);

            writeHeavyProject();
        }

        /** A project that works the CPU hard, for playing in the app itself (silently). */
        void writeHeavyProject()
        {
            auto dir = getTestDir().getChildFile ("PerfHeavy");
            dir.deleteRecursively();
            dir.createDirectory();

            ProjectSession session (getEngine(), ProjectSession::createProjectFolder (dir, "Heavy"));
            auto& edit = session.getEdit();
            edit.getMasterVolumePlugin()->setVolumeDb (-100.0f);
            auto wave = writeSineFile ("perf_heavy_wave", 60.0, 0.4f, 110.0);
            const int numTracks = juce::jmax (1, juce::SystemStats::getEnvironmentVariable ("CDAW_PERF_HEAVY_TRACKS", "30").getIntValue());

            for (int t = 0; t < numTracks; ++t)
            {
                auto track = t == 0 ? te::getAudioTracks (edit)[0]
                                    : TrackOps::addAudioTrack (edit, nullptr, nullptr, "Heavy " + juce::String (t + 1)).get();

                for (int c = 0; c < 5; ++c)
                    track->insertWaveClip ("Clip", wave, { { at (c * 60.0), te::TimeDuration::fromSeconds (60.0) }, {} }, false);

                int slot = 0;

                for (auto* type : { DistPlugin::xmlTypeName, MultibandPlugin::xmlTypeName, ReverbPlugin::xmlTypeName })
                    track->pluginList.insertPlugin (edit.getPluginCache().createNewPlugin (type, {}), slot++, nullptr);
            }

            expect (session.save());
            logMessage ("PERF heavy project: " + session.getEditFile().getFullPathName());
        }

        void measureModel (ProjectSession& session)
        {
            auto& edit = session.getEdit();

            timed ("Save", 3, [&] (int) { session.save(); });
            logMessage ("PERF project file is " + juce::File::descriptionOfSizeInBytes (session.getEditFile().getSize()));

            auto firstTrack = te::getAudioTracks (edit)[0];

            timed ("Autosave, time the UI is held up", 3, [&] (int i)
            {
                firstTrack->setName ("Audio " + juce::String (i));
                edit.markAsChanged();
                session.autosave();
            });

            // Let any write in progress finish before the timings below.
            pump (1000);
            session.discardAutosave();

            // Without an audio device the playback context builds nothing, so build what it would.
            tracktion::graph::PlayHead playHead;
            tracktion::graph::PlayHeadState playHeadState (playHead);
            te::ProcessState processState (playHeadState, edit.tempoSequence);

            timed ("Build the audio graph from the project", 5, [&] (int)
            {
                te::CreateNodeParams params { processState };
                params.sampleRate = 48000.0;
                params.blockSize = 128;
                auto node = te::createNodeForEdit (edit, params);
                expect (node != nullptr);
            });
        }

        void measureViews (ProjectSession& session)
        {
            auto& edit = session.getEdit();
            auto& vs = session.getViewState();
            vs.pixelsPerSecond = 30.0;

            auto arrange = std::make_unique<ArrangeView> (session, vs);

            timed ("Build the arrangement view", 1, [&] (int)
            {
                arrange = nullptr;
                arrange = std::make_unique<ArrangeView> (session, vs);
                arrange->setBounds (0, 0, 1600, 1000);
                settle();
            });

            // Thumbnails load in the background.
            pump (3000);

            juce::Image image (juce::Image::ARGB, arrange->getWidth(), arrange->getHeight(), true, juce::SoftwareImageType());

            auto paint = [&] (juce::Rectangle<int> area)
            {
                juce::Graphics g (image);
                g.reduceClipRegion (area);
                arrange->paintEntireComponent (g, true);
            };

            timed ("Paint the whole arrangement", 9, [&] (int) { paint (arrange->getLocalBounds()); });

            timed ("Paint a 5 px playhead strip", 41, [&] (int i)
            {
                paint ({ 300 + i * 20, 0, 5, arrange->getHeight() });
            });

            timed ("Scroll one step, with painting", 21, [&] (int i)
            {
                vs.viewStart = i * 0.5;
                settle();
                paint (arrange->getLocalBounds());
            });

            vs.viewStart = 0.0;

            timed ("Zoom one step, with painting", 21, [&] (int i)
            {
                vs.pixelsPerSecond = 20.0 + i * 1.5;
                settle();
                paint (arrange->getLocalBounds());
            });

            vs.pixelsPerSecond = 30.0;
            pump (10);

            auto& selection = session.getSelectionManager();

            timed ("Select every clip", 3, [&] (int)
            {
                selection.deselectAll();
                settle();

                for (auto track : te::getClipTracks (edit))
                    for (auto clip : track->getClips())
                        selection.addToSelection (clip);

                settle();
            });

            selection.deselectAll();
            pump (10);

            // One clip splits on a busy lane, and undo puts it back.
            auto track = te::getAudioTracks (edit)[numTakeTracks];
            const auto clipsBefore = track->getClips().size();

            timed ("Split a clip on a lane, then undo", 7, [&] (int)
            {
                selection.selectOnly (track->getClips()[3]);
                edit.getUndoManager().beginNewTransaction ("Split");
                ClipOps::splitAt (edit, selection, at (3 * 9.0 + 4.0));
                settle();
                expectEquals (track->getClips().size(), clipsBefore + 1, "The split didn't happen");
                edit.getUndoManager().undo();
                settle();
                expectEquals (track->getClips().size(), clipsBefore, "The undo didn't happen");
            });

            selection.deselectAll();
            arrange = nullptr;

            timed ("Build the mixer", 1, [&] (int)
            {
                MixerView mixer (vs);
                mixer.setBounds (0, 0, 1600, 400);
                settle();
            });

            // A big MIDI clip in the piano roll.
            auto instrument = te::getAudioTracks (edit).getLast();
            auto clip = dynamic_cast<te::MidiClip*> (instrument->getClips()[0]);

            if (clip != nullptr)
            {
                PianoRoll roll (vs, clip);
                roll.setBounds (0, 0, 1600, 700);
                pump (10);

                juce::Image rollImage (juce::Image::ARGB, roll.getWidth(), roll.getHeight(), true, juce::SoftwareImageType());

                timed ("Paint the piano roll, 2000 notes", 9, [&] (int)
                {
                    juce::Graphics g (rollImage);
                    roll.paintEntireComponent (g, true);
                });
            }
        }
    };

    static PerformanceTests performanceTests;
}
