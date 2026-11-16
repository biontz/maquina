#!/usr/bin/env python3
"""
arm_semantics.py IN.c OUT.c [clang args...]

Rewrite every float->integer conversion in IN.c so it behaves like the
Cortex-M7 (and any Arm) on every target.

Why: C leaves out-of-range float->int conversions undefined. Arm's VCVT /
FCVTZ* saturate (negative -> 0 for unsigned, NaN -> 0); x86 instead wraps or
returns 0x80000000. The firmware relies on the Arm behaviour - e.g. the FM
oscillators compute (uint32_t)(modBuffer[i] * fmMod) with negative values,
which the hardware clamps to 0 - so an x86 build of the untouched sources
sounds different from the LXR-02.

How: clang's JSON AST lists every FloatingToIntegral conversion (explicit
casts, implicit conversions) and every compound assignment of a float into an
integer, with byte offsets. The converted expression is wrapped in
lxr_arm_f2u32()/lxr_arm_f2s32() (drum02/engine/fw_shim/lxr_arm.h), which return the
32-bit value VCVT produces; a narrower destination then truncates exactly as
the firmware's code does on the hardware. `a op= b` with integer a and float b
becomes `a = conv(a op (b))` (only for side-effect-free a).

Arm builds compile the pristine sources; non-Arm builds compile the rewritten
copies under build/. Conversions in headers are rewritten when the header's
functions are compiled... they are not: header code (static inline helpers)
is reported instead - see the log - and checked to be in-range by hand.

SPDX-License-Identifier: GPL-3.0-only
"""
import json
import re
import subprocess
import sys


class Tracker:
    """clang's JSON omits "file" when unchanged since the previous location in
    output order; replay that order to know each location's file."""

    def __init__(self):
        self.file = None

    def loc(self, loc):
        """returns {'file','offset','tokLen','line'} for spelling and expansion"""
        if not loc:
            return None, None
        if "spellingLoc" in loc or "expansionLoc" in loc:
            sp = self._one(loc.get("spellingLoc"))
            ex = self._one(loc.get("expansionLoc"))
            return sp, ex
        one = self._one(loc)
        return one, one

    def _one(self, l):
        if not l:
            return None
        if "file" in l:
            self.file = l["file"]
        return dict(file=self.file, offset=l.get("offset"), tokLen=l.get("tokLen", 0), line=l.get("line"))


def is_float(t):
    return t in ("float", "double", "long double", "const float", "const double")


def conv_fn(dest):
    if dest in ("_Bool", "bool"):
        return None
    wide = "long long" in dest or "int64" in dest
    unsigned = "unsigned" in dest or dest.startswith("uint") or dest == "size_t"
    if unsigned:
        return "lxr_arm_f2u64" if wide else "lxr_arm_f2u32"
    return "lxr_arm_f2s64" if wide else "lxr_arm_f2s32"


