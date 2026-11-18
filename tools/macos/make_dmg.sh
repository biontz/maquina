#!/bin/sh
# make_dmg.sh SRC OUT.dmg VOLNAME - a disk image with the universal plugins in
# SRC (bin-macos/), an installer script and the install notes (macOS: hdiutil).
#
#   Install Maquina.command   tools/macos/install.command
#   CLAP/   Maquina*.clap
#   VST3/   Maquina*.vst3
#   README.md, INSTALL-macOS.txt, INSTALL-with-AI.md   tools/macos/
#   LICENSE                                            the repository's
#   screenshots/                                       docs/screenshots/ (README.md's images)
# SPDX-License-Identifier: GPL-3.0-only
set -e
src=$1
out=$2
vol=$3
here=$(cd "$(dirname "$0")" && pwd)
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT

mkdir -p "$stage/CLAP" "$stage/VST3"
for b in "$src"/*.clap "$src"/*.vst3; do
    [ -d "$b" ] || continue
    codesign --verify --strict "$b"
    case $b in *.clap) d=CLAP ;; *) d=VST3 ;; esac
    ditto "$b" "$stage/$d/$(basename "$b")"
done
cp "$here/install.command" "$stage/Install Maquina.command"
chmod +x "$stage/Install Maquina.command"
cp "$here/README.md" "$here/INSTALL-macOS.txt" "$here/INSTALL-with-AI.md" "$here/../../LICENSE" "$stage/"
mkdir -p "$stage/screenshots"
cp "$here/../../docs/screenshots/"*.png "$stage/screenshots/"

rm -f "$out"
hdiutil create -volname "$vol" -srcfolder "$stage" -fs HFS+ -format UDZO -ov "$out"
echo "$out: $(ls "$stage"/CLAP | wc -l | tr -d ' ') CLAP + $(ls "$stage"/VST3 | wc -l | tr -d ' ') VST3"
