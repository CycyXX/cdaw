#include "SafeAsioType.h"

namespace cdaw
{
    /** Passes the inner type's device-list change notifications on to our listeners. */
    struct SafeAsioType::Forwarder : public juce::AudioIODeviceType::Listener
    {
        explicit Forwarder (SafeAsioType& o) : owner (o) {}
        void audioDeviceListChanged() override  { owner.callDeviceChangeListeners(); }
        SafeAsioType& owner;
    };

    SafeAsioType::SafeAsioType (std::unique_ptr<juce::AudioIODeviceType> i)
        : juce::AudioIODeviceType (i->getTypeName()), inner (std::move (i))
    {
        forwarder = std::make_unique<Forwarder> (*this);
        inner->addListener (forwarder.get());
    }

    bool SafeAsioType::isHiddenDriver (const juce::String& name)
    {
        return name.containsIgnoreCase ("Generic Low Latency")
            || name.containsIgnoreCase ("Steinberg built-in");
    }

    void SafeAsioType::scanForDevices()
    {
        inner->scanForDevices();
    }

    juce::StringArray SafeAsioType::getDeviceNames (bool wantInputNames) const
    {
        juce::StringArray names;

        for (auto& n : inner->getDeviceNames (wantInputNames))
            if (! isHiddenDriver (n))
                names.add (n);

        return names;
    }

    int SafeAsioType::getDefaultDeviceIndex (bool forInput) const
    {
        auto names = getDeviceNames (forInput);

        // Prefer the user's interface driver.
        for (int i = 0; i < names.size(); ++i)
            if (names[i].containsIgnoreCase ("Yamaha Steinberg USB"))
                return i;

        auto innerNames = inner->getDeviceNames (forInput);
        auto innerDefault = innerNames[inner->getDefaultDeviceIndex (forInput)];
        return juce::jmax (0, names.indexOf (innerDefault));
    }

    int SafeAsioType::getIndexOfDevice (juce::AudioIODevice* device, bool asInput) const
    {
        return device != nullptr ? getDeviceNames (asInput).indexOf (device->getName()) : -1;
    }

    bool SafeAsioType::hasSeparateInputsAndOutputs() const
    {
        return inner->hasSeparateInputsAndOutputs();
    }

    juce::AudioIODevice* SafeAsioType::createDevice (const juce::String& outputDeviceName, const juce::String& inputDeviceName)
    {
        if (isHiddenDriver (outputDeviceName) || isHiddenDriver (inputDeviceName))
            return nullptr;

        // An empty name means "default"; resolve it ourselves so the inner type can't pick a hidden driver.
        auto names = getDeviceNames (false);
        auto out = outputDeviceName.isNotEmpty() ? outputDeviceName : names[getDefaultDeviceIndex (false)];
        auto in = inputDeviceName.isNotEmpty() ? inputDeviceName : out;

        if (out.isEmpty() || ! names.contains (out))
            return nullptr;

        return inner->createDevice (out, in);
    }

    void registerAudioDeviceTypes (juce::AudioDeviceManager& adm)
    {
       #if JUCE_ASIO
        if (auto* asio = juce::AudioIODeviceType::createAudioIODeviceType_ASIO())
            adm.addAudioDeviceType (std::make_unique<SafeAsioType> (std::unique_ptr<juce::AudioIODeviceType> (asio)));
       #endif

       #if JUCE_WASAPI
        if (auto* wasapi = juce::AudioIODeviceType::createAudioIODeviceType_WASAPI (juce::WASAPIDeviceMode::shared))
            adm.addAudioDeviceType (std::unique_ptr<juce::AudioIODeviceType> (wasapi));

        if (auto* exclusive = juce::AudioIODeviceType::createAudioIODeviceType_WASAPI (juce::WASAPIDeviceMode::exclusive))
            adm.addAudioDeviceType (std::unique_ptr<juce::AudioIODeviceType> (exclusive));
       #endif
    }
}
