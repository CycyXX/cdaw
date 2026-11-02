#include "SynthPresets.h"

namespace cdaw::SynthPresets
{
    namespace
    {
        using namespace SynthParameters;
        using Src = SynthEngine::Source;
        using Dst = SynthEngine::Destination;
        using Shape = FilterEngine::Shape;
        using Bank = Wavetables::Bank;

        constexpr float off = 0.0f;
        float bank (Bank b)     { return (float) b; }
        float shape (Shape s)   { return (float) s; }

        /** A matrix slot (0-based) set to source -> destination by amount. */
        std::vector<std::pair<int, float>> route (int k, Src source, Dst destination, float amount)
        {
            return { { slot (k, 0), (float) source }, { slot (k, 1), (float) destination }, { slot (k, 2), amount } };
        }

        Preset make (const char* name, const char* category, std::vector<std::vector<std::pair<int, float>>> parts,
                     juce::StringArray macros = getDefaultMacroNames())
        {
            Preset p { name, category, {}, macros, {} };
            for (auto& part : parts)
                p.values.insert (p.values.end(), part.begin(), part.end());
            return p;
        }

        // Slots 1-3 and 8 are each preset's own movement; 4-7 stay on the macros (see the defaults).
        auto noMovement()
        {
            std::vector<std::pair<int, float>> v;
            for (int k : { 0, 1, 2, 7 })
            {
                auto r = route (k, Src::none, Dst::none, 0.0f);
                v.insert (v.end(), r.begin(), r.end());
            }
            return v;
        }

