#pragma once

#include "../PCH.h"

namespace cdaw
{
    class AppSettings;

    namespace AudioDeviceSetup
    {
        /** On first run (or when no device could be opened) switch to ASIO, preferring
            hardware drivers such as "Yamaha Steinberg USB ASIO", at 48 kHz / 128 samples.
            Falls back to Windows Audio if no ASIO driver opens. Returns a status line. */
        juce::String ensureAsioDefault (te::Engine&, AppSettings&);

        /** Short description for the status bar, e.g. "ASIO: Yamaha Steinberg USB ASIO  48 kHz  128 smp". */
        juce::String describeCurrentDevice (te::Engine&);

        /** Opens the audio settings dialog (device, rate, buffer, channels, ASIO panel). */
        void showAudioSettings (te::Engine&);
    }
}
