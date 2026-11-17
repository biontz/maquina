#!/usr/bin/env python3
"""
la50_presets.py - Maquina LA-50's own factory presets (lasynth/la50/presets/la50/).

Roland's D-50 banks are not shipped (users import their own .syx). These are our
designs, synth partials only (structures 1 / 2), so they sound the same with the
PCM stand-ins and with a ROM. Values are the plugin's shown values; anything not
set keeps the INIT patch's value (tools/la50_layout.py). Checked with
tools/preset_measure. Run, then `make presets`.

SPDX-License-Identifier: GPL-3.0-only
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import d50_syx  # noqa: E402
import la50_layout as L  # noqa: E402

OUT = os.path.join(L.__file__.rsplit("/tools/", 1)[0], "lasynth", "la50", "presets", "la50")


def partial(prefix, saw=True, cutoff=60, reso=0, env=40, a=0, d=40, sus=80, rel=40, fa=0, fd=40, fsus=30, frel=40,
            coarse=0, fine=0, pw=0, level=90, pwm=0, pwm_lfo="+2", vib_mode="+", flfo=0, flfo_sel="+1", tlfo=0):
    p = prefix
    return {f"{p}_wg_waveform": 1 if saw else 0, f"{p}_tvf_cutoff": cutoff, f"{p}_tvf_reso": reso,
            f"{p}_tvf_env_depth": env, f"{p}_tvf_t1": fa, f"{p}_tvf_t2": fd, f"{p}_tvf_sus": fsus, f"{p}_tvf_t5": frel,
            f"{p}_tva_t1": a, f"{p}_tva_t2": d, f"{p}_tva_sus": sus, f"{p}_tva_t5": rel, f"{p}_wg_coarse": coarse,
            f"{p}_wg_fine": fine, f"{p}_wg_pw": pw, f"{p}_tva_level": level, f"{p}_wg_pw_lfo_depth": pwm,
            f"{p}_wg_pw_lfo": L.LFO_SEL.index(pwm_lfo), f"{p}_wg_lfo_mode": ["Off", "+", "-", "A&L"].index(vib_mode),
            f"{p}_tvf_lfo_depth": flfo, f"{p}_tvf_lfo": L.LFO_SEL.index(flfo_sel), f"{p}_tva_lfo_depth": tlfo}


# The chorus as these presets were voiced: sweep speed 0.05 * 2^(rate / 14) Hz and width
# depth% of the type's range (ms below; Tremolo: depth% amplitude). The engine now follows the
# D-50 (La50Fx.cpp: linear around each type's centre at 50), so voiced settings are converted
# to the Rate / Depth that give the same sweep.
VOICED_MS = [3, 5, 1.2, 2.5, 3, 0, 3, 0.4]
D50_CENTRE = [  # first sweep Hz, its width ms at Rate / Depth 50; tremolo Hz, amount (La50Fx.cpp)
    (0.70, 0.06), (1.35, 0.66), (0.30, 0.30), (0.60, 0.60), (1.25, 0.18), (1.45, 0.80), (0.70, 0.24), (0.20, 0.24)]


def d50_chorus(kind, rate, depth):
    hz = 0.05 * 2 ** (rate / 14.0)
    c_hz, c_w = D50_CENTRE[kind]
    want = depth / 100.0 if kind == 5 else VOICED_MS[kind] * depth / 100.0
    r = 50 + (hz - c_hz) / 0.05
    d = 50 + (want - c_w) * (127 if kind == 5 else 1 / 0.06)
    return max(0, min(100, round(r))), max(0, min(100, round(d)))


def common(t, structure=1, balance=50, mute=3, chorus="Chorus 1", ch_rate=30, ch_depth=40, ch_bal=40, vib=10,
           lfo1_rate=60, lfo1_delay=30, lfo2_rate=30, lfo3_rate=20):
    kind = L.CHORUS_TYPES.index(chorus)
    ch_rate, ch_depth = d50_chorus(kind, ch_rate, ch_depth)
    return {f"{t}_structure": structure - 1, f"{t}_partial_balance": balance, f"{t}_partial_mute": mute,
            f"{t}_chorus_type": kind, f"{t}_chorus_rate": ch_rate,
            f"{t}_chorus_depth": ch_depth, f"{t}_chorus_balance": ch_bal, f"{t}_pmod_lfo": vib,
            f"{t}_lfo1_rate": lfo1_rate, f"{t}_lfo1_delay": lfo1_delay, f"{t}_lfo2_rate": lfo2_rate,
            f"{t}_lfo3_rate": lfo3_rate}


def patch(key_mode="Whole", reverb="Large Hall", rev_bal=35, volume=85, split="C4", tone_bal=50, **kw):
    d = {"patch_key_mode": L.KEY_MODES.index(key_mode), "patch_reverb_type": L.REVERB_TYPES.index(reverb),
         "patch_reverb_balance": rev_bal, "patch_total_volume": volume,
         "patch_split_point": [L.note_name(36 + i) for i in range(61)].index(split), "patch_tone_balance": tone_bal}
    d.update(kw)
    return d


def upper_only():
    return {"l_partial_mute": 0}


PRESETS = []


def preset(name, category, tags, *parts):
    vals = {}
    for p in parts:
        vals.update(p)
    PRESETS.append((name, category, tags, vals))


preset("Glass Pad", "Pad", ["soft", "long"],
       partial("u_p1", cutoff=48, reso=8, env=30, a=60, d=60, sus=90, rel=65, fa=55, fd=70, fsus=45, frel=60),
       partial("u_p2", saw=False, cutoff=55, env=20, a=65, d=60, sus=85, rel=65, fine=7, pw=40, pwm=35),
       common("u", chorus="Chorus 2", ch_bal=60), upper_only(), patch(rev_bal=50))
preset("Soft Strings", "Strings", ["soft", "long"],
       partial("u_p1", cutoff=50, env=25, a=55, d=50, sus=95, rel=55, fa=50, fsus=40, fine=-6),
       partial("u_p2", cutoff=52, env=25, a=58, d=50, sus=95, rel=55, fa=50, fsus=40, fine=6),
       common("u", chorus="Chorus 1", ch_bal=70, ch_depth=55, vib=8, lfo1_delay=60), upper_only(), patch(rev_bal=45))
preset("Brass Section", "Brass", ["bright", "punchy"],
       partial("u_p1", cutoff=32, env=75, fa=18, fd=45, fsus=40, a=12, d=50, sus=85, rel=35, fine=-4),
       partial("u_p2", cutoff=34, env=70, fa=20, fd=45, fsus=38, a=14, d=50, sus=85, rel=35, fine=4),
       common("u", chorus="Chorus 1", ch_bal=35, vib=6, lfo1_delay=55), upper_only(), patch(reverb="Medium Hall", rev_bal=30))
preset("Ring Bell", "Bell", ["metallic", "long"],
       partial("u_p1", saw=False, cutoff=80, env=10, a=0, d=75, sus=0, rel=70, fsus=60),
       partial("u_p2", saw=False, cutoff=80, env=10, a=0, d=60, sus=0, rel=60, coarse=19, fsus=60),
       common("u", structure=2, balance=55, chorus="Chorus 2", ch_bal=50), upper_only(), patch(rev_bal=55))
preset("Fat Bass", "Bass", ["punchy", "dark"],
       partial("u_p1", cutoff=28, reso=6, env=70, fa=0, fd=35, fsus=10, frel=25, a=0, d=50, sus=80, rel=20, coarse=-12),
       partial("u_p2", saw=False, cutoff=30, env=50, fd=30, fsus=5, a=0, d=50, sus=80, rel=20, coarse=-24, pw=20),
       common("u", ch_bal=0, vib=0), upper_only(), patch(key_mode="Whole-S", reverb="Small Room", rev_bal=10))
preset("Pluck Keys", "Keys", ["short", "bright"],
       partial("u_p1", saw=False, pw=45, cutoff=40, env=80, fd=30, fsus=0, frel=30, a=0, d=55, sus=0, rel=40),
       partial("u_p2", cutoff=45, env=70, fd=25, fsus=0, frel=25, a=0, d=45, sus=0, rel=35, coarse=12, level=70),
       common("u", chorus="Chorus 2", ch_bal=45, vib=0), upper_only(), patch(reverb="Medium Room", rev_bal=30))
preset("Sync Lead", "Lead", ["bright"],
       partial("u_p1", cutoff=50, reso=22, env=55, fd=50, fsus=40, a=0, d=40, sus=90, rel=30),
       partial("u_p2", saw=False, cutoff=55, reso=10, env=45, pw=60, a=0, d=40, sus=90, rel=30, fine=10, level=80),
       common("u", ch_bal=20, vib=20, lfo1_delay=45, lfo1_rate=62), upper_only(),
       patch(key_mode="Whole-S", reverb="Delay 248ms", rev_bal=30, patch_porta_time=25))
preset("Fifths Lead", "Lead", ["bright"],
       partial("u_p1", cutoff=55, reso=12, env=40, a=0, d=40, sus=90, rel=30),
       partial("u_p2", cutoff=55, reso=12, env=40, a=0, d=40, sus=90, rel=30, coarse=7, level=78),
       common("u", ch_bal=25, vib=15, lfo1_delay=50), upper_only(),
       patch(key_mode="Whole-S", reverb="Cross Delay 224ms", rev_bal=30))
preset("PWM Strings", "Strings", ["modulated", "long"],
       partial("u_p1", saw=False, pw=30, pwm=60, pwm_lfo="+2", cutoff=52, env=20, a=50, d=50, sus=95, rel=55, level=62),
       partial("u_p2", saw=False, pw=50, pwm=60, pwm_lfo="-3", cutoff=50, env=20, a=52, d=50, sus=95, rel=55, fine=5,
               level=62),
       common("u", ch_bal=60, lfo2_rate=40, lfo3_rate=32), upper_only(), patch(rev_bal=45))
preset("Sweep Pad", "Pad", ["modulated", "long"],
       partial("u_p1", cutoff=40, reso=15, env=20, flfo=45, flfo_sel="+2", a=55, d=60, sus=95, rel=65),
       partial("u_p2", cutoff=42, reso=12, env=20, flfo=45, flfo_sel="-2", a=58, d=60, sus=95, rel=65, fine=-7),
       common("u", chorus="Chorus 2", ch_bal=55, lfo2_rate=18), upper_only(), patch(reverb="Large Hall", rev_bal=55))
preset("Pulse Organ", "Keys", ["clean"],
       partial("u_p1", saw=False, pw=0, cutoff=65, env=0, a=0, d=0, sus=100, rel=8),
       partial("u_p2", saw=False, pw=0, cutoff=70, env=0, a=0, d=0, sus=100, rel=8, coarse=12, level=75),
       common("u", chorus="Chor Trem", ch_rate=55, ch_bal=55, vib=0), upper_only(), patch(reverb="Small Hall", rev_bal=25))
preset("Reso Zap", "FX", ["bright", "short"],
       partial("u_p1", cutoff=10, reso=28, env=100, fa=0, fd=20, fsus=0, frel=10, a=0, d=35, sus=0, rel=20),
       {"u_p2_tva_level": 0}, common("u", ch_bal=0, vib=0), upper_only(), patch(reverb="Short Slap Back", rev_bal=25))
preset("Tremolo Keys", "Keys", ["modulated"],
       partial("u_p1", saw=False, pw=10, cutoff=55, env=40, fd=55, fsus=30, a=0, d=70, sus=40, rel=40),
       partial("u_p2", saw=False, cutoff=70, env=30, a=0, d=40, sus=0, rel=30, coarse=24, level=60),
       common("u", structure=2, balance=35, chorus="Tremolo", ch_rate=50, ch_depth=60, ch_bal=70, vib=0), upper_only(),
       patch(reverb="Medium Room", rev_bal=30))
preset("Dual Bell Pad", "Pad", ["long", "metallic"],
       partial("u_p1", saw=False, cutoff=80, env=10, a=0, d=70, sus=0, rel=65),
       partial("u_p2", saw=False, cutoff=80, env=10, a=0, d=55, sus=0, rel=60, coarse=24),
       common("u", structure=2, chorus="Chorus 2", ch_bal=40),
       partial("l_p1", cutoff=45, reso=6, env=25, a=55, d=60, sus=90, rel=60),
       partial("l_p2", saw=False, pw=40, cutoff=48, env=20, a=58, d=60, sus=90, rel=60, fine=6),
       common("l", mute=3, chorus="Chorus 1", ch_bal=60),
       patch(key_mode="Dual", rev_bal=50, tone_bal=45))
preset("Split Bass Pad", "Bass", ["punchy"],
       partial("u_p1", cutoff=48, env=25, a=50, d=60, sus=90, rel=55),
       partial("u_p2", saw=False, pw=40, pwm=40, cutoff=50, env=20, a=52, d=60, sus=90, rel=55, fine=6),
       common("u", chorus="Chorus 2", ch_bal=55),
       partial("l_p1", cutoff=30, reso=8, env=65, fd=35, fsus=10, a=0, d=50, sus=80, rel=20, coarse=-12),
       partial("l_p2", saw=False, cutoff=30, env=50, fsus=5, a=0, d=50, sus=80, rel=20, coarse=-24),
       common("l", mute=3, ch_bal=0, vib=0),
       patch(key_mode="Split", split="C4", rev_bal=35))
preset("Wide Saws", "Pad", ["bright", "long"],
       partial("u_p1", cutoff=60, env=15, a=30, d=60, sus=95, rel=50, fine=-9),
       partial("u_p2", cutoff=60, env=15, a=30, d=60, sus=95, rel=50, fine=9),
       common("u", chorus="Dimension", ch_bal=60), upper_only(), patch(reverb="Bright Hall", rev_bal=40))


def main():
    os.makedirs(OUT, exist_ok=True)
    for f in os.listdir(OUT):
        if f.endswith(".maquina"):
            os.remove(os.path.join(OUT, f))
    defaults = {p["symbol"]: p["default"] + (p["shown"] if p["kind"] == "Int" else 0) for p in L.params()}
    for name, category, tags, vals in PRESETS:
        full = dict(defaults)
        for k, v in vals.items():
            if k not in full:
                sys.exit(f"{name}: unknown parameter {k}")
            full[k] = v
        slug = re.sub(r"[^a-z0-9]+", "-", f"{category}-{name}".lower()).strip("-")
        d50_syx.write_maquina(os.path.join(OUT, slug + ".maquina"), name, full, category=category, tags=tags,
                              comment="our design (synth partials only)")
    print(f"{len(PRESETS)} presets -> {os.path.relpath(OUT)}")


if __name__ == "__main__":
    main()
