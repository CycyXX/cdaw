# Builds one of CDAW's shared effects (C-Delay, C-Chorus, C-Phaser, C-Filter) as a VST3 for Cubase and
# other hosts. Its DSP, parameters and editor are the same source files CDAW's built-in plugin compiles
# (Source/Shared), so a change to the effect lands in both; only its processor file and
# VST3/Common/SharedEffectProcessor.h are VST3-specific.
#
#   cdaw_add_effect_vst3(CDelay PRODUCT_NAME "C-Delay" PLUGIN_CODE Cdly CATEGORY Delay SOURCES ...)
#   cdaw_add_effect_vst3(CSynth SYNTH PRODUCT_NAME "C-Synth" ...)      an instrument: MIDI in, no audio in

function(cdaw_add_effect_vst3 target)
    cmake_parse_arguments(FX "SYNTH" "PRODUCT_NAME;PLUGIN_CODE;CATEGORY" "SOURCES" ${ARGN})

    if(FX_SYNTH)
        set(is_synth TRUE)
        set(categories Instrument ${FX_CATEGORY})
    else()
        set(is_synth FALSE)
        set(categories Fx ${FX_CATEGORY})
    endif()

    juce_add_plugin(${target}
        PRODUCT_NAME "${FX_PRODUCT_NAME}"
        COMPANY_NAME "Cyrill Durrer"
        VERSION ${PROJECT_VERSION}
        PLUGIN_MANUFACTURER_CODE Cdaw
        PLUGIN_CODE ${FX_PLUGIN_CODE}
        FORMATS VST3
        IS_SYNTH ${is_synth}
        NEEDS_MIDI_INPUT ${is_synth}
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        VST3_CATEGORIES ${categories}
        COPY_PLUGIN_AFTER_BUILD FALSE)

    set(CDAW_SOURCE "${CMAKE_SOURCE_DIR}/Source")

    # The shared code (none of it uses Tracktion), then what every effect's window needs.
    target_sources(${target} PRIVATE
        ${FX_SOURCES}
        "${CMAKE_SOURCE_DIR}/VST3/Common/SharedEffectProcessor.h"
        "${CDAW_SOURCE}/Shared/DSP/AnalyserFifo.cpp"
        "${CDAW_SOURCE}/Shared/UI/EffectWidgets.cpp"
        "${CDAW_SOURCE}/Shared/UI/ParamKnob.cpp"
        "${CDAW_SOURCE}/UI/LookAndFeel/CDAWLookAndFeel.cpp")

    target_include_directories(${target} PRIVATE "${CDAW_SOURCE}" "${CMAKE_SOURCE_DIR}/VST3")

    target_compile_definitions(${target} PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

    target_link_libraries(${target}
        PRIVATE
            juce::juce_audio_utils
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    set_target_properties(${target} PROPERTIES FOLDER "Plugins")
endfunction()
