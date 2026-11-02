#include "BuiltInPlugins.h"
#include "ChorusPlugin.h"
#include "CompressorPlugin.h"
#include "DelayPlugin.h"
#include "EqPlugin.h"
#include "DistPlugin.h"
#include "EsserPlugin.h"
#include "FilterPlugin.h"
#include "LeadPlugin.h"
#include "MultibandPlugin.h"
#include "PhaserPlugin.h"
#include "ReverbPlugin.h"
#include "SimpleReverbPlugin.h"
#include "SynthPlugin.h"

namespace cdaw::BuiltInPlugins
{
    void registerTypes (te::Engine& engine)
    {
        auto& pm = engine.getPluginManager();
        pm.createBuiltInType<CompressorPlugin>();
        pm.createBuiltInType<EqPlugin>();
        pm.createBuiltInType<MultibandPlugin>();
        pm.createBuiltInType<ReverbPlugin>();
        pm.createBuiltInType<SimpleReverbPlugin>();
        pm.createBuiltInType<DelayPlugin>();
        pm.createBuiltInType<ChorusPlugin>();
        pm.createBuiltInType<PhaserPlugin>();
        pm.createBuiltInType<FilterPlugin>();
        pm.createBuiltInType<DistPlugin>();
        pm.createBuiltInType<EsserPlugin>();
        pm.createBuiltInType<SynthPlugin>();
        pm.createBuiltInType<LeadPlugin>();
    }

    template <typename PluginType>
    static juce::PluginDescription describe()
    {
        auto d = te::PluginManager::createBuiltInPluginDescription<PluginType>();
        d.manufacturerName = "CDAW";
        return d;
    }

    juce::PluginDescription getDelayDescription()       { return describe<DelayPlugin>(); }
    juce::PluginDescription getChorusDescription()      { return describe<ChorusPlugin>(); }
    juce::PluginDescription getPhaserDescription()      { return describe<PhaserPlugin>(); }
    juce::PluginDescription getFilterDescription()      { return describe<FilterPlugin>(); }
    juce::PluginDescription getDistDescription()        { return describe<DistPlugin>(); }
    juce::PluginDescription getEsserDescription()       { return describe<EsserPlugin>(); }

    juce::PluginDescription getSynthDescription()
    {
        auto d = te::PluginManager::createBuiltInPluginDescription<SynthPlugin> (true);
        d.manufacturerName = "CDAW";
        return d;
    }

    juce::PluginDescription getLeadDescription()
    {
        auto d = te::PluginManager::createBuiltInPluginDescription<LeadPlugin> (true);
        d.manufacturerName = "CDAW";
        return d;
    }

    juce::PluginDescription getCompressorDescription()
    {
        auto d = te::PluginManager::createBuiltInPluginDescription<CompressorPlugin>();
        d.manufacturerName = "CDAW";
        return d;
    }

    juce::PluginDescription getEqDescription()
    {
        auto d = te::PluginManager::createBuiltInPluginDescription<EqPlugin>();
        d.manufacturerName = "CDAW";
        return d;
    }

    juce::PluginDescription getMultibandDescription()
    {
        auto d = te::PluginManager::createBuiltInPluginDescription<MultibandPlugin>();
        d.manufacturerName = "CDAW";
        return d;
    }

    juce::PluginDescription getReverbDescription()
    {
        auto d = te::PluginManager::createBuiltInPluginDescription<ReverbPlugin>();
        d.manufacturerName = "CDAW";
        return d;
    }

    juce::PluginDescription getSimpleReverbDescription()
    {
        auto d = te::PluginManager::createBuiltInPluginDescription<SimpleReverbPlugin>();
        d.manufacturerName = "CDAW";
        return d;
    }

    static void convertOldReverb (juce::ValueTree& p)
    {
        // Tracktion's Freeverb-style reverb: room size, damping, wet / dry levels and width.
        const float roomSize = p.getProperty ("roomSize", 0.3f);
        const float damp = p.getProperty ("damp", 0.5f);
        const float wet = (float) p.getProperty ("wet", 1.0f / 3.0f) * 3.0f;
        const float dry = (float) p.getProperty ("dry", 0.5f) * 2.0f;
        const float width = p.getProperty ("width", 1.0f);

        for (auto* id : { "roomSize", "damp", "wet", "dry", "width", "mode" })
            p.removeProperty (id, nullptr);

        // Width has no equivalent in the simple Reverb (it's always full width).
        juce::ignoreUnused (width);
        p.setProperty ("size", juce::jlimit (0.0f, 1.0f, roomSize), nullptr);
        p.setProperty ("tone", juce::jlimit (-1.0f, 1.0f, 0.6f - 1.2f * damp), nullptr);
        p.setProperty ("decay", 0.8f + 4.0f * roomSize, nullptr);
        p.setProperty ("mix", juce::jlimit (0.0f, 1.0f, wet / juce::jmax (0.001f, wet + dry)), nullptr);

        for (int i = p.getNumChildren(); --i >= 0;)
            if (p.getChild (i).hasType (te::IDs::AUTOMATIONCURVE))
                p.removeChild (i, nullptr);

        p.setProperty (te::IDs::type, SimpleReverbPlugin::xmlTypeName, nullptr);
    }

