#!/usr/bin/env python3
"""
gen_presets.py - factory presets from <module>/presets/<voice>/*.maquina.

Validates every preset against the generated parameter tables (symbols,
ranges, labels, category, tags) and writes:

  common/FactoryPresets.generated.hpp   compiled into the plugins
                                                (host presets/programs + the
                                                editor's browser)
  machinista/banks/maquina-<voice>/<NN>-<category>.json
                                                Machinista parameter banks, one
                                                per category (copy to
                                                presets/plugin-banks/maquina-<voice>/)

The preset format is described in common/Presets.hpp.
SPDX-License-Identifier: GPL-3.0-only
"""
import glob
import json
import os
import re
import sys

from layout import COMMON, ROOT, preset_dir

TABLES = os.path.join(COMMON, "VoiceTables.generated.hpp")
PRESETS_HPP = os.path.join(COMMON, "Presets.hpp")
RANDOM_HPP = os.path.join(COMMON, "Randomizer.hpp")
OUT_HPP = os.path.join(COMMON, "FactoryPresets.generated.hpp")
VOICES = ["kick", "tom", "snare", "cymbal", "hihat", "dredrum", "plethora", "oneiroi", "oneiroi_fx",
          "klang", "klong", "klong_fx", "acida", "acida_fx"]


