# paths.mk - where the module families live. Include with MAQUINA_ROOT set to
# the repository root (the top Makefile, common/plugin.mk, tools/render_ref).
#
#   drum02/                 Erica Synths LXR-02 firmware: Kick Tom Snare Cymbal HiHat
#   befaco/plethora/        Befaco Noise Plethora
#   befaco/oneiroi/         Befaco Oneiroi: Oneiroi + Oneiroi FX
#   euro-modules/dredrum/   Dredrum
#   mutable-instruments/    Mutable Instruments: klang/ (Plaits), klong/ (Rings: Klong + Klong FX),
#                           third_party/eurorack (shared, built as libklang.a / libklong.a)
#   openrb/acida/           OpenRB-338 (ReBirth RB-338 reimplementation): Acida (303) + Acida FX,
#                           the DSP built as libacida.a (openrb/Makefile)
#   s1vave/bl101/           S1Vave (SH-101-style firmware): BL-101 + BL-101 FX,
#                           the DSP built as libbl101.a (s1vave/Makefile)
#   lasynth/la50/           LA synthesis playing D-50 patches (dsynkant + Munt's LA32): LA-50,
#                           the engine built as liblasynth.a (lasynth/Makefile)
#   stringmachine/strings/  a string machine (after jpcima's string-machine) + Plaits' wavetables:
#                           Strings (the editor-first skeleton: a silent stub host for now)
#
# each with engine/ (port + host class), third_party/ (vendored sources),
# plugins/<plugin>/ and presets/<voice>/; common/ is shared by all plugins.

DRUM02_DIR   := $(MAQUINA_ROOT)/drum02
PLETHORA_DIR := $(MAQUINA_ROOT)/befaco/plethora
ONEIROI_DIR  := $(MAQUINA_ROOT)/befaco/oneiroi
DREDRUM_DIR  := $(MAQUINA_ROOT)/euro-modules/dredrum
MUTABLE_DIR  := $(MAQUINA_ROOT)/mutable-instruments
KLANG_DIR    := $(MUTABLE_DIR)/klang
KLONG_DIR    := $(MUTABLE_DIR)/klong
ACIDA_DIR    := $(MAQUINA_ROOT)/openrb/acida
BL101_DIR    := $(MAQUINA_ROOT)/s1vave/bl101
LA50_DIR     := $(MAQUINA_ROOT)/lasynth/la50
STRINGS_DIR  := $(MAQUINA_ROOT)/stringmachine/strings
LXR_FW_INC   := $(DRUM02_DIR)/third_party/lxr-firmware/Core/Inc

# plugin directories, in build order
PLUGIN_DIRS := $(addprefix drum02/plugins/,kick tom snare cymbal hihat) \
	euro-modules/dredrum/plugins/dredrum befaco/plethora/plugins/plethora \
	befaco/oneiroi/plugins/oneiroi befaco/oneiroi/plugins/oneiroi-fx \
	mutable-instruments/klang/plugins/klang mutable-instruments/klong/plugins/klong \
	mutable-instruments/klong/plugins/klong-fx \
	openrb/acida/plugins/acida openrb/acida/plugins/acida-fx \
	s1vave/bl101/plugins/bl101 s1vave/bl101/plugins/bl101-fx \
	lasynth/la50/plugins/la50 stringmachine/strings/plugins/strings

# every plugin and tool: shared code, the host classes, and the LXR firmware
# headers (VoiceTypes.hpp uses its Parameters.h)
MAQUINA_INCLUDES := -I$(MAQUINA_ROOT)/common -I$(DRUM02_DIR)/engine -I$(DRUM02_DIR)/engine/fw_shim \
	-I$(LXR_FW_INC) -I$(DREDRUM_DIR)/engine -I$(PLETHORA_DIR)/engine -I$(ONEIROI_DIR)/engine \
	-I$(KLANG_DIR)/engine -I$(KLONG_DIR)/engine -I$(ACIDA_DIR)/engine -I$(BL101_DIR)/engine \
	-I$(LA50_DIR)/engine -I$(STRINGS_DIR)/engine

# the vendored Noise Plethora port + its VCV Rack stand-in
PLETHORA_INCLUDES := -I$(PLETHORA_DIR)/engine/compat -I$(PLETHORA_DIR)/third_party/noise-plethora/src

# the vendored Oneiroi engine + OWL library; compat/ first (Patch.h, heap.h stand-ins)
ONEIROI_INCLUDES := -I$(ONEIROI_DIR)/engine/compat -I$(ONEIROI_DIR)/engine \
	-I$(ONEIROI_DIR)/third_party/oneiroi -I$(ONEIROI_DIR)/third_party/owlprogram/LibSource

# the vendored S1Vave firmware headers (the engine port and its reference only)
S1VAVE_INCLUDES := -I$(BL101_DIR)/third_party/s1vave/firmware/src -I$(BL101_DIR)/third_party/s1vave/firmware/gen

# the vendored Mutable Instruments code (Engine.cpp of Klang / Klong defines TEST,
# as the libraries are built: see mutable-instruments/eurorack.mk)
EURORACK_INCLUDES := -I$(MUTABLE_DIR)/third_party/eurorack
