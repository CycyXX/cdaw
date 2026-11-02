#include "TestEngine.h"
#include "Model/CompOps.h"
#include "Model/EditViewState.h"
#include "Model/MissingMedia.h"
#include "Model/TrackOps.h"
#include "UI/Mixer/InputStrips.h"
#include "Model/ProjectSession.h"
#include "Engine/AudioDeviceSetup.h"
#include <thread>

namespace cdaw::test
{
    struct InputTests : public juce::UnitTest
    {
        InputTests() : juce::UnitTest ("Mono / stereo inputs", "CDAW") {}

        static std::optional<std::pair<int, int>> rangeOf (te::AudioTrack& t)
        {
            if (auto in = TrackOps::getAssignedInput (t))
                return TrackOps::getChannelRange (in->getInputDevice());

            return {};
        }

        static int countWaveInputs (te::DeviceManager& dm)
        {
            int n = 0;
            for (auto* wi : dm.getWaveInputDevices())
                if (! wi->isTrackDevice())
                    ++n;
            return n;
        }

        void runTest() override
        {
            auto& engine = getEngine();
            auto& dm = engine.getDeviceManager();

            // A fake interface with four inputs, split into mono inputs as CDAW does on first run.
            te::HostedAudioDeviceInterface::Parameters params;
            params.sampleRate = 48000.0;
            params.blockSize = 256;
            params.inputChannels = 4;
            params.outputChannels = 2;
            params.inputNames = { "In 1", "In 2", "In 3", "In 4" };

            auto& io = dm.getHostedAudioDeviceInterface();
            io.initialise (params);
            io.prepareToPlay (params.sampleRate, params.blockSize);
            dm.dispatchPendingUpdates();
            dm.setAllWaveInputsToNumChannels (1);
            dm.dispatchPendingUpdates();

            {
                auto edit = te::test_utilities::createTestEdit (engine, 0, te::Edit::EditRole::forEditing);
                edit->getTransport().ensureContextAllocated();

                beginTest ("Input choices follow the track's mode");
                {
                    auto mono = TrackOps::getAudioInputChoices (engine, ChannelMode::mono);
                    auto stereo = TrackOps::getAudioInputChoices (engine, ChannelMode::stereo);
                    expectEquals (mono.size(), 4);
                    expectEquals (stereo.size(), 2);
                    expectEquals (mono[2].name, juce::String ("In 3"));
                    expectEquals (stereo[0].name, juce::String ("In 1+2"));
                    expectEquals (stereo[1].name, juce::String ("In 3+4"));
                    expectEquals (stereo[1].firstChannel, 2);
                    expectEquals (TrackOps::getNumHardwareInputs (engine), 4);
                }

                auto a = TrackOps::addAudioTrack (*edit, nullptr, nullptr, "Guitar");
                auto b = TrackOps::addAudioTrack (*edit, nullptr, nullptr, "Vocal");
                auto c = TrackOps::addAudioTrack (*edit, nullptr, nullptr, "Keys", ChannelMode::stereo);

                beginTest ("New audio tracks are mono or stereo");
                {
                    expect (TrackOps::getChannelMode (*a) == ChannelMode::mono);
                    expect (TrackOps::getChannelMode (*c) == ChannelMode::stereo);
                    expectEquals (TrackOps::getKindName (*a), juce::String::fromUTF8 ("Audio \xc2\xb7 Mono"));
                    expectEquals (TrackOps::getKindName (*c), juce::String::fromUTF8 ("Audio \xc2\xb7 Stereo"));
                }

                beginTest ("Mono tracks take single inputs");
                {
                    expect (TrackOps::assignAudioInput (*a, 0, 1).isEmpty());
                    expect (TrackOps::assignAudioInput (*b, 1, 1).isEmpty());
                    expect (rangeOf (*a) == std::pair (0, 1));
                    expect (rangeOf (*b) == std::pair (1, 1));
                    expectEquals (countWaveInputs (dm), 4);
                }

                beginTest ("A stereo track pairs the channels; tracks on them lose their input");
                {
                    TrackOps::setArmed (*a, true);
                    auto lost = TrackOps::assignAudioInput (*c, 0, 2);
                    expectEquals (lost.joinIntoString (","), juce::String ("Guitar,Vocal"));
                    expect (rangeOf (*c) == std::pair (0, 2));
                    expect (TrackOps::getAssignedInput (*a) == nullptr);
                    expect (TrackOps::getAssignedInput (*b) == nullptr);
                    expectEquals (countWaveInputs (dm), 3, "In 1+2, In 3, In 4");

                    // The record format etc. stay with the edit: the stereo input records into the track.
                    TrackOps::setArmed (*c, true);
                    expect (TrackOps::isArmed (*c));
                }

                beginTest ("Picking a single channel splits the pair again");
                {
                    auto lost = TrackOps::assignAudioInput (*a, 0, 1);
                    expectEquals (lost.joinIntoString (","), juce::String ("Keys"));
                    expect (rangeOf (*a) == std::pair (0, 1));
                    expect (TrackOps::getAssignedInput (*c) == nullptr);
                    expectEquals (countWaveInputs (dm), 4);

                    // Vocal doesn't get In 2 back by itself.
                    expect (TrackOps::getAssignedInput (*b) == nullptr);
                }

                beginTest ("Switching a track's mode moves it to the matching input");
                {
                    TrackOps::setArmed (*a, true);
                    TrackOps::setChannelMode (*a, ChannelMode::stereo);
                    expect (TrackOps::getChannelMode (*a) == ChannelMode::stereo);
                    expect (rangeOf (*a) == std::pair (0, 2));
                    expect (TrackOps::isArmed (*a), "Arming survives the move");

                    TrackOps::setChannelMode (*a, ChannelMode::mono);
                    expect (rangeOf (*a) == std::pair (0, 1));

                    // In 4 -> In 3+4 (pairs start on odd-numbered inputs).
                    TrackOps::assignAudioInput (*b, 3, 1);
                    TrackOps::setChannelMode (*b, ChannelMode::stereo);
                    expect (rangeOf (*b) == std::pair (2, 2));
                }

                beginTest ("Tracks from before the mode existed follow their input");
                {
                    b->state.removeProperty (ViewIDs::cdawChannels, nullptr);
                    expect (TrackOps::getChannelMode (*b) == ChannelMode::stereo);
                    a->state.removeProperty (ViewIDs::cdawChannels, nullptr);
                    expect (TrackOps::getChannelMode (*a) == ChannelMode::mono);
                }

                beginTest ("Mixer input strips survive the inputs being regrouped");
                {
                    te::SelectionManager sm (engine);
                    EditViewState vs (*edit, sm);
                    InputsPanel panel (vs);
                    panel.setSize (600, 400);
                    expectEquals (panel.getIdealWidth(), 3 * InputStrip::width, "In 1, In 2, In 3+4");

                    // Splitting In 3+4 deletes the device under its strip.
                    TrackOps::regroupInputs (*edit, 2, 1);
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
                    expectEquals (panel.getIdealWidth(), 4 * InputStrip::width);
                    expect (TrackOps::getAssignedInput (*b) == nullptr);

                    vs.state.setProperty (ViewIDs::showMixerInputs, false, nullptr);
                    expectEquals (panel.getIdealWidth(), (int) InputsPanel::foldedWidth);
                }

                beginTest ("Undoing an input change doesn't bring back inputs a regroup removed");
                {
                    TrackOps::assignAudioInput (*a, 0, 1);
                    edit->getUndoManager().beginNewTransaction ("Pair");
                    TrackOps::assignAudioInput (*c, 0, 2);   // takes In 1 away from Guitar
                    edit->getUndoManager().undo();

                    // The pairing stays; Guitar isn't quietly reattached to the old In 1.
                    expect (TrackOps::getAssignedInput (*a) == nullptr);
                    TrackOps::regroupInputs (*edit, 0, 1);
                    expect (TrackOps::getAssignedInput (*a) == nullptr, "Splitting again doesn't revive it");
                }
            }

            beginTest ("A take recorded when the project closes is kept");
            {
                auto project = ProjectSession::createProjectFolder (getTestDir(), "Recording");
                ProjectSession session (engine, project);
                expect (session.isValid());

                auto track = TrackOps::addAudioTrack (session.getEdit(), nullptr, nullptr, "Vocal");
                TrackOps::assignAudioInput (*track, 0, 1);
                TrackOps::setArmed (*track, true);
                expect (session.save());
                expect (! session.hasChangedSinceSaved());

                // Feed the fake interface a tone from an "audio thread" while recording.
                std::atomic<bool> stop { false };
                std::thread audio ([&]
                {
                    juce::AudioBuffer<float> block (4, params.blockSize);
                    juce::MidiBuffer midi;
                    double phase = 0.0;

                    while (! stop)
                    {
                        for (int i = 0; i < block.getNumSamples(); ++i, phase += 0.05)
                            for (int ch = 0; ch < block.getNumChannels(); ++ch)
                                block.setSample (ch, i, 0.5f * (float) std::sin (phase));

                        io.processBlock (block, midi);
                        std::this_thread::sleep_for (std::chrono::milliseconds (4));
                    }
                });

                session.getTransport().record (false);
                juce::MessageManager::getInstance()->runDispatchLoopUntil (800);
                expect (session.getTransport().isRecording());

                // What the save prompt now does first.
                expect (session.stopRecordingKeepingTakes());
                stop = true;
                audio.join();

                expectEquals (track->getClips().size(), 1, "The take is in the project");
                juce::MessageManager::getInstance()->runDispatchLoopUntil (300);   // the change flag is set asynchronously
                expect (session.hasChangedSinceSaved(), "...so closing asks to save it");
                expect (! session.stopRecordingKeepingTakes(), "Nothing to stop the second time");
            }

            recordingOverClips (io, params);

            beginTest ("Only a device picked in the device settings is remembered as the choice");
            {
                auto& adm = dm.deviceManager;
                auto open = AudioDeviceSetup::describeOpenDevice (adm);
                expect (open != nullptr);
                expect (AudioDeviceSetup::isOpen (adm, *open));

                juce::XmlElement busy ("DEVICESETUP");
                busy.setAttribute ("deviceType", "ASIO");
                busy.setAttribute ("audioOutputDeviceName", "Yamaha Steinberg USB ASIO");
                expect (! AudioDeviceSetup::isOpen (adm, busy));

                int remembered = 0;
                auto count = [&remembered] (const juce::XmlElement&) { ++remembered; };
                AudioDeviceSetup::PreferredDeviceTracker tracker (adm, count);

                // A device opened by a fallback (the chosen interface was busy or unplugged).
                expect (! AudioDeviceSetup::areSettingsShowing());
                adm.sendChangeMessage();
                juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
                expectEquals (remembered, 0, "a stand-in isn't a choice");

                {
                    // The user picks a device in the settings.
                    AudioDeviceSetup::ScopedSettingsShowing showing;
                    expect (AudioDeviceSetup::areSettingsShowing());
                    adm.sendChangeMessage();
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
                    expectEquals (remembered, 1, "a pick in the settings is the choice");
                }

                // The change a pick causes may land just after the settings close: still a pick.
                adm.sendChangeMessage();
                juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
                expectEquals (remembered, 2);
            }

            // The fake device stays (as in Tracktion's own tests): removing it leaves the device
            // manager without a block size, which later renders trip over.
        }

