#!/usr/bin/make -f
# Builds the LXR firmware sound engine + host glue as a static library.
#   make                -> build/engine/libmaquinaengine.a  (native)
#   make CC="clang --target=..." ENGINE_BUILD=...           (cross)
#
# On non-Arm targets the firmware sources are first passed through
# tools/arm_semantics.py, which makes float->int conversions behave like the
# Cortex-M7 (needs clang for the AST). Arm targets compile them untouched.

MAQUINA_ROOT := $(abspath ..)
include $(MAQUINA_ROOT)/engine/engine.mk

CC ?= cc
AR ?= ar
ENGINE_BUILD ?= $(MAQUINA_ROOT)/build/engine
ENGINE_LIB := $(ENGINE_BUILD)/libmaquinaengine.a
ENGINE_OBJS := $(patsubst $(MAQUINA_ROOT)/%.c,$(ENGINE_BUILD)/obj/%.o,$(ENGINE_C_SRCS))

TARGET_MACHINE := $(shell $(CC) -dumpmachine)
ifneq ($(filter arm% aarch64%,$(TARGET_MACHINE)),)
ARM_TARGET := true
endif

all: $(ENGINE_LIB)

ifeq ($(ARM_TARGET),true)
$(ENGINE_BUILD)/obj/%.o: $(MAQUINA_ROOT)/%.c
	@mkdir -p $(dir $@)
	@echo "Compiling $(notdir $<)"
	@$(CC) $(ENGINE_CFLAGS) $(EXTRA_CFLAGS) -c $< -o $@
else
$(ENGINE_BUILD)/armsem/%.c: $(MAQUINA_ROOT)/%.c $(MAQUINA_ROOT)/tools/arm_semantics.py
	@mkdir -p $(dir $@)
	@python3 $(MAQUINA_ROOT)/tools/arm_semantics.py $< $@ $(ENGINE_INCLUDES) 2>$@.log || (cat $@.log; exit 1)

$(ENGINE_BUILD)/obj/%.o: $(ENGINE_BUILD)/armsem/%.c
	@mkdir -p $(dir $@)
	@echo "Compiling $(notdir $<) (Arm conversion semantics)"
	@$(CC) $(ENGINE_CFLAGS) $(EXTRA_CFLAGS) -I$(dir $(MAQUINA_ROOT)/$*) -c $< -o $@
endif

$(ENGINE_LIB): $(ENGINE_OBJS)
	@echo "Creating $(notdir $@)"
	@rm -f $@
	@$(AR) rcs $@ $^

clean:
	rm -rf $(ENGINE_BUILD)

.PHONY: all clean
