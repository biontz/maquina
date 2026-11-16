#!/bin/sh
# crash_report.sh NAME - print the crashed thread of the newest macOS crash
# report (~/Library/Logs/DiagnosticReports/NAME*.ips) for a process that
# just crashed (used by make macos-test). The report takes a moment to appear.
# SPDX-License-Identifier: GPL-3.0-only
dir="$HOME/Library/Logs/DiagnosticReports"
for i in 1 2 3 4 5 6 7 8 9 10; do
    f=$(ls -t "$dir"/"$1"*.ips 2>/dev/null | head -n 1)
    [ -n "$f" ] && break
    sleep 1
done
[ -z "$f" ] && { echo "  (no crash report for $1)"; exit 0; }
python3 - "$f" <<'PY'
import json, sys
text = open(sys.argv[1]).read()
head, body = text.split("\n", 1)
r = json.loads(body)
images = r.get("usedImages", [])
print("  crash:", r.get("exception", {}).get("type"), r.get("exception", {}).get("signal"),
      r.get("exception", {}).get("subtype", ""))
import subprocess
def atos(img, addr):
    """symbol of an address in an image (needs local symbols: SKIP_STRIPPING)"""
    if not img.get("path"):
        return ""
    try:
        return subprocess.run(["atos", "-o", img["path"], "-arch", img.get("arch", "arm64"), "-l",
                               hex(img["base"]), hex(addr)], capture_output=True, text=True,
                              timeout=20).stdout.strip()
    except Exception as e:
        return "(atos: %s)" % e
th = next((t for t in r.get("threads", []) if t.get("triggered")), None)
frames = (th or {}).get("frames", [])
for fr in frames[:25]:
    img = images[fr["imageIndex"]] if fr.get("imageIndex", -1) < len(images) else {}
    sym = fr.get("symbol") or atos(img, img.get("base", 0) + fr.get("imageOffset", 0))
    print("   ", img.get("name", "?"), sym, "+%s" % fr.get("symbolLocation", "?"),
          "offset 0x%x" % fr.get("imageOffset", 0))
# the crashing instruction, disassembled
if frames:
    fr = frames[0]
    img = images[fr["imageIndex"]]
    off = fr.get("imageOffset", 0)
    out = subprocess.run(["objdump", "-d", "--no-show-raw-insn", "--start-address=0x%x" % max(0, off - 48),
                          "--stop-address=0x%x" % (off + 8), img["path"]], capture_output=True, text=True).stdout
    print("\n".join("    " + l for l in out.splitlines()[-16:]))
# the faulting address: image, offset, nearest symbol
st = r.get("threadState", (th or {}).get("threadState", {}))
sub = str(r.get("exception", {}).get("subtype", ""))
try:
    fault = int(sub.split(" at ")[-1], 16)
    for img in images:
        base = img.get("base", 0)
        if img.get("path") and base <= fault < base + img.get("size", 1 << 32) and "Maquina" in img.get("name", ""):
            print("  fault address = %s + 0x%x: %s" % (img.get("name"), fault - base, atos(img, fault)))
            out = subprocess.run(["otool", "-l", img["path"]], capture_output=True, text=True).stdout
            sect = seg = None
            for line in out.splitlines():
                w = line.split()
                if w[:1] == ["sectname"]: sect = w[1]
                if w[:1] == ["segname"]: seg = w[1]
                if w[:1] == ["addr"]: start = int(w[1], 16)
                if w[:1] == ["size"] and sect and start <= fault - base < start + int(w[1], 16):
                    print("  in section", seg, sect)
            nm = subprocess.run(["nm", "-n", img["path"]], capture_output=True, text=True).stdout.splitlines()
            prev = None
            for line in nm:
                w = line.split()
                if len(w) == 3 and int(w[0], 16) > fault - base:
                    break
                if len(w) == 3: prev = line
            print("  nearest symbol below:", prev)
except Exception as e:
    print("  (fault address: %s)" % e)
PY
rm -f "$f"
