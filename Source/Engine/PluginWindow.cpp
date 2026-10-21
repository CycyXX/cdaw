#include "PluginWindow.h"
#include "../App/AppContext.h"
#include "../UI/LookAndFeel/Palette.h"

namespace cdaw
{
    //==============================================================================
    namespace PluginDpi
    {
        static juce::StringArray getUnawareList()
        {
            juce::StringArray list;
            list.addLines (AppContext::get().settings.getString (SettingKeys::pluginDpiUnaware));
            list.removeEmptyStrings();
            return list;
        }

        bool isDpiAware (te::Plugin& p)
        {
            return ! getUnawareList().contains (p.getIdentifierString());
        }

        void setDpiAware (te::Plugin& p, bool aware)
        {
            auto list = getUnawareList();
            list.removeString (p.getIdentifierString());

            if (! aware)
                list.add (p.getIdentifierString());

            AppContext::get().settings.setString (SettingKeys::pluginDpiUnaware, list.joinIntoString ("\n"));
        }
    }

    //==============================================================================
    PluginWindow::PluginWindow (te::Plugin& p)
        : DocumentWindow (p.getName(), Palette::windowBackground, DocumentWindow::closeButton | DocumentWindow::minimiseButton, true),
          plugin (p)
    {
        setUsingNativeTitleBar (true);
        getConstrainer()->setMinimumOnscreenAmounts (0x10000, 50, 30, 50);
        setResizeLimits (100, 50, 6000, 4000);

        recreateEditor();

        setBoundsConstrained (getLocalBounds() + plugin.windowState->choosePositionForPluginWindow());

        if (AppContext::get().settings.getBool (SettingKeys::pluginWindowsOnTop, true))
            setAlwaysOnTop (true);

        // Let transport shortcuts (Space, Numpad *) work while a plugin window has focus.
        addKeyListener (AppContext::get().commandManager.getKeyMappings());

        updateStoredBounds = true;
    }

    PluginWindow::~PluginWindow()
    {
        updateStoredBounds = false;
        plugin.edit.flushPluginStateIfNeeded (plugin);
        setEditor (nullptr);
    }

    std::unique_ptr<juce::Component> PluginWindow::create (te::Plugin& plugin)
    {
        if (auto external = dynamic_cast<te::ExternalPlugin*> (&plugin))
            if (external->getAudioPluginInstance() == nullptr)
                return {};

        std::unique_ptr<PluginWindow> w;

        {
            struct Blocker : public juce::Component { void inputAttemptWhenModal() override {} };
            Blocker blocker;
            blocker.enterModalState (false);

           #if JUCE_WINDOWS && JUCE_WIN_PER_MONITOR_DPI_AWARE
            if (! PluginDpi::isDpiAware (plugin))
            {
                juce::ScopedDPIAwarenessDisabler disabler;
                w = std::make_unique<PluginWindow> (plugin);
            }
            else
           #endif
            {
                w = std::make_unique<PluginWindow> (plugin);
            }
        }

        if (w == nullptr || w->getEditor() == nullptr)
            return {};

        w->show();
        return w;
    }

    void PluginWindow::show()
    {
        setVisible (true);
        toFront (true);
        setBoundsConstrained (getBounds());
    }

    void PluginWindow::setEditor (std::unique_ptr<te::Plugin::EditorComponent> newEditor)
    {
        setConstrainer (nullptr);
        editor.reset();

        if (newEditor != nullptr)
        {
            editor = std::move (newEditor);
            setContentNonOwned (editor.get(), true);
        }

        setResizable (editor == nullptr || editor->allowWindowResizing(), false);

        if (editor != nullptr && editor->allowWindowResizing())
            setConstrainer (editor->getBoundsConstrainer());
    }

    void PluginWindow::recreateEditor()
    {
        setEditor (nullptr);

        auto ed = plugin.createEditor();

        if (ed == nullptr)
            ed = std::make_unique<GenericPluginEditor> (plugin);

        setEditor (std::move (ed));
    }

    void PluginWindow::recreateEditorAsync()
    {
        setEditor (nullptr);

        juce::Timer::callAfterDelay (50, [sp = juce::Component::SafePointer<PluginWindow> (this)]
        {
            if (sp != nullptr)
                sp->recreateEditor();
        });
    }

