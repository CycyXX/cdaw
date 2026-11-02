#include "CompressorEditor.h"
#include "TeKnobParameter.h"
#include "../../Plugins/CompressorPlugin.h"
#include "../../Model/MixerOps.h"

namespace cdaw
{
    namespace
    {
        /** A Tracktion parameter shown in the units of CompressorParameters: the plugin stores
            threshold as a gain and ratio as 1 / ratio, the view works in dB and N:1. */
        struct ConvertedKnobParameter : public TeKnobParameter
        {
            ConvertedKnobParameter (te::AutomatableParameter& p, CompressorParameters::ID i,
                                    std::function<float (float)> toShared, std::function<float (float)> fromShared)
                : TeKnobParameter (p), spec (CompressorParameters::get (i)), toSharedUnit (std::move (toShared)), fromSharedUnit (std::move (fromShared)) {}

            float getValue() const override                     { return toSharedUnit (param->getCurrentValue()); }
            void setValue (float v) override                    { param->setParameter (fromSharedUnit (v), juce::sendNotification); }
            juce::Range<float> getRange() const override        { return { spec.minimum, spec.maximum }; }
            juce::String getText (float v) const override       { return spec.toText (v); }

            const CompressorParameters::Spec& spec;
            std::function<float (float)> toSharedUnit, fromSharedUnit;
        };
    }

    CompressorEditor::CompressorEditor (CompressorPlugin& p) : plugin (p), pluginRef (&p)
    {
        using namespace CompressorParameters;

        parameters.resize ((size_t) count);
        parameters[threshold] = std::make_shared<ConvertedKnobParameter> (plugin.thresholdGain.getParameter(), threshold,
            [] (float gain)  { return juce::Decibels::gainToDecibels (gain, -100.0f); },
            [] (float db)    { return juce::jlimit (te::CompressorPlugin::getMinThreshold(), te::CompressorPlugin::getMaxThreshold(), juce::Decibels::decibelsToGain (db)); });
        parameters[ratio] = std::make_shared<ConvertedKnobParameter> (plugin.ratio.getParameter(), ratio,
            [] (float inverse)  { return inverse > 0.001f ? juce::jmin (limitRatio, 1.0f / inverse) : limitRatio; },
            [] (float r)        { return r >= limitRatio ? 0.0f : juce::jlimit (0.0f, 0.95f, 1.0f / r); });
        parameters[knee] = knobParameter (plugin.knee.getParameter());
        parameters[attack] = knobParameter (plugin.attackMs.getParameter());
        parameters[release] = knobParameter (plugin.releaseMs.getParameter());
        parameters[makeUp] = knobParameter (plugin.outputDb.getParameter());
        parameters[sidechainGain] = knobParameter (plugin.sidechainDb.getParameter());

        view = std::make_unique<CompressorView> (*this);
        addAndMakeVisible (*view);

        constrainer.setMinimumSize (CompressorView::minimumWidth, CompressorView::minimumHeight);
        setSize (CompressorView::defaultWidth, CompressorView::defaultHeight);
    }

    CompressorEditor::~CompressorEditor()
    {
        view.reset();
    }

    void CompressorEditor::resized()
    {
        view->setBounds (getLocalBounds());
    }

    std::shared_ptr<KnobParameter> CompressorEditor::parameter (CompressorParameters::ID id)
    {
        return parameters[(size_t) id];
    }

    CompressorEngine& CompressorEditor::getEngine()
    {
        return plugin.engine;
    }

    void CompressorEditor::beginUndoTransaction (const juce::String& name)
    {
        plugin.edit.getUndoManager().beginNewTransaction (name);
    }

    juce::String CompressorEditor::getSidechainSource()
    {
        auto* source = MixerOps::getSidechainSource (plugin);
        return source != nullptr ? source->getName() : juce::String();
    }

    void CompressorEditor::showSidechainMenu (juce::Component& target)
    {
        MixerOps::createSidechainMenu (plugin).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target));
    }
}
