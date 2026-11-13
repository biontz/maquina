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

PLUGINS := kick tom snare cymbal hihat

all: plugins gen

# factory presets (presets/<voice>/*.maquina) -> compiled table + Machinista banks
presets:
	python3 tools/gen_presets.py
	python3 tools/gen_remote_pages.py

engine:
	$(MAKE) -C engine

# local fixes to the DPF submodule (patches/dpf/*.patch), applied once;
# a patch that is already applied is skipped
dpf-patches:
	@for p in $(sort $(wildcard $(CURDIR)/patches/dpf/*.patch)); do \
		if git -C dpf apply --reverse --check $$p 2>/dev/null; then :; \
		else echo "Applying DPF patch $$(basename $$p)"; git -C dpf apply $$p || exit 1; fi; \
	done

plugins: engine dpf-patches
	@for p in $(PLUGINS); do $(MAKE) -C plugins/$$p || exit 1; done

ifneq ($(CROSS_COMPILING),true)
gen: plugins dpf/utils/lv2_ttl_generator
	@$(CURDIR)/dpf/utils/generate-ttl.sh
else
gen:
endif

dpf/utils/lv2_ttl_generator:
	$(MAKE) -C dpf/utils/lv2-ttl-generator

tools: engine
	$(MAKE) -C tools/render_ref
	$(CC) -O2 -std=gnu11 $(shell pkg-config --cflags lilv-0) tools/lv2_render/lv2_render.c \
		$(shell pkg-config --libs lilv-0) -lm -o build/tools/lv2_render
	$(CXX) -O2 -std=gnu++17 -Iplugins/common tools/resampler_test/resampler_test.cpp -o build/tools/resampler_test
	$(CXX) -O1 -std=gnu++17 -Iplugins/common -Iengine -Iengine/fw_shim -Ithird_party/lxr-firmware/Core/Inc \
		tools/check_defaults/check_defaults.cpp plugins/common/EngineHost.cpp build/engine/libmaquinaengine.a \
		-lm -o build/tools/check_defaults
	$(CXX) -O2 -std=gnu++17 -Iplugins/common -Iengine/fw_shim -Ithird_party/lxr-firmware/Core/Inc \
		tools/check_random/check_random.cpp -o build/tools/check_random
	$(CXX) -O2 -std=gnu++17 -Iplugins/common -Iengine/fw_shim -Ithird_party/lxr-firmware/Core/Inc \
		tools/check_presets/check_presets.cpp -o build/tools/check_presets
	$(CXX) -O2 -std=gnu++17 -Iplugins/common -Iengine -Iengine/fw_shim -Ithird_party/lxr-firmware/Core/Inc \
		tools/preset_measure/preset_measure.cpp plugins/common/EngineHost.cpp build/engine/libmaquinaengine.a \
		-lm -o build/tools/preset_measure
	$(CC) -O1 -std=gnu11 -Idpf/distrho/src tools/clap_latency_check/clap_latency_check.c \
		-ldl -o build/tools/clap_latency_check
	$(CC) -O1 -std=gnu11 -Idpf/distrho/src tools/clap_remote_check/clap_remote_check.c \
		-ldl -o build/tools/clap_remote_check

# engine fidelity + isolation, state completeness, resampler quality, GUI
# defaults, randomizer rules, LV2 validation, every plugin rendered through a real
# LV2 host, CLAP state restore + activation without restart requests / latency-change reports,
# and the CLAP remote-controls pages
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

install: all
	mkdir -p ~/.lv2 ~/.clap ~/.vst3
	cp -r bin/*.lv2 ~/.lv2/
	cp -r bin/*.clap ~/.clap/
	cp -r bin/*.vst3 ~/.vst3/

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
	$(MAKE) -C engine $(A64_ENV) ENGINE_BUILD=$(CURDIR)/build/engine-aarch64
	@for p in $(PLUGINS); do $(MAKE) -C plugins/$$p $(A64_ENV) TARGETS="lv2_dsp clap vst3" || exit 1; done
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

clean:
	$(MAKE) -C engine clean
	$(MAKE) clean -C dpf/utils/lv2-ttl-generator
	rm -rf bin build

.PHONY: presets all engine dpf-patches plugins gen tools test install clean aarch64 test-aarch64
