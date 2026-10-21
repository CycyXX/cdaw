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

add_subdirectory(Source)

option(CDAW_BUILD_TESTS "Build the CDAW_Tests target" ON)
if(CDAW_BUILD_TESTS)
    enable_testing()
    add_subdirectory(Tests)
endif()
