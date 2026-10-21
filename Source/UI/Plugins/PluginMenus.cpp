#include "PluginMenus.h"
#include "../LookAndFeel/Palette.h"
#include "../../App/AppSettings.h"

namespace cdaw::PluginMenus
{
    te::Plugin::Ptr createPlugin (te::Edit& edit, const juce::PluginDescription& desc)
    {
        auto type = te::PluginManager::isBuiltInPlugin (desc) ? desc.fileOrIdentifier
                                                               : juce::String (te::ExternalPlugin::xmlTypeName);
        return edit.getPluginCache().createNewPlugin (type, desc);
    }

    juce::Array<juce::PluginDescription> getBuiltInEffects()
    {
        using PM = te::PluginManager;
        return {
            PM::createBuiltInPluginDescription<te::EqualiserPlugin>(),
            PM::createBuiltInPluginDescription<te::CompressorPlugin>(),
            PM::createBuiltInPluginDescription<te::ReverbPlugin>(),
            PM::createBuiltInPluginDescription<te::DelayPlugin>(),
            PM::createBuiltInPluginDescription<te::ChorusPlugin>(),
            PM::createBuiltInPluginDescription<te::PhaserPlugin>(),
            PM::createBuiltInPluginDescription<te::PitchShiftPlugin>(),
            PM::createBuiltInPluginDescription<te::LowPassPlugin>(),
            PM::createBuiltInPluginDescription<te::ImpulseResponsePlugin>(),
            PM::createBuiltInPluginDescription<te::AuxSendPlugin>(),
            PM::createBuiltInPluginDescription<te::ToneGeneratorPlugin>(),
        };
    }

    juce::Array<juce::PluginDescription> getBuiltInInstruments()
    {
        using PM = te::PluginManager;
        return {
            PM::createBuiltInPluginDescription<te::FourOscPlugin> (true),
            PM::createBuiltInPluginDescription<te::SamplerPlugin> (true),
        };
    }

    juce::PopupMenu createMenu (te::Engine& engine, Kind kind, std::function<void (const juce::PluginDescription&)> onChosen)
    {
        juce::PopupMenu menu;
        auto& known = engine.getPluginManager().knownPluginList;

        // Group VST3s by manufacturer.
        std::map<juce::String, juce::Array<juce::PluginDescription>> byVendor;
        auto* behaviour = &engine.getEngineBehaviour();

        for (auto& d : known.getTypes())
        {
            if (kind == Kind::effects && d.isInstrument)      continue;
            if (kind == Kind::instruments && ! d.isInstrument) continue;
            if (behaviour->isPluginDisabled (d.createIdentifierString())) continue;

            byVendor[d.manufacturerName.isNotEmpty() ? d.manufacturerName : juce::String ("Unknown")].add (d);
        }

        if (byVendor.empty())
            menu.addItem ("(no plugins found - use Plugins > Plugin Manager to scan)", false, false, [] {});

        for (auto& [vendor, list] : byVendor)
        {
            juce::PopupMenu sub;
            auto sorted = list;
            std::sort (sorted.begin(), sorted.end(), [] (const auto& a, const auto& b) { return a.name.compareIgnoreCase (b.name) < 0; });

            for (auto& d : sorted)
                sub.addItem (d.name + (d.isInstrument && kind == Kind::all ? "  (instrument)" : ""), [onChosen, d] { onChosen (d); });

            menu.addSubMenu (vendor, sub);
        }

        menu.addSeparator();
        juce::PopupMenu builtIn;

        if (kind != Kind::instruments)
            for (auto& d : getBuiltInEffects())
                builtIn.addItem (d.name, [onChosen, d] { onChosen (d); });

        if (kind != Kind::effects)
        {
            if (builtIn.getNumItems() > 0)
                builtIn.addSeparator();

            for (auto& d : getBuiltInInstruments())
                builtIn.addItem (d.name, [onChosen, d] { onChosen (d); });
        }

        menu.addSubMenu ("CDAW built-in", builtIn);
        return menu;
    }