        std::vector<Preset> makeFactory()
        {
            std::vector<Preset> p;

            p.push_back (make ("Init", "Init", { noMovement(), {
                { aPosition, 0.66f }, { aUnison, 1.0f }, { bOn, off }, { noiseLevel, 0.0f }, { drift, 0.0f },
                { cutoff, 20000.0f }, { resonance, 0.0f }, { keyTrack, 0.0f }, { filterEnv, 0.0f },
                { ampAttack, 0.005f }, { ampDecay, 0.3f }, { ampSustain, 1.0f }, { ampRelease, 0.2f },
                { chorusOn, off }, { reverbOn, off } } }));

            // Pads ------------------------------------------------------------------------
            p.push_back (make ("Warm Horizon", "Pads", {}));

            p.push_back (make ("Choir of Glass", "Pads", { noMovement(), route (0, Src::lfo1, Dst::positionA, 0.35f), route (7, Src::random, Dst::pan, 0.3f), {
                { aBank, bank (Bank::vowel) }, { aPosition, 0.2f }, { aUnison, 5.0f }, { aDetune, 0.3f },
                { bBank, bank (Bank::glass) }, { bPosition, 0.4f }, { bLevel, 0.35f },
                { lfo1Rate, 0.12f }, { cutoff, 3500.0f }, { ampAttack, 1.2f }, { ampRelease, 3.5f },
                { chorusMix, 0.35f }, { reverbDecay, 6.0f }, { reverbMix, 0.45f } } }));

            p.push_back (make ("Velvet Strings", "Pads", { noMovement(), route (1, Src::lfo2, Dst::cutoff, 0.08f), route (2, Src::lfo1, Dst::pitch, 0.003f), {
                { aBank, bank (Bank::analog) }, { aPosition, 0.66f }, { aUnison, 7.0f }, { aDetune, 0.45f }, { aSpread, 1.0f },
                { bBank, bank (Bank::analog) }, { bPosition, 0.66f }, { bOctave, -1.0f }, { bLevel, 0.4f }, { bUnison, 3.0f },
                { lfo1Rate, 5.0f }, { lfo1Fade, 1.5f },
                { cutoff, 1800.0f }, { resonance, 0.1f }, { filterEnv, 0.3f },
                { ampAttack, 0.6f }, { ampDecay, 2.0f }, { ampSustain, 0.9f }, { ampRelease, 1.8f }, { modAttack, 1.5f },
                { chorusDepth, 0.5f }, { chorusMix, 0.4f }, { reverbMix, 0.3f } } }));

            p.push_back (make ("Analog Dream", "Pads", { noMovement(), route (0, Src::lfo1, Dst::positionA, 0.4f), route (7, Src::random, Dst::cutoff, 0.06f), {
                { aBank, bank (Bank::pulse) }, { aPosition, 0.3f }, { aUnison, 4.0f }, { aDetune, 0.25f },
                { bBank, bank (Bank::analog) }, { bPosition, 0.66f }, { bOctave, 0.0f }, { bFine, -9.0f }, { bUnison, 3.0f },
                { lfo1Rate, 0.3f }, { drift, 0.6f },
                { cutoff, 1200.0f }, { resonance, 0.35f }, { filterSlope, 1.0f }, { filterEnv, 0.45f },
                { modAttack, 3.0f }, { modDecay, 4.0f }, { modSustain, 0.3f }, { ampAttack, 0.5f }, { ampRelease, 2.0f } } }));

            p.push_back (make ("Slow Bloom", "Pads", { noMovement(), route (2, Src::modEnv, Dst::positionA, 0.6f), route (0, Src::lfo1, Dst::positionB, 0.2f), {
                { aBank, bank (Bank::air) }, { aPosition, 0.15f }, { aUnison, 5.0f },
                { bBank, bank (Bank::vowel) }, { bPosition, 0.6f }, { bLevel, 0.5f },
                { ampAttack, 4.0f }, { ampDecay, 4.0f }, { ampSustain, 0.8f }, { ampRelease, 5.0f },
                { modAttack, 6.0f }, { modSustain, 1.0f }, { filterEnv, 0.4f }, { cutoff, 900.0f },
                { reverbDecay, 8.0f }, { reverbMix, 0.5f } } }, { "Motion", "Brightness", "Breath", "Width" }));

            p.push_back (make ("Cathedral Organ", "Pads", { noMovement(), {
                { aBank, bank (Bank::organ) }, { aPosition, 0.6f }, { aUnison, 2.0f }, { aDetune, 0.1f },
                { bBank, bank (Bank::organ) }, { bPosition, 0.25f }, { bOctave, -1.0f }, { bLevel, 0.5f }, { bUnison, 1.0f },
                { cutoff, 8000.0f }, { keyTrack, 0.5f }, { filterEnv, 0.0f }, { drift, 0.15f },
                { ampAttack, 0.15f }, { ampDecay, 0.5f }, { ampSustain, 1.0f }, { ampRelease, 1.5f },
                { chorusDepth, 0.2f }, { chorusMix, 0.25f }, { reverbSize, 1.0f }, { reverbDecay, 8.0f }, { reverbMix, 0.45f } } },
                { "Motion", "Brightness", "Wind", "Width" }));

            p.push_back (make ("Sunrise Swell", "Pads", { noMovement(), route (2, Src::modEnv, Dst::positionB, 0.4f), route (0, Src::lfo1, Dst::positionA, 0.1f), {
                { aBank, bank (Bank::analog) }, { aPosition, 0.66f }, { aUnison, 7.0f }, { aDetune, 0.5f },
                { bBank, bank (Bank::air) }, { bPosition, 0.6f }, { bLevel, 0.4f },
                { modAttack, 8.0f }, { modDecay, 1.0f }, { modSustain, 1.0f }, { filterEnv, 0.8f }, { cutoff, 300.0f },
                { ampAttack, 2.0f }, { ampRelease, 4.0f }, { reverbMix, 0.45f } } }));

            p.push_back (make ("Hollow Voices", "Pads", { noMovement(), route (0, Src::lfo1, Dst::cutoff, 0.25f), route (1, Src::lfo2, Dst::positionA, 0.3f), {
                { aBank, bank (Bank::vowel) }, { aPosition, 0.45f }, { aUnison, 5.0f }, { aDetune, 0.35f },
                { bBank, bank (Bank::vowel) }, { bPosition, 0.85f }, { bOctave, 1.0f }, { bLevel, 0.4f },
                { filterType, 3.0f }, { cutoff, 1800.0f }, { resonance, 0.4f }, { lfo1Rate, 0.1f },
                { reverbDecay, 7.0f }, { reverbMix, 0.5f } } }));

            // Atmospheres -----------------------------------------------------------------
            p.push_back (make ("Frozen Lake", "Atmospheres", { noMovement(), route (1, Src::lfo2, Dst::positionA, 0.3f), {
                { aBank, bank (Bank::glass) }, { aPosition, 0.7f }, { aUnison, 3.0f }, { aOctave, 1.0f },
                { bBank, bank (Bank::air) }, { bPosition, 0.5f },
                { noiseLevel, 0.15f }, { noiseColour, 0.8f }, { cutoff, 6000.0f },
                { lfo2Shape, shape (Shape::random) }, { lfo2Rate, 0.5f },
                { ampAttack, 2.5f }, { ampRelease, 6.0f },
                { delayOn, 1.0f }, { delayDivision, 11.0f }, { delayFeedback, 0.5f }, { delayMix, 0.25f },
                { reverbSize, 0.9f }, { reverbDecay, 10.0f }, { reverbMix, 0.55f } } }, { "Motion", "Brightness", "Frost", "Width" }));

            p.push_back (make ("Distant Shores", "Atmospheres", { noMovement(), route (0, Src::lfo1, Dst::noise, 0.3f), route (1, Src::lfo2, Dst::cutoff, 0.25f), {
                { aBank, bank (Bank::analog) }, { aPosition, 0.0f }, { aUnison, 3.0f },
                { bBank, bank (Bank::organ) }, { bPosition, 0.4f }, { bOctave, 1.0f }, { bLevel, 0.3f },
                { noiseLevel, 0.25f }, { noiseColour, -0.4f }, { lfo1Rate, 0.08f }, { cutoff, 1500.0f },
                { ampAttack, 3.0f }, { ampRelease, 6.0f }, { reverbDecay, 9.0f }, { reverbMix, 0.55f } } },
                { "Motion", "Brightness", "Surf", "Width" }));

            p.push_back (make ("Nebula", "Atmospheres", { noMovement(), route (0, Src::lfo1, Dst::positionA, 0.5f), route (1, Src::lfo2, Dst::positionB, 0.5f),
                                                         route (7, Src::random, Dst::pitch, 0.004f), {
                { aBank, bank (Bank::vowel) }, { aPosition, 0.5f }, { aUnison, 7.0f }, { aDetune, 0.6f }, { aSpread, 1.0f },
                { bBank, bank (Bank::glass) }, { bPosition, 0.8f }, { bOctave, 2.0f }, { bLevel, 0.25f },
                { lfo1Rate, 0.05f }, { lfo2Rate, 0.11f }, { cutoff, 2600.0f }, { resonance, 0.3f },
                { chorusDepth, 0.6f }, { delayOn, 1.0f }, { delayDivision, 12.0f }, { delayFeedback, 0.45f }, { delayMix, 0.2f },
                { reverbDecay, 12.0f }, { reverbMix, 0.6f } } }));

            p.push_back (make ("Aurora", "Atmospheres", { noMovement(), route (0, Src::lfo1, Dst::positionA, 0.4f), route (1, Src::lfo2, Dst::positionB, -0.4f),
                                                         route (7, Src::random, Dst::pan, 0.5f), {
                { aBank, bank (Bank::air) }, { aPosition, 0.4f }, { aUnison, 5.0f },
                { bBank, bank (Bank::air) }, { bPosition, 0.8f }, { bOctave, 1.0f }, { bLevel, 0.4f },
                { lfo1Rate, 0.07f }, { lfo2Rate, 0.13f }, { cutoff, 12000.0f },
                { delayOn, 1.0f }, { delayDivision, 11.0f }, { delayFeedback, 0.4f }, { delayMix, 0.2f },
                { reverbDecay, 7.0f }, { reverbMix, 0.5f } } }));

            p.push_back (make ("Deep Ocean", "Atmospheres", { noMovement(), route (0, Src::lfo1, Dst::cutoff, 0.3f), {
                { aBank, bank (Bank::analog) }, { aPosition, 0.1f }, { aOctave, -1.0f }, { aUnison, 3.0f },
                { subLevel, 0.5f }, { bBank, bank (Bank::vowel) }, { bPosition, 0.9f }, { bLevel, 0.3f },
                { noiseLevel, 0.2f }, { noiseColour, -0.8f }, { cutoff, 600.0f }, { resonance, 0.2f }, { lfo1Rate, 0.06f },
                { ampAttack, 2.5f }, { ampRelease, 5.0f }, { reverbDecay, 10.0f }, { reverbMix, 0.45f } } },
                { "Motion", "Brightness", "Current", "Width" }));

            p.push_back (make ("Shimmer Veil", "Atmospheres", { noMovement(), route (0, Src::lfo1, Dst::positionA, 0.2f), {
                { aBank, bank (Bank::glass) }, { aPosition, 0.9f }, { aOctave, 1.0f }, { aUnison, 5.0f }, { aDetune, 0.4f }, { aSpread, 1.0f },
                { bBank, bank (Bank::air) }, { bPosition, 0.7f }, { bOctave, 2.0f }, { bLevel, 0.3f },
                { cutoff, 9000.0f }, { ampAttack, 1.5f }, { ampRelease, 7.0f }, { chorusDepth, 0.5f },
                { delayOn, 1.0f }, { delayDivision, 9.0f }, { delayFeedback, 0.6f }, { delayMix, 0.3f },
                { reverbSize, 1.0f }, { reverbDecay, 14.0f }, { reverbMix, 0.6f } } }));

            // Textures --------------------------------------------------------------------
            p.push_back (make ("Breathing Room", "Textures", { noMovement(), route (0, Src::lfo1, Dst::cutoff, 0.35f), route (1, Src::lfo2, Dst::positionA, 0.4f), {
                { aBank, bank (Bank::vowel) }, { aPosition, 0.8f }, { aUnison, 3.0f }, { aLevel, 0.9f }, { bOn, off },
                { noiseLevel, 0.8f }, { noiseColour, 0.6f }, { filterType, 2.0f }, { cutoff, 1100.0f }, { resonance, 0.55f },
                { lfo1Rate, 0.18f }, { lfo2Rate, 0.06f }, { ampAttack, 1.5f }, { ampRelease, 3.0f }, { reverbMix, 0.5f } } },
                { "Motion", "Brightness", "Breath", "Width" }));

            p.push_back (make ("Tape Memory", "Textures", { noMovement(), {
                { aBank, bank (Bank::analog) }, { aPosition, 0.5f }, { aUnison, 3.0f },
                { bBank, bank (Bank::pulse) }, { bPosition, 0.2f }, { bOctave, 0.0f }, { bLevel, 0.4f },
                { drift, 1.0f }, { cutoff, 1600.0f }, { resonance, 0.2f }, { filterDrive, 0.3f },
                { chorusDepth, 0.7f }, { chorusMix, 0.45f }, { delayOn, 1.0f }, { delayDivision, 8.0f }, { delayFeedback, 0.55f }, { delayMix, 0.3f },
                { ampAttack, 0.3f }, { ampRelease, 2.0f } } }, { "Motion", "Brightness", "Hiss", "Width" }));

            p.push_back (make ("Dark Matter", "Textures", { noMovement(), route (0, Src::lfo1, Dst::cutoff, 0.2f), {
                { aBank, bank (Bank::pulse) }, { aPosition, 0.9f }, { aOctave, -1.0f }, { aUnison, 4.0f },
                { bBank, bank (Bank::glass) }, { bPosition, 0.2f }, { bOctave, 0.0f }, { bSemitones, 7.0f }, { bLevel, 0.3f },
                { cutoff, 700.0f }, { resonance, 0.45f }, { filterSlope, 1.0f }, { filterDrive, 0.4f },
                { lfo1Shape, shape (Shape::random) }, { lfo1Rate, 2.0f }, { noiseLevel, 0.15f }, { noiseColour, -0.6f },
                { reverbDecay, 9.0f }, { reverbMix, 0.4f } } }));

            // Keys & plucks ---------------------------------------------------------------
            p.push_back (make ("Glass Rain", "Keys & Plucks", { noMovement(), {
                { aBank, bank (Bank::glass) }, { aPosition, 0.6f }, { aUnison, 2.0f },
                { bBank, bank (Bank::analog) }, { bPosition, 0.0f }, { bOctave, 1.0f }, { bLevel, 0.3f }, { bUnison, 1.0f },
                { ampAttack, 0.002f }, { ampDecay, 1.8f }, { ampSustain, 0.0f }, { ampRelease, 1.5f }, { ampVelocity, 0.7f },
                { modAttack, 0.001f }, { modDecay, 0.8f }, { modSustain, 0.0f }, { filterEnv, 0.5f }, { cutoff, 1200.0f },
                { delayOn, 1.0f }, { delayDivision, 8.0f }, { delayFeedback, 0.5f }, { delayMix, 0.3f },
                { reverbDecay, 5.0f }, { reverbMix, 0.45f } } }));

            p.push_back (make ("Soft Keys", "Keys & Plucks", { noMovement(), {
                { aBank, bank (Bank::analog) }, { aPosition, 0.33f }, { aUnison, 2.0f }, { aDetune, 0.1f },
                { bBank, bank (Bank::organ) }, { bPosition, 0.1f }, { bOctave, 0.0f }, { bLevel, 0.3f }, { bUnison, 1.0f },
                { ampAttack, 0.004f }, { ampDecay, 2.5f }, { ampSustain, 0.25f }, { ampRelease, 0.8f }, { ampVelocity, 0.6f },
                { modAttack, 0.001f }, { modDecay, 1.2f }, { modSustain, 0.2f }, { filterEnv, 0.35f }, { cutoff, 1500.0f },
                { chorusMix, 0.3f }, { reverbMix, 0.3f } } }));

            p.push_back (make ("Gliding Lead", "Keys & Plucks", { noMovement(), route (0, Src::lfo1, Dst::pitch, 0.004f), {
                { voiceMode, 2.0f }, { glide, 0.15f },
                { aBank, bank (Bank::analog) }, { aPosition, 0.66f }, { aUnison, 3.0f }, { bOn, off },
                { lfo1Rate, 5.5f }, { lfo1Fade, 0.6f }, { lfo1Retrigger, 1.0f },
                { ampAttack, 0.01f }, { ampSustain, 0.9f }, { ampRelease, 0.4f }, { cutoff, 2500.0f }, { filterEnv, 0.3f },
                { delayOn, 1.0f }, { reverbMix, 0.25f } } }));

            // Each preset's volume, so a five-note chord peaks around -6 dBFS whatever the layers and the space.
            static const std::map<juce::String, float> volumes {
                { "Init", -1.0f }, { "Choir of Glass", -9.3f }, { "Velvet Strings", -6.1f }, { "Analog Dream", -3.7f },
                { "Slow Bloom", -0.9f }, { "Cathedral Organ", -11.5f }, { "Sunrise Swell", -4.9f }, { "Hollow Voices", -10.0f },
                { "Frozen Lake", -8.3f }, { "Distant Shores", -6.8f }, { "Nebula", -13.4f }, { "Aurora", -3.7f },
                { "Deep Ocean", -5.5f }, { "Shimmer Veil", -11.7f }, { "Breathing Room", 6.0f }, { "Tape Memory", -7.3f },
                { "Dark Matter", -1.9f }, { "Glass Rain", -3.2f }, { "Soft Keys", -5.3f }, { "Gliding Lead", 1.3f } };

            for (auto& preset : p)
                if (auto found = volumes.find (preset.name); found != volumes.end())
                    preset.values.push_back ({ volume, found->second });

            return p;
        }

