#include "AudioDeviceMonitor.h"
#include "../App/AppSettings.h"
#include "../App/HangWatchdog.h"

namespace cdaw
{
    namespace
    {
        double now()    { return juce::Time::getMillisecondCounterHiRes() / 1000.0; }

        juce::String nameOf (const juce::XmlElement& setup)
        {
            auto name = setup.getStringAttribute ("audioOutputDeviceName");
            return name.isNotEmpty() ? name : setup.getStringAttribute ("audioInputDeviceName");
        }

        constexpr int silentChecksBeforeRecovery = 3, runningChecksToTrustAgain = 10, maxRecoveriesInARow = 3;
        constexpr double preferredRetryGap = 20.0, preferredRetryGapAfterFailure = 120.0, recoveryRetryGap = 15.0;

        /** The engine's real devices. */
        struct EngineDevice : public AudioDeviceMonitor::Device
        {
            EngineDevice (te::Engine& e, AppSettings& s) : engine (e), settings (s) {}

            juce::AudioDeviceManager& adm()     { return engine.getDeviceManager().deviceManager; }

            bool isRunning() override
            {
                // Tracktion's stream time moves on with every audio callback.
                auto* d = adm().getCurrentAudioDevice();
                const auto t = engine.getDeviceManager().getCurrentStreamTime();
                const bool advancing = t != lastStreamTime;
                lastStreamTime = t;
                return d != nullptr && d->isOpen() && d->isPlaying() && advancing;
            }

            bool isOnPreferred() override
            {
                auto p = AudioDeviceSetup::getPreferredDevice (settings);
                return p == nullptr || AudioDeviceSetup::isOpen (adm(), *p);
            }

            Opened reopen() override
            {
                auto preferred = AudioDeviceSetup::getPreferredDevice (settings);
                auto current = AudioDeviceSetup::describeOpenDevice (adm());
                juce::StringArray failed;
                lastStreamTime = -1.0;

                if (preferred != nullptr && AudioDeviceSetup::openPreferred (adm(), *preferred, failed))
                    return Opened::preferred;

                if (preferred == nullptr && current != nullptr && AudioDeviceSetup::openPreferred (adm(), *current, failed))
                    return Opened::preferred;       // nothing chosen: the device it had is the one

                if (current != nullptr && AudioDeviceSetup::openPreferred (adm(), *current, failed))
                    return Opened::standIn;

                return AudioDeviceSetup::openStandIn (adm(), failed) ? Opened::standIn : Opened::nothing;
            }

            bool switchToPreferred() override
            {
                auto preferred = AudioDeviceSetup::getPreferredDevice (settings);
                if (preferred == nullptr)
                    return false;

                auto standIn = AudioDeviceSetup::describeOpenDevice (adm());
                juce::StringArray failed;
                lastStreamTime = -1.0;

                if (AudioDeviceSetup::openPreferred (adm(), *preferred, failed))
                    return true;

                if (standIn == nullptr || ! AudioDeviceSetup::openPreferred (adm(), *standIn, failed))
                    AudioDeviceSetup::openStandIn (adm(), failed);

                return false;
            }

            bool isBusy() override
            {
                if (auto* edit = engine.getUIBehaviour().getCurrentlyFocusedEdit())
                    return edit->getTransport().isPlaying() || edit->getTransport().isRecording();
                return false;
            }

            void stopTransport() override
            {
                if (auto* edit = engine.getUIBehaviour().getCurrentlyFocusedEdit())
                    edit->getTransport().stop (false, false);
            }

            juce::String describeOpen() override    { return AudioDeviceSetup::describeCurrentDevice (engine); }

            juce::String preferredName() override
            {
                auto p = AudioDeviceSetup::getPreferredDevice (settings);
                return p != nullptr ? nameOf (*p) : juce::String();
            }

            te::Engine& engine;
            AppSettings& settings;
            double lastStreamTime = -1.0;
        };
    }

    //==============================================================================
    AudioDeviceMonitor::AudioDeviceMonitor (te::Engine& engine, AppSettings& settings, std::function<void (const juce::String&)> status)
        : AudioDeviceMonitor (std::make_unique<EngineDevice> (engine, settings), std::move (status))
    {
        watched = &engine.getDeviceManager().deviceManager;
        watched->addChangeListener (this);
        startTimer (1000);
    }

    AudioDeviceMonitor::AudioDeviceMonitor (std::unique_ptr<Device> d, std::function<void (const juce::String&)> status)
        : device (std::move (d)), showStatus (std::move (status))
    {
    }

    AudioDeviceMonitor::~AudioDeviceMonitor()
    {
        stopTimer();

        if (watched != nullptr)
            watched->removeChangeListener (this);
    }

    void AudioDeviceMonitor::timerCallback()
    {
        check (now(), juce::Process::isForegroundProcess());
    }

