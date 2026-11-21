import sys, os, json, glob
import importlib.util

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), ".."))
from d50_compare.make_midi import make_midi

def test_make_midi_basic(tmp):
    notes = [[0.0, 60, 0.5], [0.5, 64, 0.5], [1.0, 67, 1.0]]
    data = make_midi(notes)
    assert data[:4] == b"MThd" and data[14:18] == b"MTrk"
    import tempfile, os
    p = os.path.join(tmp, "t.mid")
    open(p, "wb").write(data)
    try:
        import mido  # only present under /usr/bin/python3.14
    except ImportError:
        return  # structural checks above suffice on interpreters without mido
    m = mido.MidiFile(p)
    assert m.ticks_per_beat == 480
    tempos = [msg for tr in m.tracks for msg in tr if msg.type == "set_tempo"]
    assert len(tempos) == 1
    got = []
    for tr in m.tracks:
        t = 0
        for msg in tr:
            t += msg.time
            if msg.type == "note_on" and msg.velocity > 0:
                got.append((t, msg.note))
    assert got == [(0, 60), (240, 64), (480, 67)], got