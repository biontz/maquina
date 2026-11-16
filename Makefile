#!/usr/bin/make -f
# Maquina - drum voice plugins from the LXR-02 firmware sound engine.
#
#   make            engine + all plugins (LV2, CLAP, VST3, JACK) + LV2 TTL
#   make test       engine fidelity/isolation tests + LV2 validation
#   make install    copy bundles to ~/.lv2 ~/.clap ~/.vst3
#   make aarch64    cross-build for Raspberry Pi OS 64-bit into bin-aarch64/
#                   (clang + lld + a Debian bookworm sysroot, no root needed)
#   make test-aarch64  build + run the engine tests under qemu-aarch64

include dpf/Makefile.base.mk

MAQUINA_ROOT := $(CURDIR)
include common/paths.mk

all: plugins gen

# factory presets (<module>/presets/<voice>/*.maquina) -> compiled table + Machinista banks
presets:
	python3 tools/gen_presets.py
	python3 tools/gen_remote_pages.py

engine:
	$(MAKE) -C drum02/engine
	$(MAKE) -C mutable-instruments

# local fixes to the DPF submodule (patches/dpf/*.patch), applied once;
# a patch that is already applied is skipped
dpf-patches:
	@for p in $(sort $(wildcard $(CURDIR)/patches/dpf/*.patch)); do \
		if git -C dpf apply --reverse --check $$p 2>/dev/null; then :; \
		else echo "Applying DPF patch $$(basename $$p)"; git -C dpf apply $$p || exit 1; fi; \
	done

plugins: engine dpf-patches
	@for p in $(PLUGIN_DIRS); do $(MAKE) -C $$p || exit 1; done

ifneq ($(CROSS_COMPILING),true)
gen: plugins dpf/utils/lv2_ttl_generator
	@$(CURDIR)/dpf/utils/generate-ttl.sh
else
gen:
endif

dpf/utils/lv2_ttl_generator:
	$(MAKE) -C dpf/utils/lv2-ttl-generator

# Maquina Oneiroi test tools: the vendored engine + OWL library (see common/plugin.mk)
ONEIROI_FLAGS := -O2 -std=gnu++17 -fno-strict-aliasing -w $(ONEIROI_INCLUDES) $(MAQUINA_INCLUDES)
# Maquina Klang / Klong test tool: both hosts + the Mutable libraries (ENGINE_DIR: per arch)
MUTABLE_TEST_SRCS := tools/mutable_test/mutable_test.cpp $(KLANG_DIR)/plugins/klang/Engine.cpp \
	$(KLONG_DIR)/plugins/klong/Engine.cpp
MUTABLE_TEST_FLAGS = -O2 -std=gnu++17 -DTEST -w $(EURORACK_INCLUDES) $(MAQUINA_INCLUDES) $(MUTABLE_TEST_SRCS) \
	$(ENGINE_DIR)/libklang.a $(ENGINE_DIR)/libklong.a
tools: ENGINE_DIR := build/engine
test-aarch64: ENGINE_DIR := build/engine-aarch64

tools: engine
	$(MAKE) -C tools/render_ref
	$(CC) -O2 -std=gnu11 $(shell pkg-config --cflags lilv-0) tools/lv2_render/lv2_render.c \
		$(shell pkg-config --libs lilv-0) -lm -o build/tools/lv2_render
	$(CXX) -O2 -std=gnu++17 $(MAQUINA_INCLUDES) tools/resampler_test/resampler_test.cpp -o build/tools/resampler_test
	$(CXX) -O1 -std=gnu++17 $(MAQUINA_INCLUDES) \
		tools/check_defaults/check_defaults.cpp drum02/engine/EngineHost.cpp build/engine/libmaquinaengine.a \
		-lm -o build/tools/check_defaults
	$(CXX) -O2 -std=gnu++17 $(MAQUINA_INCLUDES) tools/check_random/check_random.cpp -o build/tools/check_random
	$(CXX) -O2 -std=gnu++17 $(MAQUINA_INCLUDES) tools/check_presets/check_presets.cpp -o build/tools/check_presets
	$(CXX) -O2 -std=gnu++17 $(MAQUINA_INCLUDES) $(PLETHORA_INCLUDES) \
		tools/preset_measure/preset_measure.cpp drum02/engine/EngineHost.cpp build/engine/libmaquinaengine.a \
		euro-modules/dredrum/engine/DredrumHost.cpp -x c++ euro-modules/dredrum/engine/dredrum_core.c -x none \
		befaco/plethora/plugins/plethora/Engine.cpp $(EURORACK_INCLUDES) $(KLANG_DIR)/plugins/klang/Engine.cpp \
		$(KLONG_DIR)/plugins/klong/Engine.cpp build/engine/libklang.a build/engine/libklong.a \
		-lm -o build/tools/preset_measure
	$(CC) -O1 -std=gnu11 -Idpf/distrho/src tools/clap_latency_check/clap_latency_check.c \
		-ldl -o build/tools/clap_latency_check
	$(CC) -O1 -std=gnu11 -Idpf/distrho/src tools/clap_remote_check/clap_remote_check.c \
		-ldl -o build/tools/clap_remote_check
	$(CXX) $(ONEIROI_FLAGS) tools/oneiroi_ref/oneiroi_ref.cpp -o build/tools/oneiroi_ref
	$(CXX) $(ONEIROI_FLAGS) tools/oneiroi_test/oneiroi_test.cpp -o build/tools/oneiroi_test
	$(CXX) $(MUTABLE_TEST_FLAGS) -o build/tools/mutable_test

# engine fidelity + isolation, state completeness, resampler quality, GUI
# defaults, randomizer rules, LV2 validation, every plugin rendered through a real
# LV2 host, CLAP state restore + activation without restart requests / latency-change reports,
# the CLAP remote-controls pages, and the engine fidelity + behaviour of Maquina
# Oneiroi, Klang and Klong
test: all tools
	tools/check_state.sh
	build/tools/resampler_test
	build/tools/check_defaults
	build/tools/check_random
	build/tools/check_presets
	@lv2_validate $(wildcard bin/*.lv2/*.ttl) 2>&1 | tools/lv2_validate_filter.sh
	tools/test_plugins.sh
	build/tools/clap_latency_check $(wildcard bin/*.clap)
	build/tools/clap_remote_check $(wildcard bin/*.clap)
	build/tools/oneiroi_ref
	build/tools/oneiroi_test
	build/tools/mutable_test
	build/tools/mutable_test --mutate | tail -n 1
	@if command -v avr-gcc >/dev/null && [ -e /usr/include/simavr/sim_avr.h ]; then \
		$(MAKE) --no-print-directory dredrum-check; \
	else echo "dredrum-check: SKIPPED (needs avr-gcc + simavr)"; fi

# Maquina Oneiroi: OneiroiCore == the module's Oneiroi.h (bit-exact), and the
# check notices a 1-ulp change
oneiroi-check:
	@mkdir -p build/tools
	$(CXX) $(ONEIROI_FLAGS) tools/oneiroi_ref/oneiroi_ref.cpp -o build/tools/oneiroi_ref
	build/tools/oneiroi_ref
	build/tools/oneiroi_ref --mutate | tail -n 1

# Maquina Dredrum: the noise wave's program-memory bytes (needs avr-gcc;
# output checked in) and the bit-exact check against the firmware on simavr
dredrum-flash:
	python3 tools/dredrum_flash/gen_dredrum_flash.py

SIMAVR_LIBS ?= $(shell pkg-config --libs simavr 2>/dev/null || echo -lsimavr -lelf)
dredrum-check:
	python3 tools/dredrum_flash/gen_dredrum_flash.py --check
	@mkdir -p build/tools
	$(CC) -O2 -std=gnu11 -Ieuro-modules/dredrum/engine tools/dredrum_ref/dredrum_ref.c \
		euro-modules/dredrum/engine/dredrum_core.c \
		$(SIMAVR_LIBS) -o build/tools/dredrum_ref
	build/tools/dredrum_ref build/dredrum

# Each file is copied to a temp name and renamed over the old one: a running
# host (Bitwig) keeps the old inode it has mapped. Copying over a loaded .so in
# place (plain cp) crashes the host's plugin process.
install: all
	@cd bin && for f in $$(find *.lv2 *.clap *.vst3 -type f); do \
		case $$f in *.lv2/*) d=~/.lv2;; *.clap) d=~/.clap;; *) d=~/.vst3;; esac; \
		mkdir -p "$$d/$$(dirname $$f)" && cp "$$f" "$$d/$$f.tmp$$$$" && mv -f "$$d/$$f.tmp$$$$" "$$d/$$f" || exit 1; \
	done; echo "installed to ~/.lv2 ~/.clap ~/.vst3"

# ---------------------------------------------------------------------------
# Raspberry Pi (aarch64) cross build

SYSROOT_A64 ?= $(CURDIR)/build/sysroot-aarch64
A64_FLAGS := --target=aarch64-linux-gnu --sysroot=$(SYSROOT_A64) -fuse-ld=lld
A64_ENV := CC="clang $(A64_FLAGS)" CXX="clang++ $(A64_FLAGS)" AR=llvm-ar \
	PKG_CONFIG=false CROSS_COMPILING=true MAQUINA_ARCH=aarch64 MAQUINA_NO_UI=true \
	EXTRA_CFLAGS="-mtune=cortex-a72" CXXFLAGS="-mtune=cortex-a72"
QEMU_A64 := qemu-aarch64-static -L $(SYSROOT_A64)

$(SYSROOT_A64)/usr/lib/aarch64-linux-gnu/libc.so:
	python3 tools/aarch64/make_sysroot.py $(SYSROOT_A64)

aarch64: $(SYSROOT_A64)/usr/lib/aarch64-linux-gnu/libc.so dpf-patches
	$(MAKE) -C drum02/engine $(A64_ENV) ENGINE_BUILD=$(CURDIR)/build/engine-aarch64
	$(MAKE) -C mutable-instruments $(A64_ENV) ENGINE_BUILD=$(CURDIR)/build/engine-aarch64
	@for p in $(PLUGIN_DIRS); do $(MAKE) -C $$p $(A64_ENV) TARGETS="lv2_dsp clap vst3" || exit 1; done
	clang $(A64_FLAGS) dpf/utils/lv2-ttl-generator/lv2_ttl_generator.c -ldl -o build/aarch64/lv2_ttl_generator
	@for b in bin-aarch64/*.lv2; do \
		(cd $$b && $(QEMU_A64) $(CURDIR)/build/aarch64/lv2_ttl_generator ./$$(ls *.so | head -n1)) || exit 1; \
	done

test-aarch64: aarch64
	$(MAKE) -C tools/render_ref CC="clang $(A64_FLAGS)" ENGINE_BUILD=$(CURDIR)/build/engine-aarch64 \
		BUILD=$(CURDIR)/build/tools-aarch64 $(CURDIR)/build/tools-aarch64/render_ref
	clang $(A64_FLAGS) -O2 -std=gnu11 -I$(SYSROOT_A64)/usr/include/lilv-0 tools/lv2_render/lv2_render.c \
		-llilv-0 -lm -o build/tools-aarch64/lv2_render
	QEMU="$(QEMU_A64)" TOOLS=build/tools-aarch64 BIN=bin-aarch64 tools/test_plugins.sh
	clang++ $(A64_FLAGS) $(ONEIROI_FLAGS) tools/oneiroi_test/oneiroi_test.cpp -o build/tools-aarch64/oneiroi_test
	$(QEMU_A64) build/tools-aarch64/oneiroi_test
	clang++ $(A64_FLAGS) $(MUTABLE_TEST_FLAGS) -o build/tools-aarch64/mutable_test
	$(QEMU_A64) build/tools-aarch64/mutable_test

# ---------------------------------------------------------------------------
# macOS (on a Mac; CI: .github/workflows/macos.yml): universal CLAP + VST3
#
#   make macos         MACOS_PLUGINS for x86_64 + arm64, merged into bin-macos/
#   make macos-test    CLAP checks per arch (x86_64 under Rosetta on Apple
#                      Silicon), universal binaries, signatures, exports
#   make macos-dist    bin-macos/ -> dist/Maquina-macOS-<version>.zip
#
# Each arch is a full build of its own (bin-macos-<arch>/, own engine library:
# x86_64 gets the Arm float->int pass, arm64 compiles the firmware untouched);
# tools/macos/lipo_bundles.sh merges them and signs ad hoc. -target (not
# -arch) so DPF's `$(CC) -dumpmachine` sees the arch it builds for.

MACOS_PLUGINS ?= befaco/plethora/plugins/plethora
MACOS_ARCHS := x86_64 arm64
MACOS_MIN := 10.15
macos_cc = -target $(1)-apple-macos$(MACOS_MIN)
# BUILD_DIR_SUFFIX: DPF builds its UI library (DGL) in dpf/build<suffix>,
# shared by every plugin; one per arch, or the second arch links the first's
macos_env = CC="clang $(call macos_cc,$(1))" CXX="clang++ $(call macos_cc,$(1))" \
	MAQUINA_ARCH=macos-$(1) ARM_TARGET=$(if $(filter arm64,$(1)),true,false) BUILD_DIR_SUFFIX=-macos-$(1)
MACOS_VERSION = $(shell git describe --tags --always --dirty)

macos-x86_64 macos-arm64: macos-%: dpf-patches
	$(MAKE) -C drum02/engine $(call macos_env,$*) ENGINE_BUILD=$(CURDIR)/build/engine-macos-$*
	$(MAKE) -C mutable-instruments $(call macos_env,$*) ENGINE_BUILD=$(CURDIR)/build/engine-macos-$*
	@for p in $(MACOS_PLUGINS); do $(MAKE) -C $$p $(call macos_env,$*) TARGETS="clap vst3" || exit 1; done

macos: $(MACOS_ARCHS:%=macos-%)
	tools/macos/lipo_bundles.sh bin-macos $(MACOS_ARCHS)

macos-test:
	@mkdir -p build/tools-macos
	@for a in $(MACOS_ARCHS); do \
		for t in clap_latency_check clap_remote_check; do \
			clang $(call macos_cc,$$a) -O1 -std=gnu11 -Idpf/distrho/src tools/$$t/$$t.c \
				-o build/tools-macos/$$t-$$a || exit 1; \
		done; \
		echo "== $$a"; \
		arch -$$a build/tools-macos/clap_latency_check-$$a bin-macos/*.clap || exit 1; \
		arch -$$a build/tools-macos/clap_remote_check-$$a bin-macos/*.clap || exit 1; \
	done
	tools/macos/check_bundles.sh bin-macos $(MACOS_ARCHS)

macos-dist:
	@mkdir -p dist
	rm -f dist/Maquina-macOS-$(MACOS_VERSION).zip
	cd bin-macos && zip -qry ../dist/Maquina-macOS-$(MACOS_VERSION).zip *.clap *.vst3
	zip -qj dist/Maquina-macOS-$(MACOS_VERSION).zip tools/macos/INSTALL-macOS.txt
	@ls -l dist/

clean:
	$(MAKE) -C drum02/engine clean
	$(MAKE) -C mutable-instruments clean
	$(MAKE) clean -C dpf/utils/lv2-ttl-generator
	rm -rf bin build bin-macos bin-macos-* dist

.PHONY: presets all engine dpf-patches plugins gen tools test install clean aarch64 test-aarch64 dredrum-flash dredrum-check \
	oneiroi-check macos macos-test macos-dist
