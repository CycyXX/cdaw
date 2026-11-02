#include "LeadPresets.h"

namespace cdaw::LeadPresets
{
    namespace
    {
        using namespace LeadParameters;
        using Src = LeadEngine::Source;
        using Dst = LeadEngine::Destination;
        using Shape = FilterEngine::Shape;
        using Wave = LeadEngine::Wave;

        constexpr float off = 0.0f, on = 1.0f;
        float wave (Wave w)                     { return (float) w; }
        float shape (Shape s)                   { return (float) s; }
        float filter (LeadEngine::FilterType t) { return (float) t; }
        float driveOf (LeadEngine::DriveType t) { return (float) t; }
        float mode (LeadEngine::VoiceMode m)    { return (float) m; }

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

        // Slots 1-3 and 8 are each preset's own movement; 4-7 stay on the macros.
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
            using FT = LeadEngine::FilterType;
            using DT = LeadEngine::DriveType;
            using VM = LeadEngine::VoiceMode;
            std::vector<Preset> p;

            p.push_back (make ("Init", "Init", { noMovement(), {
                { aUnison, 1.0f }, { bOn, off }, { drive, 0.0f }, { cutoff, 20000.0f }, { resonance, 0.0f }, { keyTrack, 0.0f },
                { filterEnv, 0.0f }, { filterVelocity, 0.0f }, { vibratoDepth, 0.0f }, { glide, 0.0f },
                { ampAttack, 0.002f }, { ampDecay, 0.3f }, { ampSustain, 1.0f }, { ampRelease, 0.1f },
                { chorusOn, off }, { delayOn, off }, { reverbOn, off } } }));

            // Classic ------------------------------------------------------------------------
            p.push_back (make ("Classic Saw Lead", "Classic", {}));

            p.push_back (make ("Square Hero", "Classic", { noMovement(), route (0, Src::lfo1, Dst::pulseWidth, 0.25f), {
                { aWave, wave (Wave::pulse) }, { aUnison, 1.0f }, { bWave, wave (Wave::pulse) }, { bOctave, -1.0f }, { bFine, 0.0f },
                { bLevel, 0.4f }, { bPulseWidth, 0.35f }, { cutoff, 1800.0f }, { resonance, 0.35f }, { filterEnv, 0.45f },
                { vibratoDepth, 20.0f } } }));

            p.push_back (make ("Smooth Sine Lead", "Classic", { noMovement(), {
                { aWave, wave (Wave::sine) }, { aUnison, 1.0f }, { bWave, wave (Wave::triangle) }, { bOctave, 1.0f }, { bFine, 0.0f },
                { bLevel, 0.25f }, { drive, 0.25f }, { filterType, filter (FT::lowPass) }, { cutoff, 6000.0f }, { resonance, 0.0f },
                { vibratoDepth, 18.0f }, { vibratoDelay, 0.25f }, { glide, 0.1f }, { glideMode, 0.0f }, { reverbMix, 0.35f } } }));

            p.push_back (make ("Supersaw Anthem", "Classic", { noMovement(), {
                { voiceMode, mode (VM::poly) }, { aUnison, 7.0f }, { aDetune, 0.5f }, { aSpread, 0.9f },
                { bUnison, 5.0f }, { bOctave, 1.0f }, { bFine, 0.0f }, { bLevel, 0.35f }, { bDetune, 0.4f }, { bSpread, 0.7f },
                { cutoff, 6000.0f }, { resonance, 0.1f }, { filterEnv, 0.2f }, { vibratoDepth, 8.0f },
                { chorusMix, 0.25f }, { delayMix, 0.22f }, { reverbMix, 0.35f }, { reverbDecay, 3.0f } } }));

            p.push_back (make ("Portamento Whistle", "Classic", { noMovement(), {
                { aWave, wave (Wave::sine) }, { aUnison, 1.0f }, { bWave, wave (Wave::triangle) }, { bOctave, 1.0f }, { bFine, 0.0f },
                { bLevel, 0.2f }, { noiseLevel, 0.05f }, { glide, 0.25f }, { glideMode, 0.0f }, { vibratoDepth, 25.0f },
                { vibratoDelay, 0.15f }, { vibratoRate, 6.0f }, { filterType, filter (FT::lowPass) }, { cutoff, 5000.0f },
                { resonance, 0.0f }, { drive, 0.0f }, { reverbMix, 0.35f } } }));

