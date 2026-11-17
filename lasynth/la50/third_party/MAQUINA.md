# Maquina LA-50: vendored code

## dsynkant (`dsynkant/`)

Source: https://github.com/ngeiswei/dsynkant, commit dc1bfa9 (2026-01-21), Nil Geisweiller,
GPL-2.0-or-later (`dsynkant/COPYING`). "Emulation of the legendary Roland D-50" - in fact
the D-50's patch data model only: it has no sound engine (`Tone::audio_process()` is a TODO).

Vendored: `src/dsynkant/set/` and `src/dsynkant/commondef/`, unmodified, as the reference
for the patch format (blocks, fields, ranges, reverb names). Not compiled into the plugin.
The field layout Maquina uses is `tools/la50_layout.py`, checked against the D-50 MIDI
Implementation 2.00 (pages 10-11) in dsynkant's `doc/D-50` (not vendored).

## Munt (`munt/`)

Source: https://github.com/munt/munt, commit f5b968c (2026-11-14), mt32emu, Dean Beeler,
Jerome Fisher, Sergey V. Mikayev, LGPL-2.1-or-later (`munt/COPYING.LESSER.txt`).

Vendored from `mt32emu/src`: `LA32WaveGenerator.{h,cpp}`, `LA32Ramp.{h,cpp}`,
`Tables.{h,cpp}`, `mmath.h`, `internals.h`, `globals.h`, `Types.h` - Munt's model of the
LA32 chip's wave generator (synth square / saw with pulse width, cutoff and resonance, PCM
playback, partial pairs with mixing / ring modulation). `config.h` is ours
(`engine/munt_compat/config.h`).

One change, marked `MAQUINA:` in the files: `setPulseWidth()` on `LA32WaveGenerator` and
`LA32IntPartialPair`, so the pulse width can change while a note plays (the D-50's PWM by
LFO and aftertouch; Munt sets it once per note).

`lasynth/la50/engine/La50Engine.cpp` adapts the envelope logic of Munt's `TVA.cpp`,
`TVF.cpp` and `TVP.cpp` (MT-32 code, same 0-100 scales) to the D-50's parameters; it is
used under GPL-3.0-only as LGPL section 3 allows.

Where the D-50 differs from that MT-32 logic, LA-50 follows an analysis of the D-50 v2.22
program ROM (read with MAME's uPD78K3 disassembler; no Roland code or data is included):
the release (T5) rate is 128 - 1.25 x (T5 - time key follow) with no level term, the TVA
time key follow is (key - 60) >> (4 - TKF), and the first TVA segment subtracts both the
time velocity follow and the time key follow.

The chorus modulation follows the same analysis: per type a tremolo (types 6 and 7) and two
delay sweeps, each at a centre value that Rate and Depth move linearly ((value - 50),
clamped). The ROM gives these in the chorus chip's units; LA-50 assumes 0.05 Hz and 0.06 ms
per unit and keeps its own delay lines and stereo.

The plugin is unofficial and not affiliated with Roland or with these projects' authors.
