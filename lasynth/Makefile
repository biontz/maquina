#!/usr/bin/make -f
# Builds Maquina LA-50's engine (La50Engine, La50Fx, La50Pcm, D50Sysex) and the
# vendored LA32 subset of Munt (third_party/munt/src) as
#   $(ENGINE_BUILD)/liblasynth.a
# ENGINE_BUILD and CXX as for drum02/engine (native, aarch64, macOS per arch).

MAQUINA_ROOT := $(abspath ..)
LA50_ENGINE := $(MAQUINA_ROOT)/lasynth/la50/engine
MUNT_SRC := $(MAQUINA_ROOT)/lasynth/la50/third_party/munt/src

CXX ?= c++
AR ?= ar
ENGINE_BUILD ?= $(MAQUINA_ROOT)/build/engine
OBJ := $(ENGINE_BUILD)/lasynth

LA50_CXXFLAGS := -std=gnu++17 -O2 -fPIC -fvisibility=hidden -Wall -Wextra \
	-I$(LA50_ENGINE) -I$(LA50_ENGINE)/munt_compat -I$(MUNT_SRC)
# Munt's code as it is (its own warnings are not ours to fix)
MUNT_CXXFLAGS := -std=gnu++17 -O2 -fPIC -fvisibility=hidden -w -I$(LA50_ENGINE)/munt_compat -I$(MUNT_SRC)

ENGINE_SRCS := La50Engine La50Fx La50Pcm D50Sysex
MUNT_SRCS := LA32WaveGenerator LA32Ramp Tables
OBJS := $(ENGINE_SRCS:%=$(OBJ)/%.o) $(MUNT_SRCS:%=$(OBJ)/munt_%.o)
HEADERS := $(wildcard $(LA50_ENGINE)/*.hpp) $(wildcard $(MUNT_SRC)/*.h)

all: $(ENGINE_BUILD)/liblasynth.a

$(OBJ)/%.o: $(LA50_ENGINE)/%.cpp $(HEADERS)
	@mkdir -p $(dir $@)
	@echo "Compiling $(notdir $<)"
	@$(CXX) $(LA50_CXXFLAGS) $(CXXFLAGS) -c $< -o $@

$(OBJ)/munt_%.o: $(MUNT_SRC)/%.cpp $(HEADERS)
	@mkdir -p $(dir $@)
	@echo "Compiling $(notdir $<)"
	@$(CXX) $(MUNT_CXXFLAGS) $(CXXFLAGS) -c $< -o $@

$(ENGINE_BUILD)/liblasynth.a: $(OBJS)
	@echo "Creating $(notdir $@)"
	@rm -f $@
	@$(AR) rcs $@ $^

clean:
	rm -rf $(OBJ) $(ENGINE_BUILD)/liblasynth.a

.PHONY: all clean
