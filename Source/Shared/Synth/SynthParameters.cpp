#include "SynthParameters.h"
#include "../DSP/TempoSync.h"
#include "../Filter/FilterParameters.h"

namespace cdaw::SynthParameters
{
    namespace
    {
        using namespace SpecText;
        using Source = SynthEngine::Source;
        using Destination = SynthEngine::Destination;

        juce::String seconds (float v)          { return v < 1.0f ? juce::String (juce::roundToInt (v * 1000.0f)) + " ms" : juce::String (v, v < 10.0f ? 2 : 1) + " s"; }
        float parseSeconds (const juce::String& s)
        {
            auto v = s.getFloatValue();
            return s.containsIgnoreCase ("ms") ? v * 0.001f : v;
        }

        juce::String signedPercent (float v)    { return (v > 0.005f ? "+" : "") + juce::String (juce::roundToInt (v * 100.0f)) + " %"; }
        juce::String integer (float v)          { return juce::String (juce::roundToInt (v)); }
        juce::String signedInteger (float v)    { auto i = juce::roundToInt (v); return (i > 0 ? "+" : "") + juce::String (i); }
        juce::String pan (float v)
        {
            const int p = juce::roundToInt (v * 100.0f);
            return p == 0 ? juce::String ("C") : (p < 0 ? "L" : "R") + juce::String (std::abs (p));
        }
        float parsePan (const juce::String& s)
        {
            const auto v = s.retainCharacters ("0123456789.").getFloatValue() / 100.0f;
            return s.containsIgnoreCase ("l") ? -v : v;
        }

        void oscillatorSpecs (ParameterSpecs& specs, int o)
        {
            const auto banks = Wavetables::getBankNames();
            const char* names[] { "A", "B" };
            static const char* ids[2][oscillatorParameters] {
                { "aon", "abank", "apos", "aoct", "asemi", "afine", "alevel", "apan", "aunison", "adetune", "aspread" },
                { "bon", "bbank", "bpos", "boct", "bsemi", "bfine", "blevel", "bpan", "bunison", "bdetune", "bspread" } };
            static juce::StringArray labels[2];
            static const char* parts[] { " On", " Wavetable", " Position", " Octave", " Semitones", " Fine", " Level", " Pan", " Unison", " Detune", " Spread" };

            // Oscillator B is a quieter second layer: the vowel bank, an octave up.
            const bool b = o == 1;
            const float defaults[oscillatorParameters] { 1.0f, b ? 1.0f : 0.0f, b ? 0.3f : 0.62f, b ? 1.0f : 0.0f, 0.0f, b ? 7.0f : 0.0f,
                                                         b ? 0.45f : 0.8f, 0.0f, b ? 3.0f : 5.0f, b ? 0.25f : 0.35f, b ? 0.6f : 0.8f };
            labels[o].clear();
            for (auto* part : parts)
                labels[o].add (juce::String (names[o]) + part);

            auto name = [o] (int part) { return labels[o][part].toRawUTF8(); };
            auto at = [&specs, o] (ID first) -> ParameterSpec& { return specs[(size_t) oscillator (o, first)]; };

            at (aOn)        = { ids[o][0],  name (0),  0.0f,  1.0f,   defaults[0],  0.0f, 1.0f, onOff, parseOnOff };
            at (aBank)      = { ids[o][1],  name (1),  0.0f,  (float) (Wavetables::numBanks - 1), defaults[1], 0.0f, 1.0f, choice (banks), parseChoice (banks) };
            at (aPosition)  = { ids[o][2],  name (2),  0.0f,  1.0f,   defaults[2],  0.0f, 0.0f, percent, parsePercent };
            at (aOctave)    = { ids[o][3],  name (3),  -3.0f, 3.0f,   defaults[3],  0.0f, 1.0f, signedInteger, parseFloat };
            at (aSemitones) = { ids[o][4],  name (4),  -12.0f, 12.0f, defaults[4],  0.0f, 1.0f, signedInteger, parseFloat };
            at (aFine)      = { ids[o][5],  name (5),  -100.0f, 100.0f, defaults[5], 0.0f, 0.0f,
                                [] (float v) { return signedInteger (v) + " ct"; }, parseFloat };
            at (aLevel)     = { ids[o][6],  name (6),  0.0f,  1.0f,   defaults[6],  0.0f, 0.0f, percent, parsePercent };
            at (aPan)       = { ids[o][7],  name (7),  -1.0f, 1.0f,   defaults[7],  0.0f, 0.0f, pan, parsePan };
            at (aUnison)    = { ids[o][8],  name (8),  1.0f,  (float) SynthEngine::maxUnison, defaults[8], 0.0f, 1.0f, integer, parseFloat };
            at (aDetune)    = { ids[o][9],  name (9),  0.0f,  1.0f,   defaults[9],  0.0f, 0.0f, percent, parsePercent };
            at (aSpread)    = { ids[o][10], name (10), 0.0f,  1.0f,   defaults[10], 0.0f, 0.0f, percent, parsePercent };
        }

