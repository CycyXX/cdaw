# C-Filter as a VST3 plugin (see VST3/Common/CDAWEffect.cmake).
set(CDAW_SOURCE "${CMAKE_SOURCE_DIR}/Source")

cdaw_add_effect_vst3(CFilter
    PRODUCT_NAME "C-Filter"
    PLUGIN_CODE Cflt
    CATEGORY Filter
    SOURCES
        CFilterProcessor.cpp
        "${CDAW_SOURCE}/Shared/DSP/FilterEngine.cpp"
        "${CDAW_SOURCE}/Shared/Filter/FilterParameters.cpp"
        "${CDAW_SOURCE}/Shared/Filter/FilterView.cpp")
