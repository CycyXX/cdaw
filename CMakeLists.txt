cmake_minimum_required(VERSION 3.25)

project(CDAW VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Static MSVC runtime everywhere (same as Tracktion's examples). Must be set before any target.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
# /Z7: debug info inside the objects, avoids PDB lock contention in parallel builds.
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>")
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# RelWithDebInfo is the build that ships (dist\CDAW): full optimisation and inlining (CMake's
# default is /Ob1), and a non-incremental link so /OPT:REF and /OPT:ICF can drop and fold code.
# Debug info stays (/Z7 + /DEBUG) so crash reports can name functions.
if(MSVC)
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "/O2 /Ob2 /DNDEBUG")
    set(CMAKE_C_FLAGS_RELWITHDEBINFO "/O2 /Ob2 /DNDEBUG")
    set(CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO "/DEBUG /INCREMENTAL:NO /OPT:REF /OPT:ICF")
    set(CMAKE_MODULE_LINKER_FLAGS_RELWITHDEBINFO "/DEBUG /INCREMENTAL:NO /OPT:REF /OPT:ICF")     # the VST3 plugins
    set(CMAKE_SHARED_LINKER_FLAGS_RELWITHDEBINFO "/DEBUG /INCREMENTAL:NO /OPT:REF /OPT:ICF")
endif()

# Tracktion's module translation units are huge and use a lot of RAM: cap their parallelism.
set_property(GLOBAL APPEND PROPERTY JOB_POOLS heavy=4)

set(TE_ADD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(TE_ADD_TESTS OFF CACHE BOOL "" FORCE)

set(TE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/tracktion_engine")
if(NOT EXISTS "${TE_DIR}/modules/juce/CMakeLists.txt")
    message(FATAL_ERROR "Tracktion Engine / JUCE sources missing. Run scripts\\bootstrap.ps1 first.")
endif()

add_subdirectory("${TE_DIR}" tracktion_engine EXCLUDE_FROM_ALL)

include(cmake/CDAWModules.cmake)

# The C- plugins live in their own repository, as a submodule. CDAW compiles their shared code into its
# built-in effects (Source/CMakeLists.txt). It also builds their VST3s, using the JUCE Tracktion brings, because
# its tests compare against them. It doesn't install them: cdaw-plugins does that, with its scripts\install.ps1.
set(CDAW_PLUGINS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cdaw-plugins")
if(NOT EXISTS "${CDAW_PLUGINS_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "The plugins are missing. Run scripts\\bootstrap.ps1 first.")
endif()

add_subdirectory(Source)
add_subdirectory("${CDAW_PLUGINS_DIR}" cdaw-plugins)

option(CDAW_BUILD_TESTS "Build the CDAW_Tests target" ON)
if(CDAW_BUILD_TESTS)
    enable_testing()
    add_subdirectory(Tests)
endif()
