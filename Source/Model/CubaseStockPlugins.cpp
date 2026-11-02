#include "CubaseStockPlugins.h"
#include "../Plugins/CompressorPlugin.h"
#include "../Plugins/EsserPlugin.h"

namespace cdaw::CubaseStockPlugins
{
    namespace
    {
        juce::String num (double v)
        {
            auto s = juce::String (v, 9);
            if (s.containsChar ('.'))
                s = s.trimCharactersAtEnd ("0").trimCharactersAtEnd (".");
            return s.isEmpty() || s == "-0" ? juce::String ("0") : s;
        }

        double get (const Parameters& p, const char* key, double fallback)
        {
            auto it = p.find (key);
            return it != p.end() ? it->second : fallback;
        }

        bool isOn (const Parameters& p, const juce::String& key, bool fallback)
        {
            auto it = p.find (key);
            return it != p.end() ? it->second >= 0.5 : fallback;
        }

        void setEnabled (juce::XmlElement& device, bool enabled)
        {
            device.createNewChildElement ("Enabled")->setAttribute ("value", enabled ? "true" : "false");
        }

        /** CDAW's own device: a built-in plugin by type, parameters by ID. */
        struct BuiltIn
        {
            BuiltIn (const char* type, const char* name) : xml (std::make_unique<juce::XmlElement> ("BuiltInPlugin"))
            {
                xml->setAttribute ("type", type);
                xml->setAttribute ("deviceName", name);
                xml->setAttribute ("deviceRole", "audioFX");
                xml->setAttribute ("name", name);
            }

            BuiltIn& set (const char* id, double value)
            {
                auto* p = xml->createNewChildElement ("Param");
                p->setAttribute ("id", id);
                p->setAttribute ("value", num (value));
                return *this;
            }

            std::unique_ptr<juce::XmlElement> xml;
        };

        //==============================================================================
        // Compressors and limiters: C-Comp

        struct CompSettings
        {
            double thresholdDb = -20.0, ratio = 2.0, attackMs = 10.0, releaseMs = 100.0, outputDb = 0.0, kneeDb = 6.0;
        };

        constexpr double limiterRatio = 1000.0;     // C-Comp's ratio 0 (stored as 1 / ratio)

        Device writeComp (const CompSettings& s, bool enabled, juce::StringArray notes)
        {
            if (s.releaseMs > 300.0)
                notes.add ("release times above 300 ms became 300 ms");
            if (s.thresholdDb < -40.0)
                notes.add ("thresholds below -40 dB became -40 dB");

            BuiltIn d (CompressorPlugin::xmlTypeName, "C-Comp");
            d.set ("threshold", juce::Decibels::decibelsToGain (juce::jlimit (-40.0, 0.0, s.thresholdDb)))
             .set ("ratio", s.ratio >= limiterRatio ? 0.0 : juce::jlimit (0.0, 0.95, 1.0 / juce::jmax (1.0, s.ratio)))
             .set ("attack", juce::jlimit (0.3, 200.0, s.attackMs))
             .set ("release", juce::jlimit (10.0, 300.0, s.releaseMs))
             .set ("output gain", juce::jlimit (-10.0, 24.0, s.outputDb))
             .set ("knee", juce::jlimit (0.0, 24.0, s.kneeDb));
            setEnabled (*d.xml, enabled);
            return { std::move (d.xml), notes };
        }

        /** A dry / compressed mix as one ratio: above the threshold the blend rises by roughly
            mix * (1 - 1 / ratio) dB less per dB. */
        double blendedRatio (double ratio, double mix)
        {
            return 1.0 / (1.0 - juce::jlimit (0.0, 1.0, mix) * (1.0 - 1.0 / juce::jmax (1.0, ratio)));
        }

        Device compressor (const Parameters& p, bool enabled)
        {
            if (p.count ("threshold") == 0 || p.count ("ratio") == 0)
                return {};

            juce::StringArray notes;
            if (isOn (p, "automakeup", false))
                notes.add ("auto make-up gain: set C-Comp's output by ear");

            CompSettings s;
            s.thresholdDb = get (p, "threshold", -20.0);
            s.ratio = get (p, "ratio", 2.0);
            s.attackMs = get (p, "attack", 10.0);
            s.releaseMs = get (p, "release", 100.0);
            s.outputDb = get (p, "makeUp", 0.0);
            s.kneeDb = isOn (p, "softknee", true) ? 6.0 : 0.0;
            return writeComp (s, enabled, notes);
        }

        /** Tube and Vintage Compressor have no threshold: the input gain drives a fixed one. The
            same as a threshold lower by the input gain, with the input gain added after. */
        constexpr double inputDrivenThresholdDb = -12.0;

        Device tubeCompressor (const Parameters& p, bool enabled)
        {
            if (p.count ("inputgain") == 0)
                return {};

            const double input = get (p, "inputgain", 0.0);
            CompSettings s;
            s.thresholdDb = inputDrivenThresholdDb - input;
            s.ratio = blendedRatio (isOn (p, "highratio", false) ? 8.0 : 2.5, get (p, "mix", 100.0) / 100.0);
            s.attackMs = get (p, "attack", 10.0);
            s.releaseMs = get (p, "release", 150.0);
            s.outputDb = input + get (p, "outputgain", 0.0);
            return writeComp (s, enabled, { "the tube drive and character aren't modelled" });
        }

