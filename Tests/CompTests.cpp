#include "TestEngine.h"
#include "Model/ClipOps.h"
#include "Model/CompOps.h"
#include "Model/GroupEdit.h"
#include "Model/MissingMedia.h"
#include "UI/PianoRoll/PianoRoll.h"
#include "Model/TrackOps.h"

namespace cdaw::test
{
    struct CompTests : public juce::UnitTest
    {
        CompTests() : juce::UnitTest ("Takes and comping", "CDAW") {}

        void runTest() override
        {
            auto& engine = getEngine();

            // Three takes of the same 4 s riff at different levels (as loop recording would leave them).
            auto take1 = writeSineFile ("take1", 4.0, 0.1f);
            auto take2 = writeSineFile ("take2", 4.0, 0.3f);
            auto take3 = writeSineFile ("take3", 4.0, 0.6f);

            auto edit = te::test_utilities::createTestEdit (engine, 1);
            auto track = te::getAudioTracks (*edit)[0];
            auto clip = track->insertWaveClip ("riff", take1, { { te::TimePosition(), te::TimeDuration::fromSeconds (4.0) }, {} }, false);
            clip->addTake (take1);
            clip->addTake (take2);
            clip->addTake (take3);

            beginTest ("Take list and switching");
            {
                expectEquals (CompOps::getNumTakes (*clip), 3);
                expectEquals (CompOps::getActiveTake (*clip), 0);
                CompOps::setActiveTake (*clip, 1);
                expectEquals (CompOps::getActiveTake (*clip), 1);
                expectEquals (CompOps::getNumTakes (*clip), 3, "Switching must not delete takes");
                CompOps::setActiveTake (*clip, 0);
            }

            beginTest ("Comp the middle of the clip to take 3");
            {
                CompOps::compRange (*track, { te::TimePosition::fromSeconds (1.0), te::TimePosition::fromSeconds (3.0) }, 2);
                auto clips = track->getClips();
                expectEquals (clips.size(), 3);

                auto out = te::test_utilities::renderToAudioBuffer (*edit);
                auto peakIn = [&] (double a, double b)
                {
                    auto s = (int) (a * out.sampleRate), e = (int) (b * out.sampleRate);
                    return out.buffer.getMagnitude (0, s, e - s);
                };

                expectWithinAbsoluteError (peakIn (0.2, 0.8), 0.1f, 0.02f);
                expectWithinAbsoluteError (peakIn (1.2, 2.8), 0.6f, 0.05f);
                expectWithinAbsoluteError (peakIn (3.2, 3.8), 0.1f, 0.02f);

                // Every piece still knows all its takes.
                for (auto c : track->getClips())
                    expectEquals (CompOps::getNumTakes (*c), 3);
            }

            beginTest ("Unpack takes to tracks");
            {
                auto pieces = track->getClips();
                if (auto wc = dynamic_cast<te::WaveAudioClip*> (pieces[0]))
                {
                    auto created = CompOps::unpackTakesToTracks (*wc);
                    expectEquals (created.size(), 3);
                    expectEquals (te::getAudioTracks (*edit).size(), 4);
                }
            }

            recordingOverAudio();
            recordingOverMidi();
            takeHousekeeping();
            missingTakeFiles();
            editorOfARemovedClip();
            groupEditing();
        }

        static te::TimePosition at (double seconds)     { return te::TimePosition::fromSeconds (seconds); }

        static te::Clip* clipAt (te::ClipTrack& t, double seconds)
        {
            for (auto c : t.getClips())
                if (c->getEditTimeRange().contains (at (seconds)))
                    return c;

            return nullptr;
        }

        /** Stands in for a finished recording: Tracktion leaves the new clip on top of the old ones. */
        static te::WaveAudioClip::Ptr recordAudio (te::AudioTrack& t, const juce::File& file, double start, double seconds)
        {
            auto c = t.insertWaveClip (file.getFileNameWithoutExtension(), file, { { at (start), te::TimeDuration::fromSeconds (seconds) }, {} }, false);
            CompOps::absorbRecording (*c);
            return c;
        }

        static te::MidiClip::Ptr recordMidi (te::AudioTrack& t, int note, double start, double seconds)
        {
            auto c = te::insertMIDIClip (t, "midi", { at (start), at (start + seconds) });
            c->getSequence().addNote (note, {}, te::BeatDuration::fromBeats (1.0), 100, 0, nullptr);
            CompOps::absorbRecording (*c);
            return c;
        }

