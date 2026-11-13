# plugin.mk - shared Makefile part for the voice plugins.
# Set NAME and MAQUINA_VOICE_INDEX, then include this file.

MAQUINA_ROOT := $(abspath ../..)

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

# MAQUINA_NO_UI=true: DSP-only build (Raspberry Pi / Machinista)
ifneq ($(MAQUINA_NO_UI),true)
FILES_UI = UI.cpp
UI_TYPE = opengl
endif

include $(MAQUINA_ROOT)/dpf/Makefile.plugins.mk

FW_INC := $(MAQUINA_ROOT)/third_party/lxr-firmware/Core/Inc
ifeq ($(MAQUINA_NO_UI),true)
BUILD_CXX_FLAGS += -DMAQUINA_NO_UI
endif
BUILD_CXX_FLAGS += -std=gnu++17 -DMAQUINA_VOICE_INDEX=$(MAQUINA_VOICE_INDEX) \
	-I$(MAQUINA_ROOT)/plugins/common -I$(MAQUINA_ROOT)/engine \
	-I$(MAQUINA_ROOT)/engine/fw_shim -I$(FW_INC)
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
