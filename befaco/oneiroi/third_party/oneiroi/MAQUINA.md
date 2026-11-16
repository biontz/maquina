# Befaco Oneiroi firmware (vendored)

The patch that runs on the Befaco Oneiroi Eurorack module (Rebel Technology
OWL mk3, 48 kHz, 32-frame blocks), by Roberto Noris. Source: see
`UPSTREAM_COMMIT` (patch 1.2.2; `README.md`, `CHANGELOG` and `LICENSE` are
upstream's).

What Maquina uses:

- **The engine**, unmodified: `Oneiroi.h` and its DSP headers (looper,
  oscillators, filter, resonator, echo, ambience, modulation, `Clock.h`).
  `befaco/oneiroi/engine/OneiroiCore.h` is a copy of `Oneiroi.h` with two marked
  additions (an envelope on the sources, oscillators optional);
  `tools/oneiroi_ref` checks that it renders bit-identically to `Oneiroi.h`.
- **Not compiled:** the module's UI layer (`Ui.h`, `ParamController.h`,
  `Midi.h`, `Led.h`, `Oneiroi_1_2_2Patch.hpp`): knobs, SHIFT / MOD / CV pages,
  catch-up, saving, LEDs. `befaco/oneiroi/engine/OneiroiHost.cpp` replaces it with
  the plugin parameters, doing what `Ui::Poll()` does with them.

One change, marked `MAQUINA:`:

- `DelayLine.h`, `readAt()`: wraps the read index as often as needed. Upstream
  wraps once; Ambience's last diffuser asks for up to ~49800 samples of its
  48000-sample line at the ends of SPACETIME, and the module then reads
  outside the buffer (other heap memory). Here it reads inside the line.

Off the module there is no ARM CMSIS DSP library, so `FloatArray` maths and
`sin`/`cos` use the C library instead of CMSIS's approximations: the sound is
the module's, but not bit-identical to the hardware's output.

## Licence

GPL-3.0-or-later (`LICENSE`), by Roberto Noris. `LICENSE_VCV.md` adds a
commercial licence for Befaco's closed-source VCV Rack module; the GPL still
holds and is what Maquina uses (Maquina is GPL-3.0-only, which the
"or later" allows).
