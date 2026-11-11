cmake_minimum_required(VERSION 3.22)
project(NeedleScratch VERSION 0.9.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
  add_compile_options(/utf-8)
endif()
set(JUCE_SOURCE_DIR "" CACHE PATH "Optional existing JUCE 8.0.13 checkout")
if(JUCE_SOURCE_DIR)
  add_subdirectory("${JUCE_SOURCE_DIR}" juce)
else()
  include(FetchContent)
  FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.13 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(JUCE)
endif()
juce_add_plugin(NeedleScratch
  COMPANY_NAME "Needle Audio" BUNDLE_ID "audio.needle.scratch"
  PLUGIN_MANUFACTURER_CODE Ndle PLUGIN_CODE Nscr
  FORMATS VST3 Standalone PRODUCT_NAME "SKRATCH.EXE FX"
  IS_SYNTH FALSE NEEDS_MIDI_INPUT TRUE NEEDS_MIDI_OUTPUT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS TRUE COPY_PLUGIN_AFTER_BUILD FALSE
  VST3_CATEGORIES Fx Sampler)
set_target_properties(NeedleScratch_Standalone PROPERTIES OUTPUT_NAME "SKRATCH")
target_sources(NeedleScratch_Standalone PRIVATE Source/StandaloneXp.cpp)
target_compile_definitions(NeedleScratch_Standalone PRIVATE JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1)
juce_add_plugin(NeedleInstrument
  COMPANY_NAME "Needle Audio" BUNDLE_ID "audio.needle.scratch.instrument"
  PLUGIN_MANUFACTURER_CODE Ndle PLUGIN_CODE Nsin
  FORMATS VST3 PRODUCT_NAME "SKRATCH.EXE Instrument"
  IS_SYNTH TRUE NEEDS_MIDI_INPUT TRUE NEEDS_MIDI_OUTPUT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS TRUE COPY_PLUGIN_AFTER_BUILD FALSE
  VST3_CATEGORIES Instrument Sampler)
juce_add_binary_data(SkratchSkinData HEADER_NAME SkratchSkinData.h NAMESPACE SkratchSkinData SOURCES
  Assets/deck.png Assets/platter_matte.png Assets/vinyl_matte.png Assets/knob_cap.png
  Assets/mixer.png Assets/logo.png Assets/deck_xp.png Assets/deck_headshell_underlay.png Assets/mixer_xp.png
  Assets/blackopsone.ttf Assets/teko.ttf Assets/teko_hardware_medium.ttf Assets/rajdhani.ttf Assets/ibmplexmono.ttf Assets/dseg.ttf)
foreach(target IN ITEMS NeedleScratch NeedleInstrument)
  target_sources(${target} PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/ToolPanels.cpp Source/Performance.cpp Source/Gamepad.cpp Source/SkinAssets.cpp Source/DjFx.cpp Source/DjControl.cpp Source/DjPanel.cpp Source/MixerDsp.cpp)
  target_link_libraries(${target} PRIVATE SkratchSkinData)
  if(WIN32)
    target_link_libraries(${target} PUBLIC xinput9_1_0 winmm)
  endif()
  target_include_directories(${target} PUBLIC Source)
  target_compile_definitions(${target} PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
  target_compile_definitions(${target} PRIVATE SKRATCH_PUBLIC_VERSION="${PROJECT_VERSION}-beta")
  target_link_libraries(${target} PUBLIC juce::juce_audio_utils juce::juce_dsp
    PRIVATE juce::juce_recommended_config_flags)
endforeach()
option(SKRATCH_BUILD_BENCHMARK "Build the Release performance harness" OFF)
if(SKRATCH_BUILD_BENCHMARK)
  juce_add_console_app(LatencyBench)
  target_sources(LatencyBench PRIVATE Tests/LatencyBench.cpp)
  target_compile_definitions(LatencyBench PRIVATE SKRATCH_BENCH_TELEMETRY=1)
  target_link_libraries(LatencyBench PRIVATE NeedleScratch)
endif()
juce_add_console_app(NeedleTests PRODUCT_NAME "Needle Tests")
target_sources(NeedleTests PRIVATE Tests/Tests.cpp Tests/UpgradeTests.cpp Tests/SkinTests.cpp Tests/InstrumentTests.cpp Tests/DjTests.cpp Tests/MixerTests.cpp)
target_compile_definitions(NeedleTests PRIVATE SKRATCH_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Tests/Fixtures")
target_link_libraries(NeedleTests PRIVATE NeedleScratch)
enable_testing()
add_test(NAME NeedleTests COMMAND NeedleTests)

juce_add_console_app(AudioFunctionalTests)
target_sources(AudioFunctionalTests PRIVATE Tests/AudioFunctionalTests.cpp)
target_link_libraries(AudioFunctionalTests PRIVATE NeedleScratch)
add_test(NAME AudioFunctionalTests COMMAND AudioFunctionalTests)
