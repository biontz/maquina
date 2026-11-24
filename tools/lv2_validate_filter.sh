#!/bin/sh
# Filter lv2_validate output: DPF bundles reference the KXStudio programs and
# control-input-port-change-request extensions and the MOD vocabulary, whose
# schemas lv2_validate does not ship. Those messages (and only those) are
# expected; anything else fails.
# SPDX-License-Identifier: GPL-3.0-only
awk '
/^error: Object not in range <http:\/\/lv2plug.in\/ns\/lv2core#ExtensionData>/ { next }
/^error: Property <http:\/\/moddevices.com\/ns\/mod#(brand|label)> has no label/ { next }
# the optional feature behind MIDI program change (ProgramMap.hpp): error, blank,
# subject, predicate, then the feature URI, which must be the KXStudio one
/^error: Object not in range <http:\/\/lv2plug.in\/ns\/lv2core#Feature>/ { feat=4; next }
feat > 1 { feat--; next }
feat == 1 { feat=0
            if ($0 !~ /kx.studio\/ns\/lv2ext\/control-input-port-change-request/) { print "unexpected feature: " $0; bad=1 }
            next }
/^error: Use of undefined property/ { undef=2; next }
undef == 2 { undef=1; next }                       # subject line
undef == 1 { undef=0
             if ($0 !~ /moddevices.com\/ns\/mod#|kxstudio.sf.net\/ns\/lv2ext\/programs/) { print "undefined property: " $0; bad=1 }
             next }
/^error:/ { print; bad=1 }
END { if (bad) { print "lv2_validate: unexpected errors"; exit 1 }
      print "lv2_validate: ok (only known DPF vocabulary notes)" }'
