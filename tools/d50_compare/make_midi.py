"""make_midi.py - notes JSON -> probe MIDI file (480 ppq).

Usage: python3 make_midi.py IN.notes.json OUT.mid [BPM]
Notes JSON: [[start_sec, midi, dur_sec], ...]  (transcribe.py output)
"""
import json
import struct
import sys

PPQ = 480

def vlq(v):
    out = b""
    while True:
        b7 = v & 0x7F
        v >>= 7
        out = bytes([b7 | (0x80 if out else 0)]) + out
        if not v:
            return out

def chunk(tag, data):
    return tag + struct.pack(">I", len(data)) + data

def make_midi(notes, bpm=120):
    s16 = PPQ // 4
    ev = []
    for start, midi, dur in notes:
        t = int(round(start * PPQ))
        n = int(midi)
        ev.append((t, 1, bytes([0x90, n, 100])))
        ev.append((t + max(int(round(dur * PPQ)) - 5, 10), 0, bytes([0x80, n, 0])))
    ev.sort(key=lambda e: (e[0], e[1]))
    track = b""
    last = 0
    for tick, _, msg in ev:
        track += vlq(tick - last) + msg
        last = tick
    track += vlq(0) + b"\xff\x2f\x00"
    tempo = int(60_000_000 / bpm)
    t0 = vlq(0) + b"\xff\x51\x03" + struct.pack(">I", tempo)[1:] + vlq(0) + b"\xff\x2f\x00"
    return (b"MThd" + struct.pack(">IHH", 6, 1, 2) + struct.pack(">H", PPQ)
            + chunk(b"MTrk", t0) + chunk(b"MTrk", track))

if __name__ == "__main__":
    notes = json.load(open(sys.argv[1]))
    bpm = int(sys.argv[3]) if len(sys.argv) > 3 else 120
    open(sys.argv[2], "wb").write(make_midi(notes, bpm))
    print(f"{len(notes)} notes -> {sys.argv[2]}")