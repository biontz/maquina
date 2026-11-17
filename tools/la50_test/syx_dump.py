#!/usr/bin/env python3
"""syx_dump.py FILE.syx - every patch as tools/la50_test --dump prints it (the Python reading,
tools/d50_syx.py), for `make la50-check`.
SPDX-License-Identifier: GPL-3.0-only"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import d50_syx as D  # noqa: E402
import la50_layout as L  # noqa: E402

mx = [0] * L.PATCH_BYTES
for p in L.params():
    mx[p["byte"]] = p["max"]
mem, _ = D.parse(open(sys.argv[1], "rb").read())
for n, raw in D.patches(mem):
    data = bytes(min(b, mx[k]) if mx[k] else b & 127 for k, b in enumerate(raw))
    print(f"{n:02d} {D.patch_name(raw)} {data.hex()}")
