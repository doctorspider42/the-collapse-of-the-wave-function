cmake_minimum_required(VERSION 3.22)

project(Collapse
    VERSION 1.2.0
    DESCRIPTION "The Collapse of The Wave Function - two bass channels, one crossover"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Static MSVC runtime: without it the .vst3 refuses to load on any machine that has not
# installed the matching Visual C++ redistributable.
if (MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

option(COLLAPSE_COPY_AFTER_BUILD "Install the built plug-in into the user VST3 folder" ON)
option(COLLAPSE_BUILD_TOOLS      "Build the developer tool (offline checks / renderer)" OFF)

# Off by default, and deliberately. Link-time optimisation buys a few per cent of CPU in
# the finished binary and costs minutes on every single link - the plug-in, the
# standalone and the tool each re-optimise the whole of JUCE. Release builds turn it on;
# nobody working on the code should have to.
option(COLLAPSE_LTO "Link-time optimisation (slow links, for release builds)" OFF)

# Shown in the UI next to the version so a binary can be traced back to its source. Kept
# separate from the plug-in version, which stays stable so hosts do not re-scan on every
# development build.
set(COLLAPSE_BUILD_ID "" CACHE STRING "Build identifier shown in the UI (e.g. a commit SHA)")

# ---------------------------------------------------------------------------
# JUCE, pinned as a submodule so builds are reproducible and the exact framework
# revision behind any binary is recorded in history. That is what makes the AGPL
# "corresponding source" obligation a one-liner rather than archaeology.
# ---------------------------------------------------------------------------
if (NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libs/JUCE/CMakeLists.txt")
    message(FATAL_ERROR
        "JUCE not found in libs/JUCE.\n"
        "Run: git submodule update --init --recursive")
endif()

add_subdirectory(libs/JUCE)

# ---------------------------------------------------------------------------
# The plug-in
# ---------------------------------------------------------------------------
juce_add_plugin(Collapse
    PRODUCT_NAME                "The Collapse of The Wave Function"
    COMPANY_NAME                "doctorspider42"
    BUNDLE_ID                   "com.doctorspider42.collapse"
    PLUGIN_MANUFACTURER_CODE    Dspi        # exactly 4 chars, first uppercase
    PLUGIN_CODE                 Cwfn        # exactly 4 chars, unique per plug-in
    FORMATS                     VST3 Standalone
    VST3_CATEGORIES             Fx Distortion
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     ${COLLAPSE_COPY_AFTER_BUILD})

juce_generate_juce_header(Collapse)

target_sources(Collapse PRIVATE
    Source/Parameters.cpp
    Source/Presets.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/dsp/CollapseEngine.cpp
    Source/gui/Skin.cpp
    Source/gui/Backdrop.cpp
    Source/gui/WaveView.cpp
    Source/gui/CollapseKnob.cpp
    Source/gui/StateSelector.cpp
    Source/gui/PowerToggle.cpp)

target_include_directories(Collapse PRIVATE Source)

if (COLLAPSE_BUILD_ID)
    string(SUBSTRING "${COLLAPSE_BUILD_ID}" 0 7 COLLAPSE_BUILD_ID_SHORT)
    target_compile_definitions(Collapse PRIVATE
        COLLAPSE_BUILD_ID="${COLLAPSE_BUILD_ID_SHORT}")
endif()

target_compile_definitions(Collapse PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_MODAL_LOOPS_PERMITTED=0)

target_link_libraries(Collapse
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

if (COLLAPSE_LTO)
    target_link_libraries(Collapse PUBLIC juce::juce_recommended_lto_flags)
endif()

# -ffast-math is deliberately absent: it breaks denormal handling and NaN semantics, both
# of which matter in audio code.
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(Collapse PRIVATE
        $<$<CONFIG:Release>:-O3 -fno-math-errno>)
endif()

# ---------------------------------------------------------------------------
# Developer tool: offline DSP checks, a level sweep and a headless panel renderer.
# None of the modes needs an audio device or a display, so all of them run in CI.
# ---------------------------------------------------------------------------
if (COLLAPSE_BUILD_TOOLS)
    juce_add_console_app(CollapseDevTool)

    target_sources(CollapseDevTool PRIVATE tools/DevTool.cpp)
    target_include_directories(CollapseDevTool PRIVATE Source)

    # The JUCE modules must be listed again: the plug-in target links them PRIVATE, so
    # their include directories do not propagate to consumers.
    target_link_libraries(CollapseDevTool PRIVATE
        Collapse
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_gui_extra
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

    target_compile_definitions(CollapseDevTool PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_MODAL_LOOPS_PERMITTED=0)
endif()
