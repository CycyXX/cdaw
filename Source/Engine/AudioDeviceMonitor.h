#pragma once

#include "../PCH.h"
#include "AudioDeviceSetup.h"

namespace cdaw
{
    class AppSettings;

    /** Keeps audio running while devices come and go.

        - Notices when audio stops: the device closed, or its audio callbacks stopped coming
          (unplugged, taken over by another program, a driver reset that couldn't reopen it).
          Then it reopens the chosen device, or else a stand-in, and says so in the status bar.
          After three reopenings in a row that didn't get audio running it stops trying, until
          the devices change or the user reconnects.
        - On a stand-in, switches back to the chosen device once it's likely available again:
          when the device list changes (an interface plugged in) or when CDAW comes back to the
          front (e.g. after closing Cubase). At most every 20 s (2 min after a failed attempt),
          never while playing or recording.
        - Does nothing while the device settings are open (the user is choosing).

        Every attempt is a single call into the driver: busy drivers can block when hammered. */
    class AudioDeviceMonitor : private juce::Timer,
                               private juce::ChangeListener
    {
    public:
        /** What the monitor watches and does. The default works on the engine's devices; tests
            use a fake. */
        struct Device
        {
            enum class Opened { preferred, standIn, nothing };

            virtual ~Device() = default;
            virtual bool isRunning() = 0;               // open, and its audio callbacks coming
            virtual bool isOnPreferred() = 0;           // the chosen device is open (true if none was chosen)
            virtual Opened reopen() = 0;                // the chosen device, else a stand-in
            virtual bool switchToPreferred() = 0;       // from a stand-in; stays on it if the chosen one won't open
            virtual bool isBusy() = 0;                  // playing or recording
            virtual void stopTransport() = 0;
            virtual juce::String describeOpen() = 0;    // for messages
            virtual juce::String preferredName() = 0;
        };

        AudioDeviceMonitor (te::Engine&, AppSettings&, std::function<void (const juce::String&)> showStatus);
        AudioDeviceMonitor (std::unique_ptr<Device>, std::function<void (const juce::String&)> showStatus);
        ~AudioDeviceMonitor() override;

        /** Opens the chosen device again now (e.g. after another program released it). */
        void reconnect();

        /** One check (once a second): `time` in seconds, `inFront` whether CDAW is the active app. */
        void check (double time, bool inFront);

        /** The device lists changed or a device restarted (not by the monitor itself). */
        void devicesChanged (double time);

    private:
        void timerCallback() override;
        void changeListenerCallback (juce::ChangeBroadcaster*) override;

        void recover (double time);
        void tryPreferredAgain (double time, const juce::String& why);
        void say (const juce::String&);
        void switched (double time);

        std::unique_ptr<Device> device;
        std::function<void (const juce::String&)> showStatus;
        juce::AudioDeviceManager* watched = nullptr;

        int silentChecks = 0, runningChecks = 0, recoveriesInARow = 0;
        double nextRecoveryAllowed = 0.0, nextPreferredAttempt = 0.0, ignoreChangesUntil = 0.0;
        bool devicesHaveChanged = false, wasInFront = true, switching = false, started = false;
        juce::String lastDeviceName;
    };
}
