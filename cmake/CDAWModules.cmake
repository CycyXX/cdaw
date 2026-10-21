# cdaw_modules: compiles JUCE + Tracktion Engine once, shared by the app and the tests.
# Pattern documented by JUCE ("Using JUCE in a static library"): link the modules PRIVATE,
# then re-export their include directories and compile definitions through the INTERFACE.

add_library(cdaw_modules STATIC)

target_link_libraries(cdaw_modules
    PRIVATE
        tracktion::tracktion_core
        tracktion::tracktion_graph
        tracktion::tracktion_engine
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_core
        juce::juce_data_structures
        juce::juce_dsp
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
        juce::juce_osc
    PUBLIC
        juce::juce_recommended_config_flags)

target_compile_definitions(cdaw_modules
    PUBLIC
        JUCE_STANDALONE_APPLICATION=1
        JUCE_APPLICATION_NAME_STRING="CDAW"
        JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}"
        JUCE_PLUGINHOST_VST3=1
        JUCE_PLUGINHOST_VST=0
        JUCE_PLUGINHOST_AU=0
        JUCE_PLUGINHOST_LADSPA=0
        JUCE_PLUGINHOST_LV2=0
        JUCE_ASIO=1
        JUCE_WASAPI=1
        JUCE_DIRECTSOUND=0
        JUCE_USE_FLAC=1
        JUCE_USE_OGGVORBIS=1
        JUCE_USE_MP3AUDIOFORMAT=1
        JUCE_USE_LAME_AUDIO_FORMAT=0
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_STRICT_REFCOUNTEDPOINTER=1
        JUCE_USE_CURL=0
        JUCE_WEB_BROWSER=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_SILENCE_XCODE_15_LINKER_WARNING=1
        TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH=1
        TRACKTION_ENABLE_LIBLAME=1
    INTERFACE
        $<TARGET_PROPERTY:cdaw_modules,COMPILE_DEFINITIONS>)

target_include_directories(cdaw_modules
    INTERFACE
        $<TARGET_PROPERTY:cdaw_modules,INCLUDE_DIRECTORIES>)

if(MSVC)
    target_compile_options(cdaw_modules PRIVATE /bigobj /utf-8 /MP1 /W0)
    set_target_properties(cdaw_modules PROPERTIES JOB_POOL_COMPILE heavy)
endif()

set_target_properties(cdaw_modules PROPERTIES FOLDER "ThirdParty")

# Compile options for CDAW's own code (not applied to JUCE/Tracktion sources).
function(cdaw_apply_compile_options target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8 /bigobj
            /wd4100  # unreferenced parameter
            /wd4127  # conditional expression is constant
            /wd4244  # conversion, possible loss of data (juce-style float/int code)
            /wd4267  # size_t conversion
            /wd4458) # declaration hides class member
    endif()
endfunction()
