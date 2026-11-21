"""compare.py - the D-50 A/B driver: transcribe references, render LA-50,
compute metrics, emit compare-report.html.

Usage: python3 tools/d50_compare/compare.py
Assumes: build/render_d50, the bank syx, build/compare/ref/*.wav (32 kHz).
"""
import html
import json
import os
import subprocess
import sys
import wave

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)  # tools dir; repo root is one up
sys.path.insert(0, HERE)              # the script's own dir: plain module imports
ROOT = os.path.dirname(REPO)  # repo root
from transcribe import track_notes  # noqa: E402

BANK = os.path.expanduser("~/Music/la50-banks/PN-D50-00/PND50-00.syx")
REF = os.path.join(ROOT, "build/compare")
SR = 32000

def read_wav_mono(path):
    with wave.open(path) as w:
        n = w.getnframes()
        x = np.frombuffer(w.readframes(n), dtype=np.int16).astype(np.float64) / 32768.0
    return x

def metrics(x):
    """spectral centroid / rolloff / HF-LF ratio of the whole clip"""
    f = np.abs(np.fft.rfft(x * np.hanning(len(x))))
    freqs = np.fft.rfftfreq(len(x), 1.0 / SR)
    e = f * f
    tot = e.sum()
    if tot <= 0:
        return None
    centroid = float((freqs * e).sum() / tot)
    cum = np.cumsum(e) / tot
    roff = float(freqs[int(np.searchsorted(cum, 0.85))])
    hf = float(e[freqs > 5000].sum() / tot)
    lf = float(e[(freqs > 20) & (freqs < 200)].sum() / tot)
    return dict(centroid=centroid, rolloff=roff, hf=hf, lf=lf)

def rel_delta(a, b):
    return (b - a) / a if a else 0.0

def main():
    n_patches = 64
    rows = []
    for i in range(1, n_patches + 1):
        idx = i - 1
        ref_wav = f"{REF}/ref/{i:02d}.wav"
        la_wav = f"{REF}/la50-{i:02d}.wav"
        if not os.path.exists(ref_wav):
            continue
        # transcribe the reference
        notes = track_notes(ref_wav)
        notes_txt = f"{REF}/{i:02d}.notes.txt"
        with open(notes_txt, "w") as f:
            for t, m, d in notes:
                f.write(f"{t:.3f} {m} {d:.3f} 100\n")
        # render LA-50 with the same notes
        if not os.path.exists(la_wav):
            subprocess.run([os.path.join(ROOT, "build/render_d50"), BANK, str(idx), notes_txt, la_wav],
                           check=True, capture_output=True)
        xm = metrics(read_wav_mono(ref_wav))
        ym = metrics(read_wav_mono(la_wav))
        rows.append(dict(n=i, notes=len(notes),
                         ref= xm, la=ym,
                         d_cent=rel_delta(xm["centroid"], ym["centroid"]),
                         d_roff=rel_delta(xm["rolloff"], ym["rolloff"])))
        print(f"{i:02d}: {len(notes)} notes, centroid {xm['centroid']:.0f} -> {ym['centroid']:.0f} Hz")

    # report
    out = f"{REF}/compare-report.html"
    with open(out, "w") as h:
        h.write("<html><head><title>LA-50 vs D-50</title>"
                "<style>body{font-family:sans-serif;background:#111;color:#ddd}"
                "td,th{padding:4px 10px;border-bottom:1px solid #333;text-align:right}"
                "td:first-child,th:first-child{text-align:left}audio{width:280px}"
                ".big{color:#e55}.small{color:#5c5}</style></head><body>"
                "<h1>LA-50 stand-ins vs D-50 reference (PN-D50-00)</h1>"
                "<p>LA-50 renders play the notes transcribed from each reference demo."
                " Centroid/rolloff deltas &gt; 40% flagged red.</p><table>"
                "<tr><th>#</th><th>notes</th><th>ref centroid</th><th>LA centroid</th>"
                "<th>&Delta;centroid</th><th>&Delta;rolloff</th><th>reference</th><th>LA-50</th></tr>")
        for r in rows:
            hot = abs(r["d_cent"]) > 0.4
            cls = ' class="big"' if hot else ""
            h.write(f"<tr><td>{r['n']:02d}</td><td>{r['notes']}</td>"
                    f"<td>{r['ref']['centroid']:.0f} Hz</td><td>{r['la']['centroid']:.0f} Hz</td>"
                    f"<td{cls}>{r['d_cent']*100:+.0f}%</td><td>{r['d_roff']*100:+.0f}%</td>"
                    f"<td><audio controls src='ref/{r['n']:02d}.wav'></audio></td>"
                    f"<td><audio controls src='la50-{r['n']:02d}.wav'></audio></td></tr>")
        h.write("</table></body></html>")
    print(f"\nreport: {out}")

if __name__ == "__main__":
    main()