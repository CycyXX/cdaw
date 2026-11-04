#pragma once

#include "../../PCH.h"
#include "../../Plugins/SharedEffectPlugin.h"
#include "Shared/Common/EffectModel.h"
#include "TeKnobParameter.h"

namespace cdaw
{
    /** The window of C-Delay, C-Chorus, C-Phaser and C-Filter in CDAW: the view shared with the
        VST3 build, connected to the built-in plugin's parameters, undo and the edit's tempo. */
    template <typename View, typename Engine>
    class SharedEffectEditor : public te::Plugin::EditorComponent,
                               public EffectModel
    {
    public:
        SharedEffectEditor (SharedEffectPlugin& p, Engine& engine) : plugin (p), pluginRef (&p)
        {
            for (int i = 0; i < plugin.getNumSharedParameters(); ++i)
                parameters.push_back (knobParameter (plugin.parameter (i).getParameter()));

            view = std::make_unique<View> (*this, engine);
            addAndMakeVisible (*view);

            constrainer.setMinimumSize (View::minimumWidth, View::minimumHeight);
            setSize (View::defaultWidth, View::defaultHeight);
        }

        ~SharedEffectEditor() override
        {
            view.reset();
        }

        bool allowWindowResizing() override                                 { return true; }
        juce::ComponentBoundsConstrainer* getBoundsConstrainer() override   { return &constrainer; }
        void resized() override                                             { view->setBounds (getLocalBounds()); }

        /** For tests and snapshots. */
        View& getView()                                                     { return *view; }

        std::shared_ptr<KnobParameter> parameter (int id) override          { return parameters[(size_t) id]; }
        void beginUndoTransaction (const juce::String& name) override       { plugin.edit.getUndoManager().beginNewTransaction (name); }
        double getBpm() override                                            { return plugin.getBpmNow(); }

        juce::String getText (const juce::String& key) override             { return plugin.state[juce::Identifier (key)].toString(); }
        void setText (const juce::String& key, const juce::String& value) override
        {
            plugin.state.setProperty (juce::Identifier (key), value, plugin.getUndoManager());
        }

    private:
        SharedEffectPlugin& plugin;
        te::Plugin::Ptr pluginRef;
        std::vector<std::shared_ptr<KnobParameter>> parameters;
        std::unique_ptr<View> view;
        juce::ComponentBoundsConstrainer constrainer;
    };
}
