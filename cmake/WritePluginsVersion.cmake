# Writes the plugins' current commit to OUT, for Plugins > Check for Plugin Updates, which compares it
# with cdaw-plugins' main. Run at every build (cmake -DREPO=<plugins> -DOUT=<header> -P ...), and only
# touches OUT when the commit changes, so nothing recompiles otherwise.
execute_process(COMMAND git -C "${REPO}" rev-parse HEAD
    OUTPUT_VARIABLE commit
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE result)

if(NOT result EQUAL 0)
    set(commit "")   # no git, or not a checkout: the check falls back to the submodule's commit
endif()

set(content "#pragma once\n// Written by cmake/WritePluginsVersion.cmake at build time.\n#define CDAW_PLUGINS_COMMIT \"${commit}\"\n")

set(old "")
if(EXISTS "${OUT}")
    file(READ "${OUT}" old)
endif()

if(NOT old STREQUAL content)
    file(WRITE "${OUT}" "${content}")
endif()