        /** Real recordings through the engine: what lands on the track when something is already there. */
        void recordingOverClips (te::HostedAudioDeviceInterface& io, const te::HostedAudioDeviceInterface::Parameters& params)
        {
            auto& engine = getEngine();
            auto& dm = engine.getDeviceManager();
            auto project = ProjectSession::createProjectFolder (getTestDir(), "RecordingTakes");
            auto replaceMode = te::WaveInputDevice::getMergeModes()[1];

            // The fake interface plays a tone and repeats one MIDI note.
            std::atomic<bool> stop { false };
            std::atomic<juce::int64> samplesFed { 0 };
            std::atomic<int> noteToPlay { 60 };

            std::thread audio ([&]
            {
                juce::AudioBuffer<float> block (4, params.blockSize);
                juce::MidiBuffer midi;
                double phase = 0.0;
                int blockCount = 0;
                const auto blockTime = std::chrono::microseconds ((juce::int64) (params.blockSize * 1.0e6 / params.sampleRate));
                auto nextBlock = std::chrono::steady_clock::now();

                while (! stop)
                {
                    for (int i = 0; i < block.getNumSamples(); ++i, phase += 0.05)
                        for (int ch = 0; ch < block.getNumChannels(); ++ch)
                            block.setSample (ch, i, 0.5f * (float) std::sin (phase));

                    midi.clear();

                    if (blockCount % 16 == 0)
                        midi.addEvent (juce::MidiMessage::noteOn (1, noteToPlay, 0.8f), 0);
                    else if (blockCount % 16 == 8)
                        midi.addEvent (juce::MidiMessage::noteOff (1, noteToPlay), 0);

                    ++blockCount;
                    io.processBlock (block, midi);
                    samplesFed += params.blockSize;

                    // At the rate a real interface runs: a short sleep is 2 ms or 15 ms depending on
                    // what else is running, and recordings came out wrong far from real time.
                    nextBlock += blockTime;
                    std::this_thread::sleep_until (nextBlock);
                }
            });

            auto waitFor = [&] (double seconds)
            {
                auto target = samplesFed.load() + (juce::int64) (seconds * params.sampleRate);

                while (samplesFed.load() < target)
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
            };

            auto record = [&] (te::TransportControl& transport, double from, double seconds, bool discard = false)
            {
                transport.setPosition (te::TimePosition::fromSeconds (from));
                transport.record (false);
                waitFor (seconds);
                expect (transport.isRecording());
                transport.stop (discard, false);
                juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
            };

            auto clipAt = [] (te::ClipTrack& t, double seconds) -> te::Clip*
            {
                for (auto c : t.getClips())
                    if (c->getEditTimeRange().contains (te::TimePosition::fromSeconds (seconds)))
                        return c;

                return nullptr;
            };

            auto firstNote = [] (te::Clip* c)
            {
                auto* mc = dynamic_cast<te::MidiClip*> (c);
                return mc != nullptr && ! mc->getSequence().getNotes().isEmpty() ? mc->getSequence().getNotes().getFirst()->getNoteNumber() : -1;
            };

            auto setMidiToTakes = [&dm]
            {
                for (auto& in : dm.getMidiInDevices())
                {
                    in->setEnabled (true);
                    in->mergeRecordings = false;       // what Preferences' default, "New take", sets
                    in->replaceExistingClips = false;
                }
            };

            // The fake interface's MIDI input appears on the device manager's next scan.
            dm.rescanMidiDeviceList();

            for (int tries = 0; tries < 100 && dm.getMidiInDevices().empty(); ++tries)
            {
                dm.dispatchPendingUpdates();
                juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
            }

            setMidiToTakes();
            dm.dispatchPendingUpdates();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (100);

            {
                ProjectSession session (engine, project);
                auto& edit = session.getEdit();
                auto& transport = session.getTransport();
                edit.getTransport().ensureContextAllocated();

                auto track = TrackOps::addAudioTrack (edit, nullptr, nullptr, "Guitar");
                TrackOps::assignAudioInput (*track, 0, 1);
                TrackOps::setArmed (*track, true);

                auto* wave = dynamic_cast<te::WaveInputDevice*> (&TrackOps::getAssignedInput (*track)->getInputDevice());
                expect (wave != nullptr);
                wave->setMergeMode (replaceMode);   // as CDAWEngine::applyRecordingDefaults sets it

                // Assigning an input rebuilds the device list a moment later. A recording that starts
                // before that writes only a few blocks to the input being replaced.
                dm.dispatchPendingUpdates();
                juce::MessageManager::getInstance()->runDispatchLoopUntil (300);

                beginTest ("Recording audio over a clip: both passes stay, the new one plays");
                {
                    record (transport, 0.0, 2.0);
                    expectEquals (track->getClips().size(), 1);
                    expect (! CompOps::hasTakes (*track->getClips()[0]));
                    expect (wave->getMergeMode() == replaceMode, "Back to replace after the recording");
                    auto firstLength = track->getClips()[0]->getPosition().getLength().inSeconds();
                    expect (firstLength > 1.0, "The first pass is " + juce::String (firstLength, 3) + " s long, starting at "
                                                 + juce::String (track->getClips()[0]->getPosition().getStart().inSeconds(), 3)
                                                 + ", the transport now at " + juce::String (transport.getPosition().inSeconds(), 3));

                    record (transport, 0.5, 0.4);
                    expectEquals (track->getClips().size(), 3, "The old pass either side of the new one");
                    expect (wave->getMergeMode() == replaceMode);

                    for (auto c : track->getClips())
                        expectEquals (CompOps::getNumTakes (*c), 2);

                    expectEquals (CompOps::getActiveTake (*clipAt (*track, 0.2)), 0);
                    expectEquals (CompOps::getActiveTake (*clipAt (*track, 0.6)), 1);
                    expectEquals (CompOps::getActiveTake (*track->getClips()[2]), 0);

                    // Back on the first pass, whole.
                    CompOps::useTake (*clipAt (*track, 0.6), 0);
                    expectEquals (track->getClips().size(), 1);
                    expectWithinAbsoluteError (track->getClips()[0]->getPosition().getLength().inSeconds(), firstLength, 0.01);
                    CompOps::useTake (*track->getClips()[0], 1);
                    expectEquals (track->getClips().size(), 3);
                }

                beginTest ("Aborting a recording leaves the takes as they were");
                {
                    record (transport, 0.5, 0.3, true);
                    expectEquals (track->getClips().size(), 3);
                    expectEquals (CompOps::getMaxTakesOnTrack (*track), 2);
                    expect (wave->getMergeMode() == replaceMode);
                }

                TrackOps::setArmed (*track, false);

                beginTest ("Loop-recording audio over a clip adds each pass as a take");
                {
                    auto looped = TrackOps::addAudioTrack (edit, nullptr, nullptr, "Lead");
                    TrackOps::assignAudioInput (*looped, 1, 1);
                    TrackOps::setArmed (*looped, true);
                    dynamic_cast<te::WaveInputDevice&> (TrackOps::getAssignedInput (*looped)->getInputDevice()).setMergeMode (replaceMode);

                    transport.looping = false;
                    record (transport, 0.0, 1.0);
                    expectEquals (looped->getClips().size(), 1);

                    // (Tracktion needs a loop of more than 2 s to record into.)
                    transport.setLoopRange ({ te::TimePosition(), te::TimePosition::fromSeconds (2.5) });
                    transport.looping = true;
                    record (transport, 0.0, 6.5);
                    transport.looping = false;

                    auto takes = CompOps::getMaxTakesOnTrack (*looped);
                    expect (takes >= 3, "The first clip and at least two passes, got " + juce::String (takes));

                    // The recording stopped partway through its last pass: the one before it plays.
                    auto* clip = clipAt (*looped, 0.3);
                    expectEquals (CompOps::getActiveTake (*clip), takes - 2, "The last complete pass plays");

                    for (auto& take : CompOps::getTakes (*clip))
                        if (take.active)
                            expect (take.range.getEnd() >= clip->getPosition().getEnd() - te::TimeDuration::fromSeconds (0.02));

                    TrackOps::setArmed (*looped, false);
                }

                beginTest ("Recording MIDI over a clip: both passes stay, the new one plays");
                {
                    auto keys = TrackOps::addAudioTrack (edit, nullptr, nullptr, "Keys");
                    auto choices = TrackOps::getInputChoices (edit, true);
                    expect (! choices.isEmpty(), "The fake interface has a MIDI input");

                    if (! choices.isEmpty())
                    {
                        TrackOps::assignInput (*keys, choices[0].instance);
                        TrackOps::setArmed (*keys, true);

                        noteToPlay = 60;
                        record (transport, 0.0, 2.0);
                        expectEquals (keys->getClips().size(), 1);
                        expectEquals (firstNote (keys->getClips()[0]), 60);

                        noteToPlay = 72;
                        record (transport, 0.5, 0.5);
                        expectEquals (keys->getClips().size(), 3);
                        expectEquals (CompOps::getMaxTakesOnTrack (*keys), 2);
                        expectEquals (firstNote (clipAt (*keys, 0.6)), 72);
                        expectEquals (firstNote (clipAt (*keys, 0.2)), 60);

                        CompOps::useTake (*clipAt (*keys, 0.6), 0);
                        expectEquals (keys->getClips().size(), 1);
                        expectEquals (firstNote (keys->getClips()[0]), 60);
                        CompOps::useTake (*keys->getClips()[0], 1);
                        expectEquals (firstNote (clipAt (*keys, 0.6)), 72);

                        TrackOps::setArmed (*keys, false);
                    }
                }

                expect (session.save());
            }

            beginTest ("Takes from real recordings are there when the project opens again");
            {
                ProjectSession reopened (engine, project);
                auto tracks = te::getAudioTracks (reopened.getEdit());
                te::AudioTrack* guitar = nullptr;
                te::AudioTrack* keys = nullptr;

                for (auto* t : tracks)
                {
                    if (t->getName() == "Guitar")  guitar = t;
                    if (t->getName() == "Keys")    keys = t;
                }

                expect (guitar != nullptr && keys != nullptr);

                if (guitar != nullptr && keys != nullptr)
                {
                    expectEquals (guitar->getClips().size(), 3);
                    expectEquals (CompOps::getActiveTake (*clipAt (*guitar, 0.6)), 1);
                    expect (MissingMedia::findMissingAudio (reopened.getEdit()).isEmpty());
                    expectEquals (firstNote (clipAt (*keys, 0.6)), 72);
                    expectEquals (firstNote (clipAt (*keys, 0.2)), 60);
                }
            }

            stop = true;
            audio.join();
        }
    };

    static InputTests inputTests;
}
