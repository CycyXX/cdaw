#pragma once

#include "../PCH.h"

namespace cdaw
{
    class AppSettings;

    namespace AudioDeviceSetup
    {
        /** Reopens the device the user chose if the engine couldn't (it may have been busy at
            the time). On first run (or when no device could be opened) switches to ASIO,
            preferring hardware drivers such as "Yamaha Steinberg USB ASIO", at 48 kHz / 128
            samples, then Windows Audio. A stand-in never replaces the chosen device. Returns a
            status line. */
        juce::String ensureAsioDefault (te::Engine&, AppSettings&);

        /** Short description for the status bar, e.g. "ASIO: Yamaha Steinberg USB ASIO  48 kHz  128 smp". */
        juce::String describeCurrentDevice (te::Engine&);

        /** Opens the audio settings dialog (device, rate, buffer, channels, ASIO panel). */
        void showAudioSettings (te::Engine&);

        /** The open device as a DEVICESETUP element (type, output, input, rate, buffer), or null. */
        std::unique_ptr<juce::XmlElement> describeOpenDevice (juce::AudioDeviceManager&);

        /** Whether the device described by a DEVICESETUP element is the one open now. */
        bool isOpen (juce::AudioDeviceManager&, const juce::XmlElement& setup);

        /** The device the user chose (CDAW keeps its own record of it), or null. */
        std::unique_ptr<juce::XmlElement> getPreferredDevice (AppSettings&);

        /** Opens the chosen device with its saved settings; other rates / buffer sizes only if the
            driver objects to those. A device that won't open goes into `failed`. */
        bool openPreferred (juce::AudioDeviceManager&, const juce::XmlElement& preferred, juce::StringArray& failed);

        /** Opens another working device: hardware ASIO drivers first, then Windows Audio. Devices in
            `failed` (or that fail now) aren't tried. */
        bool openStandIn (juce::AudioDeviceManager&, juce::StringArray& failed);

        /** While one exists, the device settings are on screen: nothing changes the device behind
            the user's back (see AudioDeviceMonitor). */
        struct ScopedSettingsShowing
        {
            ScopedSettingsShowing();
            ~ScopedSettingsShowing();
        };

        bool areSettingsShowing();

        /** Remembers the device the user picks in the device settings. Devices opened any other way
            (a stand-in because the chosen one was busy or unplugged) never replace their choice. */
        class PreferredDeviceTracker : private juce::ChangeListener
        {
        public:
            PreferredDeviceTracker (juce::AudioDeviceManager&, std::function<void (const juce::XmlElement&)> remember);
            ~PreferredDeviceTracker() override;

        private:
            void changeListenerCallback (juce::ChangeBroadcaster*) override;

            juce::AudioDeviceManager& adm;
            std::function<void (const juce::XmlElement&)> remember;
        };

        /** Starts remembering the user's device choices in CDAW's settings (after ensureAsioDefault). */
        std::unique_ptr<PreferredDeviceTracker> trackPreferredDevice (te::Engine&, AppSettings&);
    }
}
