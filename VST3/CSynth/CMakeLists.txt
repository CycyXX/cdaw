# C-Synth as a VST3 instrument (see VST3/Common/CDAWEffect.cmake).
set(CDAW_SOURCE "${CMAKE_SOURCE_DIR}/Source")

cdaw_add_effect_vst3(CSynth SYNTH
    PRODUCT_NAME "C-Synth"
    PLUGIN_CODE Csyn
    CATEGORY Synth
    SOURCES
        CSynthProcessor.cpp
        "${CDAW_SOURCE}/Shared/DSP/SynthEngine.cpp"
        "${CDAW_SOURCE}/Shared/DSP/Wavetables.cpp"
        "${CDAW_SOURCE}/Shared/DSP/ChorusEngine.cpp"
        "${CDAW_SOURCE}/Shared/DSP/DelayEngine.cpp"
        "${CDAW_SOURCE}/Shared/DSP/ReverbEngine.cpp"
        "${CDAW_SOURCE}/Shared/DSP/FilterEngine.cpp"
        "${CDAW_SOURCE}/Shared/Filter/FilterParameters.cpp"
        "${CDAW_SOURCE}/Shared/Synth/SynthParameters.cpp"
        "${CDAW_SOURCE}/Shared/Synth/SynthPresets.cpp"
        "${CDAW_SOURCE}/Shared/Synth/SynthView.cpp"
        "${CDAW_SOURCE}/Shared/Synth/SynthPanels.cpp"
        "${CDAW_SOURCE}/Shared/Synth/SynthModPanels.cpp")
