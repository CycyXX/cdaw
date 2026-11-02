#pragma once

#include "SharedEffectPlugin.h"
#include "../Shared/DSP/FilterEngine.h"
#include "../Shared/Filter/FilterParameters.h"

namespace cdaw
{
    /** C-Filter: CDAW's modulated multimode filter (see FilterEngine), replacing Tracktion's
        low / high-pass filter (old projects are converted by BuiltInPlugins::migrateEditState,
        and the cutoff keeps that filter's "frequency" ID so its automation still applies). Its
        DSP, parameters (FilterParameters) and editor view (FilterView) are shared with the VST3
        build in VST3/CFilter. */
    class FilterPlugin : public SharedEffectPlugin
    {
    public:
        explicit FilterPlugin (te::PluginCreationInfo);
        ~FilterPlugin() override;

        static const char* getPluginName()      { return NEEDS_TRANS("C-Filter"); }
        static const char* xmlTypeName;

        juce::String getName() const override                   { return TRANS("C-Filter"); }
        juce::String getPluginType() override                   { return xmlTypeName; }
        juce::String getShortName (int) override                { return "C-Filter"; }
        juce::String getSelectableDescription() override        { return TRANS("C-Filter"); }

        void initialise (const te::PluginInitialisationInfo&) override;
        void deinitialise() override                            {}
        void reset() override                                   { engine.reset(); }
        void applyToBuffer (const te::PluginRenderContext&) override;
        std::unique_ptr<EditorComponent> createEditor() override;

        FilterEngine::Settings getSettings() const              { return FilterParameters::toSettings (valueOf()); }

        FilterEngine engine;

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FilterPlugin)
    };
}
