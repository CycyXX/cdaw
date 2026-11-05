juce_add_console_app(CDAW_Tests PRODUCT_NAME "CDAW_Tests")

file(GLOB CDAW_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/*.h")
target_sources(CDAW_Tests PRIVATE ${CDAW_TEST_SOURCES})
target_link_libraries(CDAW_Tests PRIVATE cdaw_lib)
target_precompile_headers(CDAW_Tests REUSE_FROM cdaw_lib)
cdaw_apply_compile_options(CDAW_Tests)

# Function names in stack traces, in every build type: HangWatchdogTests checks them, and the hang
# guard in TestMain.cpp prints them.
if(MSVC)
    target_link_options(CDAW_Tests PRIVATE /DEBUG)
endif()

add_test(NAME CDAW_Tests COMMAND CDAW_Tests)
set_tests_properties(CDAW_Tests PROPERTIES TIMEOUT 600)