        void recordingOverAudio()
        {
            auto& engine = getEngine();
            auto first = writeSineFile ("pass1", 8.0, 0.1f);
            auto second = writeSineFile ("pass2", 4.0, 0.3f);
            auto third = writeSineFile ("pass3", 8.0, 0.6f);

            auto edit = te::test_utilities::createTestEdit (engine, 1);
            auto track = te::getAudioTracks (*edit)[0];

            auto peakIn = [&] (double a, double b)
            {
                auto out = te::test_utilities::renderToAudioBuffer (*edit);
                auto s = (int) (a * out.sampleRate), e = (int) (b * out.sampleRate);
                return out.buffer.getMagnitude (0, s, e - s);
            };

            beginTest ("Recording over a clip keeps it as a take");
            {
                recordAudio (*track, first, 0.0, 8.0);
                expectEquals (track->getClips().size(), 1, "Nothing under the first pass: a plain clip");
                expect (! CompOps::hasTakes (*track->getClips()[0]));

                recordAudio (*track, second, 2.0, 4.0);
                expectEquals (track->getClips().size(), 3, "The old pass plays either side of the new one");

                for (auto c : track->getClips())
                    expectEquals (CompOps::getNumTakes (*c), 2, "Every piece knows both takes");

                expectEquals (CompOps::getActiveTake (*clipAt (*track, 1.0)), 0);
                expectEquals (CompOps::getActiveTake (*clipAt (*track, 3.0)), 1, "The new recording plays");
                expectEquals (CompOps::getActiveTake (*clipAt (*track, 7.0)), 0);
                expectEquals (CompOps::getMaxTakesOnTrack (*track), 2);

                expectWithinAbsoluteError (peakIn (0.5, 1.5), 0.1f, 0.02f);
                expectWithinAbsoluteError (peakIn (3.0, 5.0), 0.3f, 0.03f);
                expectWithinAbsoluteError (peakIn (6.5, 7.5), 0.1f, 0.02f);
            }

            beginTest ("A take that starts later stays in place when comping back");
            {
                // Back to the first pass: it rejoins into one clip, playing from where it always did.
                CompOps::compRange (*track, { at (2.0), at (6.0) }, 0);
                expectEquals (track->getClips().size(), 1, "Same take without a break is one clip again");
                expectWithinAbsoluteError (track->getClips()[0]->getPosition().getOffset().inSeconds(), 0.0, 1.0e-6);
                expectWithinAbsoluteError (track->getClips()[0]->getPosition().getLength().inSeconds(), 8.0, 1.0e-6);
                expectEquals (CompOps::getNumTakes (*track->getClips()[0]), 2);

                // The second pass only covers 2-6 s: swiping past that switches just that part.
                CompOps::compRange (*track, { at (0.0), at (8.0) }, 1);
                expectEquals (track->getClips().size(), 3);
                expectWithinAbsoluteError (clipAt (*track, 3.0)->getPosition().getStart().inSeconds(), 2.0, 1.0e-6);
                expectWithinAbsoluteError (clipAt (*track, 3.0)->getPosition().getOffset().inSeconds(), 0.0, 1.0e-6);
                expectWithinAbsoluteError (peakIn (3.0, 5.0), 0.3f, 0.03f);
                expectWithinAbsoluteError (peakIn (0.5, 1.5), 0.1f, 0.02f);
            }

            beginTest ("Takes keep their place when the comp is moved");
            {
                juce::Array<te::Clip*> all;
                for (auto c : track->getClips())
                    all.add (c);

                for (auto c : all)
                {
                    auto pos = c->getPosition();
                    pos.time = pos.time + te::TimeDuration::fromSeconds (10.0);
                    c->setPosition (pos);
                }

                auto* middle = clipAt (*track, 13.0);
                expect (middle != nullptr);
                CompOps::setActiveTake (*middle, 0);
                expectWithinAbsoluteError (middle->getPosition().getOffset().inSeconds(), 2.0, 1.0e-6,
                                           "Pass 1 under the moved piece is still 2 s into its file");
                CompOps::setActiveTake (*middle, 1);
                expectWithinAbsoluteError (middle->getPosition().getOffset().inSeconds(), 0.0, 1.0e-6);

                for (auto c : all)
                {
                    auto pos = c->getPosition();
                    pos.time = pos.time - te::TimeDuration::fromSeconds (10.0);
                    c->setPosition (pos);
                }
            }

            beginTest ("A later, longer pass extends the comp and joins every piece");
            {
                recordAudio (*track, third, 4.0, 8.0);
                expectEquals (CompOps::getMaxTakesOnTrack (*track), 3);

                for (auto c : track->getClips())
                    expectEquals (CompOps::getNumTakes (*c), 3, "Pieces outside the new range get the take too");

                expectEquals (CompOps::getActiveTake (*clipAt (*track, 5.0)), 2);
                expectEquals (CompOps::getActiveTake (*clipAt (*track, 11.0)), 2);
                expectEquals (CompOps::getActiveTake (*clipAt (*track, 3.0)), 1, "The part it doesn't cover is unchanged");
                expectWithinAbsoluteError (peakIn (9.0, 11.0), 0.6f, 0.05f);

                // The next take after the last is the first, which only reaches 8 s: the rest stays.
                CompOps::stepTake (*clipAt (*track, 11.0), 1);
                expectEquals (CompOps::getActiveTake (*clipAt (*track, 5.0)), 0);
                expectEquals (CompOps::getActiveTake (*clipAt (*track, 11.0)), 2);
            }

            beginTest ("Undoing removes the take and brings the old clips back");
            {
                auto edit2 = te::test_utilities::createTestEdit (engine, 1);
                auto t2 = te::getAudioTracks (*edit2)[0];
                auto& um = edit2->getUndoManager();
                recordAudio (*t2, first, 0.0, 8.0);
                um.beginNewTransaction();
                recordAudio (*t2, second, 2.0, 4.0);
                expectEquals (t2->getClips().size(), 3);
                um.undo();
                expectEquals (t2->getClips().size(), 1);
                expect (! CompOps::hasTakes (*t2->getClips()[0]));
                expectWithinAbsoluteError (t2->getClips()[0]->getPosition().getLength().inSeconds(), 8.0, 1.0e-6);
                um.redo();
                expectEquals (t2->getClips().size(), 3);
                expectEquals (CompOps::getActiveTake (*clipAt (*t2, 3.0)), 1);
            }
        }

