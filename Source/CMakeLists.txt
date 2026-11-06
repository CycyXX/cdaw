# cdaw_lib: all of CDAW's code except the application entry point.
# Shared by the CDAW app and CDAW_Tests.
file(GLOB_RECURSE CDAW_LIB_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h")
list(REMOVE_ITEM CDAW_LIB_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/App/Main.cpp")

# The plugins' shared code (engines, parameters, views), compiled in for the built-in effects.
file(GLOB_RECURSE CDAW_PLUGIN_SOURCES CONFIGURE_DEPENDS
    "${CDAW_PLUGINS_DIR}/Shared/*.cpp"
    "${CDAW_PLUGINS_DIR}/Shared/*.h")

# Images compiled into the exe (splash screen logo).
juce_add_binary_data(cdaw_binary
    NAMESPACE CDAWBinary
    HEADER_NAME CDAWBinary.h
    SOURCES "${CMAKE_SOURCE_DIR}/resources/logo/splash_logo.png")

add_library(cdaw_lib STATIC ${CDAW_LIB_SOURCES} ${CDAW_PLUGIN_SOURCES})
target_link_libraries(cdaw_lib PUBLIC cdaw_modules cdaw_binary)
target_include_directories(cdaw_lib PUBLIC "${CDAW_PLUGINS_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}")

# The plugins' commit this build compiles in (PluginsVersion.h), checked at every build because the
# submodule can move without CMake noticing. The header only changes when the commit does.
set(CDAW_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
add_custom_target(cdaw_plugins_version
    COMMAND "${CMAKE_COMMAND}" "-DREPO=${CDAW_PLUGINS_DIR}" "-DOUT=${CDAW_GENERATED_DIR}/PluginsVersion.h"
            -P "${CMAKE_SOURCE_DIR}/cmake/WritePluginsVersion.cmake"
    BYPRODUCTS "${CDAW_GENERATED_DIR}/PluginsVersion.h"
    VERBATIM)
add_dependencies(cdaw_lib cdaw_plugins_version)
target_include_directories(cdaw_lib PRIVATE "${CDAW_GENERATED_DIR}")
target_precompile_headers(cdaw_lib PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/PCH.h")
cdaw_apply_compile_options(cdaw_lib)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${CDAW_LIB_SOURCES})
source_group(TREE "${CDAW_PLUGINS_DIR}" PREFIX "Plugins" FILES ${CDAW_PLUGIN_SOURCES})

# The application
juce_add_gui_app(CDAW
    PRODUCT_NAME "CDAW"
    COMPANY_NAME "Cyrill Durrer"
    # JUCE would make it from COMPANY_NAME, but a bundle ID can't have a space. Only macOS reads it.
    BUNDLE_ID "com.CyrillDurrer.CDAW"
    VERSION ${PROJECT_VERSION}
    DOCUMENT_EXTENSIONS cdaw
    ICON_BIG "${CMAKE_SOURCE_DIR}/resources/logo/cdaw-logo-1024.png"
    ICON_SMALL "${CMAKE_SOURCE_DIR}/resources/logo/cdaw-logo-small.png"
    NEEDS_CURL FALSE
    NEEDS_WEB_BROWSER FALSE)

target_sources(CDAW PRIVATE App/Main.cpp)
target_link_libraries(CDAW PRIVATE cdaw_lib)
target_precompile_headers(CDAW REUSE_FROM cdaw_lib)
cdaw_apply_compile_options(CDAW)

# Ship the drum maps next to the executable.
add_custom_command(TARGET CDAW POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/resources/drummaps"
        "$<TARGET_FILE_DIR:CDAW>/DrumMaps")
