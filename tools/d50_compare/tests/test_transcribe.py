import pathlib
import numpy as np, os, sys, struct, wave

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from d50_compare.transcribe import track_notes, write_wav

def test_known_phrase(tmp_path):
    tmp_path = pathlib.Path(tmp_path)
    sr = 32000
    # C4 E4 G4 arpeggio: 0.0-0.5s C4(60), 0.5-1.0s E4(64), 1.0-2.0s G4(67)
    t = np.arange(int(2.0 * sr)) / sr
    segs = [(0.0, 0.5, 261.63), (0.5, 1.0, 329.63), (1.0, 2.0, 392.0)]
    x = np.zeros(int(2.0 * sr))
    for start, end, f in segs:
        m = (t >= start) & (t < end)
        x[m] = np.sin(2 * np.pi * f * t[m]) + 0.3 * np.sin(2 * np.pi * 2 * f * t[m])
    x += 0.001 * np.random.default_rng(0).standard_normal(len(x))
    wav = tmp_path / "phrase.wav"
    write_wav(str(wav), x, sr)
    notes = track_notes(str(wav))
    assert [n[1] for n in notes] == [60, 64, 67]
    assert abs(notes[0][0] - 0.0) < 0.05 and abs(notes[1][0] - 0.5) < 0.05
    assert abs(notes[2][2] - (notes[2][1] and (2.0 - 1.0))) < 0.05 or notes[2][2] > 0.9  # G4 ~1 s

def test_silence_then_note(tmp_path):
    tmp_path = pathlib.Path(tmp_path)
    sr = 32000
    t = np.arange(int(1.5 * sr)) / sr
    x = np.zeros(len(t))
    m = t >= 0.5
    x[m] = 0.5 * np.sin(2 * np.pi * 220.0 * t[m])  # A3 = 57
    wav = tmp_path / "one.wav"
    write_wav(str(wav), x, sr)
    notes = track_notes(str(wav))
    assert len(notes) == 1 and notes[0][1] == 57
    assert abs(notes[0][0] - 0.5) < 0.05