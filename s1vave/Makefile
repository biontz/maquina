#!/usr/bin/make -f
# Builds the C++ port of S1Vave's sound engine (Maquina BL-101 / BL-101 FX) as
#   $(ENGINE_BUILD)/libbl101.a
# ENGINE_BUILD and CXX as for drum02/engine (native, aarch64, macOS per arch).
#
# The port is bit-exact with the firmware (tools/bl101_test against
# tools/bl101_ref): integers only; -fwrapv as the reference is built (the
# firmware's arithmetic wraps on the chip), no -ffast-math.

MAQUINA_ROOT := $(abspath ..)
BL101_ENGINE := $(MAQUINA_ROOT)/s1vave/bl101/engine
S1VAVE_FW := $(MAQUINA_ROOT)/s1vave/bl101/third_party/s1vave/firmware

CXX ?= c++
AR ?= ar
ENGINE_BUILD ?= $(MAQUINA_ROOT)/build/engine
OBJ := $(ENGINE_BUILD)/bl101

BL101_CXXFLAGS := -std=gnu++17 -O2 -fPIC -fvisibility=hidden -fwrapv -ffp-contract=off -fno-fast-math \
	-Wall -Wextra -I$(BL101_ENGINE) -I$(S1VAVE_FW)/src -I$(S1VAVE_FW)/gen

all: $(ENGINE_BUILD)/libbl101.a

$(OBJ)/Bl101Dsp.o: $(BL101_ENGINE)/Bl101Dsp.cpp $(BL101_ENGINE)/Bl101Dsp.hpp $(wildcard $(S1VAVE_FW)/src/*.h) \
		$(S1VAVE_FW)/gen/obv_tables.h
	@mkdir -p $(dir $@)
	@echo "Compiling $(notdir $<)"
	@$(CXX) $(BL101_CXXFLAGS) $(CXXFLAGS) -c $< -o $@

$(ENGINE_BUILD)/libbl101.a: $(OBJ)/Bl101Dsp.o
	@echo "Creating $(notdir $@)"
	@rm -f $@
	@$(AR) rcs $@ $^

clean:
	rm -rf $(OBJ) $(ENGINE_BUILD)/libbl101.a

.PHONY: all clean
