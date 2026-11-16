# paths.mk - where the module families live. Include with MAQUINA_ROOT set to
# the repository root (the top Makefile, common/plugin.mk, tools/render_ref).
#
#   drum02/                 Erica Synths LXR-02 firmware: Kick Tom Snare Cymbal HiHat
#   befaco/plethora/        Befaco Noise Plethora
#   befaco/oneiroi/         Befaco Oneiroi: Oneiroi + Oneiroi FX
#   euro-modules/dredrum/   Dredrum
#   mutable-instruments/    Mutable Instruments: klang/ (Plaits), klong/ (Rings: Klong + Klong FX),
#                           third_party/eurorack (shared, built as libklang.a / libklong.a)
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
LXR_FW_INC   := $(DRUM02_DIR)/third_party/lxr-firmware/Core/Inc

# plugin directories, in build order
PLUGIN_DIRS := $(addprefix drum02/plugins/,kick tom snare cymbal hihat) \
	euro-modules/dredrum/plugins/dredrum befaco/plethora/plugins/plethora \
	befaco/oneiroi/plugins/oneiroi befaco/oneiroi/plugins/oneiroi-fx \
	mutable-instruments/klang/plugins/klang mutable-instruments/klong/plugins/klong \
	mutable-instruments/klong/plugins/klong-fx

# every plugin and tool: shared code, the host classes, and the LXR firmware
# headers (VoiceTypes.hpp uses its Parameters.h)
MAQUINA_INCLUDES := -I$(MAQUINA_ROOT)/common -I$(DRUM02_DIR)/engine -I$(DRUM02_DIR)/engine/fw_shim \
	-I$(LXR_FW_INC) -I$(DREDRUM_DIR)/engine -I$(PLETHORA_DIR)/engine -I$(ONEIROI_DIR)/engine \
	-I$(KLANG_DIR)/engine -I$(KLONG_DIR)/engine

# the vendored Noise Plethora port + its VCV Rack stand-in
PLETHORA_INCLUDES := -I$(PLETHORA_DIR)/engine/compat -I$(PLETHORA_DIR)/third_party/noise-plethora/src

# the vendored Oneiroi engine + OWL library; compat/ first (Patch.h, heap.h stand-ins)
ONEIROI_INCLUDES := -I$(ONEIROI_DIR)/engine/compat -I$(ONEIROI_DIR)/engine \
	-I$(ONEIROI_DIR)/third_party/oneiroi -I$(ONEIROI_DIR)/third_party/owlprogram/LibSource

# the vendored Mutable Instruments code (Engine.cpp of Klang / Klong defines TEST,
# as the libraries are built: see mutable-instruments/eurorack.mk)
EURORACK_INCLUDES := -I$(MUTABLE_DIR)/third_party/eurorack