    int getPreFaderInsertIndex (te::Track& track)
    {
        return getPreFaderInsertIndex (track.pluginList);
    }

    int getPreFaderInsertIndex (te::PluginList& list)
    {
        auto plugins = list.getPlugins();

        for (int i = 0; i < plugins.size(); ++i)
            if (dynamic_cast<te::VolumeAndPanPlugin*> (plugins[i].get()) != nullptr
                 || dynamic_cast<te::LevelMeterPlugin*> (plugins[i].get()) != nullptr)
                return i;

        return plugins.size();
    }

    void showInsertMenu (te::Track& track, int index, juce::Component* target)
    {
        showInsertMenu (track.pluginList, index, target);
    }

    void showInsertMenu (te::PluginList& list, int index, juce::Component* target)
    {
        // Keep the owning track alive while the menu is open.
        te::Track::Ptr owner = list.getOwnerTrack();
        auto* listPtr = &list;
        auto& editRef = list.getEdit();
        auto menu = createMenu (editRef.engine, owner != nullptr ? Kind::all : Kind::effects,
                                [owner, listPtr, index, &editRef] (const juce::PluginDescription& desc)
        {
            auto& edit = editRef;
            edit.getUndoManager().beginNewTransaction ("Insert " + desc.name);

            if (auto p = createPlugin (edit, desc))
            {
                auto idx = index < 0 ? getPreFaderInsertIndex (*listPtr) : index;
                listPtr->insertPlugin (p, idx, nullptr);

                if (dynamic_cast<te::ExternalPlugin*> (p.get()) != nullptr)
                    p->showWindowExplicitly();
            }
            else
            {
                edit.engine.getUIBehaviour().showWarningMessage ("Could not load " + desc.name);
            }
        });

        menu.showMenuAsync (target != nullptr ? juce::PopupMenu::Options().withTargetComponent (target)
                                              : juce::PopupMenu::Options());
    }

    //==============================================================================
    namespace
    {
        struct PluginManagerContent : public juce::Component
        {
            explicit PluginManagerContent (te::Engine& e)
                : list (e.getPluginManager().pluginFormatManager,
                        e.getPluginManager().knownPluginList,
                        AppSettings::getSettingsFolder().getChildFile ("PluginScanDeadMansPedal"),
                        &e.getPropertyStorage().getPropertiesFile(),
                        true)
            {
                info.setText ("Plugins are scanned in a separate process, so a crashing plugin cannot take CDAW down. "
                              "Use Options > \"Scan for new or updated VST3 plug-ins\". Default folder: "
                              "C:\\Program Files\\Common Files\\VST3", juce::dontSendNotification);
                info.setColour (juce::Label::textColourId, Palette::textDim);
                addAndMakeVisible (info);
                addAndMakeVisible (list);
                setSize (820, 600);
            }

            void resized() override
            {
                auto r = getLocalBounds().reduced (8);
                info.setBounds (r.removeFromTop (40));
                list.setBounds (r);
            }

            juce::Label info;
            juce::PluginListComponent list;
        };

        struct PluginManagerWindow : public juce::DocumentWindow
        {
            explicit PluginManagerWindow (te::Engine& e)
                : DocumentWindow ("Plugin Manager", Palette::panelBackground, DocumentWindow::closeButton)
            {
                setUsingNativeTitleBar (true);
                setContentOwned (new PluginManagerContent (e), true);
                setResizable (true, false);
                centreWithSize (getWidth(), getHeight());
                setVisible (true);
            }

            void closeButtonPressed() override  { instance.reset(); }

            static inline std::unique_ptr<PluginManagerWindow> instance;
        };
    }

    void showPluginManager (te::Engine& engine)
    {
        if (PluginManagerWindow::instance == nullptr)
            PluginManagerWindow::instance = std::make_unique<PluginManagerWindow> (engine);

        PluginManagerWindow::instance->toFront (true);
    }

    void closePluginManager()
    {
        PluginManagerWindow::instance.reset();
    }
}
