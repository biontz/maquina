#!/usr/bin/env python3
"""
d50_syx.py - D-50 SysEx files for Maquina LA-50 (the Python twin of
lasynth/la50/engine/D50Sysex.cpp, used by the tests and for presets).

  d50_syx.py list FILE.syx              the patches in a file
  d50_syx.py import FILE.syx DIR        each patch -> DIR/NN-name.maquina
  d50_syx.py export DIR OUT.syx         .maquina files (la50) -> a bank dump

Reads Roland exclusive messages for the D-50 (F0 41 dev 14 cmd addr.. data.. sum
F7): DT1 (12H, normal mode) and DAT (42H, data transfer mode: a real D-50's
bulk dump); WSD / EOD / ACK etc. are skipped. Data is placed by address
(7-bit, 3 bytes): the temporary area (00-00-00, one patch: what the D-50 sends
on a patch change) and the patch memory (02-00-00 + n * 448, patch n of 64; a
bank dump). Bad checksums are reported and the message is ignored.

SPDX-License-Identifier: GPL-3.0-only
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import la50_layout as L  # noqa: E402

TEMP_BASE = 0
MEMORY_BASE = 2 << 14
NUM_PATCHES = 64


def flat(a, b, c):
    return (a << 14) | (b << 7) | c


def unflat(addr):
    return (addr >> 14) & 127, (addr >> 7) & 127, addr & 127


def messages(data):
    """F0 .. F7 messages of a byte string"""
    i = 0
    while True:
        s = data.find(b"\xf0", i)
        if s < 0:
            return
        e = data.find(b"\xf7", s)
        if e < 0:
            return
        yield data[s:e + 1]
        i = e + 1


def parse(data):
    """-> (memory: {address: byte}, warnings)"""
    mem, warnings = {}, []
    for m in messages(data):
        if len(m) < 10 or m[1] != 0x41 or m[3] != 0x14 or m[4] not in (0x12, 0x42):
            continue
        body = m[5:-2]
        if (sum(body) + m[-2]) & 127:
            warnings.append(f"checksum error in a message at {unflat(flat(*body[:3]))}")
            continue
        addr = flat(*body[:3])
        for k, v in enumerate(body[3:]):
            # a DT1 / DAT address counts in 7-bit steps
            mem[addr + k] = v & 127
    return mem, warnings


def name_of(raw):
    return "".join(L.CHARSET[b & 63] for b in raw).strip()


def patches(mem):
    """the patches found: list of (number 1..64 or 0 = temporary area, 448 bytes)"""
    out = []
    if all(TEMP_BASE + k in mem for k in range(L.PATCH_BYTES)):
        out.append((0, bytes(mem[TEMP_BASE + k] for k in range(L.PATCH_BYTES))))
    for n in range(NUM_PATCHES):
        base = MEMORY_BASE + n * L.PATCH_BYTES
        if all(base + k in mem for k in range(L.PATCH_BYTES)):
            out.append((n + 1, bytes(mem[base + k] for k in range(L.PATCH_BYTES))))
    return out


def patch_name(raw):
    return name_of(raw[6 * 64:6 * 64 + 18])


def values(raw):
    """448 bytes -> {symbol: shown value} (clamped to each byte's range)"""
    out = {}
    for p in L.params():
        v = min(raw[p["byte"]], p["max"])
        out[p["symbol"]] = v + p["shown"] if p["kind"] == "Int" else v
    return out


def raw_of(vals, name="INIT"):
    """{symbol: shown value} -> 448 bytes (missing symbols: defaults), with a patch name"""
    raw = bytearray(L.PATCH_BYTES)
    for p in L.params():
        v = vals.get(p["symbol"])
        b = p["default"] if v is None else (v - p["shown"] if p["kind"] == "Int" else v)
        raw[p["byte"]] = max(0, min(p["max"], b))
    nm = (name + " " * 18)[:18]
    for k, ch in enumerate(nm):
        raw[6 * 64 + k] = L.CHARSET.find(ch) if ch in L.CHARSET else 0
    for blk in (2, 5):  # tone names: the patch name's first 10 characters
        raw[blk * 64:blk * 64 + 10] = raw[6 * 64:6 * 64 + 10]
    return bytes(raw)


def dt1(addr, payload, device=0):
    a = list(unflat(addr))
    body = a + list(payload)
    return bytes([0xF0, 0x41, device & 15, 0x14, 0x12] + body + [(-sum(body)) & 127, 0xF7])


def bank_syx(raws):
    """a bank dump: patches at 02-00-00.., DT1 messages of 256 data bytes"""
    out = bytearray()
    blob = b"".join(raws)
    for k in range(0, len(blob), 256):
        out += dt1(MEMORY_BASE + k, blob[k:k + 256])
    return bytes(out)


def read_maquina(path):
    vals, name = {}, "INIT"
    for line in open(path, encoding="utf-8"):
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        k, v = (x.strip() for x in line.split("=", 1))
        if k == "name":
            name = v
            continue
        vals[k] = v
    by_sym = {p["symbol"]: p for p in L.params()}
    out = {}
    for k, v in vals.items():
        p = by_sym.get(k)
        if not p:
            continue
        out[k] = p["labels"].index(v) if p["labels"] and v in p["labels"] else int(v)
    return out, name


def write_maquina(path, name, vals, category="Imported", tags=(), comment=""):
    lines = ["# Maquina preset (LA-50)"] + ([f"# {comment}"] if comment else []) + [
        "voice = la50", f"name = {name}", f"category = {category}", "tags = " + ", ".join(tags)]
    for p in L.params():
        v = vals[p["symbol"]]
        dflt = p["default"] + p["shown"] if p["kind"] == "Int" else p["default"]
        if v != dflt:
            lines.append(f"{p['symbol']} = {p['labels'][v] if p['labels'] else v}")
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")


def slug(s):
    return re.sub(r"[^a-z0-9]+", "-", s.lower()).strip("-") or "patch"


def main(argv):
    if len(argv) >= 2 and argv[0] == "list":
        mem, warn = parse(open(argv[1], "rb").read())
        for w in warn:
            print("warning:", w)
        for n, raw in patches(mem):
            print(f"{n:2d}  {patch_name(raw)}")
        return 0
    if len(argv) >= 3 and argv[0] == "import":
        mem, warn = parse(open(argv[1], "rb").read())
        os.makedirs(argv[2], exist_ok=True)
        tag = os.path.splitext(os.path.basename(argv[1]))[0]
        for n, raw in patches(mem):
            nm = patch_name(raw) or f"Patch {n}"
            write_maquina(os.path.join(argv[2], f"{n:02d}-{slug(nm)}.maquina"), f"{n:02d} {nm}", values(raw),
                          tags=[tag], comment=f"imported from {os.path.basename(argv[1])}")
        print(f"{len(patches(mem))} patches", *(f"(warning: {w})" for w in warn))
        return 0
    if len(argv) >= 3 and argv[0] == "export":
        files = sorted(f for f in os.listdir(argv[1]) if f.endswith(".maquina"))[:NUM_PATCHES]
        raws = [raw_of(*read_maquina(os.path.join(argv[1], f))) for f in files]
        raws += [raw_of({}, "INIT")] * (NUM_PATCHES - len(raws))
        open(argv[2], "wb").write(bank_syx(raws))
        print(f"{len(files)} patches -> {argv[2]}")
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