            // Sync & FM -----------------------------------------------------------------------
            p.push_back (make ("Sync Screamer", "Sync & FM", { noMovement(), route (1, Src::modEnv, Dst::syncPitch, 0.6f), {
                { sync, on }, { aUnison, 1.0f }, { aLevel, 0.0f }, { bSemitones, 7.0f }, { bFine, 0.0f }, { bLevel, 0.8f },
                { driveType, driveOf (DT::tube) }, { drive, 0.35f }, { cutoff, 5000.0f }, { resonance, 0.15f },
                { modDecay, 0.8f }, { modSustain, 0.15f }, { glide, 0.04f } } }, { "Brightness", "Grit", "Sync", "Bite" }));

            p.push_back (make ("Sync Wobble", "Sync & FM", { noMovement(), route (0, Src::lfo1, Dst::syncPitch, 0.4f), {
                { sync, on }, { aUnison, 1.0f }, { aLevel, 0.2f }, { bSemitones, 12.0f }, { bFine, 0.0f }, { bLevel, 0.7f },
                { lfo1Shape, shape (Shape::triangle) }, { lfo1Sync, on }, { lfo1Division, 6.0f }, { cutoff, 4000.0f },
                { drive, 0.25f }, { vibratoDepth, 0.0f } } }));

            p.push_back (make ("FM Bell Lead", "Sync & FM", { noMovement(), route (2, Src::modEnv, Dst::fm, 0.4f), {
                { aWave, wave (Wave::sine) }, { aUnison, 1.0f }, { aLevel, 0.3f }, { bWave, wave (Wave::sine) }, { bOctave, 1.0f },
                { bFine, 0.0f }, { bLevel, 0.8f }, { fm, 0.35f }, { ampDecay, 1.2f }, { ampSustain, 0.4f }, { modDecay, 0.6f },
                { modSustain, 0.0f }, { filterType, filter (FT::lowPass) }, { cutoff, 9000.0f }, { resonance, 0.0f }, { filterEnv, 0.0f },
                { drive, 0.0f }, { delayMix, 0.25f } } }));

            p.push_back (make ("Ring Mod Lead", "Sync & FM", { noMovement(), {
                { aUnison, 1.0f }, { bWave, wave (Wave::sine) }, { bSemitones, 5.0f }, { bFine, 30.0f }, { bLevel, 0.2f },
                { ring, 0.7f }, { cutoff, 6000.0f }, { vibratoDepth, 20.0f } } }));

            // Character -----------------------------------------------------------------------
            p.push_back (make ("Talking Lead", "Character", { noMovement(), route (1, Src::lfo2, Dst::cutoff, 0.35f), {
                { aUnison, 2.0f }, { filterType, filter (FT::bandPass) }, { cutoff, 900.0f }, { resonance, 0.65f },
                { lfo2Shape, shape (Shape::triangle) }, { lfo2Rate, 3.0f }, { drive, 0.2f } } }, { "Brightness", "Grit", "Sync", "Mouth" }));

            p.push_back (make ("Acid Line", "Character", { noMovement(), {
                { voiceMode, mode (VM::legato) }, { glide, 0.08f }, { aUnison, 1.0f }, { bOn, off },
                { cutoff, 400.0f }, { resonance, 0.8f }, { filterEnv, 0.7f }, { modDecay, 0.25f }, { modSustain, 0.0f },
                { driveType, driveOf (DT::tube) }, { drive, 0.4f }, { ampDecay, 0.3f }, { ampSustain, 0.6f }, { vibratoDepth, 0.0f },
                { delayFeedback, 0.4f }, { delayMix, 0.2f }, { chorusOn, off }, { reverbMix, 0.15f } } },
                { "Brightness", "Grit", "Sync", "Squelch" }));

            p.push_back (make ("Dirty Fold", "Character", { noMovement(), route (0, Src::lfo1, Dst::drive, 0.25f), {
                { aWave, wave (Wave::triangle) }, { aUnison, 1.0f }, { bWave, wave (Wave::sine) }, { bOctave, 1.0f }, { bFine, 0.0f },
                { bLevel, 0.4f }, { driveType, driveOf (DT::fold) }, { drive, 0.6f }, { cutoff, 3500.0f }, { resonance, 0.3f } } }));