        void lfoSpecs (ParameterSpecs& specs, int l)
        {
            const auto shapes = FilterParameters::getShapeNames(), divisions = TempoSync::getNames();
            static const char* ids[2][lfoParameters] { { "l1shape", "l1rate", "l1sync", "l1div", "l1retrig", "l1fade" },
                                                       { "l2shape", "l2rate", "l2sync", "l2div", "l2retrig", "l2fade" } };
            static const char* names[2][lfoParameters] { { "LFO 1 Shape", "LFO 1 Rate", "LFO 1 Sync", "LFO 1 Note", "LFO 1 Retrigger", "LFO 1 Fade" },
                                                         { "LFO 2 Shape", "LFO 2 Rate", "LFO 2 Sync", "LFO 2 Note", "LFO 2 Retrigger", "LFO 2 Fade" } };
            auto at = [&specs, l] (ID first) -> ParameterSpec& { return specs[(size_t) lfo (l, first)]; };

            // LFO 1 a slow sine, LFO 2 a slower triangle.
            at (lfo1Shape)     = { ids[l][0], names[l][0], 0.0f, (float) ((int) FilterEngine::Shape::numShapes - 1), l == 0 ? 0.0f : 1.0f, 0.0f, 1.0f,
                                   choice (shapes), parseChoice (shapes) };
            at (lfo1Rate)      = { ids[l][1], names[l][1], 0.01f, 20.0f, l == 0 ? 0.25f : 0.07f, 1.0f, 0.0f, hz, parseHz };
            at (lfo1Sync)      = { ids[l][2], names[l][2], 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, onOff, parseOnOff };
            at (lfo1Division)  = { ids[l][3], names[l][3], 0.0f, (float) (TempoSync::divisions.size() - 1), l == 0 ? 15.0f : 17.0f, 0.0f, 1.0f,
                                   choice (divisions), parseChoice (divisions) };
            at (lfo1Retrigger) = { ids[l][4], names[l][4], 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, onOff, parseOnOff };
            at (lfo1Fade)      = { ids[l][5], names[l][5], 0.0f, 10.0f, 0.0f, 1.5f, 0.0f, seconds, parseSeconds };
        }

        void slotSpecs (ParameterSpecs& specs)
        {
            const auto sources = getSourceNames(), destinations = getDestinationNames();
            static juce::StringArray ids, names;
            ids.clear();
            names.clear();
            for (int k = 0; k < SynthEngine::numSlots; ++k)
                for (auto* part : { "src", "dst", "amt" })
                    ids.add ("m" + juce::String (k + 1) + part);
            for (int k = 0; k < SynthEngine::numSlots; ++k)
                for (auto* part : { " Source", " Destination", " Amount" })
                    names.add ("Mod " + juce::String (k + 1) + part);

            // The default pad's movement: the LFOs sweep the wavetables and the filter, the mod
            // envelope opens oscillator B, the macros reach the obvious places.
            struct Default { Source source; Destination destination; float amount; };
            static constexpr Default defaults[SynthEngine::numSlots] {
                { Source::lfo1, Destination::positionA, 0.25f }, { Source::lfo2, Destination::cutoff, 0.12f },
                { Source::modEnv, Destination::positionB, 0.4f }, { Source::macro1, Destination::lfo1Rate, 0.6f },
                { Source::macro2, Destination::cutoff, 0.5f }, { Source::macro3, Destination::noise, 0.25f },
                { Source::macro4, Destination::spread, 0.5f }, { Source::random, Destination::pan, 0.25f } };

            for (int k = 0; k < SynthEngine::numSlots; ++k)
            {
                auto id = [&] (int part) { return ids[k * 3 + part].toRawUTF8(); };
                auto name = [&] (int part) { return names[k * 3 + part].toRawUTF8(); };
                specs[(size_t) slot (k, 0)] = { id (0), name (0), 0.0f, (float) (SynthEngine::numSources - 1), (float) defaults[k].source, 0.0f, 1.0f,
                                                choice (sources), parseChoice (sources) };
                specs[(size_t) slot (k, 1)] = { id (1), name (1), 0.0f, (float) (SynthEngine::numDestinations - 1), (float) defaults[k].destination, 0.0f, 1.0f,
                                                choice (destinations), parseChoice (destinations) };
                specs[(size_t) slot (k, 2)] = { id (2), name (2), -1.0f, 1.0f, defaults[k].amount, 0.0f, 0.0f, signedPercent, parsePercent };
            }
        }

