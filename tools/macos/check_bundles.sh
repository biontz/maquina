#!/bin/sh
# check_bundles.sh DIR ARCH... - every bundle in DIR is universal (all ARCHs),
# validly signed, and exports only its plugin entry points (DPF's .exp
# lists). The last matters for Maquina Oneiroi, which replaces operator
# new/delete for its own binary only.
# SPDX-License-Identifier: GPL-3.0-only
set -e
dir=$1; shift
fail=0
for b in "$dir"/*.clap "$dir"/*.vst3; do
    [ -d "$b" ] || continue
    name=$(basename "$b"); name=${name%.*}
    bin="$b/Contents/MacOS/$name"
    case $b in
        *.clap) want="_clap_entry" ;;
        *.vst3) want="_GetPluginFactory _bundleEntry _bundleExit" ;;
    esac
    want=$(printf '%s\n' $want | sort | tr '\n' ' ')
    msg=""
    have_archs=$(lipo -archs "$bin")
    for a in "$@"; do
        case " $have_archs " in *" $a "*) ;; *) msg="$msg no $a;" ;; esac
        got=$(nm -gU -arch "$a" "$bin" | awk '{print $NF}' | grep -v '^__mh_' | sort | tr '\n' ' ')
        [ "$got" = "$want" ] || msg="$msg $a exports: $got;"
    done
    codesign --verify --strict "$b" 2>/dev/null || msg="$msg bad signature;"
    if [ -n "$msg" ]; then echo "  $(basename "$b"): FAIL$msg"; fail=1
    else echo "  $(basename "$b"): $have_archs, signed, exports ok"; fi
done
[ $fail -eq 0 ] && echo "check_bundles: PASSED" || { echo "check_bundles: FAILED"; exit 1; }