        juce::String userExtension()     { return ".csynth"; }
    }

    const std::vector<Preset>& factory()
    {
        static const auto presets = makeFactory();
        return presets;
    }

    std::vector<float> valuesOf (const Preset& preset)
    {
        std::vector<float> values;
        for (auto& spec : SynthParameters::all())
            values.push_back (spec.defaultValue);

        for (auto& [id, value] : preset.values)
            if (id >= 0 && id < (int) values.size())
                values[(size_t) id] = value;

        return values;
    }

    static juce::File& userFolderOverride()
    {
        static juce::File folder;
        return folder;
    }

    void setUserFolder (const juce::File& folder)
    {
        userFolderOverride() = folder;
    }

    juce::File getUserFolder()
    {
        if (userFolderOverride() != juce::File())
            return userFolderOverride();

        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("CDAW").getChildFile ("Presets").getChildFile ("C-Synth");
    }

    std::vector<Preset> loadUser()
    {
        std::vector<Preset> presets;
        const auto& specs = SynthParameters::all();

        for (auto& file : getUserFolder().findChildFiles (juce::File::findFiles, false, "*" + userExtension()))
        {
            auto xml = juce::XmlDocument::parse (file);
            if (xml == nullptr || ! xml->hasTagName ("CSynthPreset"))
                continue;

            Preset p { xml->getStringAttribute ("name", file.getFileNameWithoutExtension()), "User", {}, getDefaultMacroNames(), file };

            for (int m = 0; m < SynthEngine::numMacros; ++m)
                p.macroNames.set (m, xml->getStringAttribute ("macro" + juce::String (m + 1), p.macroNames[m]));

            for (auto* param : xml->getChildWithTagNameIterator ("Param"))
            {
                const auto id = param->getStringAttribute ("id");
                for (int i = 0; i < (int) specs.size(); ++i)
                    if (id == specs[(size_t) i].id)
                        p.values.push_back ({ i, juce::jlimit (specs[(size_t) i].minimum, specs[(size_t) i].maximum, (float) param->getDoubleAttribute ("value")) });
            }

            presets.push_back (std::move (p));
        }

        std::sort (presets.begin(), presets.end(), [] (const Preset& a, const Preset& b) { return a.name.compareNatural (b.name) < 0; });
        return presets;
    }

