#!/usr/bin/make -f
# Builds the C++ port of OpenRB-338's engine (Maquina Acida / Acida FX) as
#   $(ENGINE_BUILD)/libacida.a
# ENGINE_BUILD and CXX as for drum02/engine (native, aarch64, macOS per arch).
#
# The port is bit-exact with the Rust engine (tools/acida_test): no
# -ffast-math, no fused multiply-add (-ffp-contract=off), as the plugins'
# DPF flags would otherwise give.

MAQUINA_ROOT := $(abspath ..)
ACIDA_ENGINE := $(MAQUINA_ROOT)/openrb/acida/engine

CXX ?= c++
AR ?= ar
ENGINE_BUILD ?= $(MAQUINA_ROOT)/build/engine
OBJ := $(ENGINE_BUILD)/acida

ACIDA_CXXFLAGS := -std=gnu++17 -O2 -fPIC -fvisibility=hidden -ffp-contract=off -fno-fast-math \
	-Wall -Wextra -I$(ACIDA_ENGINE)

all: $(ENGINE_BUILD)/libacida.a

$(OBJ)/AcidaDsp.o: $(ACIDA_ENGINE)/AcidaDsp.cpp $(ACIDA_ENGINE)/AcidaDsp.hpp $(ACIDA_ENGINE)/PcfPatterns.generated.hpp
	@mkdir -p $(dir $@)
	@echo "Compiling $(notdir $<)"
	@$(CXX) $(ACIDA_CXXFLAGS) $(CXXFLAGS) -c $< -o $@

$(ENGINE_BUILD)/libacida.a: $(OBJ)/AcidaDsp.o
	@echo "Creating $(notdir $@)"
	@rm -f $@
	@$(AR) rcs $@ $^

clean:
	rm -rf $(OBJ) $(ENGINE_BUILD)/libacida.a

.PHONY: all clean
