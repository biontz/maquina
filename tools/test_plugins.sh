#!/bin/sh
# test_plugins.sh - end-to-end tests of the built LV2 bundles.
#
#  1. engine: single-voice path == firmware mixer, instance isolation (render_ref)
#  2. per plugin, at the engine rate (44140 Hz): the plugin output must equal
#     the firmware engine's output sample for sample (notes aligned to blocks)
#  3. per plugin: two instances interleaved in one process do not interact
#  4. per plugin at 44.1/48/96 kHz: renders, finite, not silent
#
# Env: TOOLS (default build/tools), BIN (default bin), QEMU (runner prefix,
# e.g. "qemu-aarch64-static -L sysroot" for the cross build).
# SPDX-License-Identifier: GPL-3.0-only
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
TOOLS=${TOOLS:-build/tools}
BIN=${BIN:-bin}
OUT=build/test-out/$(basename "$BIN")
mkdir -p "$OUT" "build/lv2path-$(basename "$BIN")"
for b in "$BIN"/*.lv2; do ln -sfn "$ROOT/$b" "build/lv2path-$(basename "$BIN")/"; done
export LV2_PATH="$ROOT/build/lv2path-$(basename "$BIN")"
export MAQUINA_RNG_SEED=7

echo "== engine"
$QEMU "$TOOLS/render_ref"

fail=0
for p in kick:0 tom:1 snare:3 cymbal:4 hihat:5; do
    n=${p%%:*}; v=${p##*:}
    echo "== $n"
    $QEMU "$TOOLS/lv2_render" "urn:maquina:$n" 44140 4 "$OUT/$n-44140.wav" --aligned --block 100 >/dev/null
    $QEMU "$TOOLS/render_ref" --plugin-ref "$v" 4 "$OUT/$n-44140.raw"
    python3 tools/compare_ref.py "$OUT/$n-44140.wav" "$OUT/$n-44140.raw" || fail=1
    if [ "$n" = hihat ]; then
        echo "  hat_mode=Open"
        $QEMU "$TOOLS/lv2_render" "urn:maquina:$n" 44140 4 "$OUT/$n-open-44140.wav" --aligned --block 100 hat_mode=1 >/dev/null
        $QEMU "$TOOLS/render_ref" --plugin-ref "$v" 4 "$OUT/$n-open-44140.raw" open
        python3 tools/compare_ref.py "$OUT/$n-open-44140.wav" "$OUT/$n-open-44140.raw" || fail=1
    fi
    $QEMU "$TOOLS/lv2_render" "urn:maquina:$n" 48000 3 "$OUT/$n-twin.wav" --twin \
        twin:filter_freq=20 twin:volume=60 twin:osc_coarse=10 | sed -n 's/^  twin/  twin/p' || fail=1
    for rate in 44100 48000 96000; do
        $QEMU "$TOOLS/lv2_render" "urn:maquina:$n" $rate 3 "$OUT/$n-$rate.wav" | sed 's/^/  /' || fail=1
    done
done
[ $fail -eq 0 ] && echo "test_plugins: PASSED" || { echo "test_plugins: FAILED"; exit 1; }
