# MorphWorx VCV Rack Plugin Makefile

# Point to the Rack SDK
RACK_DIR ?= C:/Rack-SDK

# Add source files
SOURCES += src/plugin.cpp
SOURCES += src/Trigonomicon.cpp
SOURCES += src/SlideWyrm.cpp
SOURCES += src/PhaseWarpedDrums.cpp
SOURCES += src/core/MetricEngine.cpp
SOURCES += src/core/EnergyField.cpp
SOURCES += src/core/PhaseWarper.cpp
SOURCES += src/core/TriggerExtractor.cpp
SOURCES += src/core/PatternMemory.cpp
SOURCES += src/dsp/LookupTables.cpp

# Amenolith multisample kit player
SOURCES += src/Amenolith.cpp
SOURCES += src/sampler/DrumKits.cpp

# Phaseon1 (performance-first PM+wavetable voice)
SOURCES += src/Phaseon1.cpp
SOURCES += src/phaseon/PhaseonOperator.cpp
SOURCES += src/phaseon/PhaseonWavetable.cpp

# Xenostasis (autonomous hybrid synthesis organism)
SOURCES += src/Xenostasis.cpp

# Minimalith (PreenFM2 engine port)
SOURCES += src/Minimalith.cpp
SOURCES += src/pfm/PfmEngine.cpp
SOURCES += src/pfm/PfmBankLoader.cpp
SOURCES += src/pfm/waveforms/UserWaveforms.cpp
# PreenFM2 synth engine (adapted from original GPL-3.0 source)
SOURCES += src/pfm/synth/Voice.cpp
SOURCES += src/pfm/synth/Timbre.cpp
SOURCES += src/pfm/synth/Osc.cpp
SOURCES += src/pfm/synth/Env.cpp
SOURCES += src/pfm/synth/Matrix.cpp
SOURCES += src/pfm/synth/Lfo.cpp
SOURCES += src/pfm/synth/LfoOsc.cpp
SOURCES += src/pfm/synth/LfoEnv.cpp
SOURCES += src/pfm/synth/LfoEnv2.cpp
SOURCES += src/pfm/synth/LfoStepSeq.cpp
SOURCES += src/pfm/synth/Presets.cpp
SOURCES += src/pfm/synth/Common.cpp
SOURCES += src/pfm/synth/note_stack.cpp
SOURCES += src/pfm/synth/event_scheduler.cpp

# PreenFM2 shim headers take priority over originals
FLAGS += -Isrc/pfm -Isrc/pfm/synth -DCVIN

# Add resource files to distributables
DISTRIBUTABLES += res
DISTRIBUTABLES += userwaveforms
# Force-include waveform payload files.
# On Windows/MSYS, copying the userwaveforms folder has sometimes resulted in only README.txt being packaged.
# Explicitly listing the actual waveform files ensures they are included in the .vcvplugin.
DISTRIBUTABLES += $(wildcard userwaveforms/usr*.txt)
DISTRIBUTABLES += $(wildcard userwaveforms/USR*.BIN)
# Phaseon1 factory preset bank — bundled so it auto-loads on first use
DISTRIBUTABLES += userwaveforms/Phbank.bnk
DISTRIBUTABLES += $(wildcard LICENSE*)

# Include the Rack plugin makefile framework
include $(RACK_DIR)/plugin.mk

# Ensure our tool wrappers (e.g. quiet zstd) are found first.
export PATH := $(CURDIR)/tools:$(PATH)

# GCC doesn't recognize this Clang warning flag (and emits noisy diagnostics).
FLAGS := $(filter-out -Wno-vla-extension,$(FLAGS))
