#!/usr/bin/make -f
# Builds Maquina Strings' engine (strings/engine/StringsEngine.cpp) and the vendored
# string-machine DSP (strings/third_party/string-machine: the Faust-generated blocks, the
# envelope, the Solina ensemble with its BBD / digital delays) as
#   $(ENGINE_BUILD)/libstrings.a
# Plaits' wavetable engine comes from libklang.a (mutable-instruments/Makefile), linked after
# it. ENGINE_BUILD and CXX as for drum02/engine (native, aarch64, macOS per arch).

MAQUINA_ROOT := $(abspath ..)
STRINGS_ENGINE := $(MAQUINA_ROOT)/stringmachine/strings/engine
SM := $(MAQUINA_ROOT)/stringmachine/strings/third_party/string-machine
EURORACK := $(MAQUINA_ROOT)/mutable-instruments/third_party/eurorack

CXX ?= c++
AR ?= ar
ENGINE_BUILD ?= $(MAQUINA_ROOT)/build/engine
OBJ := $(ENGINE_BUILD)/strings

# not bit-exact to anything: fast maths is fine (string-machine itself builds with it)
SM_INCLUDES := -I$(SM)/sources -I$(SM)/gen
OWN_CXXFLAGS := -std=gnu++17 -O2 -ffast-math -fPIC -fvisibility=hidden -Wall -Wextra -Wno-unused-local-typedefs \
	-I$(STRINGS_ENGINE) $(SM_INCLUDES) -I$(EURORACK) -DTEST
# the vendored code as it is (its own warnings are not ours to fix)
SM_CXXFLAGS := -std=gnu++17 -O2 -ffast-math -fPIC -fvisibility=hidden -w $(SM_INCLUDES)

OWN_SRCS := StringsEngine
SM_SRCS := gen/dsp/AsymWaveshaper gen/dsp/StringFiltersHighshelf \
	gen/dsp/LFO3PhaseDual gen/dsp/Delay3PhaseDigitalStereo sources/dsp/AHDSREnvelope \
	sources/dsp/Delay3PhaseStereo sources/bbd/bbd_filter sources/bbd/bbd_line sources/SolinaChorusStereo
OBJS := $(OWN_SRCS:%=$(OBJ)/%.o) $(SM_SRCS:%=$(OBJ)/sm/%.o)
HEADERS := $(wildcard $(STRINGS_ENGINE)/*.hpp) $(wildcard $(SM)/sources/*.h $(SM)/sources/*/*.h $(SM)/gen/dsp/*.hpp)

all: $(ENGINE_BUILD)/libstrings.a

$(OBJ)/%.o: $(STRINGS_ENGINE)/%.cpp $(HEADERS)
	@mkdir -p $(dir $@)
	@echo "Compiling $(notdir $<)"
	@$(CXX) $(OWN_CXXFLAGS) $(CXXFLAGS) -c $< -o $@

$(OBJ)/sm/%.o: $(SM)/%.cpp $(HEADERS)
	@mkdir -p $(dir $@)
	@echo "Compiling $(notdir $<)"
	@$(CXX) $(SM_CXXFLAGS) $(CXXFLAGS) -c $< -o $@

$(ENGINE_BUILD)/libstrings.a: $(OBJS)
	@echo "Creating $(notdir $@)"
	@rm -f $@
	@$(AR) rcs $@ $^

clean:
	rm -rf $(OBJ) $(ENGINE_BUILD)/libstrings.a

.PHONY: all clean