    void AudioDeviceMonitor::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        devicesChanged (now());
    }

    void AudioDeviceMonitor::devicesChanged (double time)
    {
        // The change messages our own switching causes arrive asynchronously: they're not news.
        if (switching || time < ignoreChangesUntil)
            return;

        devicesHaveChanged = true;
        recoveriesInARow = 0;       // something changed: worth trying again
    }

    void AudioDeviceMonitor::say (const juce::String& message)
    {
        juce::Logger::writeToLog ("Audio device: " + message);

        if (showStatus)
            showStatus (message);
    }

    void AudioDeviceMonitor::switched (double time)
    {
        ignoreChangesUntil = time + 2.0;
        devicesHaveChanged = false;
        silentChecks = 0;
        HangWatchdog::setActivity ({});
    }

    void AudioDeviceMonitor::check (double time, bool inFront)
    {
        if (! started)
        {
            started = true;
            nextPreferredAttempt = time + preferredRetryGap;
        }

        const bool cameToFront = inFront && ! wasInFront;
        wasInFront = inFront;

        if (switching || AudioDeviceSetup::areSettingsShowing())
        {
            silentChecks = 0;
            return;
        }

        const bool running = device->isRunning();
        silentChecks = running ? 0 : silentChecks + 1;
        runningChecks = running ? runningChecks + 1 : 0;

        if (running)
            lastDeviceName = device->describeOpen();

        if (runningChecks >= runningChecksToTrustAgain)
            recoveriesInARow = 0;

        if (silentChecks >= silentChecksBeforeRecovery && time >= nextRecoveryAllowed)
        {
            if (recoveriesInARow < maxRecoveriesInARow)
            {
                recover (time);
                return;
            }

            if (recoveriesInARow == maxRecoveriesInARow)
            {
                // Reopening didn't get it running: stop trying until something changes.
                ++recoveriesInARow;
                say ("The audio device keeps stopping. Click here for the audio settings, or reconnect the interface.");
            }
        }

        if (running && (devicesHaveChanged || cameToFront) && time >= nextPreferredAttempt
             && ! device->isBusy() && ! device->isOnPreferred())
            tryPreferredAgain (time, devicesHaveChanged ? "the audio devices changed" : "CDAW came back to the front");
    }

    void AudioDeviceMonitor::recover (double time)
    {
        const juce::ScopedValueSetter<bool> guard (switching, true);
        const auto stopped = lastDeviceName.isNotEmpty() ? lastDeviceName : juce::String ("The audio device");

        juce::Logger::writeToLog ("Audio device: " + stopped + " stopped running; reconnecting");
        HangWatchdog::setActivity ("Reconnecting the audio device after " + stopped + " stopped");
        ++recoveriesInARow;
        nextRecoveryAllowed = time + recoveryRetryGap;
        device->stopTransport();

        switch (device->reopen())
        {
            case Device::Opened::preferred:
                say ("Audio had stopped; reconnected " + device->describeOpen());
                break;

            case Device::Opened::standIn:
                say ("Audio had stopped and " + device->preferredName() + " isn't available. Using " + device->describeOpen()
                     + " for now; CDAW switches back when it can.");
                nextPreferredAttempt = time + preferredRetryGapAfterFailure;
                break;

            case Device::Opened::nothing:
                say ("Audio stopped and no audio device could be opened. Check the interface, then click here for the audio settings.");
                break;
        }

        switched (time);
    }

    void AudioDeviceMonitor::tryPreferredAgain (double time, const juce::String& why)
    {
        const juce::ScopedValueSetter<bool> guard (switching, true);
        const auto name = device->preferredName();

        juce::Logger::writeToLog ("Audio device: trying " + name + " again (" + why + ")");
        HangWatchdog::setActivity ("Opening " + name);

        if (device->switchToPreferred())
        {
            say ("Switched back to " + device->describeOpen());
            nextPreferredAttempt = time + preferredRetryGap;
        }
        else
        {
            // Still busy or unplugged: stay on the stand-in, and leave that driver alone for a while.
            juce::Logger::writeToLog ("Audio device: " + name + " still isn't available; staying on " + device->describeOpen());
            nextPreferredAttempt = time + preferredRetryGapAfterFailure;
        }

        switched (time);
    }

    void AudioDeviceMonitor::reconnect()
    {
        const auto time = now();
        const juce::ScopedValueSetter<bool> guard (switching, true);
        const auto name = device->preferredName();

        juce::Logger::writeToLog ("Audio device: reconnecting " + name + " (asked to)");
        HangWatchdog::setActivity ("Reconnecting " + name);
        recoveriesInARow = 0;

        switch (device->reopen())
        {
            case Device::Opened::preferred:
                say ("Reconnected " + device->describeOpen());
                break;

            case Device::Opened::standIn:
                say (name + " isn't available (in use by another program, or not connected). Using " + device->describeOpen() + " for now.");
                nextPreferredAttempt = time + preferredRetryGapAfterFailure;
                break;

            case Device::Opened::nothing:
                say ("No audio device could be opened. Check the interface, then click here for the audio settings.");
                break;
        }

        switched (time);
    }
}
