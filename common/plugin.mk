# plugin.mk - shared Makefile part for the voice plugins.
# Set NAME and MAQUINA_VOICE_INDEX (and MAQUINA_DREDRUM = 1 for Maquina
# Dredrum, MAQUINA_PLETHORA = 1 for Maquina Plethora, MAQUINA_ONEIROI = 1 for
# Maquina Oneiroi / Oneiroi FX), then include this file.

# the repository root, from where this file is (plugins sit at different depths)
MAQUINA_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))..)
include $(MAQUINA_ROOT)/common/paths.mk

# MAQUINA_ARCH: empty for a native build, e.g. "aarch64" for a cross build
# (see the top-level Makefile); keeps objects and bundles apart.
ifeq ($(MAQUINA_ARCH),)
ENGINE_BUILD ?= $(MAQUINA_ROOT)/build/engine
DPF_BUILD_DIR ?= $(MAQUINA_ROOT)/build/$(NAME)
DPF_TARGET_DIR ?= $(MAQUINA_ROOT)/bin
else
ENGINE_BUILD ?= $(MAQUINA_ROOT)/build/engine-$(MAQUINA_ARCH)
DPF_BUILD_DIR ?= $(MAQUINA_ROOT)/build/$(MAQUINA_ARCH)/$(NAME)
DPF_TARGET_DIR ?= $(MAQUINA_ROOT)/bin-$(MAQUINA_ARCH)
endif

FILES_DSP = Plugin.cpp
# Maquina Plethora: the Noise Plethora port in its own translation unit
ifeq ($(MAQUINA_PLETHORA),1)
FILES_DSP += Engine.cpp
endif
# Maquina Oneiroi: the Oneiroi engine + OWL library in their own translation unit
ifeq ($(MAQUINA_ONEIROI),1)
FILES_DSP += Engine.cpp
endif

# MAQUINA_NO_UI=true: DSP-only build (Raspberry Pi / Machinista)
ifneq ($(MAQUINA_NO_UI),true)
FILES_UI = UI.cpp
UI_TYPE = opengl
endif

include $(MAQUINA_ROOT)/dpf/Makefile.plugins.mk

ifeq ($(MAQUINA_NO_UI),true)
BUILD_CXX_FLAGS += -DMAQUINA_NO_UI
endif
# MAQUINA_DREDRUM=1: the Dredrum engine (DredrumHost) instead of the LXR one
ifeq ($(MAQUINA_DREDRUM),1)
BUILD_CXX_FLAGS += -DMAQUINA_DREDRUM=1
endif
# MAQUINA_PLETHORA=1: the Noise Plethora port (PlethoraHost)
ifeq ($(MAQUINA_PLETHORA),1)
BUILD_CXX_FLAGS += -DMAQUINA_PLETHORA=1 $(PLETHORA_INCLUDES)
endif
# MAQUINA_ONEIROI=1: the Befaco Oneiroi engine (OneiroiHost). The OWL library
# type-puns floats (fastpow.c, Commons.h Power()): no strict aliasing.
ifeq ($(MAQUINA_ONEIROI),1)
BUILD_CXX_FLAGS += -DMAQUINA_ONEIROI=1 -fno-strict-aliasing $(ONEIROI_INCLUDES)
endif
BUILD_CXX_FLAGS += -std=gnu++17 -DMAQUINA_VOICE_INDEX=$(MAQUINA_VOICE_INDEX) $(MAQUINA_INCLUDES)
LINK_FLAGS += $(ENGINE_BUILD)/libmaquinaengine.a -lm

# relink when the engine library changes (DPF only tracks the plugin's objects)
$(lv2_dsp) $(clap) $(vst3) $(jack): $(ENGINE_BUILD)/libmaquinaengine.a

# LV2 is built as separate DSP + UI binaries (lv2_sep), so a headless host
# such as Machinista never loads the GUI. DSP-only builds use lv2_dsp.
ifeq ($(MAQUINA_NO_UI),true)
TARGETS ?= lv2_dsp clap vst3
else
TARGETS ?= lv2_sep clap vst3 jack
endif

all: $(TARGETS)
