#pragma once

#include "../PCH.h"

namespace cdaw
{
    /** Remembers which plugins must be opened without per-monitor DPI awareness
        (older plugins that otherwise appear tiny or blurry). Stored in CDAW.settings. */
    namespace PluginDpi
    {
        bool isDpiAware (te::Plugin&);
        void setDpiAware (te::Plugin&, bool);
    }

    /** Floating window hosting a plugin's editor (or a generic editor for built-ins). */
    class PluginWindow : public juce::DocumentWindow
    {
    public:
        explicit PluginWindow (te::Plugin&);
        ~PluginWindow() override;

        static std::unique_ptr<juce::Component> create (te::Plugin&);

        void show();
        void recreateEditor();
        void recreateEditorAsync();
        te::Plugin::EditorComponent* getEditor() const      { return editor.get(); }

    private:
        void setEditor (std::unique_ptr<te::Plugin::EditorComponent>);
        void moved() override;
        void closeButtonPressed() override                  { plugin.windowState->closeWindowExplicitly(); }
        float getDesktopScaleFactor() const override        { return 1.0f; }
        void mouseDown (const juce::MouseEvent&) override;

        te::Plugin& plugin;
        std::unique_ptr<te::Plugin::EditorComponent> editor;
        bool updateStoredBounds = false;
    };

    /** Editor for plugins without their own GUI (CDAW's built-in effects):
        one labelled slider per automatable parameter. */
    class GenericPluginEditor : public te::Plugin::EditorComponent,
                                private juce::Timer
    {
    public:
        explicit GenericPluginEditor (te::Plugin&);
        ~GenericPluginEditor() override;

        bool allowWindowResizing() override                             { return true; }
        juce::ComponentBoundsConstrainer* getBoundsConstrainer() override { return &constrainer; }
        void resized() override;
        void paint (juce::Graphics&) override;

    private:
        struct ParamRow;
        void timerCallback() override;

        te::Plugin::Ptr plugin;
        juce::OwnedArray<ParamRow> rows;
        juce::Viewport viewport;
        juce::Component content;
        juce::ComponentBoundsConstrainer constrainer;
    };
}
