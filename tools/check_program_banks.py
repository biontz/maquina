#!/usr/bin/env python3
"""check_program_banks.py - the MIDI program map (common/ProgramMap.hpp, dumped by
build/tools/program_map) numbers the factory presets the same way as the
Machinista banks (machinista/banks/maquina-<voice>/NN-<category>.json):

  bank NN (CC 0)  = the NN-<category>.json file
  its presets     = that file's patches, in order (a reduced Machinista profile,
                    e.g. Plethora's channel A, may leave presets out; the rest
                    keep their order)

Runs with an empty user library (XDG_CONFIG_HOME = a temporary folder), from the
repository root. SPDX-License-Identifier: GPL-3.0-only
"""
import glob
import json
import os
import re
import subprocess
import sys
import tempfile

from layout import ROOT

VOICES = ["kick", "tom", "snare", "cymbal", "hihat", "dredrum", "plethora", "oneiroi", "oneiroi_fx",
          "klang", "klong", "klong_fx", "acida", "acida_fx", "bl101", "bl101_fx", "la50"]


def main():
    tool = os.path.join(ROOT, "build", "tools", "program_map")
    errors = 0
    with tempfile.TemporaryDirectory() as tmp:
        env = dict(os.environ, XDG_CONFIG_HOME=tmp)
        for v in VOICES:
            m = json.loads(subprocess.check_output([tool, v], env=env))
            banks = {b["msb"]: b for b in m["banks"]}
            for path in sorted(glob.glob(os.path.join(ROOT, "machinista", "banks", f"maquina-{v}", "*.json"))):
                nn = int(re.match(r"(\d+)-", os.path.basename(path)).group(1))
                bank = json.load(open(path))
                if nn not in banks or banks[nn]["name"] != bank["name"]:
                    print(f"{v}: {os.path.basename(path)} is bank {nn}, the program map has "
                          f"{banks.get(nn, {}).get('name')!r}")
                    errors += 1
                    continue
                names = [p["name"] for p in banks[nn]["presets"]]
                patches = [p["name"] for p in bank["patches"]]
                it = iter(names)
                if not all(any(p == n for n in it) for p in patches):  # patches: an ordered subset
                    print(f"{v}: {os.path.basename(path)}: patch order differs from bank {nn}")
                    errors += 1
    print(f"check_program_banks: {'ok' if not errors else f'{errors} error(s)'}")
    sys.exit(1 if errors else 0)


if __name__ == "__main__":
    main()
