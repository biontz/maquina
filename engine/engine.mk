# engine.mk - LXR firmware sound engine + host glue, shared by the plugins and tools.
# Include with MAQUINA_ROOT set to the repository root.

FW_CORE := $(MAQUINA_ROOT)/third_party/lxr-firmware/Core
FW_SRC  := $(FW_CORE)/Src

ENGINE_C_SRCS := \
	$(wildcard $(FW_SRC)/Soundengine/DSPAudio/*.c) \
	$(wildcard $(FW_SRC)/Soundengine/DSPAudio/FX/*.c) \
	$(wildcard $(FW_SRC)/Soundengine/DSPAudio/FX/VerbTools/*.c) \
	$(FW_SRC)/Soundengine/FxBus.c \
	$(FW_SRC)/Soundengine/MIDI/MidiParser.c \
	$(FW_SRC)/Soundengine/MIDI/ParameterArray.c \
	$(FW_SRC)/Soundengine/MIDI/MidiVoiceControl.c \
	$(FW_SRC)/Soundengine/MIDI/MidiMessages.c \
	$(FW_SRC)/Parameters.c \
	$(FW_SRC)/menu/Cc2Text.c \
	$(FW_SRC)/Preset/initKit.c \
	$(MAQUINA_ROOT)/engine/lxr_host.c

# The shim directory must come first: it replaces the STM32 HAL, main.h,
# delay.h and Soundengine/config.h (memory-section macros emptied).
ENGINE_INCLUDES := -I$(MAQUINA_ROOT)/engine/fw_shim -I$(FW_CORE)/Inc -I$(MAQUINA_ROOT)/engine

# Same maths model as the firmware build (-ffast-math -freciprocal-math).
# Warnings are the firmware's own; it is compiled as-is, so silence them.
ENGINE_CFLAGS := -std=gnu11 -O2 -ffast-math -fPIC -fvisibility=hidden -w $(ENGINE_INCLUDES)
