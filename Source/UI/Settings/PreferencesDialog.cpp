#include "PreferencesDialog.h"
#include "../LookAndFeel/Palette.h"
#include "../Plugins/PluginMenus.h"
#include "../../App/AppContext.h"
#include "../../App/AppSettings.h"
#include "../../Model/ProjectSession.h"

namespace cdaw
{
    namespace
    {
        /** Simple vertical form: label + control rows. */
        struct FormPage : public juce::Component
        {
            void addRow (const juce::String& label, juce::Component& c, int height = 26)
            {
                rows.push_back ({ label, &c, height });
                addAndMakeVisible (c);
            }

            void addNote (const juce::String& text)
            {
                auto* l = notes.add (new juce::Label ({}, text));
                l->setColour (juce::Label::textColourId, Palette::textDim);
                l->setFont (juce::FontOptions (12.0f));
                l->setJustificationType (juce::Justification::topLeft);
                rows.push_back ({ {}, l, 34 });
                addAndMakeVisible (l);
            }

            void paint (juce::Graphics& g) override
            {
                g.fillAll (Palette::panelBackground);
                g.setColour (Palette::text);
                g.setFont (juce::FontOptions (13.0f));

                for (auto& r : rows)
                    if (r.label.isNotEmpty())
                        g.drawText (r.label, 16, r.comp->getY(), 200, r.comp->getHeight(), juce::Justification::centredLeft);
            }

            void resized() override
            {
                int y = 16;
                for (auto& r : rows)
                {
                    auto x = r.label.isNotEmpty() ? 230 : 16;
                    r.comp->setBounds (x, y, getWidth() - x - 16, r.height);
                    y += r.height + 8;
                }
            }

            struct Row { juce::String label; juce::Component* comp; int height; };
            std::vector<Row> rows;
            juce::OwnedArray<juce::Label> notes;
        };

        //==============================================================================
        struct AudioPage : public juce::Component
        {
            AudioPage()
                : selector (AppContext::get().engine().getDeviceManager().deviceManager, 0, 64, 0, 64, false, false, false, false)
            {
                addAndMakeVisible (selector);

                monoButton.onClick = [] { auto& dm = AppContext::get().engine().getDeviceManager(); dm.setAllWaveInputsToNumChannels (1); dm.saveSettings(); };
                stereoButton.onClick = [] { auto& dm = AppContext::get().engine().getDeviceManager(); dm.setAllWaveInputsToNumChannels (2); dm.saveSettings(); };
                monoButton.setTooltip ("Each hardware input becomes its own mono input (best for guitar / mic tracking)");
                stereoButton.setTooltip ("Inputs are paired as stereo inputs (1+2, 3+4, ...)");
                addAndMakeVisible (monoButton);
                addAndMakeVisible (stereoButton);

                info.setText ("ASIO is recommended. The Yamaha Steinberg USB driver sets its buffer size in its own control panel "
                              "(use the 'Control Panel' button above). Lower buffer = less monitoring latency, more CPU.",
                              juce::dontSendNotification);
                info.setColour (juce::Label::textColourId, Palette::textDim);
                addAndMakeVisible (info);
            }

            void resized() override
            {
                auto r = getLocalBounds().reduced (8);
                auto bottom = r.removeFromBottom (70);
                selector.setBounds (r);
                auto row = bottom.removeFromTop (28);
                monoButton.setBounds (row.removeFromLeft (180));
                row.removeFromLeft (8);
                stereoButton.setBounds (row.removeFromLeft (180));
                info.setBounds (bottom);
            }

            juce::AudioDeviceSelectorComponent selector;
            juce::TextButton monoButton { "Inputs as mono" }, stereoButton { "Inputs as stereo pairs" };
            juce::Label info;
        };

        //==============================================================================
        struct MidiPage : public juce::Component, private juce::Timer
        {
            MidiPage()
            {
                note.setText ("MIDI outputs are only needed for external hardware. Keep virtual loop ports (e.g. teVirtualMIDI) "
                              "disabled as outputs to avoid feedback.", juce::dontSendNotification);
                note.setColour (juce::Label::textColourId, Palette::textDim);
                addAndMakeVisible (note);
                rebuild();
                startTimer (2000);
            }

            void timerCallback() override
            {
                auto& dm = AppContext::get().engine().getDeviceManager();
                if (dm.getNumMidiInDevices() + dm.getNumMidiOutDevices() != lastCount)
                    rebuild();
            }