def main():
    src, dst, args = sys.argv[1], sys.argv[2], sys.argv[3:]
    with open(src, "rb") as f:
        text = f.read()
    ast = json.loads(subprocess.check_output(
        ["clang", "-fsyntax-only", "-w", "-Xclang", "-ast-dump=json", src] + args))
    name = src.replace("\\", "/").split("/")[-1]
    tr = Tracker()
    edits, notes = [], []  # edits: (start, stop, prefix, suffix)

    def in_src(l):
        return l and l["file"] and l["file"].replace("\\", "/").endswith("/" + name) or (l and l["file"] == src)

    def span(begin, end):
        """byte span in this file, using spelling locations (macro arguments
        are spelled in the file); None if not rewritable."""
        bsp, bex = begin
        esp, eex = end
        if not (in_src(bsp) and in_src(esp)):
            return None
        if bsp["offset"] is None or esp["offset"] is None:
            return None
        a, b = bsp["offset"], esp["offset"] + esp["tokLen"]
        if b <= a:
            return None
        return a, b

    def visit(node, parent_header):
        sp_ex = tr.loc(node.get("loc"))
        rng = node.get("range") or {}
        begin = tr.loc(rng.get("begin"))
        end = tr.loc(rng.get("end"))
        here = begin[0] or sp_ex[0]
        header = parent_header
        if node.get("kind") in ("FunctionDecl", "VarDecl", "TypedefDecl", "RecordDecl"):
            header = not in_src(here) if here else parent_header

        kind = node.get("kind")
        if node.get("castKind") == "FloatingToIntegral" and node.get("inner"):
            dest = node.get("type", {}).get("qualType", "")
            fn = conv_fn(dest)
            child = node["inner"][0]
            # the child's range is visited below; compute it from its own fields
            saved = tr.file
            crng = child.get("range") or {}
            cb, ce = tr.loc(crng.get("begin")), tr.loc(crng.get("end"))
            tr.file = saved
            if fn:
                s = span(cb, ce)
                if s:
                    edits.append((s[0], s[1], fn + "(", ")"))
                elif header:
                    notes.append(f"header conversion to {dest} (line {cb[0]['line'] if cb[0] else '?'}) not rewritten")
                else:
                    notes.append(f"conversion to {dest} in a macro body (line {cb[1]['line'] if cb[1] else '?'}) not rewritten")
        elif kind == "CompoundAssignOperator":
            comp = node.get("computeResultType", {}).get("qualType", "")
            res = node.get("type", {}).get("qualType", "")
            fn = conv_fn(res)
            if is_float(comp) and not is_float(res) and fn and len(node.get("inner", [])) == 2:
                saved = tr.file
                lhs, rhs = node["inner"]
                lr = lhs.get("range") or {}
                rr = rhs.get("range") or {}
                lb, le = tr.loc(lr.get("begin")), tr.loc(lr.get("end"))
                rb, re_ = tr.loc(rr.get("begin")), tr.loc(rr.get("end"))
                tr.file = saved
                ls, rs = span(lb, le), span(rb, re_)
                if ls and rs and ls[1] <= rs[0]:
                    lhs_text = text[ls[0]:ls[1]].decode()
                    op = text[ls[1]:rs[0]].decode()
                    m = re.fullmatch(r"\s*([-+*/])=\s*", op)
                    if m and not re.search(r"\+\+|--|\(", lhs_text):
                        edits.append((ls[0], rs[1], "", ""))  # marker, replaced below
                        edits[-1] = (ls[0], rs[1], "REPLACE",
                                     f"{lhs_text} = {fn}({lhs_text} {m.group(1)} ({text[rs[0]:rs[1]].decode()}))")
                    else:
                        notes.append(f"compound assignment to {res} not rewritten (line {lb[0]['line']})")
                elif header:
                    notes.append(f"header compound assignment to {res} (line {lb[0]['line'] if lb[0] else '?'}) not rewritten")
                else:
                    notes.append(f"compound assignment to {res} in a macro (line {lb[1]['line'] if lb[1] else '?'}) not rewritten")
        for c in node.get("inner", []):
            visit(c, header)

    visit(ast, False)

    # apply: replacements are atomic; wraps are nested insertions
    repl = [(a, b, s) for a, b, p, s in edits if p == "REPLACE"]
    wraps = [(a, b, p, s) for a, b, p, s in edits if p != "REPLACE"]
    for a, b, _ in repl:  # wraps inside a replaced range are dropped (the replacement converts)
        wraps = [w for w in wraps if not (w[0] >= a and w[1] <= b)]
    ins = []
    for a, b, p, s in set(wraps):
        ins.append((a, 0, -(b - a), p.encode(), None))
        ins.append((b, 1, (b - a), s.encode(), None))
    for a, b, s in set(repl):
        ins.append((a, 0, -(b - a), s.encode(), b))
    ins.sort(key=lambda t: (t[0], t[1], t[2]))
    out, pos = [], 0
    for off, _, _, s, skip_to in ins:
        if off < pos:
            continue
        out.append(text[pos:off])
        out.append(s)
        pos = skip_to if skip_to is not None else off
    out.append(text[pos:])
    with open(dst, "wb") as f:
        f.write(b'#include "lxr_arm.h" /* added by tools/arm_semantics.py */\n')
        f.write(b'#line 1 "' + src.encode() + b'"\n')
        f.write(b"".join(out))
    for n in sorted(set(notes)):
        print(f"{name}: {n}", file=sys.stderr)
    print(f"{name}: {len(set(wraps))} conversions + {len(set(repl))} compound assignments rewritten",
          file=sys.stderr)


if __name__ == "__main__":
    main()
