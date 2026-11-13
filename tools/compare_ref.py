#!/usr/bin/env python3
"""compare_ref.py PLUGIN.wav REF.raw - plugin float WAV (x32768) must equal the
engine's int16 reference sample for sample."""
import struct
import sys


def main():
    wav, raw = sys.argv[1], sys.argv[2]
    with open(wav, "rb") as f:
        data = f.read()
    i = data.index(b"data") + 8
    plug = struct.unpack("<%df" % ((len(data) - i) // 4), data[i:])
    with open(raw, "rb") as f:
        r = f.read()
    ref = struct.unpack("<%dh" % (len(r) // 2), r)
    n = min(len(plug), len(ref))
    diff = [k for k in range(n) if round(plug[k] * 32768.0) != ref[k]]
    peak = max(abs(x) for x in ref[:n])
    if diff:
        k = diff[0]
        print(f"  MISMATCH: {len(diff)} of {n} samples differ, first at frame {k // 2}: "
              f"plugin {plug[k] * 32768.0:.1f} vs engine {ref[k]}")
        return 1
    print(f"  bit-exact: {n // 2} frames identical (peak {peak})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
