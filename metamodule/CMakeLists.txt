cmake_minimum_required(VERSION 3.22)

# ============================================================================
# MorphWorx — MetaModule Plugin Build
# ============================================================================
#
# Build instructions (Windows/MSYS2):
#   cd metamodule
#   cmake --fresh -B build -G "MSYS Makefiles"
#   cmake --build build
#
# Build instructions (Linux/macOS):
#   cd metamodule
#   cmake --fresh -B build
#   cmake --build build
#
# Output: metamodule-plugins/MorphWorx-X.Y.Z.mmplugin
# ============================================================================

# --- Locate MetaModule SDK ---
if(NOT "${METAMODULE_SDK_DIR}" STREQUAL "")
    message(STATUS "METAMODULE_SDK_DIR set by CMake variable: ${METAMODULE_SDK_DIR}")
elseif(DEFINED ENV{METAMODULE_SDK_DIR})
    set(METAMODULE_SDK_DIR "$ENV{METAMODULE_SDK_DIR}")
    message(STATUS "METAMODULE_SDK_DIR set by environment: ${METAMODULE_SDK_DIR}")
else()
    set(METAMODULE_SDK_DIR "${CMAKE_CURRENT_LIST_DIR}/../../metamodule-plugin-sdk")
    message(STATUS "METAMODULE_SDK_DIR defaulting to: ${METAMODULE_SDK_DIR}")
endif()

# The MetaModule SDK optionally validates `plugin-mm.json` via `jq`.
# On Windows, a stale cached `jq.exe` path can break builds even when the JSON is valid.
# Force-disable jq detection and provide a Python-based validator instead.
if(WIN32)
    set(JQ_EXECUTABLE "" CACHE FILEPATH "jq executable (disabled; use Python validation)" FORCE)
endif()

include(${METAMODULE_SDK_DIR}/plugin.cmake)

# --- Project ---
project(MorphWorx LANGUAGES C CXX ASM)

