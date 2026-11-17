#!/usr/bin/env python3
"""make_bank.py OUT.syx - a test bank for `make la50-check`: 64 patches of random field values and
names, some bytes beyond their range (both readers clamp them), as DT1 messages.
SPDX-License-Identifier: GPL-3.0-only"""
import os
import random
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import d50_syx as D  # noqa: E402
import la50_layout as L  # noqa: E402

rnd = random.Random(50)
raws = []
for n in range(64):
    raw = bytearray(D.raw_of({}, f"Test {n} " + "".join(rnd.choice(L.CHARSET) for _ in range(6))))
    for p in L.params():
        raw[p["byte"]] = rnd.randint(0, min(127, p["max"] + 3))
    raws.append(bytes(raw))
open(sys.argv[1], "wb").write(D.bank_syx(raws))
