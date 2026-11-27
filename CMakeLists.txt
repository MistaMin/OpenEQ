cmake_minimum_required(VERSION 3.22)

project(OpenEQ VERSION 1.0.3) # visible label: Source/Version.h (1.0.3 Beta)

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

# CLAP wrapper (MIT), pinned; it brings the CLAP headers as submodules.
FetchContent_Declare(
    clap-juce-extensions
    GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
    GIT_TAG 6bde108329ff9fc24404b79b8b397261a75cf6a8
)
FetchContent_MakeAvailable(clap-juce-extensions)

# GoodLookinUI: MIT-licensed procedural hardware UI toolkit (core + JUCE adapter).
set(GOODLOOKINUI_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/GoodLookinUI")
add_subdirectory("${GOODLOOKINUI_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/goodlookinui")

# Optional AAX: the SDK sits next to the repository and is never committed. The build does not install the
# AAX bundle (the host needs it PACE-signed first); it is copied to the build tree only.
set(OPENEQ_AAX_SDK "${CMAKE_CURRENT_SOURCE_DIR}/../aax-sdk-2-8-1" CACHE PATH "Optional AAX SDK")
option(OPENEQ_BUILD_AAX "Build the AAX format when the SDK is available" OFF)
set(OPENEQ_FORMATS AU VST3 LV2 Standalone) # AU is skipped by JUCE outside macOS; CLAP is added below
option(OPENEQ_COPY_PLUGINS "Copy built plug-ins into the user plug-in folders" ON)
if(OPENEQ_BUILD_AAX AND (APPLE OR WIN32) AND EXISTS "${OPENEQ_AAX_SDK}/Interfaces/AAX.h")
    juce_set_aax_sdk_path("${OPENEQ_AAX_SDK}")
    list(APPEND OPENEQ_FORMATS AAX)
elseif(OPENEQ_BUILD_AAX)
    message(WARNING "OPENEQ_BUILD_AAX is ON but no AAX SDK was found at ${OPENEQ_AAX_SDK}; AAX is not built")
endif()

juce_add_plugin(OpenEQ
    COMPANY_NAME "OpenEQ"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD ${OPENEQ_COPY_PLUGINS}
    LV2URI "https://github.com/MistaMin/OpenEQ"
    PLUGIN_MANUFACTURER_CODE Opeq
    PLUGIN_CODE Opq1
    FORMATS ${OPENEQ_FORMATS}
    AAX_CATEGORY AAX_ePlugInCategory_EQ
    AAX_COPY_DIR "${CMAKE_CURRENT_BINARY_DIR}/AAX-not-installed"
    PRODUCT_NAME "OpenEQ")

juce_generate_juce_header(OpenEQ)

clap_juce_extensions_plugin(TARGET OpenEQ
    CLAP_ID "com.opengrid.openeq"
    CLAP_FEATURES audio-effect equalizer)

target_sources(OpenEQ
    PRIVATE
        Source/PluginProcessor.cpp
        Source/InstanceLink.cpp
        Source/ProcessorState.cpp
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
    add_executable(OpenEQRegressionTests Tests/RegressionTests.cpp Tests/FilterResponseTests.cpp)
    target_include_directories(OpenEQRegressionTests PRIVATE
        Source
        $<TARGET_PROPERTY:OpenEQ,INCLUDE_DIRECTORIES>)
    target_compile_definitions(OpenEQRegressionTests PRIVATE
        $<TARGET_PROPERTY:OpenEQ,COMPILE_DEFINITIONS>)
    target_link_libraries(OpenEQRegressionTests PRIVATE OpenEQ)
    if(APPLE)
        # Allocation counting: dyld only honours __interpose from a different image than the caller.
        add_library(OpenEQAllocInterpose SHARED Tests/AllocInterpose.c)
        target_link_libraries(OpenEQRegressionTests PRIVATE OpenEQAllocInterpose)
    endif()
    if(MSVC)
        target_link_options(OpenEQRegressionTests PRIVATE /STACK:33554432)
    endif()
    add_test(NAME OpenEQRegressionTests COMMAND OpenEQRegressionTests)
endif()

# Measurement probes (Tools/): response_probe, response_compare and plugin_probe. They are not tests and are
# not built unless asked for: cmake -B build -DOPENEQ_BUILD_PROBES=ON
option(OPENEQ_BUILD_PROBES "Build the measurement probe tools" OFF)
if(OPENEQ_BUILD_PROBES)
    # response_probe: a tiny JUCE host that measures the installed reference VST3 (docs/FILTER_VALIDATION.md).
    juce_add_console_app(response_probe PRODUCT_NAME "response_probe")
    target_sources(response_probe PRIVATE Tools/response_probe.cpp)
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
    target_sources(response_compare PRIVATE Tools/response_compare.cpp)
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
