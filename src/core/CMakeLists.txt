# gsp_core: everything that is shared between macOS and Windows.
#
# JUCE modules are compiled once per final target (app, tests), so gsp_core is an
# INTERFACE library: its sources are compiled into whichever target links it.
# The NAM and AIDA-X loaders do not touch JUCE and live in their own static
# libraries, so NAM's Eigen/nlohmann copies never meet RTNeural's in one file.

add_library(gsp_capture_nam STATIC captures/NamCapture.cpp)
target_include_directories(gsp_capture_nam PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# NAM registers its model architectures from static initialisers, so its archive must be
# linked whole or the linker drops them ("No config parser registered for architecture").
target_link_libraries(gsp_capture_nam PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,nam_core>")

add_library(gsp_capture_aida STATIC captures/AidaCapture.cpp)
target_include_directories(gsp_capture_aida PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(gsp_capture_aida PRIVATE RTNeural)

add_library(gsp_core INTERFACE)
target_sources(gsp_core INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/captures/CaptureLoader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/dsp/Resampler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/SignalChain.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/Pedalboard.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/effects/Drives.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/effects/Modulation.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/effects/Delay.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/effects/Reverb.cpp)
target_include_directories(gsp_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(gsp_core INTERFACE
    gsp_capture_nam
    gsp_capture_aida
    juce::juce_audio_basics
    juce::juce_audio_formats
    juce::juce_dsp
    juce::juce_data_structures)
target_compile_definitions(gsp_core INTERFACE
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)
