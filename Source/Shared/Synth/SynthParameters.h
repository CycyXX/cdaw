#pragma once

#include "../Common/ParameterSpec.h"
#include "../DSP/SynthEngine.h"

namespace cdaw::SynthParameters
{
    /** C-Synth's parameters (shared by CDAW's built-in plugin and the VST3). Stored by ID in
        projects and presets: never change one, only append. The defaults are a warm, slowly
        moving pad. */
    enum ID
    {
        aOn, aBank, aPosition, aOctave, aSemitones, aFine, aLevel, aPan, aUnison, aDetune, aSpread,
        bOn, bBank, bPosition, bOctave, bSemitones, bFine, bLevel, bPan, bUnison, bDetune, bSpread,
        subLevel, subOctave, subShape, noiseLevel, noiseColour, drift,
        filterType, filterSlope, cutoff, resonance, filterDrive, keyTrack, filterEnv,
        ampAttack, ampDecay, ampSustain, ampRelease, ampVelocity,
        modAttack, modDecay, modSustain, modRelease,
        lfo1Shape, lfo1Rate, lfo1Sync, lfo1Division, lfo1Retrigger, lfo1Fade,
        lfo2Shape, lfo2Rate, lfo2Sync, lfo2Division, lfo2Retrigger, lfo2Fade,
        slot1Source, slot1Destination, slot1Amount, slot2Source, slot2Destination, slot2Amount,
        slot3Source, slot3Destination, slot3Amount, slot4Source, slot4Destination, slot4Amount,
        slot5Source, slot5Destination, slot5Amount, slot6Source, slot6Destination, slot6Amount,
        slot7Source, slot7Destination, slot7Amount, slot8Source, slot8Destination, slot8Amount,
        macro1, macro2, macro3, macro4,
        voiceMode, polyphony, glide, bendRange,
        chorusOn, chorusDepth, chorusMix, delayOn, delayDivision, delayFeedback, delayMix,
        reverbOn, reverbSize, reverbDecay, reverbMix,
        volume,
        count
    };

    constexpr int oscillatorParameters = bOn - aOn, lfoParameters = lfo2Shape - lfo1Shape, slotParameters = 3;

    /** An oscillator's parameter: `first` is the A one (e.g. aPosition). */
    inline int oscillator (int index, ID first)         { return first + index * oscillatorParameters; }
    inline int lfo (int index, ID first)                { return first + index * lfoParameters; }
    inline int slot (int index, int part)               { return slot1Source + index * slotParameters + part; }    // part 0 source, 1 destination, 2 amount

    const ParameterSpec& get (int id);
    const ParameterSpecs& all();

    SynthEngine::Settings toSettings (const std::function<float (int)>& valueOf);

    juce::StringArray getSourceNames();
    juce::StringArray getDestinationNames();

    /** The macros' default names (presets can rename them). */
    juce::StringArray getDefaultMacroNames();
}
