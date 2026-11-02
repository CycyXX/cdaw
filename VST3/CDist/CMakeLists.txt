# C-Dist as a VST3 plugin (see VST3/Common/CDAWEffect.cmake).
set(CDAW_SOURCE "${CMAKE_SOURCE_DIR}/Source")

cdaw_add_effect_vst3(CDist
    PRODUCT_NAME "C-Dist"
    PLUGIN_CODE Cdst
    CATEGORY Distortion
    SOURCES
        CDistProcessor.cpp
        "${CDAW_SOURCE}/Shared/DSP/DistEngine.cpp"
        "${CDAW_SOURCE}/Shared/Dist/DistParameters.cpp"
        "${CDAW_SOURCE}/Shared/Dist/DistPresets.cpp"
        "${CDAW_SOURCE}/Shared/Dist/DistView.cpp"
        "${CDAW_SOURCE}/Shared/Dist/DistPanels.cpp")
