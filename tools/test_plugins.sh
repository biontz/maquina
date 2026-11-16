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
    $QEMU "$TOOLS/lv2_render" "urn:maquina:$n" 48000 3 "$OUT/$n-twin.wav" --twin \
        twin:filter_freq=20 twin:volume=60 twin:osc_coarse=10 | sed -n 's/^  twin/  twin/p' || fail=1
    for rate in 44100 48000 96000; do
        $QEMU "$TOOLS/lv2_render" "urn:maquina:$n" $rate 3 "$OUT/$n-$rate.wav" | sed 's/^/  /' || fail=1
    done
done

# Key Track: every note triggers (the bit-exact runs above play notes 60 and 46
# at the set pitch with it off); on, note 46 sounds 14 semitones lower
echo "== key track (tom)"
tone="osc_wave=0 pitch_amount=0 fm_amount=0 trans_volume=0 amp_decay=127 filter_freq=127 lfo_amount=0"
$QEMU "$TOOLS/lv2_render" urn:maquina:tom 48000 3 "$OUT/tom-kt-off.wav" $tone >/dev/null
$QEMU "$TOOLS/lv2_render" urn:maquina:tom 48000 3 "$OUT/tom-kt-on.wav" $tone note_pitch=1 >/dev/null
python3 - "$OUT" <<'PY' || fail=1
import array, sys
def crossings(name):
    a = array.array("f"); a.frombytes(open(f"{sys.argv[1]}/tom-kt-{name}.wav", "rb").read()[44:])
    l = a[0::2]
    return sum(1 for i in range(1, len(l)) if (l[i - 1] < 0) != (l[i] < 0))
got, want = crossings("on") / crossings("off"), (1 + 2 ** (-14 / 12)) / 2
ok = abs(got / want - 1) < 0.06
print(f"  notes 60/46 with Key Track on: x{got:.3f} (want ~x{want:.3f})  {'ok' if ok else 'FAIL'}")
sys.exit(0 if ok else 1)
PY

# Output gain (ExtraParams.hpp, every plugin): +6 / -12 dB scale the whole
# output (the parameter is set before the first block, so no ramp)
echo "== gain"
for n in kick tom snare cymbal hihat dredrum plethora oneiroi oneiroi-fx; do
    for g in 0 6 -12; do
        $QEMU "$TOOLS/lv2_render" urn:maquina:$n 48000 2 "$OUT/$n-gain$g.wav" gain=$g >/dev/null
    done
done
python3 - "$OUT" <<'PY' || fail=1
import array, math, sys
def rms(n, g):
    a = array.array("f"); a.frombytes(open(f"{sys.argv[1]}/{n}-gain{g}.wav", "rb").read()[44:])
    return math.sqrt(sum(x * x for x in a) / len(a))
ok = True
for n in ("kick", "tom", "snare", "cymbal", "hihat", "dredrum", "plethora", "oneiroi", "oneiroi-fx"):
    dbs = [20 * math.log10(rms(n, g) / rms(n, 0)) for g in (6, -12)]
    good = abs(dbs[0] - 6) < 0.05 and abs(dbs[1] + 12) < 0.05
    ok &= good
    print(f"  {n:10s} gain=+6: {dbs[0]:+.2f} dB, gain=-12: {dbs[1]:+.2f} dB  {'ok' if good else 'FAIL'}")
sys.exit(0 if ok else 1)
PY

# Maquina Dredrum: engine bit-exactness is tools/dredrum_ref (simavr); here the
# plugin: isolation, sample rates, and Gate mode holding the decay while the
# note is down (lv2_render releases every note after 0.15 s)
echo "== dredrum"
$QEMU "$TOOLS/lv2_render" urn:maquina:dredrum 48000 3 "$OUT/dredrum-twin.wav" --twin \
    twin:osc1_pitch=200 twin:osc2_level=255 twin:trig_mode=1 | sed -n 's/^  twin/  twin/p' || fail=1
