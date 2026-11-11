cmake_minimum_required(VERSION 3.22)
project(ClarityStrip VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- JUCE ---------------------------------------------------------------------
# Use a local JUCE checkout with  -DJUCE_DIR=/path/to/JUCE , otherwise it is downloaded.
if(DEFINED JUCE_DIR)
    add_subdirectory(${JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.4
        GIT_SHALLOW    ON)
    FetchContent_MakeAvailable(JUCE)
endif()

if(APPLE)
    set(CLARITY_FORMATS VST3 AU Standalone)
else()
    set(CLARITY_FORMATS VST3 Standalone)
endif()

juce_add_plugin(ClarityStrip
    COMPANY_NAME              "OM Audio"
    BUNDLE_ID                 "com.omaudio.claritystrip"
    PLUGIN_MANUFACTURER_CODE  Omau
    PLUGIN_CODE               Clst
    FORMATS                   ${CLARITY_FORMATS}
    PRODUCT_NAME              "ClarityStrip"
    DESCRIPTION               "One-knob dynamics, EQ, compressor, limiter, enhancer, noise remover + auto clean"
    IS_SYNTH                  FALSE
    NEEDS_MIDI_INPUT          FALSE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD   FALSE
    VST3_CATEGORIES           Fx Dynamics EQ Restoration
    AU_MAIN_TYPE              kAudioUnitType_Effect)

target_sources(ClarityStrip PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_include_directories(ClarityStrip PRIVATE Source)

target_compile_definitions(ClarityStrip PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(ClarityStrip
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# --- Offline DSP tests (no JUCE needed) ---------------------------------------
option(CLARITY_BUILD_TESTS "Build the offline DSP test" ON)
if(CLARITY_BUILD_TESTS)
    add_executable(ClarityDSPTest tests/offline_test.cpp)
    target_include_directories(ClarityDSPTest PRIVATE Source)
endif()