    static void convertOldEq (juce::ValueTree& p)
    {
        // Old 4-band EQ: low shelf, two peaks, high shelf. Bands 2, 3, 5 and 7 have those shapes.
        struct Mapping { const char* freq; const char* gain; const char* q; int band; float defaultFreq; };
        static constexpr Mapping map[] {
            { "loFreq",   "loGain",   "loQ",   2, 80.0f },
            { "midFreq1", "midGain1", "midQ1", 3, 3000.0f },
            { "midFreq2", "midGain2", "midQ2", 5, 5000.0f },
            { "hiFreq",   "hiGain",   "hiQ",   7, 17000.0f },
        };

        juce::NamedValueSet values;
        for (auto& m : map)
        {
            auto prefix = "b" + juce::String (m.band);
            values.set (juce::Identifier (prefix + "freq"), (float) p.getProperty (m.freq, m.defaultFreq));
            values.set (juce::Identifier (prefix + "gain"), (float) p.getProperty (m.gain, 0.0f));
            values.set (juce::Identifier (prefix + "q"), juce::jlimit (EqPlugin::minQ, EqPlugin::maxQ, (float) p.getProperty (m.q, 0.5f)));
            values.set (juce::Identifier (prefix + "on"), 1.0f);
        }

        for (auto& m : map)
            for (auto* id : { m.freq, m.gain, m.q })
                p.removeProperty (id, nullptr);

        p.removeProperty ("phaseInvert", nullptr);

        for (auto& v : values)
            p.setProperty (v.name, v.value, nullptr);

        // Automation curves address the old parameter IDs, which no longer exist.
        for (int i = p.getNumChildren(); --i >= 0;)
            if (p.getChild (i).hasType (te::IDs::AUTOMATIONCURVE))
                p.removeChild (i, nullptr);

        p.setProperty (te::IDs::type, EqPlugin::xmlTypeName, nullptr);
    }

    /** Removes a converted plugin's automation, except of the parameters it keeps. */
    static void dropAutomation (juce::ValueTree& p, juce::StringArray keep = {})
    {
        for (int i = p.getNumChildren(); --i >= 0;)
            if (auto child = p.getChild (i); child.hasType (te::IDs::AUTOMATIONCURVE) && ! keep.contains (child[te::IDs::paramID].toString()))
                p.removeChild (i, nullptr);
    }

    /** Tracktion's simple effects crossfade dry and wet at equal power; CDAW's keep the dry at
        full level up to 50 % mix. The mix that gives the same balance of wet to dry. */
    static float convertMix (float oldMix)
    {
        if (oldMix >= 0.999f)
            return 1.0f;

        const double ratio = std::tan (juce::jlimit (0.0, 1.0, (double) oldMix) * juce::MathConstants<double>::halfPi);
        return (float) (ratio <= 1.0 ? ratio * 0.5 : 1.0 - 0.5 / ratio);
    }

    static void setAll (juce::ValueTree& p, std::initializer_list<std::pair<const char*, float>> values)
    {
        for (auto& [id, value] : values)
            p.setProperty (id, value, nullptr);
    }

    static void convertOldDelay (juce::ValueTree& p)
    {
        // Tracktion's delay: a length in ms, feedback in dB (-30 = none) and a wet / dry mix; no filtering.
        const float feedbackDb = p.getProperty ("feedback", -6.0f);
        const float oldMix = p.getProperty ("mix", 0.3f);
        const int lengthMs = p.getProperty ("length", 150);

        p.removeProperty ("length", nullptr);
        using namespace DelayParameters;
        setAll (p, { { get (time).id, (float) juce::jlimit (1, 2000, lengthMs) }, { get (sync).id, 0.0f },
                     { get (feedback).id, feedbackDb > -30.0f ? juce::Decibels::decibelsToGain (juce::jmin (0.0f, feedbackDb)) : 0.0f },
                     { get (mix).id, convertMix (oldMix) },
                     { get (lowCut).id, DelayEngine::lowCutOff }, { get (highCut).id, DelayEngine::highCutOff } });

        dropAutomation (p);     // feedback was in dB, the mix on another curve
        p.setProperty (te::IDs::type, DelayPlugin::xmlTypeName, nullptr);
    }