        void recordingOverMidi()
        {
            auto edit = te::test_utilities::createTestEdit (getEngine(), 1);
            auto track = te::getAudioTracks (*edit)[0];
            auto& ts = edit->tempoSequence;

            auto firstNote = [] (te::Clip& c)
            {
                auto notes = dynamic_cast<te::MidiClip&> (c).getSequence().getNotes();
                return notes.isEmpty() ? -1 : notes.getFirst()->getNoteNumber();
            };

            beginTest ("MIDI: recording over a clip keeps it as a take");
            {
                auto one = recordMidi (*track, 60, 2.0, 8.0);
                expectEquals (track->getClips().size(), 1);
                expect (! CompOps::hasTakes (*one));

                recordMidi (*track, 72, 4.0, 2.0);
                expectEquals (track->getClips().size(), 3);

                for (auto c : track->getClips())
                    expectEquals (CompOps::getNumTakes (*c), 2);

                expectEquals (CompOps::getActiveTake (*clipAt (*track, 3.0)), 0);
                expectEquals (CompOps::getActiveTake (*clipAt (*track, 5.0)), 1);
                expectEquals (firstNote (*clipAt (*track, 5.0)), 72, "The recording plays where it was played");

                // The note it recorded is where it played it.
                auto* mid = dynamic_cast<te::MidiClip*> (clipAt (*track, 5.0));
                auto noteTime = mid->getTimeOfContentBeat (mid->getSequence().getNotes().getFirst()->getStartBeat());
                expectWithinAbsoluteError (noteTime.inSeconds(), 4.0, 1.0e-4);
            }

            beginTest ("MIDI: switching and comping between takes");
            {
                auto* mid = clipAt (*track, 5.0);
                CompOps::useTake (*mid, 0);

                // All of the first pass again, with its note back at the clip's start.
                expectEquals (track->getClips().size(), 1, "Same take without a break is one clip again");
                auto* whole = dynamic_cast<te::MidiClip*> (track->getClips()[0]);
                expectEquals (firstNote (*whole), 60);
                expectWithinAbsoluteError (whole->getTimeOfContentBeat (whole->getSequence().getNotes().getFirst()->getStartBeat()).inSeconds(), 2.0, 1.0e-4);

                // The second pass only covers 4-6 s.
                CompOps::compRange (*track, { at (2.0), at (10.0) }, 1);
                expectEquals (track->getClips().size(), 3);
                expectWithinAbsoluteError (clipAt (*track, 5.0)->getPosition().getStart().inSeconds(), 4.0, 1.0e-4);
                expectWithinAbsoluteError (clipAt (*track, 5.0)->getPosition().getEnd().inSeconds(), 6.0, 1.0e-4);
                expectEquals (firstNote (*clipAt (*track, 5.0)), 72);
                expectEquals (firstNote (*clipAt (*track, 3.0)), 60);

                auto infos = CompOps::getTakes (*clipAt (*track, 3.0));
                expectEquals ((int) infos.size(), 2);
                expectWithinAbsoluteError (infos[1].range.getStart().inSeconds(), 4.0, 1.0e-4);
                expectWithinAbsoluteError (ts.toTime (infos[1].midiStart).inSeconds(), 4.0, 1.0e-4);
            }

            beginTest ("MIDI: a recording that starts earlier than the clip");
            {
                recordMidi (*track, 48, 1.0, 2.0);
                expectEquals (CompOps::getMaxTakesOnTrack (*track), 3);
                expectEquals (firstNote (*clipAt (*track, 1.5)), 48);
                auto* early = dynamic_cast<te::MidiClip*> (clipAt (*track, 1.5));
                expectWithinAbsoluteError (early->getTimeOfContentBeat (early->getSequence().getNotes().getFirst()->getStartBeat()).inSeconds(), 1.0, 1.0e-4);

                // Pieces still line up: the first pass's first note is at 2 s, under the new take.
                CompOps::useTake (*clipAt (*track, 2.5), 0);
                auto* backOnFirst = dynamic_cast<te::MidiClip*> (clipAt (*track, 2.5));
                expectEquals (firstNote (*backOnFirst), 60);
                expectWithinAbsoluteError (backOnFirst->getTimeOfContentBeat (backOnFirst->getSequence().getNotes().getFirst()->getStartBeat()).inSeconds(), 2.0, 1.0e-4);
            }

            beginTest ("MIDI: undo, and takes in a saved project");
            {
                auto edit2 = te::test_utilities::createTestEdit (getEngine(), 1);
                auto t2 = te::getAudioTracks (*edit2)[0];
                auto& um = edit2->getUndoManager();
                recordMidi (*t2, 60, 0.0, 8.0);
                um.beginNewTransaction();
                recordMidi (*t2, 72, 2.0, 2.0);
                um.undo();
                expectEquals (t2->getClips().size(), 1);
                expect (! CompOps::hasTakes (*t2->getClips()[0]));
                expectEquals (firstNote (*t2->getClips()[0]), 60);
                um.redo();
                expectEquals (t2->getClips().size(), 3);
                expectEquals (firstNote (*clipAt (*t2, 3.0)), 72);
                expectEquals (firstNote (*clipAt (*t2, 1.0)), 60);

                // The clips rebuild from their state as a load would, and find the same takes.
                edit2->flushState();
                te::Edit::Options options { getEngine(), edit2->state.createCopy(), te::ProjectItemRef (te::ProjectItemID::createNewID ({})) };
                auto copy = te::Edit::createEdit (std::move (options));
                auto reloaded = te::getAudioTracks (*copy)[0];
                expectEquals (reloaded->getClips().size(), 3);
                expectEquals (CompOps::getActiveTake (*clipAt (*reloaded, 3.0)), 1);
                expectEquals (firstNote (*clipAt (*reloaded, 3.0)), 72);
                expectEquals (firstNote (*clipAt (*reloaded, 1.0)), 60);
                CompOps::useTake (*clipAt (*reloaded, 3.0), 0);
                expectEquals (reloaded->getClips().size(), 1);
            }
        }

