#include "BuiltInPlugins.h"
#include "CompressorPlugin.h"
#include "EqPlugin.h"
#include "MultibandPlugin.h"
#include "ReverbPlugin.h"
#include "SimpleReverbPlugin.h"

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
            }

            for (auto child : v)
                visit (child);
        };

        visit (editState);
        return converted;
    }
}