        Device vintageCompressor (const Parameters& p, bool enabled)
        {
            if (p.count ("inputgain") == 0)
                return {};

            const double input = get (p, "inputgain", 0.0);
            CompSettings s;
            s.thresholdDb = inputDrivenThresholdDb - input;
            s.ratio = blendedRatio (get (p, "ratio", 2.0), get (p, "mix", 100.0) / 100.0);
            s.attackMs = get (p, "attack", 1.0);
            s.releaseMs = get (p, "release", 150.0);
            s.outputDb = input + get (p, "outputgain", 0.0);
            s.kneeDb = 3.0;
            return writeComp (s, enabled, { "the punch attack mode isn't modelled" });
        }

        /** Limiter: input gain, then a ceiling at the output level. C-Comp has no lookahead, so
            the fastest peaks get through. */
        Device limiter (const Parameters& p, bool enabled)
        {
            if (p.count ("input") == 0 && p.count ("output") == 0)
                return {};

            const double input = get (p, "input", 0.0);
            CompSettings s;
            s.thresholdDb = juce::jmin (0.0, get (p, "output", 0.0) - input);
            s.ratio = limiterRatio;
            s.attackMs = 0.3;
            s.releaseMs = juce::jmax (10.0, get (p, "release", 100.0));
            s.outputDb = input;
            s.kneeDb = 0.0;
            return writeComp (s, enabled, { "no lookahead: the fastest peaks get through" });
        }

        Device brickwallLimiter (const Parameters& p, bool enabled)
        {
            if (p.count ("threshold") == 0)
                return {};

            CompSettings s;
            s.thresholdDb = get (p, "threshold", 0.0);
            s.ratio = limiterRatio;
            s.attackMs = 0.3;
            s.releaseMs = juce::jmax (10.0, get (p, "release", 10.0));
            s.outputDb = 0.0;
            s.kneeDb = 0.0;
            return writeComp (s, enabled, { "no lookahead: the fastest peaks get through" });
        }

        /** Maximizer: louder by its optimise amount, peaks held at the output level. */
        Device maximizer (const Parameters& p, bool enabled)
        {
            if (p.count ("optimise") == 0)
                return {};

            const double boostDb = 0.3 * juce::jlimit (0.0, 100.0, get (p, "optimise", 0.0));
            const double ceilingDb = juce::jmin (0.0, get (p, "output", 0.0));
            CompSettings s;
            s.thresholdDb = ceilingDb - boostDb;
            s.ratio = blendedRatio (limiterRatio, get (p, "mix", 100.0) / 100.0);
            s.attackMs = 0.3;
            s.releaseMs = get (p, "release", 100.0);
            s.outputDb = boostDb;
            s.kneeDb = 3.0;
            return writeComp (s, enabled, { "an approximation of the loudness it adds; no lookahead" });
        }

        //==============================================================================
        // DeEsser: C-Esser

        Device deEsser (const Parameters& p, bool enabled)
        {
            if (p.count ("threshold") == 0 || p.count ("lowfreq") == 0)
                return {};

            using P = EsserParameters::ID;
            auto spec = [] (P id) { return EsserParameters::get (id); };
            auto clamp = [&] (P id, double v) { return juce::jlimit ((double) spec (id).minimum, (double) spec (id).maximum, v); };

            // Cubase's reduction is an intensity, 0..10: up to 24 dB.
            BuiltIn d (EsserPlugin::xmlTypeName, "C-Esser");
            d.set (spec (P::threshold).id, clamp (P::threshold, get (p, "threshold", -24.0)))
             .set (spec (P::autoThreshold).id, isOn (p, "autothreshold", false) ? 1.0 : 0.0)
             .set (spec (P::range).id, clamp (P::range, 2.4 * get (p, "reduction", 5.0)))
             .set (spec (P::release).id, clamp (P::release, get (p, "release", 100.0)))
             .set (spec (P::lowfreq).id, clamp (P::lowfreq, get (p, "lowfreq", 4000.0)))
             .set (spec (P::highfreq).id, clamp (P::highfreq, get (p, "highfreq", 10000.0)))
             .set (spec (P::listen).id, isOn (p, "listen", false) ? 1.0 : isOn (p, "diff", false) ? 2.0 : 0.0);
            setEnabled (*d.xml, enabled);
            return { std::move (d.xml), {} };
        }

        //==============================================================================
        // Frequency: CDAW's EQ

        struct BandType { const char* type; int order; bool cut; };

