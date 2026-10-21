#pragma once

#include "../../PCH.h"

namespace cdaw::PluginMenus
{
    enum class Kind { effects, instruments, all };

    /** Creates a plugin instance (VST3 or built-in) from a description. */
    te::Plugin::Ptr createPlugin (te::Edit&, const juce::PluginDescription&);

    /** Built-in effects offered in menus. */
    juce::Array<juce::PluginDescription> getBuiltInEffects();
    juce::Array<juce::PluginDescription> getBuiltInInstruments();

    /** Menu of plugins grouped by manufacturer, plus built-ins. Calls back with the chosen description. */
    juce::PopupMenu createMenu (te::Engine&, Kind, std::function<void (const juce::PluginDescription&)>);

    /** Inserts a plugin chosen from a menu into track's plugin list at index (-1 = before the volume plugin). */
    void showInsertMenu (te::Track&, int index, juce::Component* target);
    void showInsertMenu (te::PluginList&, int index, juce::Component* target);

    /** Index before the VolumeAndPan / LevelMeter plugins (so inserts are pre-fader). */
    int getPreFaderInsertIndex (te::Track&);
    int getPreFaderInsertIndex (te::PluginList&);

    /** Opens the plugin manager / scanner window. */
    void showPluginManager (te::Engine&);
    void closePluginManager();
}
