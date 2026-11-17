# S1Vave (vendored subset)

Source: https://github.com/AndrewGG88/S1Vave, commit
e8370184f85b05f7c50f7e9f65cb3f6b69642d30 (2026-11-13), GPL-3.0-only (`LICENSE`,
`LICENSING.md`). S1Vave is Andrew GG's firmware for the M-VAVE FM-1: an 8-voice
analog-modelling synth in fixed-point integer DSP, built on zvenson's OberVave.

## What is here

The firmware's sound engine and nothing else, unmodified:

- `firmware/src/obv_dsp.h` (oscillators, filter, softclip, DRAW / CHOP / pink noise),
  `obv_engine.h` (voices, envelopes, LFO, arpeggiator), `obv_fx.h` (chorus, delay,
  reverb, lo-fi slot), `obv_params.h` (the parameter table), `obv_patches.h` (the 64
  factory sounds and their DRAW tables / CHOP masks), `obv_seq.h` (the sequencer;
  kept for reference: its pattern holds the DRAW table and the CHOP masks).
- `firmware/gen/obv_tables.h` (generated tables) and `gen_tables.py`, which makes it.
- `LICENSE`, `LICENSING.md`.

Not vendored: the hardware layer, the panel / screen code (`s1vave.c`, `obv_ui.h`,
`platform.c`, `hal/`), the loader, the web simulator, the firmware images, the
installer, and **the logo, splash and other brand assets**, which are not covered by
the GPL (`TRADEMARK.md` of the original). The plugins use no logo or brand name of
S1Vave; they credit the author in text, as the GPL requires.

## How Maquina uses it

- `s1vave/bl101/engine/Bl101Dsp.{hpp,cpp}` is a C++ port of `obv_engine.h` and
  `obv_fx.h`, line by line, with the firmware's globals as members (one engine per
  plugin instance). `obv_dsp.h`, `obv_params.h`, `obv_patches.h` and `obv_tables.h`
  are included as they are.
- Left out: the sequencer and its second sound (track B), the arpeggiator, the
  metronome, the scope and the program-change fade. The effects are their own plugin
  (Maquina BL-101 FX); the synth's output is the firmware's with its effects off.
- Parameters that the engine never reads are not plugin parameters: Riser (4), Phaser
  Mix, Filter Colour, Tone, Warmth, Porta Switch, Mod Depth.
- `tools/bl101_ref` compiles these files, unmodified, for the computer (with stubs for
  the sequencer's side) and runs scenario scripts on them; `tools/bl101_test` runs the
  same scripts through the port and requires the output to match bit for bit
  (`make bl101-check`, part of `make test`).
- One firmware quirk the port keeps: `pt[2] = {{&live, 0, 0, 0, 0, 60 << 16}, ...}`
  sets the LFO's noise smoother (the sixth field), not `last_pitch16`.

The plugins are unofficial and not affiliated with Andrew GG, M-VAVE or Roland.
