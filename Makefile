# MorphWorx VCV Rack Plugin Makefile

# Point to the Rack SDK
RACK_DIR ?= C:/Rack-SDK

# Add source files
SOURCES += src/plugin.cpp

# Trigonomicon (generative drum trigger pattern generator)
SOURCES += src/DrumTrigger.cpp
SOURCES += src/core/MetricEngine.cpp
SOURCES += src/core/EnergyField.cpp
SOURCES += src/core/PhaseWarper.cpp
SOURCES += src/core/TriggerExtractor.cpp
SOURCES += src/core/PatternMemory.cpp
SOURCES += src/dsp/LookupTables.cpp

# Septagon (polyrhythmic drum pattern generator)
SOURCES += src/PhaseWarpedDrums.cpp

# Phaseon1 (4-op PM synth with wavetable)
SOURCES += src/Phaseon1.cpp
SOURCES += src/phaseon/PhaseonOperator.cpp
SOURCES += src/phaseon/PhaseonWavetable.cpp

# Xenostasis (autonomous hybrid synthesis organism)
SOURCES += src/Xenostasis.cpp

# PreenFM2 shim headers take priority over originals (kept for Xenostasis compatibility)
FLAGS += -Isrc/pfm -Isrc/pfm/synth -DCVIN

# Add resource files to distributables
DISTRIBUTABLES += res
DISTRIBUTABLES += userwaveforms
# Force-include waveform payload files.
# On Windows/MSYS, copying the userwaveforms folder has sometimes resulted in only README.txt being packaged.
# Explicitly listing the actual waveform files ensures they are included in the .vcvplugin.
DISTRIBUTABLES += $(wildcard userwaveforms/usr*.txt)
DISTRIBUTABLES += $(wildcard userwaveforms/USR*.BIN)
DISTRIBUTABLES += $(wildcard LICENSE*)

# Include the Rack plugin makefile framework
include $(RACK_DIR)/plugin.mk

# Ensure our tool wrappers (e.g. quiet zstd) are found first.
export PATH := $(CURDIR)/tools:$(PATH)

# GCC doesn't recognize this Clang warning flag (and emits noisy diagnostics).
FLAGS := $(filter-out -Wno-vla-extension,$(FLAGS))
