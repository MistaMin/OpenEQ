cmake_minimum_required(VERSION 3.22)

project(OpenEQ VERSION 1.0.0) # visible label: Source/Version.h (1.0 Beta)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.6
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(JUCE)

# GoodLookinUI: MIT-licensed procedural hardware UI toolkit (core + JUCE adapter).
set(GOODLOOKINUI_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/GoodLookinUI")
add_subdirectory("${GOODLOOKINUI_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/goodlookinui")

juce_add_plugin(OpenEQ
    COMPANY_NAME "OpenEQ"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE Opeq
    PLUGIN_CODE Opq1
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "OpenEQ")

juce_generate_juce_header(OpenEQ)

target_sources(OpenEQ
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/SpectrumAnalyzer.cpp
        Source/EQCurveComponent.cpp
        Source/BandControlPanel.cpp
        Source/DynamicsPanel.cpp
        Source/LinearPhaseEngine.cpp)

target_compile_definitions(OpenEQ
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(OpenEQ
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        goodlookinui_juce
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

include(CTest)
if(BUILD_TESTING)
    # dyld only honours __interpose from a different image than the caller.
    add_library(OpenEQAllocInterpose SHARED Tests/AllocInterpose.c)
    add_executable(OpenEQRegressionTests Tests/RegressionTests.cpp Tests/FilterResponseTests.cpp)
    # The tests stack-allocate multiple processors in one function. Debug does not reuse
    # their stack slots: checkRoadmap alone needs ~34 MB, exceeding the Release 32 MB stack.
    # This only sizes the test runner; plugin binaries and test tolerances are unchanged.
    if (APPLE)
        target_link_options(OpenEQRegressionTests PRIVATE
            "-Wl,-stack_size,$<IF:$<CONFIG:Debug>,0x4000000,0x2000000>")
    endif()
    target_include_directories(OpenEQRegressionTests PRIVATE
        Source
        $<TARGET_PROPERTY:OpenEQ,INCLUDE_DIRECTORIES>)
    target_compile_definitions(OpenEQRegressionTests PRIVATE
        $<TARGET_PROPERTY:OpenEQ,COMPILE_DEFINITIONS>)
    target_link_libraries(OpenEQRegressionTests PRIVATE OpenEQ OpenEQAllocInterpose)
    add_test(NAME OpenEQRegressionTests COMMAND OpenEQRegressionTests)
    # response_probe: a tiny JUCE host that measures the installed reference VST3 (docs/FILTER_VALIDATION.md).
    juce_add_console_app(response_probe PRODUCT_NAME "response_probe")
    target_sources(response_probe PRIVATE scratchpad/response_probe.cpp)
    target_link_libraries(response_probe PRIVATE
        juce::juce_audio_plugin_client
        juce::juce_audio_processors
        juce::juce_audio_basics
        juce::juce_core
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_data_structures
        juce::juce_recommended_config_flags)
    target_compile_definitions(response_probe PRIVATE JUCE_PLUGINHOST_VST3=1)
    juce_generate_juce_header(response_probe)
    # plugin_probe: a black-box host. Sets any VST3's parameters, plays a WAV through it and writes the output.
    juce_add_console_app(plugin_probe PRODUCT_NAME "plugin_probe")
    target_sources(plugin_probe PRIVATE Tools/plugin_probe.cpp)
    target_link_libraries(plugin_probe PRIVATE
        juce::juce_audio_processors
        juce::juce_audio_formats
        juce::juce_audio_basics
        juce::juce_core
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_data_structures
        juce::juce_recommended_config_flags)
    target_compile_definitions(plugin_probe PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
    juce_generate_juce_header(plugin_probe)
    # response_compare: OpenEQ's importer side of a fidelity row, compared against a response_probe measurement.
    juce_add_console_app(response_compare PRODUCT_NAME "response_compare")
    target_sources(response_compare PRIVATE scratchpad/response_compare.cpp)
    target_include_directories(response_compare PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Source)
    target_link_libraries(response_compare PRIVATE
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_audio_basics
        juce::juce_core
        juce::juce_events
        juce::juce_data_structures
        juce::juce_recommended_config_flags)
    juce_generate_juce_header(response_compare)
endif()


