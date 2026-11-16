#!/usr/bin/make -f
# Builds the vendored Mutable Instruments DSP code as two static libraries:
#   $(ENGINE_BUILD)/libklang.a   Plaits (Maquina Klang)
#   $(ENGINE_BUILD)/libklong.a   Rings  (Maquina Klong / Klong FX)
# ENGINE_BUILD and CXX as for drum02/engine (native, aarch64, macOS per arch).

MAQUINA_ROOT := $(abspath ..)
include $(MAQUINA_ROOT)/mutable-instruments/eurorack.mk

CXX ?= c++
AR ?= ar
ENGINE_BUILD ?= $(MAQUINA_ROOT)/build/engine
OBJ := $(ENGINE_BUILD)/eurorack

obj_of = $(patsubst $(EURORACK)/%.cc,$(OBJ)/%.o,$(1))

all: $(ENGINE_BUILD)/libklang.a $(ENGINE_BUILD)/libklong.a

$(OBJ)/%.o: $(EURORACK)/%.cc
	@mkdir -p $(dir $@)
	@echo "Compiling $(notdir $<)"
	@$(CXX) $(EURORACK_CXXFLAGS) $(CXXFLAGS) -c $< -o $@

$(ENGINE_BUILD)/libklang.a: $(call obj_of,$(KLANG_SRCS))
	@echo "Creating $(notdir $@)"
	@rm -f $@
	@$(AR) rcs $@ $^

$(ENGINE_BUILD)/libklong.a: $(call obj_of,$(KLONG_SRCS))
	@echo "Creating $(notdir $@)"
	@rm -f $@
	@$(AR) rcs $@ $^

clean:
	rm -rf $(OBJ) $(ENGINE_BUILD)/libklang.a $(ENGINE_BUILD)/libklong.a

.PHONY: all clean
