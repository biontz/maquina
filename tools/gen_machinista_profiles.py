#!/usr/bin/env python3
"""
gen_machinista_profiles.py - Machinista controller profiles for the plugins.

Machinista (github.com/photogeen/machinista) maps an LV2 plugin onto the
Maschine controller's 8 encoders through a JSON profile: a parameter list
(port index, symbol, range, choices) and pages of exactly 8 controls.

The parameter list is read from the built TTL (bin/<Name>.lv2/<Name>_dsp.ttl),
so port indices and ranges always match the binary. The page layouts live in
tools/pages.py (shared with the CLAP remote-controls pages).

Writes machinista/profiles/maquina-<id>.json. Install by copying them to
Machinista's app/plugin_profiles_data/ (see machinista/README.md).

SPDX-License-Identifier: GPL-3.0-only
"""
import json
import os
import re
import sys

from pages import MACHINISTA_PAGES, OFF_PAGE, PAGES  # page layouts, shared with gen_remote_pages.py

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

BUNDLES = {
    "kick": "MaquinaKick",
    "tom": "MaquinaTom",
    "snare": "MaquinaSnare",
    "cymbal": "MaquinaCymbal",
    "hihat": "MaquinaHiHat",
    "dredrum": "MaquinaDredrum",
    "plethora": "MaquinaPlethora",
    "oneiroi": "MaquinaOneiroi",  # Oneiroi FX: Machinista hosts instruments only
    "klang": "MaquinaKlang",
    "klong": "MaquinaKlong",  # Klong FX: an effect, as Oneiroi FX
    "acida": "MaquinaAcida",  # Acida FX: an effect, as Oneiroi FX
    "bl101": "MaquinaBL101",  # BL-101 FX: an effect, as Oneiroi FX
    "la50": "MaquinaLA50",
    "strings": "MaquinaStrings",
}

PORT_RE = re.compile(
    r"a lv2:(Input|Output)Port, lv2:(\w+)Port ;\s*lv2:index (\d+) ;(.*?)(?=\[\s*a lv2:|lv2:port \[|\Z)", re.S)


def number(s):
    v = float(s)
    return int(v) if v == int(v) else v


def parse_ttl(path):
    with open(path) as f:
        text = f.read()
    params = []
    for direction, kind, index, body in PORT_RE.findall(text):
        if direction != "Input" or kind != "Control":
            continue
        g = lambda key: re.search(r"lv2:%s (\S+?) ;" % key, body)
        name = re.search(r'lv2:name "([^"]*)"', body).group(1)
        symbol = re.search(r'lv2:symbol "([^"]*)"', body).group(1)
        props = " ".join(re.findall(r"lv2:portProperty ([^;]*);", body))
        p = dict(index=int(index), symbol=symbol, name=name.upper(),
                 default=number(g("default").group(1)), min=number(g("minimum").group(1)),
                 max=number(g("maximum").group(1)),
                 integer="lv2:integer" in props, toggle="lv2:toggled" in props, log=False)
        points = re.findall(r'rdfs:label\s+"([^"]*)" ;\s*rdf:value (\S+) ;', body)
        if "lv2:enumeration" in props and points:
            p["choices"] = [[number(v), label.upper()] for label, v in points]
            p["choice"] = True
        params.append(p)
    return params