        void editorOfARemovedClip()
        {
            beginTest ("The MIDI editor lets go of a clip that's gone");

            auto edit = te::test_utilities::createTestEdit (getEngine(), 1);
            auto track = te::getAudioTracks (*edit)[0];
            te::SelectionManager sm (getEngine());
            EditViewState vs (*edit, sm);

            auto first = recordMidi (*track, 60, 0.0, 8.0);
            PianoRoll roll (vs, first);
            roll.setBounds (0, 0, 800, 400);
            bool removed = false;
            roll.onClipRemoved = [&removed] { removed = true; };

            // A pass of the same length takes the whole clip's place.
            recordMidi (*track, 72, 0.0, 8.0);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
            expect (removed, "The recording covered the clip being edited");
        }

        void missingTakeFiles()
        {
            auto folder = getTestDir().getChildFile ("MissingTakes");
            folder.deleteRecursively();
            folder.createDirectory();

            auto stored = writeSineFile ("gone_take", 4.0, 0.2f);
            auto playing = writeSineFile ("playing_take", 4.0, 0.2f);
            auto moved = folder.getChildFile (stored.getFileName());

            auto edit = te::test_utilities::createTestEdit (getEngine(), 1);
            auto track = te::getAudioTracks (*edit)[0];
            recordAudio (*track, stored, 0.0, 4.0);
            recordAudio (*track, playing, 0.0, 4.0);
            expectEquals (CompOps::getActiveTake (*track->getClips()[0]), 1);

            beginTest ("A missing take that isn't playing is found and reconnected");
            {
                expect (MissingMedia::findMissingAudio (*edit).isEmpty());
                expect (stored.moveFileTo (moved));

                auto missing = MissingMedia::findMissingAudio (*edit);
                expectEquals (missing.size(), 1);
                expectEquals (missing[0].expected.getFileName(), stored.getFileName());
                expect (missing[0].clips.isEmpty(), "The clip's own source is fine");

                expectEquals (MissingMedia::relinkFromFolders (*edit, { folder }), 1);
                expect (MissingMedia::findMissingAudio (*edit).isEmpty());

                // The take plays from where it went.
                auto* clip = track->getClips()[0];
                CompOps::setActiveTake (*clip, 0);
                expect (dynamic_cast<te::WaveAudioClip&> (*clip).getSourceFileReference().getFile() == moved);
            }
        }

