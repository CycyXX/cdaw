#include "LeadParameters.h"
#include "../DSP/TempoSync.h"
#include "../Filter/FilterParameters.h"

namespace cdaw::LeadParameters
{
    namespace
    {
        using namespace SpecText;
        using Source = LeadEngine::Source;
        using Destination = LeadEngine::Destination;

        juce::String seconds (float v)          { return v < 1.0f ? juce::String (juce::roundToInt (v * 1000.0f)) + " ms" : juce::String (v, v < 10.0f ? 2 : 1) + " s"; }
        float parseSeconds (const juce::String& s)
        {
            auto v = s.getFloatValue();
            return s.containsIgnoreCase ("ms") ? v * 0.001f : v;
        }

        juce::String signedPercent (float v)    { return (v > 0.005f ? "+" : "") + juce::String (juce::roundToInt (v * 100.0f)) + " %"; }
        juce::String integer (float v)          { return juce::String (juce::roundToInt (v)); }
        juce::String signedInteger (float v)    { auto i = juce::roundToInt (v); return (i > 0 ? "+" : "") + juce::String (i); }
        juce::String cents (float v)            { return juce::String (juce::roundToInt (v)) + " ct"; }
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
            const auto waves = getWaveNames();
            static const char* ids[2][oscillatorParameters] {
                { "aon", "awave", "aoct", "asemi", "afine", "alevel", "apw", "apan", "aunison", "adetune", "aspread" },
                { "bon", "bwave", "boct", "bsemi", "bfine", "blevel", "bpw", "bpan", "bunison", "bdetune", "bspread" } };
            static juce::StringArray labels[2];
            static const char* parts[] { " On", " Wave", " Octave", " Semitones", " Fine", " Level", " Pulse Width", " Pan", " Unison", " Detune", " Spread" };

            // Two saws, B 7 cents up: the classic lead.
            const bool b = o == 1;
            const float defaults[oscillatorParameters] { 1.0f, 0.0f, 0.0f, 0.0f, b ? 7.0f : 0.0f, b ? 0.55f : 0.8f, 0.5f, 0.0f,
                                                         b ? 1.0f : 3.0f, b ? 0.2f : 0.15f, b ? 0.5f : 0.35f };
            labels[o].clear();
            for (auto* part : parts)
                labels[o].add (juce::String (o == 0 ? "A" : "B") + part);

            auto name = [o] (int part) { return labels[o][part].toRawUTF8(); };
            auto at = [&specs, o] (ID first) -> ParameterSpec& { return specs[(size_t) oscillator (o, first)]; };

            at (aOn)         = { ids[o][0],  name (0),  0.0f, 1.0f, defaults[0], 0.0f, 1.0f, onOff, parseOnOff };
            at (aWave)       = { ids[o][1],  name (1),  0.0f, (float) ((int) LeadEngine::Wave::numWaves - 1), defaults[1], 0.0f, 1.0f, choice (waves), parseChoice (waves) };
            at (aOctave)     = { ids[o][2],  name (2),  -3.0f, 3.0f, defaults[2], 0.0f, 1.0f, signedInteger, parseFloat };
            at (aSemitones)  = { ids[o][3],  name (3),  -12.0f, 12.0f, defaults[3], 0.0f, 1.0f, signedInteger, parseFloat };
            at (aFine)       = { ids[o][4],  name (4),  -100.0f, 100.0f, defaults[4], 0.0f, 0.0f, [] (float v) { return signedInteger (v) + " ct"; }, parseFloat };
            at (aLevel)      = { ids[o][5],  name (5),  0.0f, 1.0f, defaults[5], 0.0f, 0.0f, percent, parsePercent };
            at (aPulseWidth) = { ids[o][6],  name (6),  0.05f, 0.95f, defaults[6], 0.0f, 0.0f, percent, parsePercent };
            at (aPan)        = { ids[o][7],  name (7),  -1.0f, 1.0f, defaults[7], 0.0f, 0.0f, pan, parsePan };
            at (aUnison)     = { ids[o][8],  name (8),  1.0f, (float) LeadEngine::maxUnison, defaults[8], 0.0f, 1.0f, integer, parseFloat };
            at (aDetune)     = { ids[o][9],  name (9),  0.0f, 1.0f, defaults[9], 0.0f, 0.0f, percent, parsePercent };
            at (aSpread)     = { ids[o][10], name (10), 0.0f, 1.0f, defaults[10], 0.0f, 0.0f, percent, parsePercent };
        }