            // Through the built-in distortion (C-Dist's circuits).
            p.push_back (make ("Overdriven Lead", "Character", { noMovement(), {
                { aUnison, 2.0f }, { aDetune, 0.12f }, { cutoff, 4500.0f }, { resonance, 0.15f }, { drive, 0.0f },
                { distOn, on }, { distVoicing, 1.0f }, { distDrive, 0.6f }, { distTone, 0.45f },
                { vibratoDepth, 18.0f }, { delayMix, 0.2f }, { reverbMix, 0.25f } } }, { "Brightness", "Grit", "Sync", "Bite" }));

            p.push_back (make ("Fuzz Lead", "Character", { noMovement(), {
                { aWave, wave (Wave::pulse) }, { aUnison, 1.0f }, { bOn, off }, { cutoff, 3000.0f }, { drive, 0.0f },
                { distOn, on }, { distVoicing, 3.0f }, { distDrive, 0.8f }, { distTone, 0.35f }, { glide, 0.05f },
                { vibratoDepth, 12.0f }, { chorusOn, off } } }));

            p.push_back (make ("Screaming Ladder", "Character", { noMovement(), {
                { aUnison, 2.0f }, { cutoff, 1200.0f }, { resonance, 0.9f }, { drive, 0.5f }, { filterEnv, 0.4f },
                { vibratoDepth, 20.0f } } }));

            p.push_back (make ("Pad-Lead Hybrid", "Character", { noMovement(), route (0, Src::lfo1, Dst::cutoff, 0.12f), {
                { voiceMode, mode (VM::poly) }, { aUnison, 5.0f }, { aDetune, 0.35f }, { aSpread, 0.8f },
                { bWave, wave (Wave::triangle) }, { bOctave, 1.0f }, { bFine, 0.0f }, { bLevel, 0.3f },
                { ampAttack, 0.25f }, { ampDecay, 1.5f }, { ampSustain, 0.85f }, { ampRelease, 1.4f },
                { cutoff, 1800.0f }, { resonance, 0.2f }, { filterEnv, 0.15f }, { modAttack, 0.6f }, { modDecay, 1.5f },
                { lfo1Rate, 0.2f }, { vibratoDepth, 8.0f }, { vibratoDelay, 0.6f }, { glide, 0.0f },
                { chorusMix, 0.35f }, { reverbMix, 0.45f }, { reverbDecay, 4.5f }, { reverbSize, 0.8f }, { delayMix, 0.2f } } }));

            p.push_back (make ("Glass Lead", "Character", { noMovement(), {
                { aWave, wave (Wave::triangle) }, { aUnison, 3.0f }, { aDetune, 0.3f }, { bWave, wave (Wave::sine) }, { bOctave, 1.0f },
                { bFine, 0.0f }, { fm, 0.2f }, { ampAttack, 0.15f }, { ampRelease, 1.0f }, { filterType, filter (FT::lowPass) },
                { cutoff, 7000.0f }, { resonance, 0.0f }, { reverbMix, 0.5f }, { reverbDecay, 4.0f }, { delayMix, 0.25f } } }));

            // Plucks & keys -------------------------------------------------------------------
            p.push_back (make ("Pluck Lead", "Plucks & Keys", { noMovement(), {
                { voiceMode, mode (VM::poly) }, { aUnison, 2.0f }, { bWave, wave (Wave::pulse) }, { bOctave, 1.0f }, { bFine, 0.0f },
                { bLevel, 0.3f }, { ampAttack, 0.002f }, { ampDecay, 0.6f }, { ampSustain, 0.0f }, { ampRelease, 0.3f },
                { modDecay, 0.25f }, { modSustain, 0.0f }, { filterEnv, 0.6f }, { cutoff, 900.0f }, { vibratoDepth, 0.0f },
                { delayFeedback, 0.4f }, { delayMix, 0.25f } } }));

            p.push_back (make ("Retro Arp Lead", "Plucks & Keys", { noMovement(), {
                { voiceMode, mode (VM::poly) }, { aWave, wave (Wave::pulse) }, { aPulseWidth, 0.25f }, { aUnison, 1.0f },
                { bWave, wave (Wave::pulse) }, { bSemitones, 12.0f }, { bFine, 0.0f }, { bLevel, 0.3f },
                { ampDecay, 0.25f }, { ampSustain, 0.5f }, { ampRelease, 0.12f }, { cutoff, 2200.0f }, { filterEnv, 0.35f },
                { delayDivision, 3.0f }, { delayFeedback, 0.35f }, { delayMix, 0.2f }, { vibratoDepth, 0.0f } } }));

