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
th = next((t for t in r.get("threads", []) if t.get("triggered")), None)
for fr in (th or {}).get("frames", [])[:25]:
    img = images[fr["imageIndex"]] if fr.get("imageIndex", -1) < len(images) else {}
    print("   ", img.get("name", "?"), fr.get("symbol", ""), "+%s" % fr.get("symbolLocation", ""),
          "offset 0x%x" % fr.get("imageOffset", 0))
PY
rm -f "$f"
