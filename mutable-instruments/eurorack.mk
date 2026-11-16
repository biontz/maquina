# eurorack.mk - the vendored Mutable Instruments DSP code (third_party/eurorack,
# MIT) for Maquina Klang (Plaits) and Klong / Klong FX (Rings), shared by the
# library build (Makefile here) and the test tools. Include with MAQUINA_ROOT set.

EURORACK := $(MAQUINA_ROOT)/mutable-instruments/third_party/eurorack

STMLIB_SRCS := $(EURORACK)/stmlib/dsp/units.cc $(EURORACK)/stmlib/dsp/atan.cc $(EURORACK)/stmlib/utils/random.cc
KLANG_SRCS := $(sort $(wildcard $(EURORACK)/plaits/dsp/*.cc $(EURORACK)/plaits/dsp/*/*.cc)) \
	$(EURORACK)/plaits/resources.cc $(STMLIB_SRCS)
KLONG_SRCS := $(sort $(wildcard $(EURORACK)/rings/dsp/*.cc)) $(EURORACK)/rings/resources.cc $(STMLIB_SRCS)

# TEST: stmlib's portable code paths (no Arm inline assembly, no flash access;
# Plaits' UserData then has no user data, so the 6-op FM models use their
# built-in banks), as Mutable's own desktop tests and VCV Rack build it.
# plaits/user_data.h's TEST mock calls printf without including stdio.h.
EURORACK_CXXFLAGS := -std=gnu++17 -O2 -fPIC -fvisibility=hidden -w -DTEST -include stdio.h -I$(EURORACK)
