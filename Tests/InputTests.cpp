#include "TestEngine.h"
#include "Model/EditViewState.h"
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

            beginTest ("A stand-in audio device opened at startup isn't remembered as the choice");
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

                {
                    // The chosen interface was busy, so this device is a stand-in.
                    AudioDeviceSetup::PreferredDeviceTracker tracker (adm, std::make_unique<juce::XmlElement> (busy), count);
                    adm.sendChangeMessage();
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
                    expectEquals (remembered, 0);
                }

                {
                    // Nothing chosen yet, or this is the choice: changes are remembered.
                    AudioDeviceSetup::PreferredDeviceTracker tracker (adm, std::make_unique<juce::XmlElement> (*open), count);
                    adm.sendChangeMessage();
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
                    expectEquals (remembered, 1);
                }
            }

            // The fake device stays (as in Tracktion's own tests): removing it leaves the device
            // manager without a block size, which later renders trip over.
        }
    };

    static InputTests inputTests;
}