    void PluginWindow::moved()
    {
        if (updateStoredBounds)
        {
            plugin.windowState->lastWindowBounds = getBounds();
            plugin.edit.pluginChanged (plugin);
        }
    }

    void PluginWindow::mouseDown (const juce::MouseEvent& e)
    {
        if (! e.mods.isPopupMenu())
            return;

        // Right-click on the window frame: per-plugin options.
        juce::PopupMenu m;
        const bool aware = PluginDpi::isDpiAware (plugin);
        m.addItem ("Disable DPI scaling for this plugin", true, ! aware, [this, aware]
        {
            PluginDpi::setDpiAware (plugin, ! aware);
            plugin.windowState->closeWindowExplicitly();
            juce::MessageManager::callAsync ([p = te::Plugin::Ptr (&plugin)] { p->showWindowExplicitly(); });
        });
        m.addItem ("Always on top", true, isAlwaysOnTop(), [this]
        {
            setAlwaysOnTop (! isAlwaysOnTop());
            AppContext::get().settings.setBool (SettingKeys::pluginWindowsOnTop, isAlwaysOnTop());
        });
        m.showMenuAsync ({});
    }

    //==============================================================================
    struct GenericPluginEditor::ParamRow : public juce::Component
    {
        explicit ParamRow (te::AutomatableParameter::Ptr p) : param (p)
        {
            name.setText (param->getParameterName(), juce::dontSendNotification);
            name.setColour (juce::Label::textColourId, Palette::textDim);
            addAndMakeVisible (name);

            auto range = param->getValueRange();
            slider.setSliderStyle (juce::Slider::LinearHorizontal);
            slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
            slider.setRange (range.getStart(), range.getEnd());
            slider.setValue (param->getCurrentValue(), juce::dontSendNotification);
            slider.onDragStart = [this] { param->parameterChangeGestureBegin(); };
            slider.onDragEnd   = [this] { param->parameterChangeGestureEnd(); };
            slider.onValueChange = [this] { param->setParameter ((float) slider.getValue(), juce::sendNotification); };
            addAndMakeVisible (slider);

            value.setJustificationType (juce::Justification::centredRight);
            addAndMakeVisible (value);
            refresh();
        }

        void refresh()
        {
            if (! slider.isMouseButtonDown())
                slider.setValue (param->getCurrentValue(), juce::dontSendNotification);

            value.setText (param->getCurrentValueAsStringWithLabel(), juce::dontSendNotification);
        }

        void resized() override
        {
            auto r = getLocalBounds();
            name.setBounds (r.removeFromLeft (140));
            value.setBounds (r.removeFromRight (90));
            slider.setBounds (r);
        }

        te::AutomatableParameter::Ptr param;
        juce::Label name, value;
        juce::Slider slider;
    };

    GenericPluginEditor::GenericPluginEditor (te::Plugin& p) : plugin (&p)
    {
        for (auto param : plugin->getAutomatableParameters())
            content.addAndMakeVisible (rows.add (new ParamRow (param)));

        viewport.setViewedComponent (&content, false);
        viewport.setScrollBarsShown (true, false);
        addAndMakeVisible (viewport);

        constrainer.setMinimumSize (360, 80);
        setSize (440, juce::jlimit (80, 600, rows.size() * 28 + 16));
        startTimerHz (15);
    }

    GenericPluginEditor::~GenericPluginEditor()
    {
        stopTimer();
    }

    void GenericPluginEditor::paint (juce::Graphics& g)
    {
        g.fillAll (Palette::panelBackground);

        if (rows.isEmpty())
        {
            g.setColour (Palette::textDim);
            g.drawText ("This plugin has no adjustable parameters.", getLocalBounds(), juce::Justification::centred);
        }
    }

    void GenericPluginEditor::resized()
    {
        viewport.setBounds (getLocalBounds());
        auto w = viewport.getMaximumVisibleWidth();
        content.setSize (w, rows.size() * 28 + 16);

        int y = 8;
        for (auto r : rows)
        {
            r->setBounds (8, y, w - 16, 24);
            y += 28;
        }
    }

    void GenericPluginEditor::timerCallback()
    {
        for (auto r : rows)
            r->refresh();
    }
}
