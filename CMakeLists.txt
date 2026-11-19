cmake_minimum_required(VERSION 3.22)

# build.cmd builds both. cmake --build on its own only builds Debug, unless --config says otherwise.
# Set before project(), which reads it.
set(CMAKE_CONFIGURATION_TYPES Debug Release CACHE STRING "" FORCE)

project(CrateSnip VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)

option(CRATE_SNIP_BUILD_TESTS "Build the CrateSnip tests" OFF)

add_subdirectory(libs/JUCE)

juce_add_gui_app(CrateSnip
    COMPANY_NAME "Jack"
    PRODUCT_NAME "CrateSnip"
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE)

target_sources(CrateSnip
    PRIVATE
        src/FoobarRemote.cpp
        src/FoobarRemote.h
        src/EditorComponent.cpp
        src/EditorComponent.h
        src/LaunchRequest.cpp
        src/LaunchRequest.h
        src/Main.cpp
        src/RegionAudio.cpp
        src/RegionAudio.h
        src/WaveformView.cpp
        src/WaveformView.h)

target_compile_definitions(CrateSnip
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:CrateSnip,JUCE_PRODUCT_NAME>"
        JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:CrateSnip,JUCE_VERSION>")

target_link_libraries(CrateSnip
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(CRATE_SNIP_BUILD_TESTS)
    juce_add_console_app(CrateSnipTests PRODUCT_NAME "CrateSnipTests")

    target_sources(CrateSnipTests
        PRIVATE
            src/LaunchRequest.cpp
            src/RegionAudio.cpp
            tests/LaunchRequestTests.cpp
            tests/RegionAudioTests.cpp)

    target_include_directories(CrateSnipTests PRIVATE src)

    target_compile_definitions(CrateSnipTests
        PRIVATE
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0)

    target_link_libraries(CrateSnipTests
        PRIVATE
            juce::juce_audio_formats
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endif()