    static void convertOldChorus (juce::ValueTree& p)
    {
        // Tracktion's chorus: one voice swept from 20 ms by up to depthMs, the right side up to half a cycle later.
        const float depthMs = p.getProperty ("depthMs", 3.0f);
        const float speedHz = p.getProperty ("speedHz", 1.0f);
        const float oldWidth = p.getProperty ("width", 0.5f);
        const float oldMix = p.getProperty ("mixProportion", 0.5f);

        for (auto* id : { "depthMs", "speedHz", "mixProportion" })
            p.removeProperty (id, nullptr);

        using namespace ChorusParameters;
        setAll (p, { { get (voices).id, 1.0f }, { get (delay).id, 20.0f }, { get (rate).id, juce::jlimit (0.03f, 10.0f, speedHz) },
                     { get (depth).id, juce::jlimit (0.0f, 1.0f, depthMs / ChorusEngine::maxModulationMs) },
                     { get (spread).id, juce::jlimit (0.0f, 1.0f, oldWidth * 2.0f) }, { get (feedback).id, 0.0f },
                     { get (tone).id, ChorusEngine::toneOff }, { get (width).id, 1.0f }, { get (mix).id, convertMix (oldMix) } });

        dropAutomation (p);
        p.setProperty (te::IDs::type, ChorusPlugin::xmlTypeName, nullptr);
    }

    static void convertOldPhaser (juce::ValueTree& p)
    {
        // Tracktion's phaser: four stages swept from 100 Hz up `depth` octaves and back, `rate` cycles a
        // second, `feedback` from the last stage; the dry signal added at full level.
        const float depthOctaves = p.getProperty ("depth", 5.0f);
        const float rateHz = p.getProperty ("rate", 0.4f);
        const float feedbackGain = p.getProperty ("feedback", 0.7f);

        using namespace PhaserParameters;
        setAll (p, { { get (stages).id, 4.0f }, { get (sync).id, 0.0f }, { get (rate).id, juce::jlimit (0.02f, 10.0f, rateHz) },
                     { get (centre).id, juce::jlimit (100.0f, 6000.0f, 100.0f * std::pow (2.0f, depthOctaves * 0.5f)) },
                     { get (depth).id, juce::jlimit (0.0f, 1.0f, depthOctaves * 0.5f / PhaserEngine::maxOctaves) },
                     { get (feedback).id, juce::jlimit (-0.9f, 0.9f, feedbackGain) }, { get (spread).id, 0.0f }, { get (mix).id, 0.5f } });

        dropAutomation (p);
        p.setProperty (te::IDs::type, PhaserPlugin::xmlTypeName, nullptr);
    }

    static void convertOldLowPass (juce::ValueTree& p)
    {
        // Tracktion's low / high-pass: 12 dB/oct Butterworth at "frequency" (the same ID as C-Filter's cutoff).
        const float hz = p.getProperty ("frequency", 4000.0f);
        const bool highPass = p.getProperty ("mode", "lowpass").toString() == "highpass";

        p.removeProperty ("mode", nullptr);
        using namespace FilterParameters;
        setAll (p, { { get (type).id, highPass ? 1.0f : 0.0f }, { get (slope).id, 0.0f },
                     { get (cutoff).id, juce::jlimit (FilterEngine::minHz, FilterEngine::maxHz, hz) },
                     { get (resonance).id, 0.0f }, { get (mix).id, 1.0f } });

        dropAutomation (p, { get (cutoff).id });
        p.setProperty (te::IDs::type, FilterPlugin::xmlTypeName, nullptr);
    }

    int migrateEditState (juce::ValueTree& editState)
    {
        int converted = 0;

        std::function<void (juce::ValueTree)> visit = [&] (juce::ValueTree v)
        {
            if (v.hasType (te::IDs::PLUGIN))
            {
                auto type = v[te::IDs::type].toString();

                if (type == te::CompressorPlugin::xmlTypeName)
                {
                    v.setProperty (te::IDs::type, CompressorPlugin::xmlTypeName, nullptr);
                    ++converted;
                }
                else if (type == te::EqualiserPlugin::xmlTypeName)
                {
                    convertOldEq (v);
                    ++converted;
                }
                else if (type == te::ReverbPlugin::xmlTypeName)
                {
                    convertOldReverb (v);
                    ++converted;
                }
                else if (type == te::DelayPlugin::xmlTypeName)
                {
                    convertOldDelay (v);
                    ++converted;
                }
                else if (type == te::ChorusPlugin::xmlTypeName)
                {
                    convertOldChorus (v);
                    ++converted;
                }
                else if (type == te::PhaserPlugin::xmlTypeName)
                {
                    convertOldPhaser (v);
                    ++converted;
                }
                else if (type == te::LowPassPlugin::xmlTypeName)
                {
                    convertOldLowPass (v);
                    ++converted;
                }
            }

            for (auto child : v)
                visit (child);
        };

        visit (editState);
        return converted;
    }
}