        void takeHousekeeping()
        {
            auto edit = te::test_utilities::createTestEdit (getEngine(), 2);
            auto audio = te::getAudioTracks (*edit)[0];
            auto midi = te::getAudioTracks (*edit)[1];
            auto a = writeSineFile ("keep_a", 8.0, 0.1f), b = writeSineFile ("keep_b", 8.0, 0.2f), c = writeSineFile ("keep_c", 8.0, 0.3f);

            beginTest ("Delete unused takes closes up the lanes");
            {
                recordAudio (*audio, a, 0.0, 8.0);
                recordAudio (*audio, b, 0.0, 8.0);
                recordAudio (*audio, c, 0.0, 8.0);
                expectEquals (CompOps::getMaxTakesOnTrack (*audio), 3);

                CompOps::compRange (*audio, { at (0.0), at (2.0) }, 0);
                expectEquals (audio->getClips().size(), 2);

                CompOps::deleteUnusedTakes (*audio->getClips()[0]);
                expectEquals (CompOps::getMaxTakesOnTrack (*audio), 2, "Take 2 was unused: takes 1 and 3 remain");
                expectEquals (CompOps::getActiveTake (*clipAt (*audio, 1.0)), 0);
                expectEquals (CompOps::getActiveTake (*clipAt (*audio, 5.0)), 1);
                expectEquals (clipAt (*audio, 5.0)->getName(), juce::String ("keep_c"));

                recordMidi (*midi, 60, 0.0, 8.0);
                recordMidi (*midi, 62, 0.0, 8.0);
                recordMidi (*midi, 64, 0.0, 8.0);
                CompOps::compRange (*midi, { at (0.0), at (2.0) }, 0);

                auto& um = edit->getUndoManager();
                um.beginNewTransaction();
                CompOps::deleteUnusedTakes (*midi->getClips()[0]);
                expectEquals (CompOps::getMaxTakesOnTrack (*midi), 2);
                auto noteOf = [] (te::Clip* cl) { return dynamic_cast<te::MidiClip*> (cl)->getSequence().getNotes().getFirst()->getNoteNumber(); };
                expectEquals (noteOf (clipAt (*midi, 1.0)), 60);
                expectEquals (noteOf (clipAt (*midi, 5.0)), 64);

                um.undo();
                expectEquals (CompOps::getMaxTakesOnTrack (*midi), 3);
                expectEquals (noteOf (clipAt (*midi, 1.0)), 60, "Undo plays the same takes again");
                expectEquals (noteOf (clipAt (*midi, 5.0)), 64);
                CompOps::useTake (*clipAt (*midi, 5.0), 1);
                expectEquals (noteOf (clipAt (*midi, 5.0)), 62);
            }

            beginTest ("Flatten keeps what plays");
            {
                for (auto piece : CompOps::getComp (*clipAt (*midi, 1.0)))
                    CompOps::removeTakes (*piece);

                expectEquals (CompOps::getMaxTakesOnTrack (*midi), 0);
                expectEquals (dynamic_cast<te::MidiClip*> (clipAt (*midi, 1.0))->getSequence().getNotes().getFirst()->getNoteNumber(), 60);
                expectEquals (dynamic_cast<te::MidiClip*> (clipAt (*midi, 5.0))->getSequence().getNotes().getFirst()->getNoteNumber(), 62);
            }

            beginTest ("Next and previous take wrap round");
            {
                auto edit2 = te::test_utilities::createTestEdit (getEngine(), 1);
                auto t = te::getAudioTracks (*edit2)[0];
                recordAudio (*t, a, 0.0, 8.0);
                recordAudio (*t, b, 0.0, 8.0);
                recordAudio (*t, c, 0.0, 8.0);
                auto* clip = t->getClips()[0];
                expectEquals (CompOps::getActiveTake (*clip), 2);
                CompOps::stepTake (*clip, 1);
                expectEquals (CompOps::getActiveTake (*clip), 0);
                CompOps::stepTake (*clip, -1);
                expectEquals (CompOps::getActiveTake (*clip), 2);
                CompOps::stepTake (*clip, -1);
                expectEquals (CompOps::getActiveTake (*clip), 1);
            }

            beginTest ("Unpack gives each take at its own place");
            {
                auto edit2 = te::test_utilities::createTestEdit (getEngine(), 1);
                auto t = te::getAudioTracks (*edit2)[0];
                recordMidi (*t, 60, 2.0, 8.0);
                recordMidi (*t, 72, 4.0, 2.0);
                auto created = CompOps::unpackTakesToTracks (*clipAt (*t, 3.0));
                expectEquals (created.size(), 2);

                auto* second = dynamic_cast<te::MidiClip*> (created[1]);
                expect (second != nullptr);
                expectWithinAbsoluteError (second->getPosition().getStart().inSeconds(), 4.0, 1.0e-4);
                expectEquals (second->getSequence().getNotes().getFirst()->getNoteNumber(), 72);
            }
        }