# Keep the MetaModule project's notion of the MorphWorx version in sync with
# the single authoritative definition in src/version.h and plugin.json.
#
# Use a relative script path so this works cleanly under MSYS as well as
# native Windows CMake without confusing path rewriting.
execute_process(
    COMMAND python ../tools/gen_plugin_json.py
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
    RESULT_VARIABLE GEN_PJ_RESULT
)
if(NOT GEN_PJ_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to sync plugin.json from src/version.h (gen_plugin_json.py)")
endif()


# --- Static library target for MetaModule SDK ---
add_library(MorphWorx STATIC)

target_sources(MorphWorx PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../src/plugin.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/Trigonomicon.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/Amenolith.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/Ferroklast.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/SlideWyrm.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/PhaseWarpedDrums.cpp
    # Phaseon1: performance-first Phaseon variant for MetaModule
    ${CMAKE_CURRENT_LIST_DIR}/../src/Phaseon1.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/phaseon/PhaseonOperator.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/phaseon/PhaseonWavetable.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/core/MetricEngine.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/core/EnergyField.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/core/PhaseWarper.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/core/TriggerExtractor.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/core/PatternMemory.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/dsp/LookupTables.cpp
    # Amenolith sampler kit source
    ${CMAKE_CURRENT_LIST_DIR}/../src/sampler/DrumKits.cpp
    # Xenostasis (autonomous hybrid synthesis organism)
    ${CMAKE_CURRENT_LIST_DIR}/../src/Xenostasis.cpp
    # Minimalith + PreenFM2 engine
    ${CMAKE_CURRENT_LIST_DIR}/../src/Minimalith.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/pfm/PfmEngine.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/pfm/PfmBankLoader.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/pfm/waveforms/UserWaveforms.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/pfm/synth/Voice.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/pfm/synth/Timbre.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/pfm/synth/Osc.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/pfm/synth/Env.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/pfm/synth/Matrix.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/pfm/synth/Lfo.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/pfm/synth/LfoOsc.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/pfm/synth/LfoEnv.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/pfm/synth/LfoEnv2.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/pfm/synth/LfoStepSeq.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/pfm/synth/Presets.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/pfm/synth/Common.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/pfm/synth/note_stack.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/pfm/synth/event_scheduler.cpp
)

target_include_directories(MorphWorx PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../src
    ${CMAKE_CURRENT_LIST_DIR}/../src/pfm
    ${CMAKE_CURRENT_LIST_DIR}/../src/pfm/synth
)

# MetaModule compile definition — used for conditional compilation
# CVIN enables CV frequency mode in the PreenFM2 engine
target_compile_definitions(MorphWorx PRIVATE METAMODULE=1 CVIN=1)

# -ffast-math enables FMA instructions, reciprocal approximation, and float
# reassociation on Cortex-A7 — significant CPU saving for DSP-heavy modules.
target_compile_options(MorphWorx PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-ffast-math>
    $<$<COMPILE_LANGUAGE:C>:-ffast-math>
)

# --- Create .mmplugin ---
set(MORPHWORX_PLUGIN_JSON_PRIMARY  "${CMAKE_CURRENT_LIST_DIR}/../plugin.json")
set(MORPHWORX_PLUGIN_JSON_FALLBACK "${CMAKE_CURRENT_LIST_DIR}/../dist/MorphWorx/plugin.json")

if(EXISTS "${MORPHWORX_PLUGIN_JSON_PRIMARY}")
    set(MORPHWORX_PLUGIN_JSON "${MORPHWORX_PLUGIN_JSON_PRIMARY}")
elseif(EXISTS "${MORPHWORX_PLUGIN_JSON_FALLBACK}")
    set(MORPHWORX_PLUGIN_JSON "${MORPHWORX_PLUGIN_JSON_FALLBACK}")
else()
    message(FATAL_ERROR "Could not find plugin.json at ${MORPHWORX_PLUGIN_JSON_PRIMARY} or ${MORPHWORX_PLUGIN_JSON_FALLBACK}")
endif()

create_plugin(
    SOURCE_LIB    MorphWorx
    PLUGIN_NAME   MorphWorx
    PLUGIN_JSON   ${MORPHWORX_PLUGIN_JSON}
    SOURCE_ASSETS ${CMAKE_CURRENT_LIST_DIR}/assets
    DESTINATION   ${CMAKE_CURRENT_LIST_DIR}/metamodule-plugins
)

set(MORPHWORX_MMPLUGIN_STAGE   "${CMAKE_CURRENT_BINARY_DIR}/MorphWorx")
set(MORPHWORX_MMPLUGIN_ARCHIVE "${CMAKE_CURRENT_LIST_DIR}/metamodule-plugins/MorphWorx.mmplugin")

add_custom_target(CLEANUP_MMPLUGIN_PACKAGE ALL
    COMMAND python -c "import pathlib, shutil, tarfile; stage=pathlib.Path(r'${MORPHWORX_MMPLUGIN_STAGE}'); presets=stage / 'presets'; archive=pathlib.Path(r'${MORPHWORX_MMPLUGIN_ARCHIVE}'); presets.is_dir() and not any(presets.iterdir()) and shutil.rmtree(presets); tf=tarfile.open(archive, 'w'); tf.add(stage, arcname=stage.name); tf.close()"
    DEPENDS plugin
    VERBATIM
    USES_TERMINAL
    COMMENT "Removing empty presets folder from MetaModule package"
)

# Always validate plugin-mm.json using Python (works on Windows without MSYS2/jq).
add_custom_target(VALIDATE_PLUGIN_MM_JSON ALL
    COMMAND python -c "import json, pathlib; p=pathlib.Path(r'${CMAKE_CURRENT_LIST_DIR}/plugin-mm.json'); json.loads(p.read_text(encoding='utf-8-sig'));"
    VERBATIM
    USES_TERMINAL
    COMMENT "Validating plugin-mm.json (python)"
)
