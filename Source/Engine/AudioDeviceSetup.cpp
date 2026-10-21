#include "AudioDeviceSetup.h"
#include "../App/AppSettings.h"
#include "../UI/LookAndFeel/Palette.h"

namespace cdaw::AudioDeviceSetup
{
    static constexpr const char* asioDefaultAppliedKey = "asioDefaultApplied";

    /** 0 = preferred hardware, 1 = other hardware, -1 = never pick automatically.
        Generic wrapper drivers (e.g. Steinberg's "Generic Low Latency ASIO Driver")
        crash some systems when opened, so they are only used if the user picks them. */
    static int rankAsioDriver (const juce::String& name)
    {
        if (name.containsIgnoreCase ("Yamaha Steinberg USB"))  return 0;
        if (name.containsIgnoreCase ("Generic Low Latency"))   return -1;
        if (name.containsIgnoreCase ("built-in"))              return -1;
        if (name.containsIgnoreCase ("ASIO4ALL"))              return -1;
        if (name.containsIgnoreCase ("FlexASIO"))              return -1;
        return 1;
    }

    /** Opens exactly the named device. Uses an XML state because
        AudioDeviceManager::setCurrentAudioDeviceType() first opens the type's default
        device, which for ASIO is the first registered driver (often a generic one). */
    static bool tryOpenWith (juce::AudioDeviceManager& adm, const juce::String& typeName, const juce::String& deviceName,
                             const juce::String& inputName, double rate, int bufferSize)
    {
        juce::XmlElement xml ("DEVICESETUP");
        xml.setAttribute ("deviceType", typeName);
        xml.setAttribute ("audioOutputDeviceName", deviceName);
        xml.setAttribute ("audioInputDeviceName", inputName);

        if (rate > 0)        xml.setAttribute ("audioDeviceRate", rate);
        if (bufferSize > 0)  xml.setAttribute ("audioDeviceBufferSize", bufferSize);

        auto error = adm.initialise (te::DeviceManager::defaultNumChannelsToOpen,
                                     te::DeviceManager::defaultNumChannelsToOpen,
                                     &xml, false);

        if (error.isEmpty() && adm.getCurrentAudioDevice() != nullptr)
            return true;

        juce::Logger::writeToLog ("  " + typeName + " / " + deviceName + " @ " + juce::String (rate) + " Hz, "
                                  + juce::String (bufferSize) + " smp: " + (error.isNotEmpty() ? error : juce::String ("no device")));
        adm.closeAudioDevice();
        return false;
    }

    /** Opens exactly the named device. Uses an XML state because
        AudioDeviceManager::setCurrentAudioDeviceType() first opens the type's default
        device, which for ASIO is the first registered driver (often a generic one).
        Tries 48 kHz / 128 first, then the driver's own preferred settings. */
    static bool tryOpen (juce::AudioDeviceManager& adm, const juce::String& typeName, const juce::String& deviceName,
                         std::optional<juce::String> inputName = {})
    {
        juce::Logger::writeToLog ("Trying audio device: " + typeName + " / " + deviceName);
        auto in = inputName.value_or (deviceName);

        return tryOpenWith (adm, typeName, deviceName, in, 48000.0, 128)
            || tryOpenWith (adm, typeName, deviceName, in, 48000.0, 0)
            || tryOpenWith (adm, typeName, deviceName, in, 0.0, 0);
    }

    static juce::String getDefaultDeviceName (juce::AudioDeviceManager& adm, const juce::String& typeName, bool input = false)
    {
        for (auto* type : adm.getAvailableDeviceTypes())
        {
            if (type->getTypeName() == typeName)
            {
                type->scanForDevices();
                auto names = type->getDeviceNames (input);
                return names[juce::jmax (0, type->getDefaultDeviceIndex (input))];
            }
        }

        return {};
    }

    /** The wave device list is rebuilt asynchronously after the device opens, so wait
        for it, then split inputs into mono channels ("In 1", "In 2", ...) which suit
        guitar and mic tracking. Outputs stay stereo pairs. */
    static void applyMonoInputLayoutWhenReady (te::Engine& engine, int attempt)
    {
        auto& dm = engine.getDeviceManager();
        dm.dispatchPendingUpdates();

        if (dm.getNumWaveInDevices() == 0 && attempt < 40)
        {
            juce::Timer::callAfterDelay (250, [&engine, attempt] { applyMonoInputLayoutWhenReady (engine, attempt + 1); });
            return;
        }

        dm.setAllWaveInputsToNumChannels (1);
        dm.setAllWaveOutputsToNumChannels (2);
        dm.saveSettings();
        juce::Logger::writeToLog ("Inputs set to mono: " + juce::String (dm.getNumWaveInDevices()) + " input(s)");
    }

    juce::String ensureAsioDefault (te::Engine& engine, AppSettings& settings)
    {
        auto& dm = engine.getDeviceManager();
        auto& adm = dm.deviceManager;

        const bool firstRun = ! settings.getBool (asioDefaultAppliedKey, false);
        auto* current = adm.getCurrentAudioDevice();

        if (! firstRun && current != nullptr)
            return describeCurrentDevice (engine);

        juce::AudioIODeviceType* asioType = nullptr;

        for (auto* type : adm.getAvailableDeviceTypes())
            if (type->getTypeName() == "ASIO")
                asioType = type;

        bool opened = false;

        if (asioType != nullptr)
        {
            asioType->scanForDevices();
            auto names = asioType->getDeviceNames();

            std::stable_sort (names.begin(), names.end(), [] (const juce::String& a, const juce::String& b)
                              { return rankAsioDriver (a) < rankAsioDriver (b); });

            for (auto& name : names)
                if (rankAsioDriver (name) >= 0 && (opened = tryOpen (adm, "ASIO", name)))
                    break;
        }

        if (! opened && adm.getCurrentAudioDevice() == nullptr)
            opened = tryOpen (adm, "Windows Audio", getDefaultDeviceName (adm, "Windows Audio"),
                              getDefaultDeviceName (adm, "Windows Audio", true));

        if (opened && firstRun)
        {
            settings.setBool (asioDefaultAppliedKey, true);
            applyMonoInputLayoutWhenReady (engine, 0);
        }

        auto status = describeCurrentDevice (engine);
        juce::Logger::writeToLog ("Audio device: " + status);
        return status;
    }

    juce::String describeCurrentDevice (te::Engine& engine)
    {
        auto& adm = engine.getDeviceManager().deviceManager;

        if (auto* device = adm.getCurrentAudioDevice())
        {
            auto rate = device->getCurrentSampleRate();
            auto block = device->getCurrentBufferSizeSamples();
            auto latencyMs = (device->getInputLatencyInSamples() + device->getOutputLatencyInSamples()) * 1000.0 / juce::jmax (1.0, rate);

            return device->getTypeName() + ": " + device->getName()
                   + "   " + juce::String (rate / 1000.0, 1) + " kHz"
                   + "   " + juce::String (block) + " smp"
                   + "   RTL " + juce::String (latencyMs, 1) + " ms";
        }

        return "No audio device";
    }

    void showAudioSettings (te::Engine& engine)
    {
        auto* selector = new juce::AudioDeviceSelectorComponent (engine.getDeviceManager().deviceManager,
                                                                 0, 64, 0, 64,
                                                                 false, false, false, false);
        selector->setSize (520, 460);

        juce::DialogWindow::LaunchOptions o;
        o.dialogTitle = "Audio Settings";
        o.dialogBackgroundColour = Palette::panelBackground;
        o.content.setOwned (selector);
        o.useNativeTitleBar = true;
        o.resizable = true;
        o.launchAsync();
    }
}
