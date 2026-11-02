#pragma once

#include "SharedEffectPlugin.h"
#include "../Shared/DSP/PhaserEngine.h"
#include "../Shared/Phaser/PhaserParameters.h"

namespace cdaw
{
    /** C-Phaser: CDAW's phaser (see PhaserEngine), replacing Tracktion's simple one (old projects
        are converted by BuiltInPlugins::migrateEditState). Its DSP, parameters
        (PhaserParameters) and editor view (PhaserView) are shared with the VST3 build in
        VST3/CPhaser. */
    class PhaserPlugin : public SharedEffectPlugin
    {
    public:
        explicit PhaserPlugin (te::PluginCreationInfo);
        ~PhaserPlugin() override;

        static const char* getPluginName()      { return NEEDS_TRANS("C-Phaser"); }
        static const char* xmlTypeName;

        juce::String getName() const override                   { return TRANS("C-Phaser"); }
        juce::String getPluginType() override                   { return xmlTypeName; }
        juce::String getShortName (int) override                { return "C-Phaser"; }
        juce::String getSelectableDescription() override        { return TRANS("C-Phaser"); }

        void initialise (const te::PluginInitialisationInfo&) override;
        void deinitialise() override                            {}
        void reset() override                                   { engine.reset(); }
        void applyToBuffer (const te::PluginRenderContext&) override;
        std::unique_ptr<EditorComponent> createEditor() override;

        PhaserEngine::Settings getSettings() const              { return PhaserParameters::toSettings (valueOf()); }

        PhaserEngine engine;

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PhaserPlugin)
    };
}