    juce::Result saveUser (const juce::String& name, const std::function<float (int)>& valueOf, const juce::StringArray& macroNames)
    {
        const auto clean = juce::File::createLegalFileName (name.trim());
        if (clean.isEmpty())
            return juce::Result::fail ("The preset needs a name");

        auto folder = getUserFolder();
        if (auto made = folder.createDirectory(); made.failed())
            return made;

        juce::XmlElement xml ("CSynthPreset");
        xml.setAttribute ("name", name.trim());
        xml.setAttribute ("version", 1);
        for (int m = 0; m < SynthEngine::numMacros; ++m)
            xml.setAttribute ("macro" + juce::String (m + 1), macroNames[m]);

        const auto& specs = SynthParameters::all();
        for (int i = 0; i < (int) specs.size(); ++i)
        {
            auto* param = xml.createNewChildElement ("Param");
            param->setAttribute ("id", specs[(size_t) i].id);
            param->setAttribute ("value", valueOf (i));
        }

        auto file = folder.getChildFile (clean + userExtension());
        return xml.writeTo (file) ? juce::Result::ok() : juce::Result::fail ("Could not write " + file.getFullPathName());
    }

    std::vector<Preset> everything()
    {
        auto all = factory();
        auto user = loadUser();
        all.insert (all.end(), user.begin(), user.end());
        return all;
    }
}
