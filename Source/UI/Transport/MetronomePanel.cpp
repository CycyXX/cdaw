#include "MetronomePanel.h"
#include "../LookAndFeel/Palette.h"
#include "../../App/AppContext.h"

namespace cdaw
{
    namespace
    {
        juce::File getClickFolder()
        {
            auto f = AppSettings::getSettingsFolder().getChildFile ("Clicks");
            f.createDirectory();
            return f;
        }

        /** Writes a short click sample (generated, so no files need to ship). */
        juce::File makeClick (const juce::String& name, bool woodblock, double freq, float level)
        {
            auto file = getClickFolder().getChildFile (name + ".wav");
            if (file.existsAsFile())
                return file;

            constexpr double rate = 48000.0;
            const int length = (int) (rate * (woodblock ? 0.06 : 0.04));
            juce::AudioBuffer<float> buffer (1, length);
            juce::Random rng (1234);
            float lp = 0.0f;

            for (int i = 0; i < length; ++i)
            {
                auto t = i / rate;
                auto env = (float) std::exp (-t * (woodblock ? 90.0 : 120.0));
                float s;

                if (woodblock)
                {
                    // Resonant noise burst: band-limited noise plus a damped tone.
                    lp += 0.35f * ((rng.nextFloat() * 2.0f - 1.0f) - lp);
                    s = 0.5f * lp + 0.7f * (float) std::sin (juce::MathConstants<double>::twoPi * freq * t);
                }
                else
                {
                    s = (float) std::sin (juce::MathConstants<double>::twoPi * freq * t);
                }

                buffer.setSample (0, i, level * env * s);
            }

            juce::WavAudioFormat wav;
            if (auto out = std::unique_ptr<juce::OutputStream> (file.createOutputStream()))
                if (auto writer = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (out.get(), rate, 1, 24, {}, 0)))
                {
                    out.release();
                    writer->writeFromAudioSampleBuffer (buffer, 0, length);
                }

            return file;
        }

