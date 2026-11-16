#!/bin/sh
# lipo_bundles.sh OUT ARCH... - merge the per-arch macOS bundles
# (bin-macos-<arch>/*.clap, *.vst3) into universal ones in OUT, give each a
# Maquina bundle identifier and sign it ad hoc (lipo drops the linker's own
# arm64 signature; Apple Silicon refuses to load unsigned code).
# SPDX-License-Identifier: GPL-3.0-only
set -e
out=$1; shift
archs=$*
first=$1
rm -rf "$out"
mkdir -p "$out"
for src in "bin-macos-$first"/*.clap "bin-macos-$first"/*.vst3; do
    [ -d "$src" ] || continue
    b=$(basename "$src")
    name=${b%.*}
    ext=${b##*.}
    bin="Contents/MacOS/$name"
    cp -R "$src" "$out/$b"
    inputs=""
    for a in $archs; do
        [ -f "bin-macos-$a/$b/$bin" ] || { echo "missing bin-macos-$a/$b/$bin"; exit 1; }
        inputs="$inputs bin-macos-$a/$b/$bin"
    done
    # shellcheck disable=SC2086
    lipo -create $inputs -output "$out/$b/$bin"
    # DPF's template says studio.kx.distrho.<Name> for every format
    plutil -replace CFBundleIdentifier -string "org.maquina.$name.$ext" "$out/$b/Contents/Info.plist"
    codesign --force --sign - "$out/$b"
    echo "  $b: $(lipo -archs "$out/$b/$bin")"
done
