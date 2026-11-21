"""transcribe.py - monophonic pitch tracker for the D-50 reference demos.

Usage: python3 transcribe.py AUDIO.wav OUT.notes.json
Emits [[start_sec, midi, dur_sec], ...].

Hand-rolled numpy (no scipy/librosa on this system): per-frame FFT peak
picking with parabolic interpolation, confidence gate, note segmentation.
"""
import json
import sys
import wave

import numpy as np

SR = 32000
FRAME = 2048
HOP = 512
MIDI_MIN, MIDI_MAX = 24, 108  # keep it in musical range
F_MIN = 40.0

def read_wav(path):
    with wave.open(path, "rb") as w:
        assert w.getframerate() == SR, f"expected {SR} Hz, got {w.getframerate()}"
        assert w.getnchannels() == 1, "expected mono"
        raw = w.readframes(w.getnframes())
    return np.frombuffer(raw, dtype=np.int16).astype(np.float64) / 32768.0

def write_wav(path, x, sr=SR):
    with wave.open(path, "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(sr)
        w.writeframes((np.clip(x, -1, 1) * 32767).astype(np.int16).tobytes())

def hz_to_midi(f):
    return 69 + 12 * np.log2(f / 440.0)

def track_notes(path):
    x = read_wav(path)
    n_frames = 1 + (len(x) - FRAME) // HOP
    win = np.hanning(FRAME)
    freqs = np.fft.rfftfreq(FRAME, 1.0 / SR)
    frame_pitch = np.full(n_frames, np.nan)  # midi note per frame, NaN = rest
    for i in range(n_frames):
        seg = x[i * HOP : i * HOP + FRAME] * win
        if np.sum(seg * seg) < 1e-7:  # silence
            continue
        mag = np.abs(np.fft.rfft(seg))
        m_lo = int(np.searchsorted(freqs, F_MIN))
        k = m_lo + int(np.argmax(mag[m_lo:]))
        if k + 1 >= len(mag) or k < 1:
            continue
        # parabolic interpolation on the peak
        a, b, c = mag[k - 1], mag[k], mag[k + 1]
        denom = a - 2 * b + c
        k_hz = freqs[k] + (freqs[1] - freqs[0]) * (0.5 * (a - c) / denom if denom != 0 else 0.0)
        peak = b / max(1.0, 0.02 * np.sum(mag))  # dominance vs broadband floor
        if peak < 1.5:  # not a dominant partial -> noise/reverb tail, rest
            continue
        midi = hz_to_midi(k_hz)
        if MIDI_MIN <= midi <= MIDI_MAX:
            frame_pitch[i] = midi
    # segment: merge frames within 0.6 semitones, allow 2-frame gaps
    notes = []
    cur = None  # [start_frame, midi_acc, count]
    for i in range(n_frames):
        p = frame_pitch[i]
        if np.isnan(p):
            if cur and i - cur[3] > 3:  # gap > ~48 ms ends the note
                notes.append((_onset(cur[0]), int(round(cur[1])), (cur[3] - cur[0] + 4) * HOP / SR))
                cur = None
            continue
        if cur and abs(p - cur[1]) <= 0.6:
            cur[1] = (cur[1] * cur[2] + p) / (cur[2] + 1); cur[2] += 1; cur[3] = i
        else:
            if cur:
                notes.append((_onset(cur[0]), int(round(cur[1])), (cur[3] - cur[0] + 4) * HOP / SR))
            cur = [i, p, 1, i]
    if cur:
        notes.append((_onset(cur[0]), int(round(cur[1])), (cur[3] - cur[0] + 4) * HOP / SR))
    # drop micronotes (< 40 ms) and clamp durations >= 60 ms
    return [(t, m, max(d, 0.06)) for t, m, d in notes if d >= 0.04]

def _onset(frame):  # compensate the FRAME/2 lookahead of the analysis window
    return (frame * HOP + FRAME / 2) / SR

if __name__ == "__main__":
    notes = track_notes(sys.argv[1])
    json.dump(notes, open(sys.argv[2], "w"))
    print(f"{len(notes)} notes -> {sys.argv[2]}")