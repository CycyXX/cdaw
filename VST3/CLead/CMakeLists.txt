# C-Lead as a VST3 instrument (see VST3/Common/CDAWEffect.cmake).
set(CDAW_SOURCE "${CMAKE_SOURCE_DIR}/Source")

cdaw_add_effect_vst3(CLead SYNTH
    PRODUCT_NAME "C-Lead"
    PLUGIN_CODE Clea
    CATEGORY Synth
    SOURCES
        CLeadProcessor.cpp
        "${CDAW_SOURCE}/Shared/DSP/LeadEngine.cpp"
        "${CDAW_SOURCE}/Shared/DSP/DistEngine.cpp"
        "${CDAW_SOURCE}/Shared/DSP/ChorusEngine.cpp"
        "${CDAW_SOURCE}/Shared/DSP/DelayEngine.cpp"
        "${CDAW_SOURCE}/Shared/DSP/ReverbEngine.cpp"
        "${CDAW_SOURCE}/Shared/DSP/FilterEngine.cpp"
        "${CDAW_SOURCE}/Shared/Filter/FilterParameters.cpp"
        "${CDAW_SOURCE}/Shared/Lead/LeadParameters.cpp"
        "${CDAW_SOURCE}/Shared/Lead/LeadPresets.cpp"
        "${CDAW_SOURCE}/Shared/Lead/LeadView.cpp"
        "${CDAW_SOURCE}/Shared/Lead/LeadPanels.cpp"
        "${CDAW_SOURCE}/Shared/Lead/LeadModPanels.cpp")