for rate in 44100 48000 96000; do
    $QEMU "$TOOLS/lv2_render" urn:maquina:dredrum $rate 3 "$OUT/dredrum-$rate.wav" | sed 's/^/  /' || fail=1
done
$QEMU "$TOOLS/lv2_render" urn:maquina:dredrum 48000 3 "$OUT/dredrum-trig.wav" osc1_decay=0 >/dev/null
$QEMU "$TOOLS/lv2_render" urn:maquina:dredrum 48000 3 "$OUT/dredrum-gate.wav" osc1_decay=0 trig_mode=1 >/dev/null
python3 - "$OUT/dredrum-trig.wav" "$OUT/dredrum-gate.wav" <<'PY' || fail=1
import array, sys
def energy(path):
    a = array.array("f"); a.frombytes(open(path, "rb").read()[44:])
    return sum(x * x for x in a)
t, g = energy(sys.argv[1]), energy(sys.argv[2])
ok = g > 1.5 * t
print(f"  gate mode holds: energy gate/trig {g / t:.2f}  {'ok' if ok else 'FAIL'}")
sys.exit(0 if ok else 1)
PY

# Mute: Init only sounds on oscillator 1, so muting it must give silence
# (lv2_render fails on silence by design; here silence is the expected result)
$QEMU "$TOOLS/lv2_render" urn:maquina:dredrum 48000 2 "$OUT/dredrum-mute.wav" osc1_mute=1 >/dev/null || true
python3 - "$OUT/dredrum-mute.wav" <<'PY' || fail=1
import array, sys
a = array.array("f"); a.frombytes(open(sys.argv[1], "rb").read()[44:])
peak = max(abs(x) for x in a)
print(f"  mute: peak {peak:g}  {'ok' if peak == 0 else 'FAIL'}")
sys.exit(0 if peak == 0 else 1)
PY

# per-oscillator tuning: Octave +1 doubles the frequency (zero crossings),
# Key Track follows the pattern's notes 60 / 46 (60 = the set pitch)
tone="osc1_drop=0 osc1_decay=247 osc1_pitch=40"
$QEMU "$TOOLS/lv2_render" urn:maquina:dredrum 48000 3 "$OUT/dredrum-oct0.wav" $tone >/dev/null
$QEMU "$TOOLS/lv2_render" urn:maquina:dredrum 48000 3 "$OUT/dredrum-oct1.wav" $tone osc1_octave=1 >/dev/null
$QEMU "$TOOLS/lv2_render" urn:maquina:dredrum 48000 3 "$OUT/dredrum-semi7.wav" $tone osc1_semi=7 >/dev/null
$QEMU "$TOOLS/lv2_render" urn:maquina:dredrum 48000 3 "$OUT/dredrum-track.wav" $tone osc1_track=1 >/dev/null
python3 - "$OUT" <<'PY' || fail=1
import array, sys
def crossings(name):
    a = array.array("f"); a.frombytes(open(f"{sys.argv[1]}/dredrum-{name}.wav", "rb").read()[44:])
    l = a[0::2]
    return sum(1 for i in range(1, len(l)) if (l[i - 1] < 0) != (l[i] < 0))
c0, c1, c7, ct = (crossings(n) for n in ("oct0", "oct1", "semi7", "track"))
checks = [("octave +1", c1 / c0, 2.0), ("semi +7", c7 / c0, 2 ** (7 / 12)),
          ("key track (notes 60/46)", ct / c0, (1 + 2 ** (-14 / 12)) / 2)]
bad = 0
for name, got, want in checks:
    ok = abs(got / want - 1) < 0.03
    bad += not ok
    print(f"  tuning {name}: x{got:.3f} (want x{want:.3f})  {'ok' if ok else 'FAIL'}")
sys.exit(1 if bad else 0)
PY
# Maquina Plethora: isolation (per-instance random streams), sample rates,
# every algorithm on A and B audible and finite, Gate hold, Env Sync, Mute
echo "== plethora"
$QEMU "$TOOLS/lv2_render" urn:maquina:plethora 48000 3 "$OUT/plethora-twin.wav" --twin \
    twin:a_algo=20 twin:b_level=100 twin:c_level=90 | sed -n 's/^  twin/  twin/p' || fail=1
