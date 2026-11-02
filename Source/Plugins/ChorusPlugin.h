#pragma once

#include "SharedEffectPlugin.h"
#include "../Shared/DSP/ChorusEngine.h"
#include "../Shared/Chorus/ChorusParameters.h"

namespace cdaw
{
    /** C-Chorus: CDAW's chorus / ensemble / flanger (see ChorusEngine), replacing Tracktion's
        simple chorus (old projects are converted by BuiltInPlugins::migrateEditState). Its DSP,
        parameters (ChorusParameters) and editor view (ChorusView) are shared with the VST3
        build in VST3/CChorus. */
    class ChorusPlugin : public SharedEffectPlugin
    {
    public:
        explicit ChorusPlugin (te::PluginCreationInfo);
        ~ChorusPlugin() override;

        static const char* getPluginName()      { return NEEDS_TRANS("C-Chorus"); }
        static const char* xmlTypeName;

        juce::String getName() const override                   { return TRANS("C-Chorus"); }
        juce::String getPluginType() override                   { return xmlTypeName; }
        juce::String getShortName (int) override                { return "C-Chorus"; }
        juce::String getSelectableDescription() override        { return TRANS("C-Chorus"); }

        void initialise (const te::PluginInitialisationInfo&) override;
        void deinitialise() override                            {}
        void reset() override                                   { engine.reset(); }
        void applyToBuffer (const te::PluginRenderContext&) override;
        std::unique_ptr<EditorComponent> createEditor() override;

        ChorusEngine::Settings getSettings() const              { return ChorusParameters::toSettings (valueOf()); }

        ChorusEngine engine;

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChorusPlugin)
    };
}
