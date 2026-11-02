# C-Esser as a VST3 plugin (see VST3/Common/CDAWEffect.cmake).
set(CDAW_SOURCE "${CMAKE_SOURCE_DIR}/Source")

cdaw_add_effect_vst3(CEsser
    PRODUCT_NAME "C-Esser"
    PLUGIN_CODE Cess
    CATEGORY Dynamics
    SOURCES
        CEsserProcessor.cpp
        "${CDAW_SOURCE}/Shared/DSP/EsserEngine.cpp"
        "${CDAW_SOURCE}/Shared/Esser/EsserParameters.cpp"
        "${CDAW_SOURCE}/Shared/Esser/EsserView.cpp")