            void rebuild()
            {
                toggles.clear();
                auto& dm = AppContext::get().engine().getDeviceManager();
                lastCount = dm.getNumMidiInDevices() + dm.getNumMidiOutDevices();

                for (auto& in : dm.getMidiInDevices())
                {
                    std::shared_ptr<te::MidiInputDevice> dev = in;
                    auto* t = toggles.add (new juce::ToggleButton ("In:  " + dev->getName()));
                    t->setToggleState (dev->isEnabled(), juce::dontSendNotification);
                    t->onClick = [dev, t] { dev->setEnabled (t->getToggleState()); };
                    addAndMakeVisible (t);
                }

                for (int i = 0; i < dm.getNumMidiOutDevices(); ++i)
                {
                    auto* dev = dm.getMidiOutDevice (i);
                    auto* t = toggles.add (new juce::ToggleButton ("Out: " + dev->getName()));
                    t->setToggleState (dev->isEnabled(), juce::dontSendNotification);
                    t->onClick = [dev, t] { dev->setEnabled (t->getToggleState()); };
                    addAndMakeVisible (t);
                }

                resized();
            }

            void paint (juce::Graphics& g) override  { g.fillAll (Palette::panelBackground); }

            void resized() override
            {
                auto r = getLocalBounds().reduced (16);
                for (auto t : toggles)
                {
                    t->setBounds (r.removeFromTop (26));
                    r.removeFromTop (2);
                }
                r.removeFromTop (12);
                note.setBounds (r.removeFromTop (40));
            }

            juce::OwnedArray<juce::ToggleButton> toggles;
            juce::Label note;
            int lastCount = -1;
        };

        //==============================================================================
        struct RecordingPage : public FormPage
        {
            RecordingPage()
            {
                auto& s = AppContext::get().settings;

                depth.addItem ("16-bit", 16);
                depth.addItem ("24-bit", 24);
                depth.addItem ("32-bit float", 32);
                depth.setSelectedId (s.getInt (SettingKeys::recordBitDepth, 24), juce::dontSendNotification);
                depth.onChange = [this] { apply(); };
                addRow ("Recording bit depth", depth);

                offset.setRange (-50.0, 50.0, 0.1);
                offset.setValue (s.getDouble (SettingKeys::recordOffsetMs, 0.0), juce::dontSendNotification);
                offset.setTextValueSuffix (" ms");
                offset.setTextBoxStyle (juce::Slider::TextBoxRight, false, 80, 22);
                offset.onValueChange = [this] { apply(); };
                addRow ("Extra recording offset", offset);
                addNote ("CDAW already compensates the latency the driver reports. Measure with a loopback cable "
                         "(output -> input) and adjust here only if recordings are still early or late.");

                midiMode.addItemList ({ "Overdub (merge into existing clip)", "Replace existing clips", "New clips (keep both)" }, 1);
                midiMode.setSelectedItemIndex (s.getInt (SettingKeys::midiRecordMode, 0), juce::dontSendNotification);
                midiMode.onChange = [this] { apply(); };
                addRow ("MIDI recording", midiMode);

                countIn.addItemList ({ "1 bar", "2 bars" }, 1);
                countIn.setSelectedItemIndex (juce::jlimit (0, 1, s.getInt (SettingKeys::countInBars, 1) - 1), juce::dontSendNotification);
                countIn.onChange = [this] { apply(); };
                addRow ("Count-in length", countIn);

                clickVolume.setRange (0.2, 1.0, 0.01);
                clickVolume.setValue (s.getDouble (SettingKeys::clickVolumeDb, 0.6), juce::dontSendNotification);
                clickVolume.setTextBoxStyle (juce::Slider::TextBoxRight, false, 80, 22);
                clickVolume.onValueChange = [this] { apply(); };
                addRow ("Metronome volume", clickVolume);
            }

            void apply()
            {
                auto& ctx = AppContext::get();
                ctx.settings.setInt (SettingKeys::recordBitDepth, depth.getSelectedId());
                ctx.settings.setDouble (SettingKeys::recordOffsetMs, offset.getValue());
                ctx.settings.setInt (SettingKeys::midiRecordMode, midiMode.getSelectedItemIndex());
                ctx.settings.setInt (SettingKeys::countInBars, countIn.getSelectedItemIndex() + 1);
                ctx.settings.setDouble (SettingKeys::clickVolumeDb, clickVolume.getValue());
                ctx.engineHost.applyRecordingDefaults();

                if (auto& s = ctx.session; s != nullptr && s->isValid())
                {
                    s->getEdit().setClickTrackVolume ((float) clickVolume.getValue());
                    if (s->getEdit().getCountInMode() != te::Edit::CountIn::none)
                        s->getEdit().setCountInMode (countIn.getSelectedItemIndex() == 1 ? te::Edit::CountIn::twoBar : te::Edit::CountIn::oneBar);
                }
            }

            juce::ComboBox depth, midiMode, countIn;
            juce::Slider offset, clickVolume;
        };

