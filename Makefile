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
	$(MAKE) -C openrb
	$(MAKE) -C s1vave
	$(MAKE) -C lasynth
	$(MAKE) -C stringmachine

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
	@python3 tools/lv2_preset_banks.py bin/*.lv2
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
# Maquina Acida / Acida FX: the port (libacida.a) against OpenRB-338's Rust
# engine (tools/acida_ref, cargo) + AcidaHost's behaviour
ACIDA_TEST_FLAGS := -O2 -std=gnu++17 -ffp-contract=off $(MAQUINA_INCLUDES) tools/acida_test/acida_test.cpp \
	$(ACIDA_DIR)/plugins/acida/Engine.cpp
ACIDA_SCENARIOS := $(sort $(wildcard tools/acida_ref/scenarios/*.txt))
# Maquina BL-101 / BL-101 FX: the port (libbl101.a) against S1Vave's firmware engine
# compiled for this computer (tools/bl101_ref) + Bl101Host's behaviour
BL101_TEST_FLAGS := -O2 -std=gnu++17 -fwrapv $(MAQUINA_INCLUDES) -Itools/bl101_ref tools/bl101_test/bl101_test.cpp \
	$(BL101_DIR)/plugins/bl101/Engine.cpp
BL101_SCENARIOS := $(sort $(wildcard tools/bl101_ref/scenarios/*.txt))
# Maquina LA-50: behaviour checks + the C++ .syx reader against tools/d50_syx.py
LA50_TEST_FLAGS := -O2 -std=gnu++17 $(MAQUINA_INCLUDES) tools/la50_test/la50_test.cpp $(LA50_DIR)/plugins/la50/Engine.cpp
# Maquina Strings: behaviour checks (pitch, layers, voices, vibrato, effects, fuzz, host) and the CPU bench
STRINGS_TEST_FLAGS := -O2 -std=gnu++17 $(MAQUINA_INCLUDES) tools/strings_test/strings_test.cpp \
	$(STRINGS_DIR)/plugins/strings/Engine.cpp
STRINGS_BENCH_FLAGS := -O2 -std=gnu++17 -I$(STRINGS_DIR)/engine tools/strings_bench/strings_bench.cpp
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
		$(ACIDA_DIR)/plugins/acida/Engine.cpp build/engine/libacida.a \
		$(BL101_DIR)/plugins/bl101/Engine.cpp build/engine/libbl101.a \
		$(LA50_DIR)/plugins/la50/Engine.cpp build/engine/liblasynth.a \
		$(STRINGS_DIR)/plugins/strings/Engine.cpp build/engine/libstrings.a build/engine/libklang.a \
		-lm -o build/tools/preset_measure
	$(CC) -O1 -std=gnu11 -Idpf/distrho/src tools/clap_latency_check/clap_latency_check.c \
		-ldl -o build/tools/clap_latency_check
	$(CC) -O1 -std=gnu11 -Idpf/distrho/src tools/clap_remote_check/clap_remote_check.c \
		-ldl -o build/tools/clap_remote_check
	$(CXX) $(ONEIROI_FLAGS) tools/oneiroi_ref/oneiroi_ref.cpp -o build/tools/oneiroi_ref
	$(CXX) $(ONEIROI_FLAGS) tools/oneiroi_test/oneiroi_test.cpp -o build/tools/oneiroi_test
	$(CXX) $(MUTABLE_TEST_FLAGS) -o build/tools/mutable_test
	$(CXX) $(ACIDA_TEST_FLAGS) build/engine/libacida.a -o build/tools/acida_test
	$(CXX) $(BL101_TEST_FLAGS) build/engine/libbl101.a -o build/tools/bl101_test
	$(CXX) $(LA50_TEST_FLAGS) build/engine/liblasynth.a -o build/tools/la50_test
	$(CXX) $(STRINGS_TEST_FLAGS) build/engine/libstrings.a build/engine/libklang.a -o build/tools/strings_test
	$(CXX) $(STRINGS_BENCH_FLAGS) build/engine/libstrings.a build/engine/libklang.a -o build/tools/strings_bench

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
	build/tools/acida_test --host
	$(MAKE) --no-print-directory bl101-check
	$(MAKE) --no-print-directory la50-check
	build/tools/strings_test
	@if command -v cargo >/dev/null; then $(MAKE) --no-print-directory acida-check; \
	else echo "acida-check: SKIPPED (needs cargo for the Rust reference)"; fi
	@if command -v avr-gcc >/dev/null && [ -e /usr/include/simavr/sim_avr.h ]; then \
		$(MAKE) --no-print-directory dredrum-check; \
	else echo "dredrum-check: SKIPPED (needs avr-gcc + simavr)"; fi

# Maquina Acida / Acida FX: the reference output of OpenRB-338's own Rust engine
# for every scenario (tools/acida_ref; needs cargo, fetches the pinned libm crate)
acida-ref:
	cargo build --release --quiet --manifest-path tools/acida_ref/Cargo.toml --target-dir build/acida_ref
	@mkdir -p build/acida_ref/out
	build/acida_ref/release/acida_ref --tables build/acida_ref/out/tables.f32
	@for s in $(ACIDA_SCENARIOS); do \
		build/acida_ref/release/acida_ref $$s build/acida_ref/out/$$(basename $$s .txt).f32 || exit 1; \
	done

# the C++ port == the Rust engine, bit for bit (tables + scenarios), and the
# check notices a 1-ulp change
acida-check: acida-ref
	build/tools/acida_test --tables build/acida_ref/out/tables.f32
	build/tools/acida_test build/acida_ref/out $(ACIDA_SCENARIOS)
	@if build/tools/acida_test --mutate build/acida_ref/out $(firstword $(ACIDA_SCENARIOS)) >/dev/null; then \
		echo "acida-check: --mutate was not detected"; exit 1; else echo "acida-check: --mutate detected"; fi

# Maquina BL-101 / BL-101 FX: the reference output of S1Vave's own firmware engine
# (unmodified, built for this computer) for every scenario
bl101-ref:
	@mkdir -p build/tools build/bl101_ref/out
	$(CC) -O2 -std=gnu11 -fwrapv -ffp-contract=off -w $(S1VAVE_INCLUDES) -Itools/bl101_ref \
		tools/bl101_ref/bl101_ref.c -o build/tools/bl101_ref
	@for s in $(BL101_SCENARIOS); do \
		build/tools/bl101_ref $$s build/bl101_ref/out/$$(basename $$s .txt).raw || exit 1; \
	done

# the C++ port == the firmware, bit for bit (also in uneven host-sized blocks),
# the check notices a 1-step change, and Bl101Host's behaviour
bl101-check: bl101-ref
	build/tools/bl101_test build/bl101_ref/out $(BL101_SCENARIOS)
	build/tools/bl101_test --blocks build/bl101_ref/out $(BL101_SCENARIOS) | tail -n 1
	@if build/tools/bl101_test --mutate build/bl101_ref/out $(firstword $(BL101_SCENARIOS)) >/dev/null; then \
		echo "bl101-check: --mutate was not detected"; exit 1; else echo "bl101-check: --mutate detected"; fi
	build/tools/bl101_test --host

# Maquina LA-50: the engine's behaviour, and the C++ and Python .syx readers agree
# on a 64-patch bank (random values, out-of-range bytes)
la50-check:
	build/tools/la50_test
	@mkdir -p build/la50
	python3 tools/la50_test/make_bank.py build/la50/test-bank.syx
	build/tools/la50_test --dump build/la50/test-bank.syx > build/la50/cpp.txt
	python3 tools/la50_test/syx_dump.py build/la50/test-bank.syx > build/la50/py.txt
	@if cmp -s build/la50/cpp.txt build/la50/py.txt && [ $$(wc -l < build/la50/cpp.txt) -eq 64 ]; then \
		echo "la50-check: C++ and Python read the bank alike (64 patches)"; \
	else echo "la50-check: the .syx readers differ"; exit 1; fi

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
	$(MAKE) -C openrb $(A64_ENV) ENGINE_BUILD=$(CURDIR)/build/engine-aarch64
	$(MAKE) -C s1vave $(A64_ENV) ENGINE_BUILD=$(CURDIR)/build/engine-aarch64
	$(MAKE) -C lasynth $(A64_ENV) ENGINE_BUILD=$(CURDIR)/build/engine-aarch64
	$(MAKE) -C stringmachine $(A64_ENV) ENGINE_BUILD=$(CURDIR)/build/engine-aarch64
	@for p in $(PLUGIN_DIRS); do $(MAKE) -C $$p $(A64_ENV) TARGETS="lv2_dsp clap vst3" || exit 1; done
	clang $(A64_FLAGS) dpf/utils/lv2-ttl-generator/lv2_ttl_generator.c -ldl -o build/aarch64/lv2_ttl_generator
	@for b in bin-aarch64/*.lv2; do \
		(cd $$b && $(QEMU_A64) $(CURDIR)/build/aarch64/lv2_ttl_generator ./$$(ls *.so | head -n1)) || exit 1; \
	done
	python3 tools/lv2_preset_banks.py bin-aarch64/*.lv2

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
	clang++ $(A64_FLAGS) $(ACIDA_TEST_FLAGS) build/engine-aarch64/libacida.a -o build/tools-aarch64/acida_test
	$(QEMU_A64) build/tools-aarch64/acida_test --host
	@if command -v cargo >/dev/null; then \
		$(MAKE) --no-print-directory acida-ref && \
		$(QEMU_A64) build/tools-aarch64/acida_test --tables build/acida_ref/out/tables.f32 && \
		$(QEMU_A64) build/tools-aarch64/acida_test build/acida_ref/out $(ACIDA_SCENARIOS); \
	else echo "acida-check (aarch64): SKIPPED (needs cargo for the Rust reference)"; fi
	clang++ $(A64_FLAGS) $(BL101_TEST_FLAGS) build/engine-aarch64/libbl101.a -o build/tools-aarch64/bl101_test
	$(MAKE) --no-print-directory bl101-ref
	$(QEMU_A64) build/tools-aarch64/bl101_test build/bl101_ref/out $(BL101_SCENARIOS)
	$(QEMU_A64) build/tools-aarch64/bl101_test --host
	clang++ $(A64_FLAGS) $(LA50_TEST_FLAGS) build/engine-aarch64/liblasynth.a -o build/tools-aarch64/la50_test
	$(QEMU_A64) build/tools-aarch64/la50_test
	clang++ $(A64_FLAGS) $(STRINGS_TEST_FLAGS) build/engine-aarch64/libstrings.a build/engine-aarch64/libklang.a \
		-o build/tools-aarch64/strings_test
	$(QEMU_A64) build/tools-aarch64/strings_test

# Maquina Strings: the engine's CPU load per scenario on this computer, with a Pi 4 estimate
# (tools/strings_bench: load x 5, see there)
strings-bench: tools
	build/tools/strings_bench

# ---------------------------------------------------------------------------
# macOS (on a Mac; CI: .github/workflows/macos.yml): universal CLAP + VST3
#
#   make macos         MACOS_PLUGINS for x86_64 + arm64, merged into bin-macos/
#   make macos-test    CLAP checks per arch (x86_64 under Rosetta on Apple
#                      Silicon), universal binaries, signatures, exports
#   make macos-dist    bin-macos/ -> dist/Maquina-macOS-<version>.zip
#   make macos-dmg     bin-macos/ -> dist/Maquina-<version>-macOS.dmg (+ install test)
#   make macos-dmg-each  one dmg per instrument (+ its FX): dist/Maquina-<Name>-<version>-macOS.dmg
#
# Each arch is a full build of its own (bin-macos-<arch>/, own engine library:
# x86_64 gets the Arm float->int pass, arm64 compiles the firmware untouched);
# tools/macos/lipo_bundles.sh merges them and signs ad hoc. -target (not
# -arch) so DPF's `$(CC) -dumpmachine` sees the arch it builds for.

MACOS_PLUGINS ?= $(PLUGIN_DIRS)
MACOS_ARCHS := x86_64 arm64
MACOS_MIN := 10.15
macos_cc = -target $(1)-apple-macos$(MACOS_MIN)
# SKIP_STRIPPING: keep local symbols, so crash reports can be symbolicated.
# BUILD_DIR_SUFFIX: DPF builds its UI library (DGL) in dpf/build<suffix>,
# shared by every plugin; one per arch, or the second arch links the first's
macos_env = CC="clang $(call macos_cc,$(1))" CXX="clang++ $(call macos_cc,$(1))" \
	MAQUINA_ARCH=macos-$(1) ARM_TARGET=$(if $(filter arm64,$(1)),true,false) BUILD_DIR_SUFFIX=-macos-$(1) \
	SKIP_STRIPPING=true
# the commit (or tag); -dirty only for uncommitted changes in this repository,
# not for the DPF patches applied in the dpf submodule's working tree
MACOS_VERSION = $(shell git describe --tags --always)$(shell git diff --quiet --ignore-submodules=dirty HEAD -- || echo -dirty)

macos-x86_64 macos-arm64: macos-%: dpf-patches
	$(MAKE) -C drum02/engine $(call macos_env,$*) ENGINE_BUILD=$(CURDIR)/build/engine-macos-$*
	$(MAKE) -C mutable-instruments $(call macos_env,$*) ENGINE_BUILD=$(CURDIR)/build/engine-macos-$*
	$(MAKE) -C openrb $(call macos_env,$*) ENGINE_BUILD=$(CURDIR)/build/engine-macos-$*
	$(MAKE) -C s1vave $(call macos_env,$*) ENGINE_BUILD=$(CURDIR)/build/engine-macos-$*
	$(MAKE) -C lasynth $(call macos_env,$*) ENGINE_BUILD=$(CURDIR)/build/engine-macos-$*
	$(MAKE) -C stringmachine $(call macos_env,$*) ENGINE_BUILD=$(CURDIR)/build/engine-macos-$*
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
	done
	@# one process per plugin and check, so a crash names its plugin and the
	@# others still run; fails at the end if any failed
	@fail=0; for a in $(MACOS_ARCHS); do \
		echo "== $$a"; \
		for c in bin-macos/*.clap; do \
			for t in clap_latency_check clap_remote_check; do \
				arch -$$a build/tools-macos/$$t-$$a $$c || { echo "FAILED: $$t $$a $$c"; fail=1; \
					tools/macos/crash_report.sh $$t-$$a; }; \
			done; \
		done; \
	done; \
	tools/macos/check_bundles.sh bin-macos $(MACOS_ARCHS) || fail=1; \
	exit $$fail

macos-dist:
	@mkdir -p dist
	rm -f dist/Maquina-macOS-$(MACOS_VERSION).zip
	cd bin-macos && zip -qry ../dist/Maquina-macOS-$(MACOS_VERSION).zip *.clap *.vst3
	zip -qj dist/Maquina-macOS-$(MACOS_VERSION).zip tools/macos/README.md tools/macos/INSTALL-macOS.txt tools/macos/INSTALL-with-AI.md LICENSE
	cd docs && zip -qr ../dist/Maquina-macOS-$(MACOS_VERSION).zip screenshots
	@ls -l dist/

# one universal disk image: the plugins, an installer script and the notes
# (tools/macos/make_dmg.sh), then mount it and test-install into a temp HOME
macos-dmg:
	@mkdir -p dist
	tools/macos/make_dmg.sh bin-macos dist/Maquina-$(MACOS_VERSION)-macOS.dmg "Maquina $(MACOS_VERSION)"
	tools/macos/test_dmg.sh dist/Maquina-$(MACOS_VERSION)-macOS.dmg

# one small disk image per instrument, its FX version included (the GitHub release's
# single downloads): dist/Maquina-<Name>-<version>-macOS.dmg, each with the installer,
# the notes and an install test. Groups whose bundles are not in bin-macos/ are skipped.
MACOS_DMG_GROUPS := Kick Tom Snare Cymbal HiHat Dredrum Plethora Oneiroi Klang Klong Acida BL101 LA50 Strings
macos-dmg-each:
	@mkdir -p dist build
	@for g in $(MACOS_DMG_GROUPS); do \
		[ -d bin-macos/Maquina$$g.clap ] || { echo "skip $$g (not built)"; continue; }; \
		st=build/dmg-$$g; rm -rf $$st; mkdir -p $$st; \
		for b in bin-macos/Maquina$$g.clap bin-macos/Maquina$$g.vst3 \
		         bin-macos/Maquina$${g}FX.clap bin-macos/Maquina$${g}FX.vst3; do \
			[ -d $$b ] && ditto $$b $$st/$$(basename $$b); \
		done; \
		tools/macos/make_dmg.sh $$st dist/Maquina-$$g-$(MACOS_VERSION)-macOS.dmg "Maquina $$g $(MACOS_VERSION)" || exit 1; \
		tools/macos/test_dmg.sh dist/Maquina-$$g-$(MACOS_VERSION)-macOS.dmg || exit 1; \
		rm -rf $$st; \
	done

clean:
	$(MAKE) -C drum02/engine clean
	$(MAKE) -C mutable-instruments clean
	$(MAKE) -C openrb clean
	$(MAKE) -C s1vave clean
	$(MAKE) -C lasynth clean
	$(MAKE) -C stringmachine clean
	$(MAKE) clean -C dpf/utils/lv2-ttl-generator
	rm -rf bin build bin-macos bin-macos-* dist

.PHONY: presets all engine dpf-patches plugins gen tools test install clean aarch64 test-aarch64 dredrum-flash dredrum-check \
	oneiroi-check acida-ref acida-check bl101-ref bl101-check la50-check strings-bench macos macos-test macos-dist macos-dmg macos-dmg-each
