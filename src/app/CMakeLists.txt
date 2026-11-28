juce_add_gui_app(GuitarStudioPro
    PRODUCT_NAME                "Guitar Studio Pro"
    COMPANY_NAME                "MontsPlugin"
    BUNDLE_ID                   com.montsplugin.guitarstudiopro
    VERSION                     ${PROJECT_VERSION}
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT  "O Guitar Studio Pro precisa da entrada de audio para receber o sinal da sua guitarra."
    HARDENED_RUNTIME_ENABLED    TRUE
    HARDENED_RUNTIME_OPTIONS    com.apple.security.device.audio-input
    ICON_BIG                    "${CMAKE_SOURCE_DIR}/resources/icon.png")

target_sources(GuitarStudioPro PRIVATE
    Main.cpp
    MainComponent.cpp
    CaptureBrowser.cpp
    PedalboardView.cpp
    StudioView.cpp)

target_compile_definitions(GuitarStudioPro PRIVATE
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:GuitarStudioPro,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:GuitarStudioPro,JUCE_VERSION>"
    JUCE_ASIO=$<IF:$<PLATFORM_ID:Windows>,1,0>
    JUCE_WASAPI=1
    JUCE_DIRECTSOUND=0
    JUCE_JACK=0)

target_link_libraries(GuitarStudioPro PRIVATE
    gsp_core
    juce::juce_audio_utils
    juce::juce_audio_devices
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)