        void lfoSpecs (ParameterSpecs& specs, int l)
        {
            const auto shapes = FilterParameters::getShapeNames(), divisions = TempoSync::getNames();
            static const char* ids[2][lfoParameters] { { "l1shape", "l1rate", "l1sync", "l1div", "l1retrig", "l1fade" },
                                                       { "l2shape", "l2rate", "l2sync", "l2div", "l2retrig", "l2fade" } };
            static const char* names[2][lfoParameters] { { "LFO 1 Shape", "LFO 1 Rate", "LFO 1 Sync", "LFO 1 Note", "LFO 1 Retrigger", "LFO 1 Fade" },
                                                         { "LFO 2 Shape", "LFO 2 Rate", "LFO 2 Sync", "LFO 2 Note", "LFO 2 Retrigger", "LFO 2 Fade" } };
            auto at = [&specs, l] (ID first) -> ParameterSpec& { return specs[(size_t) lfo (l, first)]; };

            // LFO 1 a slow sine (for PWM), LFO 2 a faster triangle restarting with each note.
            at (lfo1Shape)     = { ids[l][0], names[l][0], 0.0f, (float) ((int) FilterEngine::Shape::numShapes - 1), l == 0 ? 0.0f : 1.0f, 0.0f, 1.0f,
                                   choice (shapes), parseChoice (shapes) };
            at (lfo1Rate)      = { ids[l][1], names[l][1], 0.01f, 20.0f, l == 0 ? 0.35f : 4.0f, 1.0f, 0.0f, hz, parseHz };
            at (lfo1Sync)      = { ids[l][2], names[l][2], 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, onOff, parseOnOff };
            at (lfo1Division)  = { ids[l][3], names[l][3], 0.0f, (float) (TempoSync::divisions.size() - 1), l == 0 ? 15.0f : 6.0f, 0.0f, 1.0f,
                                   choice (divisions), parseChoice (divisions) };
            at (lfo1Retrigger) = { ids[l][4], names[l][4], 0.0f, 1.0f, l == 0 ? 0.0f : 1.0f, 0.0f, 1.0f, onOff, parseOnOff };
            at (lfo1Fade)      = { ids[l][5], names[l][5], 0.0f, 10.0f, 0.0f, 1.5f, 0.0f, seconds, parseSeconds };
        }

        void slotSpecs (ParameterSpecs& specs)
        {
            const auto sources = getSourceNames(), destinations = getDestinationNames();
            static juce::StringArray ids, names;
            ids.clear();
            names.clear();
            for (int k = 0; k < LeadEngine::numSlots; ++k)
                for (auto* part : { "src", "dst", "amt" })
                    ids.add ("m" + juce::String (k + 1) + part);
            for (int k = 0; k < LeadEngine::numSlots; ++k)
                for (auto* part : { " Source", " Destination", " Amount" })
                    names.add ("Mod " + juce::String (k + 1) + part);

            // The default lead's routings: PWM from LFO 1, velocity and aftertouch open the filter,
            // the macros reach brightness, grit, the sync sweep and bite.
            struct Default { Source source; Destination destination; float amount; };
            static constexpr Default defaults[LeadEngine::numSlots] {
                { Source::lfo1, Destination::pulseWidth, 0.3f }, { Source::velocity, Destination::cutoff, 0.15f },
                { Source::aftertouch, Destination::cutoff, 0.25f }, { Source::macro1, Destination::cutoff, 0.6f },
                { Source::macro2, Destination::drive, 0.7f }, { Source::macro3, Destination::syncPitch, 0.5f },
                { Source::macro4, Destination::resonance, 0.5f }, { Source::none, Destination::none, 0.0f } };

            for (int k = 0; k < LeadEngine::numSlots; ++k)
            {
                auto id = [&] (int part) { return ids[k * 3 + part].toRawUTF8(); };
                auto name = [&] (int part) { return names[k * 3 + part].toRawUTF8(); };
                specs[(size_t) slot (k, 0)] = { id (0), name (0), 0.0f, (float) (LeadEngine::numSources - 1), (float) defaults[k].source, 0.0f, 1.0f,
                                                choice (sources), parseChoice (sources) };
                specs[(size_t) slot (k, 1)] = { id (1), name (1), 0.0f, (float) (LeadEngine::numDestinations - 1), (float) defaults[k].destination, 0.0f, 1.0f,
                                                choice (destinations), parseChoice (destinations) };
                specs[(size_t) slot (k, 2)] = { id (2), name (2), -1.0f, 1.0f, defaults[k].amount, 0.0f, 0.0f, signedPercent, parsePercent };
            }
        }