            p.push_back (make ("Brass Stab", "Plucks & Keys", { noMovement(), {
                { voiceMode, mode (VM::poly) }, { aUnison, 3.0f }, { aDetune, 0.2f }, { bFine, -6.0f },
                { cutoff, 700.0f }, { resonance, 0.15f }, { filterEnv, 0.6f }, { modAttack, 0.05f }, { modDecay, 0.35f },
                { modSustain, 0.4f }, { ampAttack, 0.03f }, { ampDecay, 0.5f }, { ampSustain, 0.7f }, { ampRelease, 0.15f },
                { filterVelocity, 0.5f }, { vibratoDepth, 6.0f } } }));

            // Bass ----------------------------------------------------------------------------
            p.push_back (make ("Mono Bass", "Bass", { noMovement(), {
                { aOctave, -1.0f }, { aUnison, 1.0f }, { bWave, wave (Wave::pulse) }, { bOctave, -2.0f }, { bFine, 0.0f },
                { bLevel, 0.5f }, { subLevel, 0.5f }, { subShape, on }, { cutoff, 600.0f }, { resonance, 0.3f },
                { filterEnv, 0.45f }, { modDecay, 0.3f }, { modSustain, 0.2f }, { driveType, driveOf (DT::tube) }, { drive, 0.3f },
                { delayOn, off }, { reverbOn, off }, { chorusOn, off }, { vibratoDepth, 0.0f }, { glide, 0.03f } } }));

            // Each preset's volume, so a phrase peaks around -6 dBFS (set from measurements).
            static const std::map<juce::String, float> volumes {
                { "Init", 4.3f }, { "Classic Saw Lead", -4.0f }, { "Square Hero", -3.8f }, { "Smooth Sine Lead", -8.1f },
                { "Supersaw Anthem", -9.7f }, { "Portamento Whistle", -7.6f }, { "Sync Screamer", -5.1f }, { "Sync Wobble", -4.1f },
                { "FM Bell Lead", -4.1f }, { "Ring Mod Lead", -3.3f }, { "Talking Lead", 3.6f }, { "Acid Line", -3.4f },
                { "Dirty Fold", -3.3f }, { "Screaming Ladder", -5.5f }, { "Glass Lead", -12.2f }, { "Pad-Lead Hybrid", -11.0f }, { "Overdriven Lead", -7.0f }, { "Fuzz Lead", -14.1f }, { "Pluck Lead", -4.1f },
                { "Retro Arp Lead", -6.3f }, { "Brass Stab", -6.9f }, { "Mono Bass", -0.5f } };

            for (auto& preset : p)
                if (auto found = volumes.find (preset.name); found != volumes.end())
                    preset.values.push_back ({ volume, found->second });

            return p;
        }

        juce::String userExtension()     { return ".clead"; }

        juce::File& userFolderOverride()
        {
            static juce::File folder;
            return folder;
        }
    }

    const std::vector<Preset>& factory()
    {
        static const auto presets = makeFactory();
        return presets;
    }

    std::vector<float> valuesOf (const Preset& preset)
    {
        std::vector<float> values;
        for (auto& spec : LeadParameters::all())
            values.push_back (spec.defaultValue);

        for (auto& [id, value] : preset.values)
            if (id >= 0 && id < (int) values.size())
                values[(size_t) id] = value;

        return values;
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
                   .getChildFile ("CDAW").getChildFile ("Presets").getChildFile ("C-Lead");
    }

    std::vector<Preset> loadUser()
    {
        std::vector<Preset> presets;
        const auto& specs = LeadParameters::all();

        for (auto& file : getUserFolder().findChildFiles (juce::File::findFiles, false, "*" + userExtension()))
        {
            auto xml = juce::XmlDocument::parse (file);
            if (xml == nullptr || ! xml->hasTagName ("CLeadPreset"))
                continue;

            Preset p { xml->getStringAttribute ("name", file.getFileNameWithoutExtension()), "User", {}, getDefaultMacroNames(), file };

            for (int m = 0; m < LeadEngine::numMacros; ++m)
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

        juce::XmlElement xml ("CLeadPreset");
        xml.setAttribute ("name", name.trim());
        xml.setAttribute ("version", 1);
        for (int m = 0; m < LeadEngine::numMacros; ++m)
            xml.setAttribute ("macro" + juce::String (m + 1), macroNames[m]);

        const auto& specs = LeadParameters::all();
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
