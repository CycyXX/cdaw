#pragma once

#include "SharedEffectPlugin.h"
#include "../Shared/DSP/EsserEngine.h"
#include "../Shared/Esser/EsserParameters.h"

namespace cdaw
{
    /** C-Esser: CDAW's de-esser (see EsserEngine). Its DSP, parameters (EsserParameters) and
        editor view (EsserView) are shared with the VST3 build in VST3/CEsser. */
    class EsserPlugin : public SharedEffectPlugin
    {
    public:
        explicit EsserPlugin (te::PluginCreationInfo);
        ~EsserPlugin() override;

        static const char* getPluginName()      { return NEEDS_TRANS("C-Esser"); }
        static const char* xmlTypeName;

        juce::String getName() const override                   { return TRANS("C-Esser"); }
        juce::String getPluginType() override                   { return xmlTypeName; }
        juce::String getShortName (int) override                { return "C-Esser"; }
        juce::String getSelectableDescription() override        { return TRANS("C-Esser"); }

        void initialise (const te::PluginInitialisationInfo&) override;
        void deinitialise() override                            {}
        void reset() override                                   { engine.reset(); }
        void applyToBuffer (const te::PluginRenderContext&) override;
        std::unique_ptr<EditorComponent> createEditor() override;

        EsserEngine::Settings getSettings() const               { return EsserParameters::toSettings (valueOf()); }

        EsserEngine engine;

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EsserPlugin)
    };
}
