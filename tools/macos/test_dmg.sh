#!/bin/sh
# test_dmg.sh DMG - mount the disk image, run its installer into a temporary
# HOME, and check what it installed (macOS; make macos-dmg-test, CI).
# SPDX-License-Identifier: GPL-3.0-only
set -e
dmg=$1
hdiutil verify "$dmg"
mnt=$(mktemp -d)
home=$(mktemp -d)
trap 'hdiutil detach "$mnt" -quiet || true; rm -rf "$home"' EXIT
hdiutil attach -nobrowse -readonly -mountpoint "$mnt" "$dmg" >/dev/null
want_clap=$(ls -d "$mnt"/CLAP/*.clap | wc -l | tr -d ' ')
want_vst3=$(ls -d "$mnt"/VST3/*.vst3 | wc -l | tr -d ' ')
HOME="$home" MAQUINA_INSTALL_YES=1 "$mnt/Install Maquina.command"
plug="$home/Library/Audio/Plug-Ins"
got_clap=$(ls -d "$plug"/CLAP/Maquina*.clap | wc -l | tr -d ' ')
got_vst3=$(ls -d "$plug"/VST3/Maquina*.vst3 | wc -l | tr -d ' ')
fail=0
[ "$got_clap" = "$want_clap" ] && [ "$got_vst3" = "$want_vst3" ] || { echo "installed $got_clap/$want_clap CLAP, $got_vst3/$want_vst3 VST3"; fail=1; }
[ "$want_clap" -gt 0 ] || { echo "no plugins in the image"; fail=1; }
if xattr -lr "$plug" | grep -q com.apple.quarantine; then echo "quarantine left"; fail=1; fi
for b in "$plug"/CLAP/*.clap "$plug"/VST3/*.vst3; do
    codesign --verify --strict "$b" || { echo "signature: $b"; fail=1; }
done
b=$(ls -d "$plug"/CLAP/*.clap | head -n 1)
echo "$(basename "$b"): $(lipo -archs "$b/Contents/MacOS/$(basename "$b" .clap)")"
[ $fail -eq 0 ] && echo "test_dmg: PASSED ($got_clap CLAP + $got_vst3 VST3)" || { echo "test_dmg: FAILED"; exit 1; }