        //==============================================================================
        struct PluginsPage : public FormPage
        {
            PluginsPage()
            {
                manager.onClick = [] { PluginMenus::showPluginManager (AppContext::get().engine()); };
                addRow ("Plugins", manager);

                reenable.onClick = [this]
                {
                    AppContext::get().settings.setString (SettingKeys::disabledPlugins, {});
                    refresh();
                };
                addRow ("Disabled plugins", reenable);

                resetDpi.onClick = [] { AppContext::get().settings.setString (SettingKeys::pluginDpiUnaware, {}); };
                addRow ("Plugin window scaling", resetDpi);

                onTop.setToggleState (AppContext::get().settings.getBool (SettingKeys::pluginWindowsOnTop, true), juce::dontSendNotification);
                onTop.onClick = [this] { AppContext::get().settings.setBool (SettingKeys::pluginWindowsOnTop, onTop.getToggleState()); };
                addRow ("", onTop);

                addNote ("A plugin that crashes CDAW is listed as disabled so the next start is safe. "
                         "Right-click a plugin window to turn DPI scaling off for plugins that look tiny or blurry.");
                refresh();
            }

            void refresh()
            {
                juce::StringArray list;
                list.addLines (AppContext::get().settings.getString (SettingKeys::disabledPlugins));
                list.removeEmptyStrings();
                reenable.setButtonText (list.isEmpty() ? "None disabled" : "Re-enable " + juce::String (list.size()) + " disabled plugin(s)");
                reenable.setEnabled (! list.isEmpty());
            }

            juce::TextButton manager { "Open Plugin Manager (scan VST3)..." }, reenable, resetDpi { "Reset per-plugin DPI settings" };
            juce::ToggleButton onTop { "Keep plugin windows on top of CDAW" };
        };

        //==============================================================================
        struct GeneralPage : public FormPage
        {
            GeneralPage()
            {
                auto& s = AppContext::get().settings;

                folderButton.setButtonText (s.getProjectsFolder().getFullPathName());
                folderButton.onClick = [this]
                {
                    chooser = std::make_unique<juce::FileChooser> ("Projects folder", AppContext::get().settings.getProjectsFolder());
                    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                          [this] (const juce::FileChooser& fc)
                                          {
                                              if (fc.getResult().isDirectory())
                                              {
                                                  AppContext::get().settings.setProjectsFolder (fc.getResult());
                                                  folderButton.setButtonText (fc.getResult().getFullPathName());
                                              }
                                          });
                };
                addRow ("Projects folder", folderButton);

                copyImports.setToggleState (s.getBool (SettingKeys::copyImportedFiles, true), juce::dontSendNotification);
                copyImports.onClick = [this] { AppContext::get().settings.setBool (SettingKeys::copyImportedFiles, copyImports.getToggleState()); };
                addRow ("", copyImports);

                reopen.setToggleState (s.getBool (SettingKeys::reopenLastProject, true), juce::dontSendNotification);
                reopen.onClick = [this] { AppContext::get().settings.setBool (SettingKeys::reopenLastProject, reopen.getToggleState()); };
                addRow ("", reopen);

                middleC.addItem ("C3 (Cubase, Yamaha, Steinberg)", 3);
                middleC.addItem ("C4 (Logic, Ableton, General MIDI)", 4);
                middleC.setSelectedId (s.getInt (SettingKeys::middleCOctave, 3), juce::dontSendNotification);
                middleC.onChange = [this] { AppContext::get().settings.setInt (SettingKeys::middleCOctave, middleC.getSelectedId()); };
                addRow ("Middle C (MIDI 60) is", middleC);

                logs.onClick = [] { AppSettings::getLogsFolder().startAsProcess(); };
                addRow ("Logs", logs);
            }

            juce::TextButton folderButton, logs { "Open log folder" };
            juce::ToggleButton copyImports { "Copy imported audio into the project folder" },
                               reopen { "Reopen the last project on start" };
            juce::ComboBox middleC;
            std::unique_ptr<juce::FileChooser> chooser;
        };
    }

    //==============================================================================
    PreferencesDialog::PreferencesDialog()
    {
        auto bg = Palette::panelBackground;
        tabs.addTab ("Audio", bg, new AudioPage(), true);
        tabs.addTab ("MIDI", bg, new MidiPage(), true);
        tabs.addTab ("Recording", bg, new RecordingPage(), true);
        tabs.addTab ("Plugins", bg, new PluginsPage(), true);
        tabs.addTab ("General", bg, new GeneralPage(), true);
        tabs.addTab ("Shortcuts", bg, new juce::KeyMappingEditorComponent (*AppContext::get().commandManager.getKeyMappings(), true), true);
        tabs.setTabBarDepth (30);
        addAndMakeVisible (tabs);
        setSize (720, 620);
    }

    PreferencesDialog::~PreferencesDialog() = default;

    void PreferencesDialog::resized()
    {
        tabs.setBounds (getLocalBounds());
    }

    void PreferencesDialog::show (int initialTab)
    {
        auto* p = new PreferencesDialog();
        p->tabs.setCurrentTabIndex (initialTab);

        juce::DialogWindow::LaunchOptions o;
        o.dialogTitle = "Preferences";
        o.dialogBackgroundColour = Palette::panelBackground;
        o.content.setOwned (p);
        o.useNativeTitleBar = true;
        o.resizable = true;
        o.launchAsync();
    }
}
