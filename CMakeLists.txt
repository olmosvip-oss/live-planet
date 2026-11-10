cmake_minimum_required(VERSION 3.22)
project(LivePlanet VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary")
endif()

add_subdirectory(libs/JUCE)
add_subdirectory(libs/clap-juce-extensions EXCLUDE_FROM_ALL)

set(LP_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND LP_FORMATS AU)
endif()

set(LP_ENGINE_SOURCES
    Source/engine/Engine.cpp
)

option(LP_BUILD_PLUGIN "Build the plugin" ON)
if(LP_BUILD_PLUGIN)
juce_add_plugin(LivePlanet
    PRODUCT_NAME "Live Planet"
    COMPANY_NAME "Live Planet Audio"
    BUNDLE_ID "com.liveplanetaudio.liveplanet"
    PLUGIN_MANUFACTURER_CODE Lpla
    PLUGIN_CODE Lpln
    FORMATS ${LP_FORMATS}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Synth Generator
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    MICROPHONE_PERMISSION_ENABLED FALSE)

target_sources(LivePlanet PRIVATE
    ${LP_ENGINE_SOURCES}
    Source/plugin/PluginProcessor.cpp
    Source/plugin/PluginEditor.cpp
    Source/plugin/WeatherService.cpp)

target_compile_definitions(LivePlanet PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=1
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(LivePlanet
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_formats
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(UNIX AND NOT APPLE)
    find_package(CURL REQUIRED)
    target_link_libraries(LivePlanet PRIVATE CURL::libcurl)
endif()

clap_juce_extensions_plugin(TARGET LivePlanet
    CLAP_ID "com.liveplanetaudio.liveplanet"
    CLAP_FEATURES instrument synthesizer ambient)
endif()

# Command-line tool that renders example skies to WAV files (for testing the sound)
juce_add_console_app(RenderDemo PRODUCT_NAME "RenderDemo")
target_sources(RenderDemo PRIVATE tools/RenderDemo.cpp ${LP_ENGINE_SOURCES})
target_compile_definitions(RenderDemo PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(RenderDemo PRIVATE juce::juce_audio_formats juce::juce_audio_basics
    PUBLIC juce::juce_recommended_config_flags)
