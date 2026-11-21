#!/bin/sh
# screenshots.sh [plugin...] - editor screenshots for the README, one per plugin, into
# docs/screenshots/<plugin>.png (Linux, Hyprland: hyprctl + grim + jq + ImageMagick).
#
# Starts each JACK standalone from bin/ with an empty config dir (no user presets,
# ratings or zoom), loads a factory preset through the MAQUINA_UI_PRESET test hook,
# finds its window by pid, captures it and closes it. No clicks or keys are sent;
# each window shows for about two seconds on the focused monitor (best on a 2x screen:
# the editor is then captured at its own 1x size; XWayland windows are drawn at 2x).
# SPDX-License-Identifier: GPL-3.0-only
set -e
cd "$(dirname "$0")/.."
out=docs/screenshots
mkdir -p "$out"
cfg=$(mktemp -d)
trap 'rm -rf "$cfg"' EXIT

# plugin  binary  factory preset
list='kick Kick|909 Punch
tom Tom|808 Tom Mid
snare Snare|909 Snare
cymbal Cymbal|Analog Ride
hihat HiHat|Analog Hat
dredrum Dredrum|8-Bit Kick
plethora Plethora|Satan Drone
oneiroi Oneiroi|Warm Saw Pad
oneiroi-fx OneiroiFX|Big Hall
klang Klang|DX Bells
klong Klong|Nylon
klong-fx KlongFX|Sympathetic Strings
acida Acida|Classic Acid
acida-fx AcidaFX|Full Chain
bl101 BL101|Warm Reso Poly
bl101-fx BL101FX|Dotted Hall
la50 LA50|Glass Pad'

echo "$list" | while IFS='|' read -r ids preset; do
    id=${ids% *}; bin=bin/Maquina${ids#* }
    if [ $# -gt 0 ]; then case " $* " in *" $id "*) ;; *) continue ;; esac; fi
    [ -x "$bin" ] || { echo "missing $bin (make first)"; exit 1; }
    XDG_CONFIG_HOME="$cfg" MAQUINA_UI_PRESET="$preset" MAQUINA_UI_SIZE=1 "$PWD/$bin" >/dev/null 2>"$cfg/err.txt" &
    pid=$!
    win=""
    for _ in $(seq 50); do
        sleep 0.2
        win=$(hyprctl clients -j | jq -r --argjson p "$pid" \
            '.[] | select(.pid == $p) | "\(.at[0]),\(.at[1]) \(.size[0])x\(.size[1])"' | head -n 1)
        [ -n "$win" ] && break
    done
    [ -n "$win" ] || { echo "$id: no window"; kill "$pid"; exit 1; }
    # a tiled window is stretched to its tile: float it, so it takes the editor's own size
    hyprctl dispatch "hl.dsp.window.float({ action = \"enable\", window = \"pid:$pid\" })" >/dev/null
    sleep 0.5
    # floated windows may get a default size: give it the editor's own (MAQUINA_UI_SIZE, in
    # pixels; the compositor works in logical units)
    size=$(sed -n 's/^maquina-ui-size //p' "$cfg/err.txt" | head -n 1)
    if [ -n "$size" ]; then
        scale=$(hyprctl monitors -j | jq -r '.[] | select(.focused) | .scale')
        w=$(echo "$size $scale" | awk '{ printf "%d", $1 / $3 + 0.5 }')
        h=$(echo "$size $scale" | awk '{ printf "%d", $2 / $3 + 0.5 }')
        hyprctl dispatch "hl.dsp.window.resize({ x = $w, y = $h, exact = true, window = \"pid:$pid\" })" >/dev/null
        sleep 0.3
    fi
    win=$(hyprctl clients -j | jq -r --argjson p "$pid" \
        '.[] | select(.pid == $p) | "\(.at[0]),\(.at[1]) \(.size[0])x\(.size[1])"' | head -n 1)
    sleep 1.5   # first draws + the preset
    grim -g "$win" "$cfg/raw.png"
    kill "$pid"
    # grim captures at the monitor scale: on a 2x screen that is the editor's 1x size
    magick "$cfg/raw.png" -strip "$out/$id.png"
    echo "$id: $win -> $out/$id.png ($(wc -c < "$out/$id.png") bytes)"
    sleep 0.5
done
