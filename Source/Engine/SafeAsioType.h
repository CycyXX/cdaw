#pragma once

#include "../PCH.h"

namespace cdaw
{
    /** Wraps JUCE's ASIO device type and hides drivers that crash when opened on some
        systems (Steinberg's "Generic Low Latency" / "built-in" wrapper drivers).

        Without this, a missing interface (e.g. the UR44C unplugged) makes JUCE fall back to
        the first ASIO driver in the registry, which is the generic one, and CDAW crashes. */
    class SafeAsioType : public juce::AudioIODeviceType
    {
    public:
        explicit SafeAsioType (std::unique_ptr<juce::AudioIODeviceType> inner);

        static bool isHiddenDriver (const juce::String& name);

        void scanForDevices() override;
        juce::StringArray getDeviceNames (bool wantInputNames) const override;
        int getDefaultDeviceIndex (bool forInput) const override;
        int getIndexOfDevice (juce::AudioIODevice*, bool asInput) const override;
        bool hasSeparateInputsAndOutputs() const override;
        juce::AudioIODevice* createDevice (const juce::String& outputDeviceName, const juce::String& inputDeviceName) override;

    private:
        struct Forwarder;
        std::unique_ptr<juce::AudioIODeviceType> inner;
        std::unique_ptr<Forwarder> forwarder;
    };

    /** Registers CDAW's audio device types (filtered ASIO + Windows Audio) on the engine's
        AudioDeviceManager. Must run before the device manager is initialised. */
    void registerAudioDeviceTypes (juce::AudioDeviceManager&);
}
