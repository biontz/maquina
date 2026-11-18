#!/usr/bin/env python3
"""lv2_preset_banks.py BUNDLE... - LV2 preset banks from the factory presets' categories.

DPF exports the factory presets as LV2 presets (manifest.ttl + presets.ttl) without
banks. Hosts that group presets by pset:bank (Zynthian, Carla, jalv) then show one
unnamed bank. This adds one pset:Bank per preset category (808, 909, Acoustic...) to
each bundle's manifest.ttl and puts every preset in its bank (presets.ttl); Init gets the bank "Init".
Preset URIs stay the same. Run after the TTL generator; running it twice changes nothing.
Fails if a preset has no category in <module>/presets/<voice>/*.maquina.
"""
import glob
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from layout import preset_dir  # noqa: E402

MARK = "# Maquina preset banks (tools/lv2_preset_banks.py)"


def voice_of(bundle):
    """MaquinaAcidaFX.lv2 -> acida_fx, MaquinaBL101.lv2 -> bl101"""
    name = os.path.basename(os.path.normpath(bundle))
    name = re.sub(r"\.lv2$", "", name)[len("Maquina"):]
    fx = name.endswith("FX")
    return name[:-2].lower() + "_fx" if fx else name.lower()


def categories(voice):
    cats = {}
    for f in glob.glob(os.path.join(preset_dir(voice), "*.maquina")):
        name = cat = None
        with open(f, encoding="utf-8") as fh:
            for line in fh:
                k, _, v = line.partition("=")
                k, v = k.strip(), v.strip()
                if k == "name":
                    name = v
                elif k == "category":
                    cat = v
                if name and cat:
                    break
        if name and cat:
            cats[name] = cat
    return cats


def ttl_string(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def slug(s):
    return re.sub(r"[^a-z0-9]+", "_", s.lower()).strip("_") or "bank"


def do_bundle(bundle):
    presets_ttl = os.path.join(bundle, "presets.ttl")
    manifest_ttl = os.path.join(bundle, "manifest.ttl")
    with open(manifest_ttl, encoding="utf-8") as fh:
        manifest = fh.read()
    if MARK in manifest:
        return f"{bundle}: banks already there"
    # <uri> a pset:Preset ; lv2:appliesTo <plugin> ; rdfs:label "name"
    presets = re.findall(
        r'<([^>]+)>\s+a pset:Preset ;\s+lv2:appliesTo <([^>]+)> ;\s+rdfs:label "((?:[^"\\]|\\.)*)"',
        manifest)
    if not presets:
        return f"{bundle}: no presets"
    voice = voice_of(bundle)
    cats = categories(voice)
    banks, links, missing = {}, [], []
    for uri, plugin, label in presets:
        label = label.replace('\\"', '"').replace("\\\\", "\\")
        cat = "Init" if label == "Init" else cats.get(label)
        if cat is None:
            missing.append(label)
            continue
        bank = f"{plugin}#bank_{slug(cat)}"
        banks[bank] = (plugin, cat)
        links.append(f"<{uri}> pset:bank <{bank}> .")
    if missing:
        sys.exit(f"{bundle}: no category for preset(s) {missing} in {preset_dir(voice)}")
    # the banks go into the manifest (hosts find them through lv2:appliesTo, which lilv
    # always loads); each preset's pset:bank next to its data in presets.ttl
    defs = [f"<{b}>\n    a pset:Bank ;\n    lv2:appliesTo <{p}> ;\n    rdfs:label {ttl_string(c)} .\n"
            for b, (p, c) in banks.items()]
    for path, old, body in ((manifest_ttl, manifest, "\n".join(defs)),
                            (presets_ttl, None, "\n".join(links) + "\n")):
        if old is None:
            with open(path, encoding="utf-8") as fh:
                old = fh.read()
        with open(path, "a", encoding="utf-8") as fh:
            fh.write(("" if old.endswith("\n") else "\n") + "\n" + MARK + "\n" + body)
    return f"{bundle}: {len(links)} presets in {len(banks)} banks"


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    for b in sys.argv[1:]:
        print(do_bundle(b))


if __name__ == "__main__":
    main()
