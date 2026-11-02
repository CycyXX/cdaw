#pragma once

#include "SharedEffectPlugin.h"
#include "../Shared/DSP/DelayEngine.h"
#include "../Shared/Delay/DelayParameters.h"

namespace cdaw
{
    /** C-Delay: CDAW's delay (see DelayEngine), replacing Tracktion's simple one (old projects are
        converted by BuiltInPlugins::migrateEditState). Its DSP, parameters (DelayParameters) and
        editor view (DelayView) are shared with the VST3 build in VST3/CDelay. */
    class DelayPlugin : public SharedEffectPlugin
    {
    public:
        explicit DelayPlugin (te::PluginCreationInfo);
        ~DelayPlugin() override;

        static const char* getPluginName()      { return NEEDS_TRANS("C-Delay"); }
        static const char* xmlTypeName;

        juce::String getName() const override                   { return TRANS("C-Delay"); }
        juce::String getPluginType() override                   { return xmlTypeName; }
        juce::String getShortName (int) override                { return "C-Delay"; }
        juce::String getSelectableDescription() override        { return TRANS("C-Delay"); }
        bool noTail() override                                  { return false; }
        double getTailLength() const override;

        void initialise (const te::PluginInitialisationInfo&) override;
        void deinitialise() override                            {}
        void reset() override                                   { engine.reset(); }
        void applyToBuffer (const te::PluginRenderContext&) override;
        std::unique_ptr<EditorComponent> createEditor() override;

        DelayEngine::Settings getSettings() const               { return DelayParameters::toSettings (valueOf()); }

        DelayEngine engine;

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DelayPlugin)
    };
}
