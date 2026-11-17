#!/usr/bin/env python3
"""
s1vave_presets.py - Maquina BL-101 / BL-101 FX factory presets from S1Vave's 64
factory sounds (s1vave/bl101/third_party/s1vave/firmware/src/obv_patches.h:
PF_01..PF_64 + FACT_WAVE, the OSC DRAW tables and CHOP masks).

  BL-101     every sound, its synth part: patch bytes -> the plugin's shown
             values (tools/gen_params.py bl101_params), the DRAW table
             (int16 / 256), the CHOP masks (one of the firmware's 6, else
             Custom + its bytes). Transpose centred, as the firmware loads them.
  BL-101 FX  the effect settings that recur among them (FX_PRESETS below,
             each named after the sounds it comes from).

The firmware calls its sounds "PTN 1-01" ..; NAMES gives each a name, category
and tags (chosen from tools/preset_measure and the patch itself).

Writes s1vave/bl101/presets/bl101/*.maquina and presets/bl101_fx/*.maquina;
then `make presets`.

SPDX-License-Identifier: GPL-3.0-only
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_params  # noqa: E402
from layout import BL101  # noqa: E402

PATCHES_H = os.path.join(BL101, "third_party", "s1vave", "firmware", "src", "obv_patches.h")
MASKS = [0xFFFF, 0x5555, 0xAAAA, 0x1111, 0x8888, 0x0000]  # gen_params.BL101_MASK_LABELS[1:]

# (name, category, tags) per factory sound 1..64
NAMES = {
    1: ("Sync Saw Pluck", "Pluck", ["short", "modulated"]),
    2: ("Reso Saw Keys", "Poly", ["long", "bright"]),
    3: ("Glide Sub Bass", "Bass", ["punchy", "dark"]),
    4: ("Chop Pulse Stab", "Pluck", ["short", "metallic"]),
    5: ("Warm Reso Poly", "Poly", ["soft", "long"]),
    6: ("Chopped Brass", "Poly", ["bright", "metallic"]),
    7: ("Drawn Pluck", "Pluck", ["dark", "long"]),
    8: ("Step Wave Keys", "Poly", ["soft"]),
    9: ("Random Filter Bass", "Sequence", ["dark", "modulated"]),
    10: ("Noisy Pulse Bass", "Bass", ["punchy", "noisy"]),
    11: ("Chord Stab", "Poly", ["punchy", "short"]),
    12: ("Ramp Gate Seq", "Sequence", ["modulated"]),
    13: ("Unison Bass", "Bass", ["boomy"]),
    14: ("Deep Unison Sub", "Bass", ["dark", "boomy"]),
    15: ("Breathy Pulse", "Poly", ["noisy", "soft"]),
    16: ("Noise Siren", "FX", ["noisy", "modulated"]),
    17: ("Slope Wave Poly", "Poly", ["soft", "bright"]),
    18: ("Square Keys", "Poly", ["clean"]),
    19: ("Inverse Ramp Steps", "Texture", ["modulated"]),
    20: ("Slope Bass", "Bass", ["dark"]),
    21: ("Slope Pluck", "Pluck", ["short"]),
    22: ("Soft Swell", "Poly", ["soft"]),
    23: ("Noise Wobble", "FX", ["noisy", "modulated"]),
    24: ("Unison Comb Lead", "Lead", ["bright", "metallic"]),
    25: ("Unison Saw Bass", "Bass", ["punchy"]),
    26: ("Square Sync Pluck", "Pluck", ["clean", "short"]),
    27: ("Square Step Keys", "Texture", ["modulated"]),
    28: ("Slope Organ", "Poly", ["clean", "long"]),
    29: ("Hollow Pulse Pad", "Pad", ["soft", "long"]),
    30: ("Square Keys Dry", "Poly", ["clean"]),
    31: ("Glide Pulse Pad", "Pad", ["soft", "long"]),
    32: ("Glide Pulse Seq", "Sequence", ["modulated"]),
    33: ("Pulse Keys", "Poly", ["clean"]),
    34: ("LFO Gate Bass", "Sequence", ["punchy"]),
    35: ("Noise Pad", "Pad", ["noisy", "long"]),
    36: ("Noise Chop Swell", "Texture", ["noisy", "metallic"]),
    37: ("Noise Step Drone", "Texture", ["noisy"]),
    38: ("Unison Ring Lead", "Lead", ["metallic", "bright"]),
    39: ("Bright Noise Wobble", "FX", ["bright", "noisy"]),
    40: ("Growl Pulse", "Lead", ["dirty"]),
    41: ("Chop Sub Bass", "Bass", ["metallic", "boomy"]),
    42: ("Open Unison Lead", "Lead", ["bright"]),
    43: ("Noise Chop Pad", "Pad", ["noisy", "metallic"]),
    44: ("Noise Pulse Seq", "Texture", ["noisy"]),
    45: ("Fast PWM Pad", "Pad", ["modulated"]),
    46: ("Fast Square Swell", "FX", ["modulated", "noisy"]),
    47: ("Square Step Lead", "Texture", ["modulated"]),
    48: ("Glide Pulse Lead", "Lead", ["clean"]),
    49: ("Inverse Ramp Poly", "Poly", ["modulated"]),
    50: ("Ring Chop Unison", "FX", ["metallic"]),
    51: ("Slope Drone", "Texture", ["long"]),
    52: ("Reso Saw Pad", "Pad", ["bright"]),
    53: ("Slope Unison Bass", "Bass", ["metallic"]),
    54: ("Soft Saw Pluck", "Pluck", ["soft"]),
    55: ("Chord Chop Stab", "Poly", ["metallic"]),
    56: ("Slow Square Pad", "Pad", ["long", "soft"]),
    57: ("Fast Saw Noise", "Texture", ["dirty", "noisy"]),
    58: ("Unison Slow Pad", "Pad", ["long"]),
    59: ("Deep Unison Bass", "Bass", ["dark", "boomy"]),
    60: ("Comb Chop Bass", "Bass", ["dark", "metallic"]),
    61: ("Noise Sweep Pad", "Pad", ["long"]),
    62: ("Square Noise Pad", "Pad", ["noisy", "modulated"]),
    63: ("Noise Pulse Keys", "Poly", ["modulated"]),
    64: ("Mono Glide Lead", "Lead", ["modulated"]),
}


def read_factory():
    src = open(PATCHES_H, encoding="utf-8").read()
    patches = {}
    for m in re.finditer(r"static const uint8_t PF_(\d\d)\[\] = \{([^}]*)\};", src):
        items = [x.strip() for x in m.group(2).split(",") if x.strip()]
        assert items[-1] == "X"
        items = items[:-1]
        patches[int(m.group(1))] = {items[i]: int(items[i + 1]) for i in range(0, len(items), 2)}
    waves = []
    body = src[src.index("FACT_WAVE[NFACT] = {"):]
    for m in re.finditer(r"\{\s*\{([^}]*)\},\s*\{([^}]*)\}\s*\}", body):
        draw = [int(x) for x in m.group(1).split(",")]
        chop = [int(x.strip().rstrip("u")) for x in m.group(2).split(",")]
        waves.append((draw, chop))
        if len(waves) == 64:
            break
    if len(patches) != 64 or len(waves) != 64:
        sys.exit(f"read {len(patches)} patches, {len(waves)} waves")
    return patches, waves


def plugin_values(params, fw, patch, wave):
    """the plugin's shown values of a factory sound (all its parameters)"""
    by_index = {idx: name for name, (idx, _, _) in fw.items()}
    bytes_ = {idx: df for name, (idx, _, df) in fw.items()}
    for name, v in patch.items():
        bytes_[fw[name][0]] = v
    bytes_[fw["P_TRANSPOSE"][0]] = 64  # (firmware) patch_from_factory
    draw, chop = wave
    out = {}
    for q in params:
        par = int(q["par"])
        if par < gen_params.BL101_DRAW_BASE:
            b = bytes_[par]
            if not (q["fwmin"] <= b <= q["fwmax"]):
                sys.exit(f"{q['symbol']}: byte {b} outside {q['fwmin']}..{q['fwmax']} ({by_index[par]})")
            out[q["symbol"]] = b + q["offset"] if q["kind"] == "Int" else b
        elif par < gen_params.BL101_DRAW_BASE + 16:
            v = draw[par - gen_params.BL101_DRAW_BASE]
            if v % 256:
                sys.exit(f"draw step {v} is not a multiple of 256")
            out[q["symbol"]] = v // 256
        elif par < gen_params.BL101_MASK_BASE + 4:
            m = chop[par - gen_params.BL101_MASK_BASE]
            out[q["symbol"]] = MASKS.index(m) + 1 if m in MASKS else 0
        elif par < gen_params.BL101_BITS_LO_BASE + 4:
            m = chop[par - gen_params.BL101_BITS_LO_BASE]
            out[q["symbol"]] = 255 if m in MASKS else m & 255
        else:
            m = chop[par - gen_params.BL101_BITS_HI_BASE]
            out[q["symbol"]] = 255 if m in MASKS else m >> 8
    return out


