#pragma once

#include "../Common/ParameterSpec.h"
#include "../DSP/LeadEngine.h"

namespace cdaw::LeadParameters
{
    /** C-Lead's parameters (shared by CDAW's built-in plugin and the VST3). Stored by ID in
        projects and presets: never change one, only append. The defaults are a classic saw lead. */
    enum ID
    {
        aOn, aWave, aOctave, aSemitones, aFine, aLevel, aPulseWidth, aPan, aUnison, aDetune, aSpread,
        bOn, bWave, bOctave, bSemitones, bFine, bLevel, bPulseWidth, bPan, bUnison, bDetune, bSpread,
        sync, fm, ring, subLevel, subOctave, subShape, noiseLevel,
        driveType, drive,
        filterType, cutoff, resonance, keyTrack, filterEnv, filterVelocity,
        ampAttack, ampDecay, ampSustain, ampRelease, ampVelocity,
        modAttack, modDecay, modSustain, modRelease,
        vibratoRate, vibratoDepth, vibratoDelay, vibratoFade, vibratoWheel, vibratoAftertouch,
        lfo1Shape, lfo1Rate, lfo1Sync, lfo1Division, lfo1Retrigger, lfo1Fade,
        lfo2Shape, lfo2Rate, lfo2Sync, lfo2Division, lfo2Retrigger, lfo2Fade,
        slot1Source, slot1Destination, slot1Amount, slot2Source, slot2Destination, slot2Amount,
        slot3Source, slot3Destination, slot3Amount, slot4Source, slot4Destination, slot4Amount,
        slot5Source, slot5Destination, slot5Amount, slot6Source, slot6Destination, slot6Amount,
        slot7Source, slot7Destination, slot7Amount, slot8Source, slot8Destination, slot8Amount,
        macro1, macro2, macro3, macro4,
        voiceMode, priority, polyphony, glide, glideMode, bendUp, bendDown,
        chorusOn, chorusDepth, chorusMix, delayOn, delayDivision, delayFeedback, delayMix,
        reverbOn, reverbSize, reverbDecay, reverbMix,
        volume,
        distOn, distVoicing, distDrive, distTone, distMix,         // the distortion before the chorus (C-Dist's, simplified)
        count
    };

    constexpr int oscillatorParameters = bOn - aOn, lfoParameters = lfo2Shape - lfo1Shape, slotParameters = 3;

    inline int oscillator (int index, ID first)         { return first + index * oscillatorParameters; }
    inline int lfo (int index, ID first)                { return first + index * lfoParameters; }
    inline int slot (int index, int part)               { return slot1Source + index * slotParameters + part; }

    const ParameterSpec& get (int id);
    const ParameterSpecs& all();

    LeadEngine::Settings toSettings (const std::function<float (int)>& valueOf);

    juce::StringArray getWaveNames();
    juce::StringArray getFilterTypeNames();
    juce::StringArray getDriveNames();
    juce::StringArray getSourceNames();
    juce::StringArray getDestinationNames();
    juce::StringArray getDefaultMacroNames();
    juce::StringArray getDistVoicingNames();
}