        ParameterSpecs makeSpecs()
        {
            ParameterSpecs specs ((size_t) count);
            oscillatorSpecs (specs, 0);
            oscillatorSpecs (specs, 1);

            const auto types = FilterParameters::getTypeNames(), divisions = TempoSync::getNames (TempoSync::numDelayDivisions);
            const juce::StringArray slopes { "12 dB", "24 dB" }, subShapes { "Sine", "Triangle" }, modes { "Poly", "Mono", "Legato" };

            specs[subLevel]    = { "sublevel",   "Sub Level",       0.0f, 1.0f, 0.0f, 0.0f, 0.0f, percent, parsePercent };
            specs[subOctave]   = { "suboct",     "Sub Octave",      1.0f, 2.0f, 1.0f, 0.0f, 1.0f, [] (float v) { return "-" + integer (v) + " oct"; },
                                   [] (const juce::String& s) { return std::abs (s.getFloatValue()); } };
            specs[subShape]    = { "subshape",   "Sub Shape",       0.0f, 1.0f, 0.0f, 0.0f, 1.0f, choice (subShapes), parseChoice (subShapes) };
            specs[noiseLevel]  = { "noiselevel", "Noise Level",     0.0f, 1.0f, 0.04f, 0.0f, 0.0f, percent, parsePercent };
            specs[noiseColour] = { "noisecolour", "Noise Colour",   -1.0f, 1.0f, 0.4f, 0.0f, 0.0f,
                                   [] (float v) { return std::abs (v) < 0.03f ? juce::String ("White") : (v < 0 ? "Dark " : "Air ") + juce::String (juce::roundToInt (std::abs (v) * 100.0f)); },
                                   [] (const juce::String& s) { auto v = s.retainCharacters ("0123456789.").getFloatValue() / 100.0f; return s.containsIgnoreCase ("dark") ? -v : v; } };
            specs[drift]       = { "drift",      "Drift",           0.0f, 1.0f, 0.35f, 0.0f, 0.0f, percent, parsePercent };

            specs[filterType]  = { "ftype",      "Filter Type",     0.0f, 3.0f, 0.0f, 0.0f, 1.0f, choice (types), parseChoice (types) };
            specs[filterSlope] = { "fslope",     "Filter Slope",    0.0f, 1.0f, 0.0f, 0.0f, 1.0f, choice (slopes), parseChoice (slopes) };
            specs[cutoff]      = { "cutoff",     "Cutoff",          20.0f, 20000.0f, 2400.0f, 1000.0f, 0.0f, hz, parseHz };
            specs[resonance]   = { "reso",       "Resonance",       0.0f, 1.0f, 0.15f, 0.0f, 0.0f, percent, parsePercent };
            specs[filterDrive] = { "fdrive",     "Filter Drive",    0.0f, 1.0f, 0.0f, 0.0f, 0.0f, percent, parsePercent };
            specs[keyTrack]    = { "keytrack",   "Key Track",       0.0f, 1.0f, 0.3f, 0.0f, 0.0f, percent, parsePercent };
            specs[filterEnv]   = { "fenv",       "Filter Envelope", -1.0f, 1.0f, 0.2f, 0.0f, 0.0f, signedPercent, parsePercent };

            specs[ampAttack]   = { "ampatk",     "Amp Attack",      0.001f, 20.0f, 0.8f, 1.0f, 0.0f, seconds, parseSeconds };
            specs[ampDecay]    = { "ampdec",     "Amp Decay",       0.001f, 20.0f, 1.5f, 1.5f, 0.0f, seconds, parseSeconds };
            specs[ampSustain]  = { "ampsus",     "Amp Sustain",     0.0f, 1.0f, 0.85f, 0.0f, 0.0f, percent, parsePercent };
            specs[ampRelease]  = { "amprel",     "Amp Release",     0.005f, 20.0f, 2.5f, 2.0f, 0.0f, seconds, parseSeconds };
            specs[ampVelocity] = { "ampvel",     "Velocity",        0.0f, 1.0f, 0.4f, 0.0f, 0.0f, percent, parsePercent };
            specs[modAttack]   = { "modatk",     "Mod Attack",      0.001f, 20.0f, 2.0f, 1.0f, 0.0f, seconds, parseSeconds };
            specs[modDecay]    = { "moddec",     "Mod Decay",       0.001f, 20.0f, 3.0f, 1.5f, 0.0f, seconds, parseSeconds };
            specs[modSustain]  = { "modsus",     "Mod Sustain",     0.0f, 1.0f, 0.4f, 0.0f, 0.0f, percent, parsePercent };
            specs[modRelease]  = { "modrel",     "Mod Release",     0.005f, 20.0f, 3.0f, 2.0f, 0.0f, seconds, parseSeconds };

            lfoSpecs (specs, 0);
            lfoSpecs (specs, 1);
            slotSpecs (specs);

            specs[macro1]      = { "macro1",     "Macro 1",         0.0f, 1.0f, 0.0f, 0.0f, 0.0f, percent, parsePercent };
            specs[macro2]      = { "macro2",     "Macro 2",         0.0f, 1.0f, 0.0f, 0.0f, 0.0f, percent, parsePercent };
            specs[macro3]      = { "macro3",     "Macro 3",         0.0f, 1.0f, 0.0f, 0.0f, 0.0f, percent, parsePercent };
            specs[macro4]      = { "macro4",     "Macro 4",         0.0f, 1.0f, 0.0f, 0.0f, 0.0f, percent, parsePercent };

            specs[voiceMode]   = { "voicemode",  "Voice Mode",      0.0f, 2.0f, 0.0f, 0.0f, 1.0f, choice (modes), parseChoice (modes) };
            specs[polyphony]   = { "polyphony",  "Polyphony",       1.0f, (float) SynthEngine::maxVoices, 12.0f, 0.0f, 1.0f, integer, parseFloat };
            specs[glide]       = { "glide",      "Glide",           0.0f, 5.0f, 0.0f, 0.5f, 0.0f,
                                   [] (float v) { return v < 0.0005f ? juce::String ("Off") : seconds (v); }, parseSeconds };
            specs[bendRange]   = { "bend",       "Bend Range",      0.0f, 24.0f, 2.0f, 0.0f, 1.0f, [] (float v) { return integer (v) + " st"; }, parseFloat };

            specs[chorusOn]      = { "chon",     "Chorus",          0.0f, 1.0f, 1.0f, 0.0f, 1.0f, onOff, parseOnOff };
            specs[chorusDepth]   = { "chdepth",  "Chorus Depth",    0.0f, 1.0f, 0.35f, 0.0f, 0.0f, percent, parsePercent };
            specs[chorusMix]     = { "chmix",    "Chorus Mix",      0.0f, 1.0f, 0.3f, 0.0f, 0.0f, percent, parsePercent };
            specs[delayOn]       = { "dlon",     "Delay",           0.0f, 1.0f, 0.0f, 0.0f, 1.0f, onOff, parseOnOff };
            specs[delayDivision] = { "dldiv",    "Delay Note",      0.0f, (float) (TempoSync::numDelayDivisions - 1), 8.0f, 0.0f, 1.0f,
                                     choice (divisions), parseChoice (divisions) };
            specs[delayFeedback] = { "dlfb",     "Delay Feedback",  0.0f, 0.95f, 0.35f, 0.0f, 0.0f, percent, parsePercent };
            specs[delayMix]      = { "dlmix",    "Delay Mix",       0.0f, 1.0f, 0.2f, 0.0f, 0.0f, percent, parsePercent };
            specs[reverbOn]      = { "rvon",     "Reverb",          0.0f, 1.0f, 1.0f, 0.0f, 1.0f, onOff, parseOnOff };
            specs[reverbSize]    = { "rvsize",   "Reverb Size",     0.0f, 1.0f, 0.7f, 0.0f, 0.0f, percent, parsePercent };
            specs[reverbDecay]   = { "rvdecay",  "Reverb Decay",    0.3f, 20.0f, 4.5f, 3.0f, 0.0f, seconds, parseSeconds };
            specs[reverbMix]     = { "rvmix",    "Reverb Mix",      0.0f, 1.0f, 0.35f, 0.0f, 0.0f, percent, parsePercent };

            specs[volume]      = { "volume",     "Volume",          -36.0f, 6.0f, -8.0f, 0.0f, 0.0f, db, parseFloat };
            return specs;
        }
    }

