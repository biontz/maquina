#!/usr/bin/env python3
"""export_user_presets.py - the user presets (~/.config/maquina/presets/<voice>/) for
hosts that do not read .maquina files, numbered like MIDI Bank Select / Program
Change in the plugins (common/ProgramMap.hpp):

  <out>/lv2/maquina-user-presets.lv2/      LV2 presets with one pset:Bank per category
                                           (Zynthian, Carla, jalv ...): copy the bundle to
                                           ~/.lv2/ (Zynthian: zynthian-my-data/presets/lv2/)
  <out>/machinista/maquina-<voice>/NN-<category>.json
                                           Machinista parameter banks, factory + user
                                           presets, NN = the MIDI bank (CC 0): replace
                                           presets/plugin-banks/maquina-<voice>/ with it

The order comes from build/tools/program_map (build it with `make tools`), which
uses the plugins' own code, so bank n / program m is the same preset everywhere.
A category with more than 128 presets continues on CC 32 (LSB) 1, 2 ...; the
Machinista bank holds all of them.

usage: export_user_presets.py [--out DIR] [VOICE ...]   (default: every voice with
       user presets; DIR default build/user-presets)
SPDX-License-Identifier: GPL-3.0-only
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys

from layout import ROOT

VOICES = ["kick", "tom", "snare", "cymbal", "hihat", "dredrum", "plethora", "oneiroi", "oneiroi_fx",
          "klang", "klong", "klong_fx", "acida", "acida_fx", "bl101", "bl101_fx", "la50"]
# DISTRHO_PLUGIN_URI of each plugin (<module>/plugins/<plugin>/DistrhoPluginInfo.h)
PLUGIN_URI = {
    "kick": "urn:maquina:kick", "tom": "urn:maquina:tom", "snare": "urn:maquina:snare",
    "cymbal": "urn:maquina:cymbal", "hihat": "urn:maquina:hihat", "dredrum": "urn:maquina:dredrum",
    "plethora": "urn:maquina:plethora", "oneiroi": "urn:maquina:oneiroi", "oneiroi_fx": "urn:maquina:oneiroi-fx",
    "klang": "urn:maquina:klang", "klong": "urn:maquina:klong", "klong_fx": "urn:maquina:klong-fx",
    "acida": "urn:maquina:acida", "acida_fx": "urn:maquina:acida-fx", "bl101": "urn:maquina:bl-101",
    "bl101_fx": "urn:maquina:bl-101-fx", "la50": "urn:maquina:la-50",
}


def config_dir():
    if os.environ.get("XDG_CONFIG_HOME"):
        return os.path.join(os.environ["XDG_CONFIG_HOME"], "maquina")
    if sys.platform == "darwin":
        return os.path.expanduser("~/Library/Application Support/Maquina")
    return os.path.expanduser("~/.config/maquina")


def bank_slug(s):  # as tools/lv2_preset_banks.py, so user presets join the factory banks
    return re.sub(r"[^a-z0-9]+", "_", s.lower()).strip("_") or "bank"


def file_slug(s):  # as tools/gen_presets.py (Machinista bank files)
    return re.sub(r"[^a-z0-9]+", "-", s.lower()).strip("-") or "bank"


def ttl(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def program_map(voice):
    tool = os.path.join(ROOT, "build", "tools", "program_map")
    if not os.path.exists(tool):
        sys.exit(f"{tool} missing - build it first (make tools)")
    return json.loads(subprocess.check_output([tool, voice]))


def export_lv2(maps, out):
    bundle = os.path.join(out, "lv2", "maquina-user-presets.lv2")
    shutil.rmtree(bundle, ignore_errors=True)
    os.makedirs(bundle)
    manifest = ["@prefix lv2: <http://lv2plug.in/ns/lv2core#> .",
                "@prefix pset: <http://lv2plug.in/ns/ext/presets#> .",
                "@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .", ""]
    data = list(manifest)
    count = 0
    for voice, m in maps.items():
        plugin = PLUGIN_URI[voice]
        used, banks = set(), {}
        for b in m["banks"]:
            if b["msb"] == 0 or b["name"] == "User":
                continue  # factory list / every user preset: each preset is in its category's bank
            for p in b["presets"]:
                if not p["user"]:
                    continue
                uri = f"urn:maquina:user-preset:{voice}:{bank_slug(p['name'])}"
                k = 2
                while uri in used:
                    uri = f"urn:maquina:user-preset:{voice}:{bank_slug(p['name'])}_{k}"
                    k += 1
                used.add(uri)
                bank = f"{plugin}#bank_{bank_slug(b['name'])}"
                banks[bank] = b["name"]
                manifest += [f"<{uri}>", "    a pset:Preset ;", f"    lv2:appliesTo <{plugin}> ;",
                             f"    rdfs:label {ttl(p['name'])} ;", "    rdfs:seeAlso <presets.ttl> .", ""]
                ports = " ,\n        ".join(f"[ lv2:symbol {ttl(s)} ; pset:value {float(v)} ]"
                                           for s, v in p["values"].items())
                data += [f"<{uri}>", f"    lv2:port {ports} ;", f"    pset:bank <{bank}> .", ""]
                count += 1
        # banks of user categories are new; the factory ones are repeated (same triples)
        for bank, label in banks.items():
            manifest += [f"<{bank}>", "    a pset:Bank ;", f"    lv2:appliesTo <{plugin}> ;",
                         f"    rdfs:label {ttl(label)} .", ""]
    with open(os.path.join(bundle, "manifest.ttl"), "w") as f:
        f.write("\n".join(manifest))
    with open(os.path.join(bundle, "presets.ttl"), "w") as f:
        f.write("\n".join(data))
    print(f"{os.path.relpath(bundle)}: {count} user presets")


def export_machinista(maps, out):
    for voice, m in maps.items():
        prof_path = os.path.join(ROOT, "machinista", "profiles", f"maquina-{voice}.json")
        if not os.path.exists(prof_path):
            continue  # no Machinista profile (the FX plugins)
        prof = json.load(open(prof_path))
        symbols = {q["symbol"] for q in prof["parameters"]}
        bank_dir = os.path.join(out, "machinista", f"maquina-{voice}")
        shutil.rmtree(bank_dir, ignore_errors=True)
        os.makedirs(bank_dir)
        files = 0
        for b in m["banks"]:
            if b["msb"] == 0:
                continue
            patches = []
            for p in b["presets"]:
                controls = {q["symbol"]: q["default"] for q in prof["parameters"]}
                # a reduced profile (e.g. Plethora = channel A) cannot play presets
                # that use other parameters (as tools/gen_presets.py)
                if any(s not in symbols and v != default_of(voice, s, m) for s, v in p["values"].items()):
                    continue
                for s, v in p["values"].items():
                    if s in controls:
                        controls[s] = v
                patches.append(dict(name=p["name"], controls=controls))
            if not patches:
                continue
            with open(os.path.join(bank_dir, f"{b['msb']:02d}-{file_slug(b['name'])}.json"), "w") as f:
                json.dump(dict(schema=1, uri=prof["uri"], name=b["name"], patches=patches), f, indent=1)
                f.write("\n")
            files += 1
        print(f"{os.path.relpath(bank_dir)}: {files} banks")


_defaults = {}


def default_of(voice, symbol, m):
    """a sound parameter's default: its value in the Init program (bank 0, program 0)"""
    if voice not in _defaults:
        _defaults[voice] = m["banks"][0]["presets"][0]["values"]
    return _defaults[voice].get(symbol)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "user-presets"))
    ap.add_argument("voices", nargs="*")
    args = ap.parse_args()
    def has_user_presets(v):
        d = os.path.join(config_dir(), "presets", v)
        return os.path.isdir(d) and any(f.endswith(".maquina") for f in os.listdir(d))

    voices = args.voices or [v for v in VOICES if has_user_presets(v)]
    for v in voices:
        if v not in VOICES:
            sys.exit(f"unknown voice {v}")
    if not voices:
        sys.exit(f"no user presets in {os.path.join(config_dir(), 'presets')}")
    maps = {v: program_map(v) for v in voices}
    export_lv2(maps, args.out)
    export_machinista(maps, args.out)


if __name__ == "__main__":
    main()
