# C-Multicomp as a VST3 plugin (for Cubase and other hosts).
# Its DSP, parameters and editor are the same source files CDAW's built-in C-Multicomp compiles
# (Source/Shared), so a change to the compressor lands in both. Only CMulticompProcessor.* is VST3-specific.

juce_add_plugin(CMulticomp
    PRODUCT_NAME "C-Multicomp"
    COMPANY_NAME "Cyrill Durrer"
    VERSION ${PROJECT_VERSION}
    PLUGIN_MANUFACTURER_CODE Cdaw
    PLUGIN_CODE Cmcp
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
set(CMULTICOMP_SHARED_SOURCES
    "${CDAW_SOURCE}/Shared/DSP/AnalyserFifo.cpp"
    "${CDAW_SOURCE}/Shared/DSP/MultibandEngine.cpp"
    "${CDAW_SOURCE}/Shared/Multiband/MultibandParameters.cpp"
    "${CDAW_SOURCE}/Shared/Multiband/MultibandView.cpp"
    "${CDAW_SOURCE}/Shared/UI/ParamKnob.cpp"
    "${CDAW_SOURCE}/UI/LookAndFeel/CDAWLookAndFeel.cpp")

target_sources(CMulticomp PRIVATE
    CMulticompProcessor.cpp
    CMulticompProcessor.h
    ${CMULTICOMP_SHARED_SOURCES})

target_include_directories(CMulticomp PRIVATE "${CDAW_SOURCE}")

target_compile_definitions(CMulticomp PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(CMulticomp
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

set_target_properties(CMulticomp PROPERTIES FOLDER "Plugins")