def shown(q, v):
    if q["kind"] in ("Enum", "Toggle") and q["labels"]:
        return q["labels"][v]
    return str(v)


def slug(s):
    return re.sub(r"[^a-z0-9]+", "-", s.lower()).strip("-")


def write_preset(path, voice, name, category, tags, params, values, comment):
    defaults = {q["symbol"]: q["default"] for q in params}
    lines = ["# Maquina factory preset", f"# {comment}", f"voice = {voice}", f"name = {name}",
             f"category = {category}", "tags = " + ", ".join(tags)]
    for q in params:
        v = values[q["symbol"]]
        if v != defaults[q["symbol"]]:
            lines.append(f"{q['symbol']} = {shown(q, v)}")
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")


# BL-101 FX: (name, category, tags, the factory sound whose effects these are,
# changes to them)
FX_PRESETS = [
    ("Dotted Spring", "Chain", ["long"], 7, {}),
    ("Dotted Hall", "Chain", ["long"], 8, {}),
    ("Triplet Plate Chorus", "Chain", ["modulated"], 57, {}),
    ("Quarter Echo", "Delay", ["long"], 2, {}),
    ("Sixteenth Echo", "Delay", ["short"], 11, {}),
    ("Slap Repeats", "Delay", ["short"], 15, {}),
    ("Chorus Plate", "Chorus", ["soft"], 48, {}),
    ("Wide Chorus Space", "Chorus", ["long", "modulated"], 61, {}),
    ("Chorus Echo", "Chorus", ["modulated"], 59, {}),
    ("Modulated Hall", "Reverb", ["long"], 52, {}),
    ("Ambience", "Reverb", ["short"], 24, {}),
    ("Short Hall", "Reverb", ["short"], 26, {}),
    # the lo-fi slot (no factory sound uses it): our own settings
    ("Rate Crush", "Lo-Fi", ["lo-fi", "dirty"], 25,
     {"lofi_mode": 0, "lofi_amount": 55, "lofi_mix": 100, "chorus_mode": 0}),
    ("Drive", "Lo-Fi", ["dirty"], 25, {"lofi_mode": 1, "lofi_amount": 60, "lofi_mix": 70, "chorus_mode": 0}),
]


