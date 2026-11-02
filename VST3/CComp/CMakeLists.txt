# C-Comp as a VST3 plugin (for Cubase and other hosts).
# Its DSP, parameters and editor are the same source files CDAW's built-in C-Comp compiles
# (Source/Shared), so a change to the compressor lands in both. Only CCompProcessor.* is VST3-specific.

juce_add_plugin(CComp
    PRODUCT_NAME "C-Comp"
    COMPANY_NAME "Cyrill Durrer"
    VERSION ${PROJECT_VERSION}
    PLUGIN_MANUFACTURER_CODE Cdaw
    PLUGIN_CODE Ccmp
    FORMATS VST3
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Fx Dynamics
    COPY_PLUGIN_AFTER_BUILD FALSE)

set(CDAW_SOURCE "${CMAKE_SOURCE_DIR}/Source")

# The shared code: the only CDAW sources the plugin needs (none of them use Tracktion).
set(CCOMP_SHARED_SOURCES
    "${CDAW_SOURCE}/Shared/DSP/CompressorEngine.cpp"
    "${CDAW_SOURCE}/Shared/Compressor/CompressorParameters.cpp"
    "${CDAW_SOURCE}/Shared/Compressor/CompressorView.cpp"
    "${CDAW_SOURCE}/Shared/UI/ParamKnob.cpp"
    "${CDAW_SOURCE}/UI/LookAndFeel/CDAWLookAndFeel.cpp")

target_sources(CComp PRIVATE
    CCompProcessor.cpp
    CCompProcessor.h
    ${CCOMP_SHARED_SOURCES})

target_include_directories(CComp PRIVATE "${CDAW_SOURCE}")

target_compile_definitions(CComp PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(CComp
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

set_target_properties(CComp PROPERTIES FOLDER "Plugins")