        void groupEditing()
        {
            auto edit = te::test_utilities::createTestEdit (getEngine(), 4);
            auto tracks = te::getAudioTracks (*edit);
            te::SelectionManager sm (getEngine());
            auto file1 = writeSineFile ("kit1", 8.0, 0.1f), file2 = writeSineFile ("kit2", 8.0, 0.3f);

            auto addClips = [&] (te::AudioTrack& t, const juce::File& f, double start, double length)
            {
                return t.insertWaveClip (f.getFileNameWithoutExtension(), f, { { at (start), te::TimeDuration::fromSeconds (length) }, {} }, false);
            };

            for (int i = 0; i < 3; ++i)
                addClips (*tracks[i], file1, 0.0, 8.0);

            addClips (*tracks[3], file1, 0.0, 8.0);

            beginTest ("Group editing: only inside a folder that has it on");
            {
                juce::Array<te::Track*> some { tracks[0], tracks[1], tracks[2] };
                auto folder = GroupEdit::groupTracks (*edit, nullptr, some);
                expect (folder != nullptr);
                expect (GroupEdit::isEnabled (*folder));
                expectEquals (folder->getAllSubTracks (true).size(), 3);
                expect (TrackOps::getArrangeTracks (*edit).indexOf (folder.get()) == 0, "The folder sits where its first track was");
                expectEquals (GroupEdit::getGroupTracks (*tracks[0]).size(), 3);
                expectEquals (GroupEdit::getGroupTracks (*tracks[3]).size(), 1);
                expect (GroupEdit::findGroupFolder (*tracks[3]) == nullptr);

                expectEquals (GroupEdit::getLinkedClips (*tracks[0]->getClips()[0]).size(), 3);
                expectEquals (GroupEdit::getLinkedClips (*tracks[3]->getClips()[0]).size(), 1);

                GroupEdit::setEnabled (*folder, false);
                expectEquals (GroupEdit::getLinkedClips (*tracks[0]->getClips()[0]).size(), 1);
                GroupEdit::setEnabled (*folder, true);
            }

            beginTest ("Group editing: selecting one clip selects what lines up");
            {
                GroupEdit::selectClip (sm, *tracks[1]->getClips()[0], false);
                expectEquals (ClipOps::getSelectedClips (sm).size(), 3);

                GroupEdit::deselectClip (sm, *tracks[2]->getClips()[0]);
                expectEquals (ClipOps::getSelectedClips (sm).size(), 0);

                // A clip that doesn't line up isn't linked.
                tracks[2]->getClips()[0]->setEnd (at (4.0), true);
                expectEquals (GroupEdit::getLinkedClips (*tracks[0]->getClips()[0]).size(), 2);
                tracks[2]->getClips()[0]->setEnd (at (8.0), true);
                expectEquals (GroupEdit::getLinkedClips (*tracks[0]->getClips()[0]).size(), 3);
            }

            beginTest ("Group editing: splits go through the group, and only the group");
            {
                GroupEdit::splitAt (*tracks[0], at (4.0));

                for (int i = 0; i < 3; ++i)
                    expectEquals (tracks[i]->getClips().size(), 2);

                expectEquals (tracks[3]->getClips().size(), 1);

                GroupEdit::selectClip (sm, *tracks[1]->getClips()[0], false);
                ClipOps::splitAt (*edit, sm, at (2.0));
                expectEquals (tracks[0]->getClips().size(), 3, "A split from the selection reaches the linked clips");
                expectEquals (tracks[2]->getClips().size(), 3);
                expectEquals (tracks[3]->getClips().size(), 1);
                sm.deselectAll();
            }

            beginTest ("Group editing: moving the selection keeps the tracks in phase");
            {
                auto delta = te::TimeDuration::fromSeconds (1.0);
                GroupEdit::selectClip (sm, *clipAt (*tracks[0], 5.0), false);
                ClipOps::moveClips (*edit, ClipOps::getSelectedClips (sm), delta, 0, false);

                for (int i = 0; i < 3; ++i)
                    expectWithinAbsoluteError (clipAt (*tracks[i], 6.0)->getPosition().getStart().inSeconds(), 5.0, 1.0e-6);

                sm.deselectAll();
            }

            beginTest ("Group editing: comping goes through the group");
            {
                auto edit2 = te::test_utilities::createTestEdit (getEngine(), 3);
                auto kit = te::getAudioTracks (*edit2);
                auto folder = GroupEdit::groupTracks (*edit2, nullptr, { kit[0], kit[1] });

                for (auto* t : { kit[0], kit[1], kit[2] })
                {
                    recordAudio (*t, file1, 0.0, 8.0);
                    recordAudio (*t, file2, 0.0, 8.0);
                }

                GroupEdit::compRange (*kit[0], { at (2.0), at (4.0) }, 0);
                expectEquals (kit[0]->getClips().size(), 3);
                expectEquals (kit[1]->getClips().size(), 3);
                expectEquals (kit[2]->getClips().size(), 1, "Outside the group nothing changes");
                expectEquals (CompOps::getActiveTake (*clipAt (*kit[1], 3.0)), 0);
                expectEquals (CompOps::getActiveTake (*clipAt (*kit[1], 5.0)), 1);

                GroupEdit::useTakeAt (*kit[1], at (3.0), 1);
                expectEquals (kit[0]->getClips().size(), 1, "Back to one take throughout, on every track");
                expectEquals (kit[1]->getClips().size(), 1);

                GroupEdit::setEnabled (*folder, false);
                GroupEdit::compRange (*kit[0], { at (2.0), at (4.0) }, 0);
                expectEquals (kit[1]->getClips().size(), 1, "Group editing off: one track at a time");
            }

            beginTest ("Crossfading a group selection crossfades each track");
            {
                auto edit2 = te::test_utilities::createTestEdit (getEngine(), 2);
                auto kit = te::getAudioTracks (*edit2);

                for (auto* t : kit)
                {
                    auto clip = addClips (*t, file1, 0.0, 8.0);
                    t->splitClip (*clip, at (4.0));
                }

                juce::Array<te::Clip*> oneEach { kit[0]->getClips()[0], kit[1]->getClips()[0] };
                expectEquals (ClipOps::crossfadeClips (oneEach, te::TimeDuration::fromSeconds (0.02)), 2);
            }
        }
    };

    static CompTests compTests;
}