QUIRKS_LXR = [
    "Drum voice from the LXR-02 firmware sound engine; parameters use the hardware's 0-127 scales.",
    "No graphical UI. Pages group the voice's firmware menu pages onto the eight encoders.",
    "Every note plays the sound at its set pitch; turn KEY TRACK on to play it chromatically.",
]
QUIRKS = {
    "dredrum": [
        "8-bit 3-oscillator voice from the Dredrum Eurorack firmware; knobs use the module's 0-255 scales.",
        "No graphical UI. One page per oscillator; any note triggers, TRACK makes an oscillator follow it.",
        "DECAY 248-255 never ends (drone). MODE GATE holds the decay while the note is down.",
    ],
    "plethora": [
        "Channel A of the Befaco Noise Plethora (VCV port): 60 algorithms with X/Y, filter, envelope.",
        "Channels B and C of the desktop plugin are not on this profile; they stay off (level 0).",
        "DECAY at max holds while playing. MODE GATE holds the sound while the note is down.",
    ],
    "oneiroi": [
        "Ambient/drone voice from the Befaco Oneiroi firmware: sine + supersaw/wavetable, looper, filter,",
        "resonator, echo, reverb. Notes open the envelope and set the pitch; DRONE keeps it sounding.",
        "The randomizer and the looper's recording source (always the output) are as on the module.",
    ],
    "klang": [
        "Macro oscillator based on Mutable Instruments Plaits: 24 models (MODEL), HARMONICS/TIMBRE/MORPH.",
        "Notes ping the low-pass gate (LPG MODE VELOCITY: velocity opens it while the note is held).",
        "ENV > FM/TMB/MRP: the internal decay envelope's amount, as the module's attenuverters.",
    ],
    "klong": [
        "Resonator based on Mutable Instruments Rings, played by its internal exciter: each note strums.",
        "POLY 1-4 voices; notes closer than 10 ms are strummed 10 ms apart (the module's strummer).",
        "MODEL PEACE ...: the string synth easter egg. Strings ring out: DAMPING sets how long.",
    ],
    "acida": [
        "Acid bass line voice: OpenRB-338's 303 (a reimplementation of the ReBirth RB-338 engine).",
        "Velocity >= ACC VEL accents a note; a note played while another is held slides to it.",
        "ENGINE 2.0 / VINTAGE: ReBirth 2's and ReBirth 1's 303 sound.",
    ],
    "la50": [
        "LA synthesis playing D-50 patches: Upper / Lower tone, 2 partials each (synth waves, PCM stand-ins).",
        "Pages: the main knobs per tone; the full D-50 parameter set is in the desktop editor.",
        "Import .syx banks in the desktop editor; they arrive here as presets.",
    ],
    "strings": [
        "String machine (REGISTER: Cello .. Organ) layered with a wavetable voice (BANK, POS X, MORPH Y).",
        "BLEND fades strings -> wave; SHIFT / TIME move it over each note; KEYS splits the keyboard.",
        "8 voices; FILTER (LP/BP/HP/COMB) shapes the wave layer; ENSEMBLE / PHASER / CHORUS / FORMANT + reverb.",
    ],
    "bl101": [
        "8-voice analog-modelling synth from the S1Vave firmware: VCO (pulse, saw, sub, noise), 4-pole VCF, ADSR.",
        "VOICING MONO / UNISON / POLY / CHORD; DRAW 1-16 shape the drawn wave (DRAW STEP / SLOPE),",
        "CHOP slices each source by its mask (Custom masks come with presets).",
    ],
}


def main():
    out_dir = os.path.join(ROOT, "machinista", "profiles")
    os.makedirs(out_dir, exist_ok=True)
    errors = 0
    for pid, bundle in BUNDLES.items():
        ttl = os.path.join(ROOT, "bin", bundle + ".lv2", bundle + "_dsp.ttl")
        if not os.path.exists(ttl):
            sys.exit(f"{ttl} missing - build first (make)")
        params = parse_ttl(ttl)
        symbols = {p["symbol"] for p in params}
        layout = MACHINISTA_PAGES.get(pid, PAGES[pid])
        if pid in MACHINISTA_PAGES:
            # reduced profile: only what is on its pages
            on_pages = {c for _, controls in layout for c in controls}
            params = [p for p in params if p["symbol"] in on_pages]
            symbols = {p["symbol"] for p in params}
        pages = []
        placed = set()
        for title, controls in layout:
            if len(controls) != 8:
                print(f"{pid}: page {title} has {len(controls)} controls, needs 8")
                errors += 1
            for c in controls:
                if not c.startswith("@") and c not in symbols:
                    print(f"{pid}: page {title}: unknown symbol {c}")
                    errors += 1
                placed.add(c)
            pages.append(dict(title=title, controls=controls))
        missing = symbols - placed - OFF_PAGE.get(pid, set())
        if missing:
            print(f"{pid}: parameters on no page: {sorted(missing)}")
            errors += 1
        profile = {
            "schema": 1,
            "id": f"maquina-{pid}",
            "uri": f"urn:maquina:{pid}",
            "title": f"Maquina {bundle[7:]}",
            "provider": "parameter-bank",
            "bank_format": "json",
            "extensions": [".json"],
            "parameters": params,
            "pages": pages,
            "quirks": QUIRKS.get(pid, QUIRKS_LXR),
        }
        path = os.path.join(out_dir, f"maquina-{pid}.json")
        with open(path, "w") as f:
            json.dump(profile, f, indent=2)
            f.write("\n")
        print(f"{pid:8s} {len(params)} parameters, {len(pages)} pages -> {os.path.relpath(path, ROOT)}")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
