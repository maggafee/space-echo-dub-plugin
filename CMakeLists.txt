cmake_minimum_required(VERSION 3.22)

project(DubascoTapeEcho VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    # Universal binary (Apple Silicon + Intel) unless the caller overrides it
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    endif()
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
endif()

# JUCE: use a local checkout if JUCE_DIR is given / present, otherwise fetch it.
set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/JUCE" CACHE PATH "Path to a JUCE checkout")
if(EXISTS "${JUCE_DIR}/CMakeLists.txt")
    add_subdirectory("${JUCE_DIR}" JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.15
        GIT_SHALLOW ON)
    FetchContent_MakeAvailable(JUCE)
endif()

set(DUBASCO_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND DUBASCO_FORMATS AU)
endif()

juce_add_plugin(DubascoTapeEcho
    COMPANY_NAME "Dubasco"
    COMPANY_WEBSITE "https://dubasco.audio"
    BUNDLE_ID "audio.dubasco.tapeecho"
    PLUGIN_MANUFACTURER_CODE Dbsc
    PLUGIN_CODE DbTe
    PRODUCT_NAME "Dubasco Tape Echo"
    DESCRIPTION "70s tape echo with spring reverb for dub"
    FORMATS ${DUBASCO_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE          # MIDI learn: also makes the AU a "MIDI-controlled effect" in Logic
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Delay Reverb
    AU_MAIN_TYPE kAudioUnitType_MusicEffect
    HARDENED_RUNTIME_ENABLED TRUE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "The standalone version of Dubasco Tape Echo can process your audio input.")

juce_generate_juce_header(DubascoTapeEcho)

target_sources(DubascoTapeEcho PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Parameters.cpp
    Source/MidiLearn.cpp
    Source/PresetManager.cpp
    Source/dsp/TapeEchoEngine.cpp
    Source/gui/Controls.cpp
    Source/gui/TopBar.cpp)

target_include_directories(DubascoTapeEcho PRIVATE Source)

juce_add_binary_data(DubascoAssets SOURCES
    Resources/images/background.jpg
    Resources/images/reel_a.png
    Resources/images/reel_b.png
    Resources/images/knob_large.png
    Resources/images/knob_medium.png
    Resources/images/knob_small.png
    Resources/images/lamp_on.png
    Resources/images/lamp_off.png
    Resources/images/lever_base.png
    Resources/images/lever_arm.png
    Resources/images/toggle_bat.png
    Resources/images/toggle_nut.png
    Resources/images/jack.png
    Resources/fonts/Oswald-SemiBold.ttf
    Resources/fonts/Oswald-Medium.ttf)

target_compile_definitions(DubascoTapeEcho PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_VST3_EMULATE_MIDI_CC_WITH_PARAMETERS=1)

target_link_libraries(DubascoTapeEcho
    PRIVATE
        DubascoAssets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Offline render tool used for testing the DSP without a host
option(DUBASCO_BUILD_TESTS "Build the offline DSP test renderer" OFF)
if(DUBASCO_BUILD_TESTS)
    add_executable(DubascoRender tests/RenderTest.cpp Source/dsp/TapeEchoEngine.cpp)
    target_include_directories(DubascoRender PRIVATE Source)
    target_compile_features(DubascoRender PRIVATE cxx_std_17)
endif()

if(DUBASCO_BUILD_TESTS)
    juce_add_console_app(DubascoSnapshot PRODUCT_NAME "DubascoSnapshot")
    target_sources(DubascoSnapshot PRIVATE tests/Snapshot.cpp)
    target_include_directories(DubascoSnapshot PRIVATE Source "${CMAKE_BINARY_DIR}/DubascoTapeEcho_artefacts/JuceLibraryCode")
    target_link_libraries(DubascoSnapshot PRIVATE DubascoTapeEcho DubascoAssets juce::juce_audio_utils juce::juce_dsp)
    target_compile_definitions(DubascoSnapshot PRIVATE JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1)
endif()
