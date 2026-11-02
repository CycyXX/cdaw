#include "TestEngine.h"
#include "Engine/AudioDeviceMonitor.h"

namespace cdaw::test
{
    struct AudioDeviceMonitorTests : public juce::UnitTest
    {
        AudioDeviceMonitorTests() : juce::UnitTest ("Audio device monitor", "CDAW") {}

        /** A device whose state the test sets; counts what the monitor asks of it. */
        struct FakeDevice : public AudioDeviceMonitor::Device
        {
            bool running = true, onPreferred = true, preferredAvailable = true, runsWhenReopened = true, busy = false;
            int reopens = 0, switches = 0, transportStops = 0;

            bool isRunning() override               { return running; }
            bool isOnPreferred() override           { return onPreferred; }
            bool isBusy() override                  { return busy; }
            void stopTransport() override           { ++transportStops; }
            juce::String describeOpen() override    { return onPreferred ? "Interface" : "Stand-in"; }
            juce::String preferredName() override   { return "Interface"; }

            Opened reopen() override
            {
                ++reopens;
                running = runsWhenReopened;
                onPreferred = preferredAvailable;
                return preferredAvailable ? Opened::preferred : Opened::standIn;
            }

            bool switchToPreferred() override
            {
                ++switches;
                onPreferred = preferredAvailable;
                return preferredAvailable;
            }
        };

        struct Rig
        {
            Rig()
            {
                auto d = std::make_unique<FakeDevice>();
                fake = d.get();
                monitor = std::make_unique<AudioDeviceMonitor> (std::move (d), [this] (const juce::String& m) { messages.add (m); });
            }

            /** One check per second from `from` to `to` (inclusive). */
            void run (int from, int to, bool inFront = true)
            {
                for (int t = from; t <= to; ++t)
                    monitor->check (t, inFront);
            }

            FakeDevice* fake = nullptr;
            std::unique_ptr<AudioDeviceMonitor> monitor;
            juce::StringArray messages;
        };

        void runTest() override
        {
            beginTest ("Running audio is left alone");
            {
                Rig r;
                r.run (0, 300);
                expectEquals (r.fake->reopens + r.fake->switches, 0);
                expect (r.messages.isEmpty());
            }

            beginTest ("Audio that stops is reconnected after 3 s, stopping the transport");
            {
                Rig r;
                r.run (0, 10);
                r.fake->running = false;
                r.run (11, 12);
                expectEquals (r.fake->reopens, 0, "a moment's silence isn't a failure");
                r.run (13, 13);
                expectEquals (r.fake->reopens, 1);
                expectEquals (r.fake->transportStops, 1);
                expect (r.messages[0].contains ("reconnected"));
                r.run (14, 100);
                expectEquals (r.fake->reopens, 1, "running again: nothing more to do");
            }

            beginTest ("If the chosen device is gone, a stand-in takes over");
            {
                Rig r;
                r.fake->preferredAvailable = false;
                r.fake->running = false;
                r.run (0, 3);
                expectEquals (r.fake->reopens, 1);
                expect (! r.fake->onPreferred);
                expect (r.messages[0].contains ("Stand-in"));
            }

            beginTest ("Reopening gives up after three tries that don't get audio running, until something changes");
            {
                Rig r;
                r.fake->running = false;
                r.fake->runsWhenReopened = false;
                r.run (0, 200);
                expectEquals (r.fake->reopens, 3);
                expect (r.messages[r.messages.size() - 1].contains ("keeps stopping"));

                const auto said = r.messages.size();
                r.run (201, 400);
                expectEquals (r.fake->reopens, 3);
                expectEquals (r.messages.size(), said, "said once, not every second");

                r.monitor->devicesChanged (401);
                r.run (402, 402);
                expectEquals (r.fake->reopens, 4, "a device change is worth another try");
            }

            beginTest ("On a stand-in it switches back when devices change or CDAW comes to the front, spaced out, never while playing");
            {
                Rig r;
                r.fake->onPreferred = false;
                r.fake->preferredAvailable = false;

                r.run (0, 5);
                r.monitor->devicesChanged (6);
                r.run (7, 19);
                expectEquals (r.fake->switches, 0, "not in the first 20 s");
                r.run (20, 20);
                expectEquals (r.fake->switches, 1, "the device change is acted on once allowed");

                r.run (21, 100);
                expectEquals (r.fake->switches, 1, "no reason to try again");

                r.run (101, 101, false);
                r.run (102, 102, true);
                expectEquals (r.fake->switches, 1, "came to the front, but the busy driver gets 2 min of peace");

                r.run (139, 139, false);
                r.run (140, 140, true);
                expectEquals (r.fake->switches, 2);

                r.fake->preferredAvailable = true;
                r.fake->busy = true;
                r.run (299, 299, false);
                r.run (300, 300, true);
                expectEquals (r.fake->switches, 2, "not while playing or recording");

                r.fake->busy = false;
                r.run (301, 301, false);
                r.run (302, 302, true);
                expectEquals (r.fake->switches, 3);
                expect (r.fake->onPreferred);
                expect (r.messages[r.messages.size() - 1].contains ("Switched back"));

                r.run (303, 400, true);
                expectEquals (r.fake->switches, 3, "on the chosen device: nothing to switch");
            }

            beginTest ("Its own switching doesn't count as a device change");
            {
                Rig r;
                r.fake->onPreferred = false;
                r.fake->preferredAvailable = false;
                r.run (0, 20);
                r.monitor->devicesChanged (21);
                r.run (22, 22);
                expectEquals (r.fake->switches, 1);

                r.monitor->devicesChanged (23);        // the echo of that switch
                r.run (24, 200);
                expectEquals (r.fake->switches, 1);
            }

            // Last: the settings count as showing for 2 s (real time) after they close.
            beginTest ("Nothing changes while the device settings are open");
            {
                Rig r;
                AudioDeviceSetup::ScopedSettingsShowing showing;
                r.fake->running = false;
                r.run (0, 60);
                expectEquals (r.fake->reopens, 0);
            }
        }
    };

    static AudioDeviceMonitorTests audioDeviceMonitorTests;
}
