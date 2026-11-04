#pragma once

#include "SharedEffectPlugin.h"
#include "Shared/DSP/DistEngine.h"
#include "Shared/Dist/DistParameters.h"

namespace cdaw
{
    /** C-Dist: CDAW's distortion pedal voiced after modern guitar amps (see DistEngine). Its DSP,
        parameters (DistParameters), presets (DistPresets) and editor view (DistView) are shared
        with the VST3 build in VST3/CDist. It reports its oversampling's latency, so Tracktion
        lines it up with the other tracks. */
    class DistPlugin : public SharedEffectPlugin
    {
    public:
        explicit DistPlugin (te::PluginCreationInfo);
        ~DistPlugin() override;

        static const char* getPluginName()      { return NEEDS_TRANS("C-Dist"); }
        static const char* xmlTypeName;

        juce::String getName() const override                   { return TRANS("C-Dist"); }
        juce::String getPluginType() override                   { return xmlTypeName; }
        juce::String getShortName (int) override                { return "C-Dist"; }
        juce::String getSelectableDescription() override        { return TRANS("C-Dist"); }

        double getLatencySeconds() override                     { return engine.getLatencySamples() / juce::jmax (1.0, sampleRate); }

        void initialise (const te::PluginInitialisationInfo&) override;
        void deinitialise() override                            {}
        void reset() override                                   { engine.reset(); }
        void applyToBuffer (const te::PluginRenderContext&) override;
        std::unique_ptr<EditorComponent> createEditor() override;

        DistEngine::Settings getSettings() const                { return DistParameters::toSettings (valueOf()); }

        DistEngine engine;

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DistPlugin)
    };
}