        ParameterSpecs makeSpecs()
        {
            ParameterSpecs specs ((size_t) count);
            oscillatorSpecs (specs, 0);
            oscillatorSpecs (specs, 1);

            const auto filterTypes = getFilterTypeNames(), drives = getDriveNames(), divisions = TempoSync::getNames (TempoSync::numDelayDivisions);
            const juce::StringArray subShapes { "Sine", "Square" }, modes { "Mono", "Legato", "Poly" }, priorities { "Last", "High", "Low" },
                                    glideModes { "Always", "Legato" };

            specs[sync]           = { "sync",       "Sync",             0.0f, 1.0f, 0.0f, 0.0f, 1.0f, onOff, parseOnOff };
            specs[fm]             = { "fm",         "FM",               0.0f, 1.0f, 0.0f, 0.0f, 0.0f, percent, parsePercent };
            specs[ring]           = { "ring",       "Ring",             0.0f, 1.0f, 0.0f, 0.0f, 0.0f, percent, parsePercent };
            specs[subLevel]       = { "sublevel",   "Sub Level",        0.0f, 1.0f, 0.0f, 0.0f, 0.0f, percent, parsePercent };
            specs[subOctave]      = { "suboct",     "Sub Octave",       1.0f, 2.0f, 1.0f, 0.0f, 1.0f, [] (float v) { return "-" + integer (v) + " oct"; },
                                      [] (const juce::String& s) { return std::abs (s.getFloatValue()); } };
            specs[subShape]       = { "subshape",   "Sub Shape",        0.0f, 1.0f, 0.0f, 0.0f, 1.0f, choice (subShapes), parseChoice (subShapes) };
            specs[noiseLevel]     = { "noiselevel", "Noise",            0.0f, 1.0f, 0.0f, 0.0f, 0.0f, percent, parsePercent };

            specs[driveType]      = { "drivetype",  "Drive Type",       0.0f, 2.0f, 0.0f, 0.0f, 1.0f, choice (drives), parseChoice (drives) };
            specs[drive]          = { "drive",      "Drive",            0.0f, 1.0f, 0.15f, 0.0f, 0.0f, percent, parsePercent };

            specs[filterType]     = { "ftype",      "Filter Type",      0.0f, 3.0f, 0.0f, 0.0f, 1.0f, choice (filterTypes), parseChoice (filterTypes) };
            specs[cutoff]         = { "cutoff",     "Cutoff",           20.0f, 20000.0f, 2500.0f, 1000.0f, 0.0f, hz, parseHz };
            specs[resonance]      = { "reso",       "Resonance",        0.0f, 1.0f, 0.25f, 0.0f, 0.0f, percent, parsePercent };
            specs[keyTrack]       = { "keytrack",   "Key Track",        0.0f, 1.0f, 0.5f, 0.0f, 0.0f, percent, parsePercent };
            specs[filterEnv]      = { "fenv",       "Filter Envelope",  -1.0f, 1.0f, 0.35f, 0.0f, 0.0f, signedPercent, parsePercent };
            specs[filterVelocity] = { "fvel",       "Filter Velocity",  0.0f, 1.0f, 0.2f, 0.0f, 0.0f, percent, parsePercent };

            specs[ampAttack]      = { "ampatk",     "Amp Attack",       0.001f, 10.0f, 0.005f, 0.5f, 0.0f, seconds, parseSeconds };
            specs[ampDecay]       = { "ampdec",     "Amp Decay",        0.001f, 10.0f, 0.4f, 0.8f, 0.0f, seconds, parseSeconds };
            specs[ampSustain]     = { "ampsus",     "Amp Sustain",      0.0f, 1.0f, 0.8f, 0.0f, 0.0f, percent, parsePercent };
            specs[ampRelease]     = { "amprel",     "Amp Release",      0.005f, 10.0f, 0.25f, 0.8f, 0.0f, seconds, parseSeconds };
            specs[ampVelocity]    = { "ampvel",     "Velocity",         0.0f, 1.0f, 0.5f, 0.0f, 0.0f, percent, parsePercent };
            specs[modAttack]      = { "modatk",     "Filter Attack",    0.001f, 10.0f, 0.002f, 0.5f, 0.0f, seconds, parseSeconds };
            specs[modDecay]       = { "moddec",     "Filter Decay",     0.001f, 10.0f, 0.5f, 0.8f, 0.0f, seconds, parseSeconds };
            specs[modSustain]     = { "modsus",     "Filter Sustain",   0.0f, 1.0f, 0.3f, 0.0f, 0.0f, percent, parsePercent };
            specs[modRelease]     = { "modrel",     "Filter Release",   0.005f, 10.0f, 0.3f, 0.8f, 0.0f, seconds, parseSeconds };

            specs[vibratoRate]       = { "vibrate",   "Vibrato Rate",       0.5f, 12.0f, 5.5f, 5.0f, 0.0f, hz, parseHz };
            specs[vibratoDepth]      = { "vibdepth",  "Vibrato Depth",      0.0f, 100.0f, 15.0f, 0.0f, 0.0f, cents, parseFloat };
            specs[vibratoDelay]      = { "vibdelay",  "Vibrato Delay",      0.0f, 3.0f, 0.35f, 0.6f, 0.0f, seconds, parseSeconds };
            specs[vibratoFade]       = { "vibfade",   "Vibrato Fade",       0.0f, 3.0f, 0.4f, 0.6f, 0.0f, seconds, parseSeconds };
            specs[vibratoWheel]      = { "vibwheel",  "Vibrato Wheel",      0.0f, 200.0f, 50.0f, 0.0f, 0.0f, cents, parseFloat };
            specs[vibratoAftertouch] = { "vibat",     "Vibrato Aftertouch", 0.0f, 200.0f, 30.0f, 0.0f, 0.0f, cents, parseFloat };

            lfoSpecs (specs, 0);
            lfoSpecs (specs, 1);
            slotSpecs (specs);

            specs[macro1]         = { "macro1",     "Macro 1",          0.0f, 1.0f, 0.0f, 0.0f, 0.0f, percent, parsePercent };
            specs[macro2]         = { "macro2",     "Macro 2",          0.0f, 1.0f, 0.0f, 0.0f, 0.0f, percent, parsePercent };
            specs[macro3]         = { "macro3",     "Macro 3",          0.0f, 1.0f, 0.0f, 0.0f, 0.0f, percent, parsePercent };
            specs[macro4]         = { "macro4",     "Macro 4",          0.0f, 1.0f, 0.0f, 0.0f, 0.0f, percent, parsePercent };

            specs[voiceMode]      = { "voicemode",  "Voice Mode",       0.0f, 2.0f, 0.0f, 0.0f, 1.0f, choice (modes), parseChoice (modes) };
            specs[priority]       = { "priority",   "Note Priority",    0.0f, 2.0f, 0.0f, 0.0f, 1.0f, choice (priorities), parseChoice (priorities) };
            specs[polyphony]      = { "polyphony",  "Polyphony",        1.0f, (float) LeadEngine::maxVoices, 6.0f, 0.0f, 1.0f, integer, parseFloat };
            specs[glide]          = { "glide",      "Glide",            0.0f, 2.0f, 0.06f, 0.3f, 0.0f,
                                      [] (float v) { return v < 0.0005f ? juce::String ("Off") : seconds (v); }, parseSeconds };
            specs[glideMode]      = { "glidemode",  "Glide Mode",       0.0f, 1.0f, 1.0f, 0.0f, 1.0f, choice (glideModes), parseChoice (glideModes) };
            specs[bendUp]         = { "bendup",     "Bend Up",          0.0f, 24.0f, 2.0f, 0.0f, 1.0f, [] (float v) { return integer (v) + " st"; }, parseFloat };
            specs[bendDown]       = { "benddown",   "Bend Down",        0.0f, 24.0f, 2.0f, 0.0f, 1.0f, [] (float v) { return integer (v) + " st"; }, parseFloat };

            specs[chorusOn]       = { "chon",       "Chorus",           0.0f, 1.0f, 1.0f, 0.0f, 1.0f, onOff, parseOnOff };
            specs[chorusDepth]    = { "chdepth",    "Chorus Depth",     0.0f, 1.0f, 0.2f, 0.0f, 0.0f, percent, parsePercent };
            specs[chorusMix]      = { "chmix",      "Chorus Mix",       0.0f, 1.0f, 0.15f, 0.0f, 0.0f, percent, parsePercent };
            specs[delayOn]        = { "dlon",       "Delay",            0.0f, 1.0f, 1.0f, 0.0f, 1.0f, onOff, parseOnOff };
            specs[delayDivision]  = { "dldiv",      "Delay Note",       0.0f, (float) (TempoSync::numDelayDivisions - 1), 8.0f, 0.0f, 1.0f,
                                      choice (divisions), parseChoice (divisions) };
            specs[delayFeedback]  = { "dlfb",       "Delay Feedback",   0.0f, 0.95f, 0.3f, 0.0f, 0.0f, percent, parsePercent };
            specs[delayMix]       = { "dlmix",      "Delay Mix",        0.0f, 1.0f, 0.18f, 0.0f, 0.0f, percent, parsePercent };
            specs[reverbOn]       = { "rvon",       "Reverb",           0.0f, 1.0f, 1.0f, 0.0f, 1.0f, onOff, parseOnOff };
            specs[reverbSize]     = { "rvsize",     "Reverb Size",      0.0f, 1.0f, 0.6f, 0.0f, 0.0f, percent, parsePercent };
            specs[reverbDecay]    = { "rvdecay",    "Reverb Decay",     0.3f, 20.0f, 2.2f, 3.0f, 0.0f, seconds, parseSeconds };
            specs[reverbMix]      = { "rvmix",      "Reverb Mix",       0.0f, 1.0f, 0.25f, 0.0f, 0.0f, percent, parsePercent };

            specs[volume]         = { "volume",     "Volume",           -36.0f, 6.0f, -4.0f, 0.0f, 0.0f, db, parseFloat };

            const auto distVoicings = getDistVoicingNames();
            specs[distOn]         = { "diston",     "Dist",             0.0f, 1.0f, 0.0f, 0.0f, 1.0f, onOff, parseOnOff };
            specs[distVoicing]    = { "distvoicing", "Dist Voicing",    0.0f, (float) (distVoicings.size() - 1), 1.0f, 0.0f, 1.0f, choice (distVoicings), parseChoice (distVoicings) };
            specs[distDrive]      = { "distdrive",  "Dist Drive",       0.0f, 1.0f, 0.5f, 0.0f, 0.0f, percent, parsePercent };
            specs[distTone]       = { "disttone",   "Dist Tone",        0.0f, 1.0f, 0.5f, 0.0f, 0.0f, percent, parsePercent };
            specs[distMix]        = { "distmix",    "Dist Mix",         0.0f, 1.0f, 1.0f, 0.0f, 0.0f, percent, parsePercent };
            return specs;
        }
    }

    juce::StringArray getWaveNames()            { return { "Saw", "Pulse", "Triangle", "Sine" }; }
    juce::StringArray getFilterTypeNames()      { return { "Ladder 24", "Low pass 12", "Band pass 12", "High pass 12" }; }
    juce::StringArray getDriveNames()           { return { "Soft", "Tube", "Fold" }; }
    juce::StringArray getDefaultMacroNames()    { return { "Brightness", "Grit", "Sync", "Bite" }; }
    juce::StringArray getDistVoicingNames()     { return { "Crunch", "Lead", "Modern", "Fuzz" }; }

    juce::StringArray getSourceNames()
    {
        return { "Off", "LFO 1", "LFO 2", "Filter Env", "Amp Env", "Velocity", "Key", "Mod Wheel", "Aftertouch", "Random",
                 "Macro 1", "Macro 2", "Macro 3", "Macro 4" };
    }

    juce::StringArray getDestinationNames()
    {
        return { "Off", "Pitch", "Pulse Width", "Sync Pitch", "FM", "Ring", "A Level", "B Level", "Cutoff", "Resonance",
                 "Drive", "Pan", "Amp", "Vibrato", "LFO 1 Rate", "LFO 2 Rate" };
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

    LeadEngine::Settings toSettings (const std::function<float (int)>& valueOf)
    {
        LeadEngine::Settings s;
        auto on = [&valueOf] (int id) { return valueOf (id) >= 0.5f; };
        auto whole = [&valueOf] (int id) { return juce::roundToInt (valueOf (id)); };

        for (int o = 0; o < LeadEngine::numOscillators; ++o)
        {
            auto& osc = s.osc[o];
            osc.on = on (oscillator (o, aOn));
            osc.wave = (LeadEngine::Wave) juce::jlimit (0, (int) LeadEngine::Wave::numWaves - 1, whole (oscillator (o, aWave)));
            osc.octave = whole (oscillator (o, aOctave));
            osc.semitones = whole (oscillator (o, aSemitones));
            osc.fine = valueOf (oscillator (o, aFine));
            osc.level = valueOf (oscillator (o, aLevel));
            osc.pulseWidth = valueOf (oscillator (o, aPulseWidth));
            osc.pan = valueOf (oscillator (o, aPan));
            osc.unison = whole (oscillator (o, aUnison));
            osc.detune = valueOf (oscillator (o, aDetune));
            osc.spread = valueOf (oscillator (o, aSpread));
        }

        s.sync = on (sync);
        s.fm = valueOf (fm);
        s.ring = valueOf (ring);
        s.subLevel = valueOf (subLevel);
        s.subOctave = whole (subOctave);
        s.subSquare = on (subShape);
        s.noiseLevel = valueOf (noiseLevel);
        s.driveType = (LeadEngine::DriveType) juce::jlimit (0, 2, whole (driveType));
        s.drive = valueOf (drive);

        s.filterType = (LeadEngine::FilterType) juce::jlimit (0, 3, whole (filterType));
        s.cutoff = valueOf (cutoff);
        s.resonance = valueOf (resonance);
        s.keyTrack = valueOf (keyTrack);
        s.filterEnv = valueOf (filterEnv);
        s.filterVelocity = valueOf (filterVelocity);

        s.ampEnv = { valueOf (ampAttack), valueOf (ampDecay), valueOf (ampSustain), valueOf (ampRelease) };
        s.velocity = valueOf (ampVelocity);
        s.modEnv = { valueOf (modAttack), valueOf (modDecay), valueOf (modSustain), valueOf (modRelease) };

        s.vibratoRate = valueOf (vibratoRate);
        s.vibratoDepth = valueOf (vibratoDepth);
        s.vibratoDelay = valueOf (vibratoDelay);
        s.vibratoFade = valueOf (vibratoFade);
        s.vibratoWheel = valueOf (vibratoWheel);
        s.vibratoAftertouch = valueOf (vibratoAftertouch);

        for (int l = 0; l < LeadEngine::numLfos; ++l)
        {
            auto& x = s.lfo[l];
            x.shape = (FilterEngine::Shape) juce::jlimit (0, (int) FilterEngine::Shape::numShapes - 1, whole (lfo (l, lfo1Shape)));
            x.rate = valueOf (lfo (l, lfo1Rate));
            x.sync = on (lfo (l, lfo1Sync));
            x.division = whole (lfo (l, lfo1Division));
            x.retrigger = on (lfo (l, lfo1Retrigger));
            x.fade = valueOf (lfo (l, lfo1Fade));
        }

        for (int k = 0; k < LeadEngine::numSlots; ++k)
        {
            s.slots[k].source = (LeadEngine::Source) juce::jlimit (0, LeadEngine::numSources - 1, whole (slot (k, 0)));
            s.slots[k].destination = (LeadEngine::Destination) juce::jlimit (0, LeadEngine::numDestinations - 1, whole (slot (k, 1)));
            s.slots[k].amount = valueOf (slot (k, 2));
        }

        for (int m = 0; m < LeadEngine::numMacros; ++m)
            s.macros[m] = valueOf (macro1 + m);

        s.mode = (LeadEngine::VoiceMode) juce::jlimit (0, 2, whole (voiceMode));
        s.priority = (LeadEngine::Priority) juce::jlimit (0, 2, whole (priority));
        s.polyphony = whole (polyphony);
        s.glide = valueOf (glide);
        s.glideMode = (LeadEngine::GlideMode) juce::jlimit (0, 1, whole (glideMode));
        s.bendUp = valueOf (bendUp);
        s.bendDown = valueOf (bendDown);

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
        s.distOn = on (distOn);
        s.distVoicing = juce::jlimit (0, getDistVoicingNames().size() - 1, whole (distVoicing));
        s.distDrive = valueOf (distDrive);
        s.distTone = valueOf (distTone);
        s.distMix = valueOf (distMix);
        return s;
    }
}