    juce::StringArray getSourceNames()
    {
        return { "Off", "LFO 1", "LFO 2", "Mod Env", "Amp Env", "Velocity", "Key", "Mod Wheel", "Aftertouch", "Random",
                 "Macro 1", "Macro 2", "Macro 3", "Macro 4" };
    }

    juce::StringArray getDestinationNames()
    {
        return { "Off", "Pitch", "A Position", "B Position", "A Level", "B Level", "Detune", "Spread", "Cutoff", "Resonance",
                 "Noise", "Pan", "Amp", "LFO 1 Rate", "LFO 2 Rate" };
    }

    juce::StringArray getDefaultMacroNames()
    {
        return { "Motion", "Brightness", "Texture", "Width" };
    }

    const ParameterSpecs& all()
    {
        static const auto specs = makeSpecs();
        return specs;
    }

    const ParameterSpec& get (int id)
    {
        return all()[(size_t) id];
    }

    SynthEngine::Settings toSettings (const std::function<float (int)>& valueOf)
    {
        SynthEngine::Settings s;
        auto on = [&valueOf] (int id) { return valueOf (id) >= 0.5f; };
        auto whole = [&valueOf] (int id) { return juce::roundToInt (valueOf (id)); };

        for (int o = 0; o < SynthEngine::numOscillators; ++o)
        {
            auto& osc = s.osc[o];
            osc.on = on (oscillator (o, aOn));
            osc.bank = whole (oscillator (o, aBank));
            osc.position = valueOf (oscillator (o, aPosition));
            osc.octave = whole (oscillator (o, aOctave));
            osc.semitones = whole (oscillator (o, aSemitones));
            osc.fine = valueOf (oscillator (o, aFine));
            osc.level = valueOf (oscillator (o, aLevel));
            osc.pan = valueOf (oscillator (o, aPan));
            osc.unison = whole (oscillator (o, aUnison));
            osc.detune = valueOf (oscillator (o, aDetune));
            osc.spread = valueOf (oscillator (o, aSpread));
        }

        s.subLevel = valueOf (subLevel);
        s.subOctave = whole (subOctave);
        s.subTriangle = on (subShape);
        s.noiseLevel = valueOf (noiseLevel);
        s.noiseColour = valueOf (noiseColour);
        s.drift = valueOf (drift);

        s.filterType = (FilterEngine::Type) juce::jlimit (0, 3, whole (filterType));
        s.filterSteep = on (filterSlope);
        s.cutoff = valueOf (cutoff);
        s.resonance = valueOf (resonance);
        s.filterDrive = valueOf (filterDrive);
        s.keyTrack = valueOf (keyTrack);
        s.filterEnv = valueOf (filterEnv);

        s.ampEnv = { valueOf (ampAttack), valueOf (ampDecay), valueOf (ampSustain), valueOf (ampRelease) };
        s.velocity = valueOf (ampVelocity);
        s.modEnv = { valueOf (modAttack), valueOf (modDecay), valueOf (modSustain), valueOf (modRelease) };

        for (int l = 0; l < SynthEngine::numLfos; ++l)
        {
            auto& x = s.lfo[l];
            x.shape = (FilterEngine::Shape) juce::jlimit (0, (int) FilterEngine::Shape::numShapes - 1, whole (lfo (l, lfo1Shape)));
            x.rate = valueOf (lfo (l, lfo1Rate));
            x.sync = on (lfo (l, lfo1Sync));
            x.division = whole (lfo (l, lfo1Division));
            x.retrigger = on (lfo (l, lfo1Retrigger));
            x.fade = valueOf (lfo (l, lfo1Fade));
        }

        for (int k = 0; k < SynthEngine::numSlots; ++k)
        {
            s.slots[k].source = (SynthEngine::Source) juce::jlimit (0, SynthEngine::numSources - 1, whole (slot (k, 0)));
            s.slots[k].destination = (SynthEngine::Destination) juce::jlimit (0, SynthEngine::numDestinations - 1, whole (slot (k, 1)));
            s.slots[k].amount = valueOf (slot (k, 2));
        }

        for (int m = 0; m < SynthEngine::numMacros; ++m)
            s.macros[m] = valueOf (macro1 + m);

        s.mode = (SynthEngine::VoiceMode) juce::jlimit (0, 2, whole (voiceMode));
        s.polyphony = whole (polyphony);
        s.glide = valueOf (glide);
        s.bendRange = valueOf (bendRange);

        s.chorusOn = on (chorusOn);
        s.chorusDepth = valueOf (chorusDepth);
        s.chorusMix = valueOf (chorusMix);
        s.delayOn = on (delayOn);
        s.delayDivision = whole (delayDivision);
        s.delayFeedback = valueOf (delayFeedback);
        s.delayMix = valueOf (delayMix);
        s.reverbOn = on (reverbOn);
        s.reverbSize = valueOf (reverbSize);
        s.reverbDecay = valueOf (reverbDecay);
        s.reverbMix = valueOf (reverbMix);
        s.volumeDb = valueOf (volume);
        return s;
    }
}
