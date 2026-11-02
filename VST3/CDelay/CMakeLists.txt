# C-Delay as a VST3 plugin (see VST3/Common/CDAWEffect.cmake).
set(CDAW_SOURCE "${CMAKE_SOURCE_DIR}/Source")

cdaw_add_effect_vst3(CDelay
    PRODUCT_NAME "C-Delay"
    PLUGIN_CODE Cdly
    CATEGORY Delay
    SOURCES
        CDelayProcessor.cpp
        "${CDAW_SOURCE}/Shared/DSP/DelayEngine.cpp"
        "${CDAW_SOURCE}/Shared/Delay/DelayParameters.cpp"
        "${CDAW_SOURCE}/Shared/Delay/DelayView.cpp")
