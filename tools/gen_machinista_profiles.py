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

from pages import PAGES  # page layouts, shared with gen_remote_pages.py

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

BUNDLES = {
    "kick": "MaquinaKick",
    "tom": "MaquinaTom",
    "snare": "MaquinaSnare",
    "cymbal": "MaquinaCymbal",
    "hihat": "MaquinaHiHat",
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
        pages = []
        placed = set()
        for title, controls in PAGES[pid]:
            if len(controls) != 8:
                print(f"{pid}: page {title} has {len(controls)} controls, needs 8")
                errors += 1
            for c in controls:
                if not c.startswith("@") and c not in symbols:
                    print(f"{pid}: page {title}: unknown symbol {c}")
                    errors += 1
                placed.add(c)
            pages.append(dict(title=title, controls=controls))
        missing = symbols - placed
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
            "quirks": [
                "Drum voice from the LXR-02 firmware sound engine; parameters use the hardware's 0-127 scales.",
                "No graphical UI. Pages group the voice's firmware menu pages onto the eight encoders.",
                "TRIG NOTE 'ANY' plays every note at the set pitch; enable KEY TRACK to transpose.",
            ],
        }
        path = os.path.join(out_dir, f"maquina-{pid}.json")
        with open(path, "w") as f:
            json.dump(profile, f, indent=2)
            f.write("\n")
        print(f"{pid:7s} {len(params)} parameters, {len(pages)} pages -> {os.path.relpath(path, ROOT)}")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
