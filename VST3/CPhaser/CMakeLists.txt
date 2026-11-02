# C-Phaser as a VST3 plugin (see VST3/Common/CDAWEffect.cmake).
set(CDAW_SOURCE "${CMAKE_SOURCE_DIR}/Source")

cdaw_add_effect_vst3(CPhaser
    PRODUCT_NAME "C-Phaser"
    PLUGIN_CODE Cphs
    CATEGORY Modulation
    SOURCES
        CPhaserProcessor.cpp
        "${CDAW_SOURCE}/Shared/DSP/PhaserEngine.cpp"
        "${CDAW_SOURCE}/Shared/Phaser/PhaserParameters.cpp"
        "${CDAW_SOURCE}/Shared/Phaser/PhaserView.cpp")
