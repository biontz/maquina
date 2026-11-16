#!/bin/sh
# check_state.sh - every mutable global in the engine must either be part of
# the per-instance state blob (LXR_STATE(...) in drum02/engine/lxr_host.c) or be on
# the list of state the voice plugins provably never use. Run after updating
# the firmware; fails if a new global appears.
# SPDX-License-Identifier: GPL-3.0-only
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
LIB="$ROOT/build/engine/libmaquinaengine.a"
[ -f "$LIB" ] || { echo "build the engine first ($LIB missing)"; exit 1; }

# captured in the state blob
captured=$(sed -n 's/^ *LXR_STATE(\([A-Za-z0-9_]*\)),.*/\1/p' "$ROOT/drum02/engine/lxr_host.c")
captured="$captured midiParser_selectedLfoVoice"

# not captured on purpose (see the comment above lxr_state_entries)
#  - FX bus: voice plugins never route to it
#  - MIDI byte-stream parser state: the plugins call midiParser_ccHandler directly
#  - jack-detect flags: recomputed from constant GPIO stubs every mixer call
#  - sequencer pattern + UI stand-ins + read-only stubs
#  - Param(2)Enums: unused bitfield scratch in MidiMessages.c
#  - parameterOrder: a table the firmware never writes (just not declared const)
#  - lxr_pristine/lxr_initialised/lxr_rng_frozen/lxr_state_entries: host glue
ignored="fxBus FX_BUFFER seq_patternSet menu_activePage frontParser_activeTrack systick_ticks
lxr_shim_gpio_all_high hrng midiMsg_tmp midiParser_txRxFilter midiParser_mtcIgnore
midiParser_mtcIsRunning midiParser_lastMtcReceived midiParser_activeNrpnNumber midiParser_routing
checkMIDIprocPAR checkMIDIprocPAR_t parserState active_voices
r1_Available r2_Available l1_Available l2_Available ParamEnums Param2Enums
lxr_pristine lxr_initialised lxr_rng_frozen lxr_state_entries parameterOrder"

fail=0
for sym in $(nm "$LIB" 2>/dev/null | awk '$2 ~ /^[bBdDcC]$/ {print $3}' | sed 's/\..*//' | sort -u); do
    case " $(echo $captured $ignored) " in
        *" $sym "*) ;;
        *) echo "uncaptured engine global: $sym"; fail=1 ;;
    esac
done
if [ $fail -ne 0 ]; then
    echo "check_state: FAILED - add the symbol(s) to lxr_state_entries or justify ignoring them"
    exit 1
fi
echo "check_state: ok ($(echo $captured | wc -w) state entries)"
