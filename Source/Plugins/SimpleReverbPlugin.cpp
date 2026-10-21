#include "SimpleReverbPlugin.h"
#include "../UI/Plugins/SimpleReverbEditor.h"

namespace cdaw
{
    const char* SimpleReverbPlugin::xmlTypeName = "cdawSimpleReverb";

    namespace
    {
        float parseFloat (const juce::String& s)    { return s.getFloatValue(); }
        juce::String percent (float v)              { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }
        float parsePercent (const juce::String& s)  { return s.getFloatValue() / 100.0f; }

        juce::NormalisableRange<float> skewed (float lo, float hi, float centre)
        {
            juce::NormalisableRange<float> r (lo, hi);
            r.setSkewForCentre (centre);
            return r;
        }
    }

    SimpleReverbPlugin::SimpleReverbPlugin (te::PluginCreationInfo info) : te::Plugin (info)
    {
        size     = { *this, "size", "size", TRANS("Size"), 0.5f, { 0.0f, 1.0f }, percent, parsePercent };
        decay    = { *this, "decay", "decay", TRANS("Decay"), 1.8f, skewed (0.3f, 10.0f, 2.0f),
                     [] (float v) { return juce::String (v, v < 10.0f ? 1 : 0) + " s"; }, parseFloat };
        preDelay = { *this, "predelay", "predelay", TRANS("Pre-delay"), 15.0f, skewed (0.0f, 150.0f, 30.0f),
                     [] (float v) { return juce::String (juce::roundToInt (v)) + " ms"; }, parseFloat };
        tone     = { *this, "tone", "tone", TRANS("Tone"), 0.0f, { -1.0f, 1.0f },
                     [] (float v)
                     {
                         if (std::abs (v) < 0.05f) return juce::String ("Neutral");
                         return (v < 0.0f ? "Dark " : "Bright ") + juce::String (juce::roundToInt (std::abs (v) * 100.0f));
                     },
                     [] (const juce::String& s)
                     {
                         auto v = s.retainCharacters ("0123456789.").getFloatValue() / 100.0f;
                         return s.containsIgnoreCase ("dark") ? -v : v;
                     } };
        mix      = { *this, "mix", "mix", TRANS("Mix"), 0.25f, { 0.0f, 1.0f }, percent, parsePercent };
    }

    SimpleReverbPlugin::~SimpleReverbPlugin()
    {
        notifyListenersOfDeletion();

        for (auto* p : { &size, &decay, &preDelay, &tone, &mix })
            p->reset();
    }

    std::unique_ptr<te::Plugin::EditorComponent> SimpleReverbPlugin::createEditor()
    {
        return std::make_unique<SimpleReverbEditor> (*this);
    }

    void SimpleReverbPlugin::restorePluginStateFromValueTree (const juce::ValueTree& v)
    {
        for (auto* p : { &size, &decay, &preDelay, &tone, &mix })
            p->setFromValueTree (v);
    }

    ReverbEngine::Settings SimpleReverbPlugin::getSettings() const
    {
        ReverbEngine::Settings s;
        const float t = tone.getCurrentValue();
        const float roomSize = size.getCurrentValue();

        s.mix = mix.getCurrentValue();
        s.preDelay = preDelay.getCurrentValue();
        s.size = roomSize;
        s.decay = decay.getCurrentValue();
        s.diffusion = 0.8f;
        s.modulation = 0.25f;
        s.lowDecay = 1.0f;
        s.dampFreq = 5000.0f;

        // Tone: darker = highs die sooner and the reverb's top end is cut lower; brighter = the
        // highs ring as long as the rest and the high cut opens up completely.
        s.highDecay = juce::jlimit (0.15f, 0.95f, 0.55f + 0.4f * t);
        s.postHighCut = t < 0.0f ? 12000.0f * std::pow (2.0f, 2.6f * t)
                                 : juce::jmin (ReverbEngine::highCutOff, 12000.0f + 8000.0f * t);

        // Smaller rooms: earlier, stronger reflections.
        s.early = 0.25f + 0.25f * (1.0f - roomSize);
        s.width = 1.0f;
        s.preLowCut = ReverbEngine::lowCutOff;
        s.preHighCut = ReverbEngine::highCutOff;
        s.postLowCut = 120.0f;
        s.freeze = false;
        return s;
    }

    double SimpleReverbPlugin::getTailLength() const
    {
        return preDelay.getCurrentValue() * 0.001 + decay.getCurrentValue() * 1.1;
    }

    void SimpleReverbPlugin::initialise (const te::PluginInitialisationInfo& info)
    {
        engine.prepare (info.sampleRate, info.blockSizeSamples, getSettings());
    }

    void SimpleReverbPlugin::applyToBuffer (const te::PluginRenderContext& fc)
    {
        if (fc.destBuffer != nullptr)
            engine.process (*fc.destBuffer, fc.bufferStartSample, fc.bufferNumSamples, getSettings());
    }
}
