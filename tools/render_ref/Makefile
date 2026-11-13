MAQUINA_ROOT := $(abspath ../..)
include $(MAQUINA_ROOT)/engine/engine.mk

ENGINE_BUILD ?= $(MAQUINA_ROOT)/build/engine
BUILD ?= $(MAQUINA_ROOT)/build/tools
ENGINE_LIB := $(ENGINE_BUILD)/libmaquinaengine.a

all: $(BUILD)/render_ref

$(ENGINE_LIB): FORCE
	$(MAKE) -C $(MAQUINA_ROOT)/engine CC="$(CC)" ENGINE_BUILD=$(ENGINE_BUILD)

$(BUILD)/render_ref: render_ref.c $(ENGINE_LIB) $(MAQUINA_ROOT)/plugins/common/Timing.h
	@mkdir -p $(BUILD)
	$(CC) $(ENGINE_CFLAGS) render_ref.c $(ENGINE_LIB) -lm -o $@

test: $(BUILD)/render_ref
	$(BUILD)/render_ref

FORCE:
.PHONY: all test FORCE