def c_str(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def load_tables():
    s = open(TABLES).read()
    labels = {m.group(1): re.findall(r'"([^"]*)"', m.group(2))
              for m in re.finditer(r'static const char\* const (\w+)\[\] = \{ ([^}]*) \};', s)}
    voices = {}
    for v in VOICES:
        blk = s[s.index(f"k_{v}_params[] = {{"):]
        blk = blk[:blk.index("};")]
        params = []
        for m in re.finditer(r'\{ "(\w+)", "[^"]*", \d+, \w+, ParamKind::(\w+), (-?\d+), (\d+), (\d+), (\w+), (\d+), \w+, (-?\d+) \}', blk):
            sym, kind, off, lo, hi, lab, n, d = m.groups()
            off, lo, hi = int(off), int(lo), int(hi)
            p = dict(symbol=sym, kind=kind, default=int(d), labels=labels.get(lab, []))
            if kind == "Int":
                p["min"], p["max"] = lo + off, hi + off
            elif kind in ("Toggle", "Retrigger"):
                p["min"], p["max"] = 0, 1
            else:
                p["min"], p["max"] = 0, int(n) - 1
            params.append(p)
        # extras (ExtraParams.hpp): note_pitch on the LXR voices; gain (a mix
        # control, not a sound param) on all
        if v in ("kick", "tom", "snare", "cymbal", "hihat"):
            params.append(dict(symbol="note_pitch", kind="Toggle", default=0, labels=["Off", "On"], min=0, max=1))
        params.append(dict(symbol="gain", kind="Int", default=0, labels=[], min=-24, max=12))
        voices[v] = params
    return voices


def load_meta():
    s = open(PRESETS_HPP).read()
    cats = {m.group(1): re.findall(r'"([^"]*)"', m.group(2))
            for m in re.finditer(r'\{ "(\w+)", \{ ([^}]*) \} \}', s)}
    tags = re.findall(r'"([^"]*)"', s[s.index("kPresetTags[] = {"):s.index("};", s.index("kPresetTags[] = {"))])
    r = open(RANDOM_HPP).read()
    r = r[r.index("kRandGroups[] = {"):]
    r = r[:r.index("\n};")]
    sound = set(re.findall(r'"(\w+)"', r))  # group symbols (+ keys/titles, harmless)
    return cats, tags, sound


def parse(path, params, cats, tags, sound):
    errors = []
    meta = {}
    values = {}
    by_sym = {p["symbol"]: p for p in params}
    for n, line in enumerate(open(path, encoding="utf-8"), 1):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        if "=" not in line:
            errors.append(f"{path}:{n}: no '='")
            continue
        k, v = (x.strip() for x in line.split("=", 1))
        if k in ("voice", "name", "category", "tags"):
            meta[k] = v
            continue
        p = by_sym.get(k)
        if p is None:
            errors.append(f"{path}:{n}: unknown parameter {k}")
            continue
        if k not in sound:
            errors.append(f"{path}:{n}: {k} is a trigger setting, not part of a preset")
            continue
        if p["labels"] and p["kind"] != "Int" and v in p["labels"]:
            val = p["labels"].index(v)
        else:
            try:
                val = int(v)
            except ValueError:
                errors.append(f"{path}:{n}: {k}: bad value {v!r} (labels: {p['labels']})")
                continue
        if not p["min"] <= val <= p["max"]:
            errors.append(f"{path}:{n}: {k}={val} outside {p['min']}..{p['max']}")
            continue
        values[k] = val
    voice = os.path.basename(os.path.dirname(path))
    if meta.get("voice", voice) != voice:
        errors.append(f"{path}: voice {meta.get('voice')} in the {voice} folder")
    if not meta.get("name"):
        errors.append(f"{path}: no name")
    if meta.get("category") not in cats[voice]:
        errors.append(f"{path}: category {meta.get('category')!r} not in {cats[voice]}")
    ptags = [t.strip() for t in meta.get("tags", "").split(",") if t.strip()]
    for t in ptags:
        if t not in tags:
            errors.append(f"{path}: unknown tag {t!r} (known: {tags})")
    return dict(name=meta.get("name", ""), category=meta.get("category", ""), tags=ptags, values=values,
                file=os.path.basename(path)), errors


def main():
    voices = load_tables()
    cats, tags, sound = load_meta()
    errors = []
    all_presets = {}
    for v in VOICES:
        presets = []
        for path in sorted(glob.glob(os.path.join(preset_dir(v), "*.maquina"))):
            p, e = parse(path, voices[v], cats, tags, sound)
            errors += e
            presets.append(p)
        names = [p["name"] for p in presets]
        for n in set(names):
            if names.count(n) > 1:
                errors.append(f"{v}: duplicate preset name {n!r}")
        presets.sort(key=lambda p: (cats[v].index(p["category"]) if p["category"] in cats[v] else 99, p["file"]))
        all_presets[v] = presets
    if errors:
        print("\n".join(errors))
        sys.exit(1)

    # ---- C++ table
    out = ["// GENERATED by tools/gen_presets.py from <module>/presets/<voice>/*.maquina. Do not edit.",
           "// SPDX-License-Identifier: GPL-3.0-only",
           "#pragma once", '#include "Presets.hpp"', "", "namespace maquina {", ""]
    for v in VOICES:
        params = voices[v]
        index = {p["symbol"]: i for i, p in enumerate(params)}
        for k, p in enumerate(all_presets[v]):
            # only values that differ from the default
            vals = [(index[s], x) for s, x in sorted(p["values"].items(), key=lambda t: index[t[0]])
                    if x != params[index[s]]["default"]]
            body = ", ".join(f"{{ {i}, {x} }}" for i, x in vals) or "{ 0, 0 }"
            out.append(f"static const PresetValue k_{v}_pv{k}[] = {{ {body} }};")
        out.append(f"static const FactoryPreset k_{v}_presets[] = {{")
        for k, p in enumerate(all_presets[v]):
            n = sum(1 for s, x in p["values"].items() if x != params[index[s]]["default"])
            out.append(f"    {{ {c_str(p['name'])}, {c_str(p['category'])}, {c_str(', '.join(p['tags']))}, "
                       f"k_{v}_pv{k}, {n} }},")
        if not all_presets[v]:
            out.append('    { "", "", "", nullptr, 0 },')
        out.append("};")
        out.append("")
    out.append("struct VoicePresets { const FactoryPreset* presets; uint32_t count; };")
    out.append("// in k_voices order")
    out.append("static const VoicePresets k_factory_presets[] = {")
    for v in VOICES:
        out.append(f"    {{ k_{v}_presets, {len(all_presets[v])} }},")
    out.append("};")
    out.append("")
    out.append("} // namespace maquina")
    with open(OUT_HPP, "w") as f:
        f.write("\n".join(out) + "\n")

    # ---- Machinista banks: one per category, every profile parameter
    for v in VOICES:
        prof_path = os.path.join(ROOT, "machinista", "profiles", f"maquina-{v}.json")
        bank_dir = os.path.join(ROOT, "machinista", "banks", f"maquina-{v}")
        for old in glob.glob(os.path.join(bank_dir, "*.json")):
            os.remove(old)
        if not os.path.exists(prof_path):
            continue  # no Machinista profile (Oneiroi FX)
        os.makedirs(bank_dir, exist_ok=True)
        prof = json.load(open(prof_path))
        for ci, cat in enumerate(cats[v]):
            patches = []
            for p in all_presets[v]:
                if p["category"] != cat:
                    continue
                # a reduced profile (MACHINISTA_PAGES, e.g. Plethora = channel A
                # only) cannot play presets that need parameters it does not have
                if any(s not in {q["symbol"] for q in prof["parameters"]} for s in p["values"]):
                    continue
                controls = {q["symbol"]: q["default"] for q in prof["parameters"]}
                for s, x in p["values"].items():
                    if s in controls:
                        controls[s] = x
                patches.append(dict(name=p["name"], controls=controls))
            if not patches:
                continue
            bank = dict(schema=1, uri=prof["uri"], name=cat, patches=patches)
            slug = re.sub(r"[^a-z0-9]+", "-", cat.lower()).strip("-")
            with open(os.path.join(bank_dir, f"{ci + 1:02d}-{slug}.json"), "w") as f:
                json.dump(bank, f, indent=1)
                f.write("\n")

    for v in VOICES:
        by_cat = {}
        for p in all_presets[v]:
            by_cat[p["category"]] = by_cat.get(p["category"], 0) + 1
        print(f"{v:10} {len(all_presets[v]):3} presets  " + "  ".join(f"{c}:{n}" for c, n in by_cat.items()))


if __name__ == "__main__":
    main()
