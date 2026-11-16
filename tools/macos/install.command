#!/bin/sh
# Install Maquina.command - copies the Maquina CLAP and VST3 plugins from this
# disk image into ~/Library/Audio/Plug-Ins and clears macOS's download
# quarantine on them (they are signed ad hoc, not by a registered Apple
# developer). First time: right-click this file > Open (or System Settings >
# Privacy & Security > Open Anyway).
#
# MAQUINA_INSTALL_YES=1: no "press Return" at the end (CI test).
# SPDX-License-Identifier: GPL-3.0-only
cd "$(dirname "$0")" || exit 1
dest="$HOME/Library/Audio/Plug-Ins"
mkdir -p "$dest/CLAP" "$dest/VST3" || exit 1

echo "Installing Maquina into $dest"
n=0
for fmt in CLAP VST3; do
    for b in "$fmt"/Maquina*.*; do
        [ -d "$b" ] || continue
        name=$(basename "$b")
        # replace, don't merge: remove the old bundle first
        rm -rf "$dest/$fmt/$name"
        if cp -R "$b" "$dest/$fmt/$name"; then
            xattr -dr com.apple.quarantine "$dest/$fmt/$name" 2>/dev/null
            echo "  $fmt/$name"
            n=$((n + 1))
        else
            echo "  FAILED: $fmt/$name"
        fi
    done
done
echo
echo "$n plugins installed. Restart your DAW (or rescan plugins) to load them;"
echo "a DAW that was running keeps the old version until it restarts."

if [ "$MAQUINA_INSTALL_YES" != 1 ]; then
    echo
    printf "Press Return to close."
    read -r _
fi
