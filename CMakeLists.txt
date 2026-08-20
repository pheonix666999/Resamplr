cmake_minimum_required(VERSION 3.28)

include(cmake/ProductConfig.cmake)
project(
    PadFlow
    VERSION ${PADFLOW_VERSION}
    DESCRIPTION "Standalone and plug-in sampler and drum machine"
    LANGUAGES C CXX)

option(PADFLOW_BUILD_APP "Build the PadFlow desktop application" ON)
option(PADFLOW_BUILD_PLUGIN "Build the PadFlow VST3 and Apple Audio Unit plug-ins" ON)
option(PADFLOW_BUILD_TESTS "Build PadFlow tests and smoke tools" ON)
option(PADFLOW_ENABLE_FFMPEG "Enable optional user-installed FFmpeg discovery" OFF)
option(PADFLOW_ENABLE_ASIO "Enable separately licensed ASIO SDK integration" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

include(cmake/CompilerWarnings.cmake)
add_subdirectory(external/JUCE SYSTEM)

configure_file(
    Source/App/ProductInfoGenerated.h.in
    "${CMAKE_CURRENT_BINARY_DIR}/generated/ProductInfoGenerated.h"
    @ONLY)

add_library(
    padflow_core STATIC
    Source/App/ProductInfo.cpp
    Source/App/ProductInfo.h
    Source/App/ApplicationController.cpp
    Source/App/ApplicationController.h
    Source/Chopping/ChoppingSession.cpp
    Source/Chopping/ChoppingSession.h
    Source/Chopping/AssignmentPlan.cpp
    Source/Chopping/AssignmentPlan.h
    Source/Chopping/LazyMarkerCapture.cpp
    Source/Chopping/LazyMarkerCapture.h
    Source/Chopping/SliceModel.cpp
    Source/Chopping/SliceModel.h
    Source/Chopping/TransientAnalysis.cpp
    Source/Chopping/TransientAnalysis.h
    Source/Model/MusicalTime.cpp
    Source/Model/MusicalTime.h
    Source/Model/PadModel.cpp
    Source/Model/PadModel.h
    Source/Model/ProductModelConstants.h
    Source/Model/Project.cpp
    Source/Model/Project.h
    Source/Sequencing/PatternModel.cpp
    Source/Sequencing/PatternModel.h
    Source/Sequencing/Probability.cpp
    Source/Sequencing/Probability.h
    Source/Sampling/SampleAsset.cpp
    Source/Sampling/SampleAsset.h
    Source/Sampling/WaveformCache.cpp
    Source/Sampling/WaveformCache.h
    Source/Serialization/ProjectSerializer.cpp
    Source/Serialization/ProjectSerializer.h
    Source/Utilities/BackgroundJobSystem.cpp
    Source/Utilities/BackgroundJobSystem.h)
target_include_directories(padflow_core PUBLIC Source "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(padflow_core PUBLIC juce::juce_core)
target_compile_definitions(padflow_core PUBLIC JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
padflow_enable_project_warnings(padflow_core)
padflow_set_output_directory(padflow_core)

add_library(
    padflow_audio STATIC
    Source/Audio/AudioCommandQueue.h
    Source/Audio/AudioRuntime.cpp
    Source/Audio/AudioRuntime.h
    Source/Audio/CaptureWriter.cpp
    Source/Audio/CaptureWriter.h
    Source/Audio/PlaybackEngine.cpp
    Source/Audio/PlaybackEngine.h
    Source/Audio/PlaybackStatePublisher.cpp
    Source/Audio/PlaybackStatePublisher.h
    Source/Audio/PreviewPlayer.cpp
    Source/Audio/PreviewPlayer.h
    Source/Audio/SliceAuditionController.cpp
    Source/Audio/SliceAuditionController.h
    Source/Sequencing/Transport.cpp
    Source/Sequencing/Transport.h
    Source/Sequencing/Scheduler.cpp
    Source/Sequencing/Scheduler.h
    Source/Sequencing/PatternRecorder.cpp
    Source/Sequencing/PatternRecorder.h
    Source/Sequencing/SequencerStatePublisher.cpp
    Source/Sequencing/SequencerStatePublisher.h
    Source/Input/InputRouter.cpp
    Source/Input/InputRouter.h
    Source/Sampling/SampleImporter.cpp
    Source/Sampling/SampleImporter.h
    Source/Sampling/DerivedAsset.cpp
    Source/Sampling/DerivedAsset.h
    Source/Sampling/RecordedAsset.cpp
    Source/Sampling/RecordedAsset.h
    Source/Sampling/SamplePreviewController.cpp
    Source/Sampling/SamplePreviewController.h
    Source/Smoke/SmokeScenario.cpp
    Source/Smoke/SmokeScenario.h)
target_include_directories(padflow_audio PUBLIC Source)
target_link_libraries(
    padflow_audio
    PUBLIC padflow_core
           juce::juce_audio_basics
           juce::juce_audio_devices
           juce::juce_audio_formats)
padflow_enable_project_warnings(padflow_audio)
padflow_set_output_directory(padflow_audio)

juce_add_binary_data(
    padflow_brand_assets
    HEADER_NAME PadFlowBrandAssets.h
    NAMESPACE padflow::BrandAssets
    SOURCES logo.gif)

add_library(
    padflow_ui STATIC
    Source/App/AnimatedLogoComponent.cpp
    Source/App/AnimatedLogoComponent.h
    Source/App/ChoppingWorkspace.cpp
    Source/App/ChoppingWorkspace.h
    Source/App/MainWindow.cpp
    Source/App/MainWindow.h
    Source/App/RecordingPanel.cpp
    Source/App/RecordingPanel.h
    Source/App/SamplerView.cpp
    Source/App/SamplerView.h
    Source/App/SequencerWorkspace.cpp
    Source/App/SequencerWorkspace.h
    Source/App/WaveformEditor.cpp
    Source/App/WaveformEditor.h)
target_include_directories(padflow_ui PUBLIC Source)
target_link_libraries(
    padflow_ui
    PUBLIC padflow_audio juce::juce_gui_basics
    PRIVATE padflow_brand_assets)
padflow_enable_project_warnings(padflow_ui)
padflow_set_output_directory(padflow_ui)

if(PADFLOW_BUILD_APP)
    juce_add_gui_app(
        PadFlow
        PRODUCT_NAME "${PADFLOW_PRODUCT_NAME}"
        COMPANY_NAME "${PADFLOW_COMPANY_NAME}"
        BUNDLE_ID "${PADFLOW_BUNDLE_ID}"
        VERSION "${PADFLOW_VERSION}"
        ICON_BIG logo.gif
        ICON_SMALL logo.gif
        MICROPHONE_PERMISSION_ENABLED TRUE
        MICROPHONE_PERMISSION_TEXT
            "PadFlow uses audio input only after you explicitly enable an input device.")
    target_sources(PadFlow PRIVATE Source/App/Application.cpp Source/App/Application.h Source/App/Main.cpp)
    target_link_libraries(
        PadFlow
        PRIVATE padflow_audio
                padflow_ui
                juce::juce_recommended_config_flags
                juce::juce_recommended_lto_flags)
    target_compile_definitions(PadFlow PRIVATE JUCE_DISPLAY_SPLASH_SCREEN=0)
    padflow_enable_project_warnings(PadFlow)
    padflow_set_output_directory(PadFlow)
endif()

if(PADFLOW_BUILD_PLUGIN)
    set(PADFLOW_PLUGIN_FORMATS VST3)
    if(APPLE)
        list(APPEND PADFLOW_PLUGIN_FORMATS AU)
    endif()
    juce_add_plugin(
        PadFlowPlugin
        PRODUCT_NAME "${PADFLOW_PRODUCT_NAME}"
        COMPANY_NAME "${PADFLOW_COMPANY_NAME}"
        BUNDLE_ID "${PADFLOW_BUNDLE_ID}.plugin"
        VERSION "${PADFLOW_VERSION}"
        PLUGIN_MANUFACTURER_CODE Amma
        PLUGIN_CODE PdFw
        FORMATS ${PADFLOW_PLUGIN_FORMATS}
        VST3_CATEGORIES Instrument Sampler
        AU_MAIN_TYPE kAudioUnitType_MusicDevice
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        COPY_PLUGIN_AFTER_BUILD FALSE
        MICROPHONE_PERMISSION_ENABLED TRUE
        MICROPHONE_PERMISSION_TEXT
            "PadFlow receives audio input only when the plug-in host enables its input bus.")
    target_sources(
        PadFlowPlugin
        PRIVATE Source/Plugin/PadFlowAudioProcessor.cpp
                Source/Plugin/PadFlowAudioProcessor.h
                Source/Plugin/PadFlowPluginEditor.cpp
                Source/Plugin/PadFlowPluginEditor.h)
    target_link_libraries(
        PadFlowPlugin
        PRIVATE padflow_audio
                padflow_ui
                juce::juce_audio_processors
                juce::juce_recommended_config_flags
                juce::juce_recommended_lto_flags)
    target_compile_definitions(
        PadFlowPlugin
        PUBLIC JUCE_VST3_CAN_REPLACE_VST2=0
        PRIVATE JUCE_DISPLAY_SPLASH_SCREEN=0)
    padflow_enable_project_warnings(PadFlowPlugin)
    padflow_set_output_directory(PadFlowPlugin)
endif()

if(PADFLOW_BUILD_TESTS)
    enable_testing()
    add_subdirectory(Tests)

    juce_add_console_app(padflow_smoke PRODUCT_NAME "PadFlow Smoke")
    target_sources(padflow_smoke PRIVATE Source/Smoke/SmokeMain.cpp)
    target_link_libraries(padflow_smoke PRIVATE padflow_audio)
    padflow_enable_project_warnings(padflow_smoke)
    padflow_set_output_directory(padflow_smoke)

    add_test(NAME UIHEADLESS-001-console-smoke COMMAND padflow_smoke)
    if(PADFLOW_BUILD_APP)
        add_test(
            NAME UIHEADLESS-002-gui-headless-smoke
            COMMAND PadFlow --headless-smoke-test --no-audio-device)
    endif()
    if(PADFLOW_BUILD_PLUGIN)
        add_executable(padflow_plugin_smoke)
        target_sources(padflow_plugin_smoke PRIVATE Source/Plugin/PluginSmokeMain.cpp)
        target_include_directories(
            padflow_plugin_smoke
            PRIVATE Source "${CMAKE_CURRENT_BINARY_DIR}/PadFlowPlugin_artefacts/JuceLibraryCode")
        target_include_directories(
            padflow_plugin_smoke SYSTEM PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/external/JUCE/modules")
        target_link_libraries(padflow_plugin_smoke PRIVATE PadFlowPlugin)
        padflow_enable_project_warnings(padflow_plugin_smoke)
        padflow_set_output_directory(padflow_plugin_smoke)
        add_test(NAME PLUGIN-001-processor-smoke COMMAND padflow_plugin_smoke)
    endif()
endif()
