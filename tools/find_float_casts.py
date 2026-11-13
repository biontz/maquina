#!/usr/bin/env python3
"""
find_float_casts.py - list every float->unsigned and float->signed integer
conversion in the engine sources (clang JSON AST).

Arm (Cortex-M7 and aarch64) converts out-of-range floats with saturation
(negative -> 0 for unsigned); x86 does not. Sites listed here are what
engine/arm_semantics.py rewrites for non-Arm builds.

usage: find_float_casts.py file.c [clang args...]
SPDX-License-Identifier: GPL-3.0-only
"""
import json
import subprocess
import sys


def walk(node, state, out):
    def track(loc):
        if not isinstance(loc, dict):
            return
        src = loc.get("expansionLoc", loc)
        if "file" in src:
            state["file"] = src["file"]
        if "line" in src:
            state["line"] = src["line"]
        if "col" in src:
            state["col"] = src["col"]
    track(node.get("loc"))
    rng = node.get("range")
    if rng:
        track(rng.get("begin"))
    if node.get("castKind") == "FloatingToIntegral":
        qt = node.get("type", {}).get("qualType", "")
        b = rng.get("begin", {}) if rng else {}
        b = b.get("expansionLoc", b)
        out.add((b.get("file", state["file"]), b.get("line", state["line"]), b.get("col", state.get("col")),
                 node.get("kind"), qt))
    for child in node.get("inner", []):
        walk(child, state, out)
        # ranges of children may move the state forward; end of range too
    if rng:
        track(rng.get("end"))


def main():
    src, args = sys.argv[1], sys.argv[2:]
    ast = json.loads(subprocess.check_output(
        ["clang", "-fsyntax-only", "-w", "-Xclang", "-ast-dump=json", src] + args))
    out = set()
    state = {"file": None, "line": None}
    walk(ast, state, out)
    for f, line, col, kind, qt in sorted(x for x in out if x[0] and x[0].endswith(src.split("/")[-1])):
        print(f"{f.split('/')[-1]}:{line}:{col}: {kind} -> {qt}")


if __name__ == "__main__":
    main()