        /** Frequency's band types. Bands 1 and 8 also cut (low cut on 1, high cut on 8):
            0..4 cut 6 / 12 / 24 / 48 / 96 dB/oct, then 5 low shelf, 6 peak, 7 high shelf, 8 notch.
            Bands 2 to 7: 0 low shelf, 1 peak, 2 high shelf, 3 notch. (Checked in Cubase 10.5.) */
        std::optional<BandType> frequencyBandType (int band, int code)
        {
            const bool outer = band == 1 || band == 8;

            if (outer && code >= 0 && code <= 4)
                return BandType { band == 1 ? "highPass" : "lowPass", 1 << juce::jmin (code, 3), true };

            switch (outer ? code - 5 : code)
            {
                case 0:  return BandType { "lowShelf", 2, false };
                case 1:  return BandType { "bell", 2, false };
                case 2:  return BandType { "highShelf", 2, false };
                case 3:  return BandType { "notch", 2, false };
                default: return {};
            }
        }

        void addReal (juce::XmlElement& parent, const char* tag, const char* unit, double value)
        {
            auto* x = parent.createNewChildElement (tag);
            x->setAttribute ("unit", unit);
            x->setAttribute ("value", num (value));
        }

        Device frequency (const Parameters& p, bool enabled)
        {
            if (p.count ("equalizerAfreq1") == 0)
                return {};

            juce::StringArray notes;
            auto d = std::make_unique<juce::XmlElement> ("Equalizer");
            d->setAttribute ("deviceName", "EQ");
            d->setAttribute ("deviceRole", "audioFX");
            d->setAttribute ("name", "EQ");

            for (int n = 1; n <= 8; ++n)
            {
                auto key = [n] (const char* name) { return juce::String (name) + juce::String (n); };
                auto value = [&] (const char* name, double fallback) { return get (p, key (name).toRawUTF8(), fallback); };

                const int code = juce::roundToInt (value ("equalizerAtype", 1.0));
                auto type = frequencyBandType (n, code);
                if (! type)
                {
                    notes.add ("an unknown band type was imported as a peak");
                    type = BandType { "bell", 2, false };
                }

                if ((n == 1 || n == 8) && code == 4)
                    notes.add ("96 dB/oct cuts became 48 dB/oct");
                if (isOn (p, key ("dynamic"), false))
                    notes.add ("dynamic bands play as static EQ");
                if (isOn (p, key ("linearphase"), false))
                    notes.add ("linear phase bands play with minimum phase");

                auto* b = d->createNewChildElement ("Band");
                b->setAttribute ("type", type->type);
                if (type->cut)
                    b->setAttribute ("order", type->order);

                addReal (*b, "Freq", "hertz", value ("equalizerAfreq", 1000.0));
                addReal (*b, "Gain", "decibel", type->cut ? 0.0 : value ("equalizerAgain", 0.0));
                if (! type->cut)
                    addReal (*b, "Q", "linear", value ("equalizerAq", 1.0));

                b->createNewChildElement ("Enabled")->setAttribute ("value", isOn (p, key ("equalizerAbandon"), true) ? "true" : "false");
            }

            addReal (*d, "OutputGain", "decibel", get (p, "equalizerAoutput", 0.0));
            setEnabled (*d, enabled && ! isOn (p, "equalizerAbypass", false));
            return { std::move (d), notes };
        }

        //==============================================================================
        struct Mapping
        {
            const char* cubaseName;
            Counterpart counterpart;
            Device (*translate) (const Parameters&, bool enabled);
        };

        const std::vector<Mapping>& mappings()
        {
            static const std::vector<Mapping> list {
                { "Frequency",          { "EQ", false },        frequency },
                { "Compressor",         { "C-Comp", false },    compressor },
                { "Tube Compressor",    { "C-Comp", true },     tubeCompressor },
                { "VintageCompressor",  { "C-Comp", true },     vintageCompressor },
                { "Limiter",            { "C-Comp", true },     limiter },
                { "Brickwall Limiter",  { "C-Comp", true },     brickwallLimiter },
                { "Maximizer",          { "C-Comp", true },     maximizer },
                { "DeEsser",            { "C-Esser", true },    deEsser },
            };
            return list;
        }

        const Mapping* find (const juce::String& name)
        {
            for (auto& m : mappings())
                if (name == m.cubaseName)
                    return &m;
            return nullptr;
        }
    }

    //==============================================================================
    Parameters readParameters (const juce::MemoryBlock& state)
    {
        Parameters params;
        auto* d = static_cast<const char*> (state.getData());
        constexpr size_t recordSize = 140;

        for (size_t p = 4; p + recordSize <= state.getSize(); p += recordSize)
        {
            size_t len = 0;
            while (len < 128 && d[p + len] != 0)
                ++len;

            auto name = juce::String (d + p, len);
            juce::uint64 bits = juce::ByteOrder::littleEndianInt64 (d + p + 132);
            double v;
            std::memcpy (&v, &bits, sizeof (v));

            if (name.isEmpty() || ! std::isfinite (v))
                break;
            params[name] = v;
        }

        return params;
    }

    std::optional<Counterpart> counterpartOf (const juce::String& cubaseName)
    {
        if (auto* m = find (cubaseName))
            return m->counterpart;
        return {};
    }

    Device translate (const juce::String& cubaseName, const Parameters& params, bool enabled)
    {
        if (auto* m = find (cubaseName))
            return m->translate (params, enabled && get (params, "bypass", 0.0) < 0.5);
        return {};
    }
}
