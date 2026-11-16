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
for fr in (th or {}).get("frames", [])[:25]:
    img = images[fr["imageIndex"]] if fr.get("imageIndex", -1) < len(images) else {}
    sym = fr.get("symbol") or atos(img, img.get("base", 0) + fr.get("imageOffset", 0))
    print("   ", img.get("name", "?"), sym, "offset 0x%x" % fr.get("imageOffset", 0))
# the faulting address: which symbol / section of which image
try:
    fault = int(str(r.get("exception", {}).get("subtype", "")).split(" at ")[-1], 16)
    for img in images:
        if img.get("base", 0) <= fault < img.get("base", 0) + img.get("size", 0):
            print("  fault address in", img.get("name"), "offset 0x%x:" % (fault - img["base"]), atos(img, fault))
            out = subprocess.run(["otool", "-l", img["path"]], capture_output=True, text=True).stdout
            seg = None
            for line in out.splitlines():
                w = line.split()
                if w[:1] == ["segname"]: seg = w[1]
                if w[:1] == ["vmaddr"]: start = int(w[1], 16)
                if w[:1] == ["vmsize"] and seg and start <= fault - img["base"] < start + int(w[1], 16):
                    print("  in segment", seg)
                    seg = None
except Exception as e:
    print("  (fault address: %s)" % e)
PY
rm -f "$f"