        void setClickFiles (te::Engine& engine, const juce::String& accent, const juce::String& beat)
        {
            auto& storage = engine.getPropertyStorage();
            storage.setProperty (te::SettingID::clickTrackSampleBig, accent);
            storage.setProperty (te::SettingID::clickTrackSampleSmall, beat);
            te::TransportControl::restartAllTransports (engine, false);
        }
    }

    juce::StringArray MetronomePanel::getSoundNames()
    {
        return { "Classic", "Beep", "Woodblock", "Custom" };
    }

    juce::String MetronomePanel::getCurrentSoundName (te::Engine& engine)
    {
        auto accent = engine.getPropertyStorage().getProperty (te::SettingID::clickTrackSampleBig).toString();

        if (accent.isEmpty())                       return "Classic";
        if (accent.contains ("cdaw_beep"))          return "Beep";
        if (accent.contains ("cdaw_woodblock"))     return "Woodblock";
        return "Custom";
    }

    void MetronomePanel::applySound (te::Engine& engine, const juce::String& soundName)
    {
        if (soundName == "Classic")
            setClickFiles (engine, {}, {});
        else if (soundName == "Beep")
            setClickFiles (engine, makeClick ("cdaw_beep_accent", false, 1760.0, 0.8f).getFullPathName(),
                                   makeClick ("cdaw_beep_beat", false, 1175.0, 0.6f).getFullPathName());
        else if (soundName == "Woodblock")
            setClickFiles (engine, makeClick ("cdaw_woodblock_accent", true, 1600.0, 0.9f).getFullPathName(),
                                   makeClick ("cdaw_woodblock_beat", true, 1100.0, 0.7f).getFullPathName());
    }

    MetronomePanel::MetronomePanel (te::Edit& e) : edit (e)
    {
        volumeLabel.setText ("Volume", juce::dontSendNotification);
        soundLabel.setText ("Sound", juce::dontSendNotification);
        outputLabel.setText ("Output", juce::dontSendNotification);

        for (auto* l : { &volumeLabel, &soundLabel, &outputLabel })
            addAndMakeVisible (l);

        // The engine clamps the click level to 0.2 .. 1.0.
        volume.setRange (0.2, 1.0, 0.01);
        volume.setValue (edit.getClickTrackVolume(), juce::dontSendNotification);
        volume.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 20);
        volume.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v * 100)) + "%"; };
        volume.onValueChange = [this]
        {
            edit.setClickTrackVolume ((float) volume.getValue());
            AppContext::get().settings.setDouble (SettingKeys::clickVolumeDb, volume.getValue());
        };
        addAndMakeVisible (volume);

        auto names = getSoundNames();
        for (int i = 0; i < names.size(); ++i)
            sound.addItem (names[i], i + 1);
        sound.setText (getCurrentSoundName (edit.engine), juce::dontSendNotification);
        sound.onChange = [this]
        {
            auto name = sound.getText();
            if (name == "Custom")
                pickCustomFile (true);
            else
                applySound (edit.engine, name);
            refresh();
        };
        addAndMakeVisible (sound);

        customAccent.onClick = [this] { pickCustomFile (true); };
        customBeat.onClick = [this] { pickCustomFile (false); };
        addAndMakeVisible (customAccent);
        addAndMakeVisible (customBeat);

        accent.setToggleState (edit.clickTrackEmphasiseBars.get(), juce::dontSendNotification);
        accent.onClick = [this] { edit.clickTrackEmphasiseBars = accent.getToggleState(); };
        addAndMakeVisible (accent);

        recordOnly.setToggleState (edit.clickTrackRecordingOnly.get(), juce::dontSendNotification);
        recordOnly.onClick = [this] { edit.clickTrackRecordingOnly = recordOnly.getToggleState(); };
        addAndMakeVisible (recordOnly);

        // Output: default or any enabled output pair (e.g. a headphone mix for the drummer).
        auto& dm = edit.engine.getDeviceManager();
        output.addItem ("Default (main output)", 1);
        juce::StringArray ids { "" };
        for (int i = 0; i < dm.getNumWaveOutDevices(); ++i)
            if (auto* dev = dm.getWaveOutDevice (i); dev != nullptr && dev->isEnabled())
            {
                output.addItem (dev->getAliasOrName(), ids.size() + 1);
                ids.add (dev->getName());
            }

        auto current = edit.getClickTrackDevice();
        auto idx = ids.indexOf (current);
        output.setSelectedId (idx > 0 ? idx + 1 : 1, juce::dontSendNotification);
        output.onChange = [this, ids] { edit.setClickTrackOutput (ids[output.getSelectedId() - 1]); };
        addAndMakeVisible (output);

        refresh();
        setSize (380, 230);
    }

    void MetronomePanel::refresh()
    {
        const bool custom = sound.getText() == "Custom";
        customAccent.setVisible (custom);
        customBeat.setVisible (custom);
    }

    void MetronomePanel::pickCustomFile (bool accentSample)
    {
        chooser = std::make_unique<juce::FileChooser> (accentSample ? "Accent click sample" : "Beat click sample",
                                                       juce::File::getSpecialLocation (juce::File::userMusicDirectory), "*.wav;*.aif;*.aiff;*.flac");
        juce::Component::SafePointer<MetronomePanel> safe (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [safe, accentSample] (const juce::FileChooser& fc)
        {
            if (safe == nullptr || ! fc.getResult().existsAsFile())
                return;

            auto& storage = safe->edit.engine.getPropertyStorage();
            auto path = fc.getResult().getFullPathName();
            auto accentPath = accentSample ? path : storage.getProperty (te::SettingID::clickTrackSampleBig).toString();
            auto beatPath = accentSample ? storage.getProperty (te::SettingID::clickTrackSampleSmall).toString() : path;

            // One file for both until the other is chosen.
            if (accentPath.isEmpty() || accentPath.contains ("cdaw_")) accentPath = path;
            if (beatPath.isEmpty() || beatPath.contains ("cdaw_"))     beatPath = path;

            setClickFiles (safe->edit.engine, accentPath, beatPath);
            safe->sound.setText ("Custom", juce::dontSendNotification);
            safe->refresh();
        });
    }

    void MetronomePanel::paint (juce::Graphics& g)
    {
        g.fillAll (Palette::panelBackground);
    }

    void MetronomePanel::resized()
    {
        auto r = getLocalBounds().reduced (12);
        auto row = [&r] { auto x = r.removeFromTop (26); r.removeFromTop (6); return x; };

        auto a = row(); volumeLabel.setBounds (a.removeFromLeft (70)); volume.setBounds (a);
        auto b = row(); soundLabel.setBounds (b.removeFromLeft (70)); sound.setBounds (b);
        auto c = row(); c.removeFromLeft (70);
        customAccent.setBounds (c.removeFromLeft (c.getWidth() / 2 - 2)); c.removeFromLeft (4); customBeat.setBounds (c);
        auto d = row(); outputLabel.setBounds (d.removeFromLeft (70)); output.setBounds (d);
        accent.setBounds (row());
        recordOnly.setBounds (row());
    }

    void MetronomePanel::show (te::Edit& edit, juce::Component& target)
    {
        auto panel = std::make_unique<MetronomePanel> (edit);
        juce::CallOutBox::launchAsynchronously (std::move (panel), target.getScreenBounds(), nullptr);
    }
}