def main():
    if len({v[0] for v in NAMES.values()}) != len(NAMES):
        sys.exit("NAMES: a name is used twice")
    patches, waves = read_factory()
    fw = gen_params.bl101_firmware_params()
    params, _ = gen_params.bl101_params(False)
    fx_params, _ = gen_params.bl101_params(True)
    measure_only = "--list" in sys.argv

    out_dir = os.path.join(BL101, "presets", "bl101")
    os.makedirs(out_dir, exist_ok=True)
    if not measure_only:
        for f in os.listdir(out_dir):
            if f.endswith(".maquina"):
                os.remove(os.path.join(out_dir, f))
    for n in range(1, 65):
        values = plugin_values(params, fw, patches[n], waves[n - 1])
        if measure_only:
            # one line per sound for tools/preset_measure: sym=value ...
            print(f"{n:02d} " + " ".join(f"{q['symbol']}={values[q['symbol']]}" for q in params))
            continue
        name, category, tags = NAMES.get(n, (f"Sound {n:02d}", "Poly", []))
        write_preset(os.path.join(out_dir, f"{n:02d}-{slug(name)}.maquina"), "bl101", name, category, tags, params,
                     values, f"S1Vave factory sound {n} (PTN {(n - 1) // 16 + 1}-{(n - 1) % 16 + 1:02d})")
    if measure_only:
        return

    fx_dir = os.path.join(BL101, "presets", "bl101_fx")
    os.makedirs(fx_dir, exist_ok=True)
    for f in os.listdir(fx_dir):
        if f.endswith(".maquina"):
            os.remove(os.path.join(fx_dir, f))
    for name, category, tags, n, changes in FX_PRESETS:
        values = plugin_values(fx_params, fw, patches[n], waves[n - 1])
        values.update(changes)
        write_preset(os.path.join(fx_dir, f"{slug(category)}-{slug(name)}.maquina"), "bl101_fx", name, category,
                     tags, fx_params, values,
                     f"the effects of S1Vave factory sound {n}" + (", changed" if changes else ""))
    print(f"bl101: 64 presets, bl101_fx: {len(FX_PRESETS)} presets")


if __name__ == "__main__":
    main()