for rate in 44100 48000 96000; do
    $QEMU "$TOOLS/lv2_render" urn:maquina:plethora $rate 3 "$OUT/plethora-$rate.wav" b_level=100 c_level=100 \
        | sed 's/^/  /' || fail=1
done
bad=0
for i in $(seq 0 59); do
    $QEMU "$TOOLS/lv2_render" urn:maquina:plethora 44100 1 "$OUT/plethora-algo.wav" a_algo=$i a_decay=100 \
        >/dev/null || { echo "  A algorithm $i: silent or non-finite  FAIL"; bad=1; }
    $QEMU "$TOOLS/lv2_render" urn:maquina:plethora 44100 1 "$OUT/plethora-algo.wav" a_level=0 b_level=127 \
        b_algo=$i b_decay=100 >/dev/null || { echo "  B algorithm $i: silent or non-finite  FAIL"; bad=1; }
done
[ $bad -eq 0 ] && echo "  60 algorithms on A and B: audible, finite  ok" || fail=1
for v in "trig:" "gate:trig_mode=1" "sync:env_sync=1 a_decay=20 b_decay=110 b_level=127 a_level=0" \
         "nosync:a_decay=20 b_decay=110 b_level=127 a_level=0" "mute:a_mute=1"; do
    n=${v%%:*}
    $QEMU "$TOOLS/lv2_render" urn:maquina:plethora 48000 3 "$OUT/plethora-$n.wav" a_decay=20 ${v#*:} \
        >/dev/null || true
done
python3 - "$OUT" <<'PY' || fail=1
import array, sys
def energy(n):
    a = array.array("f"); a.frombytes(open(f"{sys.argv[1]}/plethora-{n}.wav", "rb").read()[44:])
    return sum(x * x for x in a)
t, g, s, ns, m = (energy(n) for n in ("trig", "gate", "sync", "nosync", "mute"))
checks = [("gate mode holds", g > 1.5 * t), ("env sync: B takes A's short decay", s < 0.5 * ns),
          ("mute", m == 0)]
for name, ok in checks:
    print(f"  {name}: {'ok' if ok else 'FAIL'}")
sys.exit(0 if all(ok for _, ok in checks) else 1)
PY

# Maquina Oneiroi / Oneiroi FX: engine fidelity and behaviour are
# tools/oneiroi_ref + tools/oneiroi_test; here the built plugins: isolation,
# sample rates, the FX processing its input (lv2_render feeds plucks)
echo "== oneiroi"
$QEMU "$TOOLS/lv2_render" urn:maquina:oneiroi 48000 3 "$OUT/oneiroi-twin.wav" --twin \
    twin:osc_pitch=30 twin:vol_looper=100 twin:mix_ambience=90 | sed -n 's/^  twin/  twin/p' || fail=1
$QEMU "$TOOLS/lv2_render" urn:maquina:oneiroi-fx 48000 3 "$OUT/oneiroi-fx-twin.wav" --twin \
    twin:mix_ambience=100 twin:vol_looper=80 twin:flt_reso=120 | sed -n 's/^  twin/  twin/p' || fail=1
for rate in 44100 48000 96000; do
    $QEMU "$TOOLS/lv2_render" urn:maquina:oneiroi $rate 3 "$OUT/oneiroi-$rate.wav" vol_osc2=90 mix_ambience=60 \
        mix_echo=50 mix_filter=127 | sed 's/^/  /' || fail=1
    $QEMU "$TOOLS/lv2_render" urn:maquina:oneiroi-fx $rate 3 "$OUT/oneiroi-fx-$rate.wav" mix_resonator=80 \
        mix_ambience=60 mix_echo=50 | sed 's/^/  /' || fail=1
done

[ $fail -eq 0 ] && echo "test_plugins: PASSED" || { echo "test_plugins: FAILED"; exit 1; }
