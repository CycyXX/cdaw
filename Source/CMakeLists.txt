# cdaw_lib: all of CDAW's code except the application entry point.
# Shared by the CDAW app and CDAW_Tests.
file(GLOB_RECURSE CDAW_LIB_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h")
list(REMOVE_ITEM CDAW_LIB_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/App/Main.cpp")

add_library(cdaw_lib STATIC ${CDAW_LIB_SOURCES})
target_link_libraries(cdaw_lib PUBLIC cdaw_modules)
target_include_directories(cdaw_lib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_precompile_headers(cdaw_lib PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/PCH.h")
cdaw_apply_compile_options(cdaw_lib)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${CDAW_LIB_SOURCES})

# The application
juce_add_gui_app(CDAW
    PRODUCT_NAME "CDAW"
    COMPANY_NAME "Cyrill Durrer"
    VERSION ${PROJECT_VERSION}
    DOCUMENT_EXTENSIONS tracktionedit
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
