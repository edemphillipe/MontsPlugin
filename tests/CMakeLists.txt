# Test assets fetched at configure time (not committed):
#  - NAM example models ship with NeuralAmpModelerCore
#  - one public AIDA-X model from the AIDA-X repository
set(GSP_TEST_AIDA_MODEL "${CMAKE_CURRENT_BINARY_DIR}/assets/aidax_tw40_clean.json")
if(NOT EXISTS "${GSP_TEST_AIDA_MODEL}")
    file(DOWNLOAD
        "https://raw.githubusercontent.com/AidaDSP/AIDA-X/1.1.0/files/tw40_california_clean_deerinkstudios.json"
        "${GSP_TEST_AIDA_MODEL}" STATUS _aida_status)
    list(GET _aida_status 0 _aida_code)
    if(NOT _aida_code EQUAL 0)
        message(WARNING "Could not download the AIDA-X test model; AIDA-X tests will be skipped.")
        file(REMOVE "${GSP_TEST_AIDA_MODEL}")
    endif()
endif()

# Reference implementations, each calling the upstream library directly.
add_library(gsp_reference_nam STATIC reference/ReferenceNam.cpp)
target_link_libraries(gsp_reference_nam PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,nam_core>")
add_library(gsp_reference_aida STATIC reference/ReferenceAida.cpp)
target_link_libraries(gsp_reference_aida PRIVATE RTNeural)

juce_add_console_app(gsp_tests PRODUCT_NAME "gsp_tests")
target_sources(gsp_tests PRIVATE CoreTests.cpp)
target_include_directories(gsp_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(gsp_tests PRIVATE
    GSP_NAM_EXAMPLES="${GSP_NAM_EXAMPLE_MODELS}"
    GSP_AIDA_MODEL="${GSP_TEST_AIDA_MODEL}"
    JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
target_link_libraries(gsp_tests PRIVATE
    gsp_core gsp_reference_nam gsp_reference_aida
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

add_test(NAME core COMMAND gsp_tests)
