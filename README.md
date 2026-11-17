# Maquina

Fourteen instrument and effect plugins, each built from the open-source code of a
hardware instrument (or, for Acida, of a classic software one):

* **Kick, Tom, Snare, Cymbal, HiHat**: the drum voices of the Erica
  Synths LXR-02 drum machine
  ([erica-synths/lxr-desktop-app](https://github.com/erica-synths/lxr-desktop-app)).
* **Dredrum**: the Dredrum Eurorack module, an 8-bit 3-oscillator
  percussion/drone generator by Jan Cumpelik
  ([buranelectrix/dredrum](https://github.com/buranelectrix/dredrum)).
* **Plethora**: the Befaco Noise Plethora Eurorack module, as ported to VCV
  Rack ([VCVRack/Befaco](https://github.com/VCVRack/Befaco)): 60 noise
  algorithms on two channels plus a filtered noise channel, with an envelope
  per channel added.
* **Oneiroi** and **Oneiroi FX**: the Befaco Oneiroi Eurorack module, an
  ambient/drone synth by Roberto Noris
  ([Befaco/Oneiroi](https://github.com/Befaco/Oneiroi)). Oneiroi is the
  whole voice (oscillators, looper, filter, resonator, echo, reverb) played
  by MIDI notes; Oneiroi FX is its looper and effects on an audio track.
* **Klang**: based on the Mutable Instruments Plaits macro oscillator by
  Émilie Gillet ([pichenettes/eurorack](https://github.com/pichenettes/eurorack)):
  24 synthesis models (firmware 1.2) and its low-pass gate.
* **Klong** and **Klong FX**: based on the Mutable Instruments Rings
  resonator: modal, string and FM resonators (and the string-synth easter
  egg). Klong is played by MIDI notes with its internal exciter; Klong FX
  puts the resonator on an audio track, excited by the input.
* **Acida** and **Acida FX**: the 303 bass line and the effects of
  [OpenRB-338](https://github.com/pingw33n/openrb-338) by pingw33n, a
  bit-exact reimplementation of the ReBirth RB-338 software synth's engine.
  Acida is the 303 played by MIDI notes (accent by velocity, slide by
  overlapping notes); Acida FX is the distortion, PCF filter, compressor and
  tempo delay on an audio track.

Formats: **LV2, CLAP, VST3** and a JACK standalone (via [DPF](https://github.com/DISTRHO/DPF)).
Platforms: Linux x86_64 and Raspberry Pi OS 64-bit (aarch64); macOS/Windows later.

> Unofficial. Not affiliated with, endorsed by or supported by Erica Synths,
> Befaco, VCV, Rebel Technology, Mutable Instruments or the Dredrum and
> Oneiroi authors. "Erica Synths", "LXR" and "LXR-02" are trademarks of Erica
> Synths, "Befaco", "Noise Plethora" and "Oneiroi" belong to Befaco,
> "Mutable Instruments" is a registered trademark of Mutable Instruments;
> these names are used here only to say what each sound engine is based on.
> As Mutable Instruments asks, the plugins built from its code have their own
> names (Klang, Klong) rather than the modules'.
> Acida is not affiliated with OpenRB-338's author, Reason Studios
> (ReBirth) or Roland; "ReBirth" and "TB-303" are trademarks of their owners
> and are not used in the plugins' names.

## How it stays close to the hardware

### LXR voices (Kick, Tom, Snare, Cymbal, HiHat)

The firmware's DSP code is compiled **unmodified** from `drum02/third_party/lxr-firmware`
(pinned commit in `UPSTREAM_COMMIT`), with a small hardware shim
(`drum02/engine/fw_shim`) in place of the STM32 HAL:

* **Same maths, same rate.** The engine runs at the hardware's real sample rate
  (44140 Hz) in its 64-sample blocks. A 64-tap windowed-sinc resampler converts
  to the host rate (flat to 19 kHz, ~90 dB SNR).
* **Same knobs.** Parameters are the hardware's 0..127 values and go through
  the firmware's own MIDI/CC handler (`midiParser_ccHandler`), so every curve,
  range and option list is the firmware's. Parameter tables, option names and
  page layout are generated from the firmware's menu tables
  (`tools/gen_params.py`). Defaults are the firmware's built-in Initkit.
* **Same integer behaviour.** The ARM DSP instructions (`__SSAT`, `__QADD16`,
  `__PKHBT`...) are re-implemented bit-exactly. On x86, float->int conversions
  are rewritten to Arm saturation semantics (`tools/arm_semantics.py`) - the
  FM oscillators depend on it; Arm builds use the sources as they are.
* **One voice per plugin.** Each plugin renders one engine voice exactly the
  way the firmware's mixer does (LFO, velocity modulation, decimator, pan law).

The firmware keeps voice state in globals. Every plugin instance owns a copy of
that state (~8 KB) and swaps it in under a lock when it renders, so any number
of instances can run in one host. `tools/check_state.sh` fails the build if a
firmware update adds a global that is not captured.

What the tests prove (`make test`, and `make test-aarch64` under qemu):

| test | what |
|---|---|
| mixer-match | single-voice render == the firmware's full mixer output, all 6 voices, 4 patches (incl. LFO + velocity modulation targets) |
| bit-exact | each LV2 plugin at 44140 Hz == the firmware engine, sample for sample, through a real LV2 host |
| isolation | two instances interleaved in one process == each alone |
| resampler | SNR / flatness at 44.1, 48, 88.2, 96 kHz |
| lv2_validate | bundles are valid LV2 |
| clap_latency_check / clap_remote_check | CLAP: no restart / latency-change reports when activated; remote-controls pages point at the right parameters |
| dredrum-check | Dredrum port == the original AVR firmware in simavr, byte for byte (needs avr-gcc + simavr) |
| dredrum / plethora | isolation, all host rates, Trig vs Gate, mutes, Dredrum tuning (octave, semitone, key track), every Plethora algorithm on A and B, Env Sync |
| check_presets / check_random | factory presets load; randomizer respects locks and ranges, incl. per-oscillator / per-channel ROLL |

Not reproducible: the hardware's true random noise source (a seeded xorshift is
used), its analog output stage, and floating-point rounding of the Cortex-M7
build (fused multiply-adds, libm) - x86 and Arm builds agree to ~90 dB.

### Dredrum

The module runs on an ATmega48P at 20 MHz. Its sound engine
(`euro-modules/dredrum/third_party/dredrum/trig` and `gate`, vendored unmodified) is ported to
portable C in `euro-modules/dredrum/engine/dredrum_core.c`:

* **Bit-exact with the firmware.** `make dredrum-check` builds the original
  firmware with avr-gcc, runs it in the simavr AVR simulator and compares
  every output byte with the port (7 scenarios). It runs as part of
  `make test` when avr-gcc and simavr are installed, and is skipped otherwise.
* **Real sample rate.** The engine runs at the module's real rate,
  20 MHz / 8 / 131 = 19084 Hz (the firmware's comment says 15625 Hz, which the
  timer setup doesn't produce).
* **Real noise.** The Noise wave reads bytes from the firmware's own program
  memory. `make dredrum-flash` compiles the firmware with avr-gcc and stores
  those bytes (they differ between the trig and gate builds).
* **Output stage modelled.** The module's 8-bit PWM output goes through an
  RC low-pass and a Sallen-Key low-pass (~10.6 kHz) into an op-amp follower.
  That filter is modelled at 8x the engine rate, then resampled to the host
  rate. The held 8-bit samples keep their aliasing images, as on the module.
* **Trig and Gate firmware.** Both versions are in one plugin (**Mode**):
  Trig restarts the decay on each note, Gate holds it while the note is down.

### Plethora

The algorithms and DSP come from the VCV Rack port of the Noise Plethora
(`befaco/plethora/third_party/noise-plethora`, vendored unmodified at a pinned commit), with a
small compatibility header (`befaco/plethora/engine/compat`) in place of VCV Rack:

* **Same chain as the VCV module.**
  * *A and B:* algorithm with X/Y, per-algorithm gain, multimode filter
    (state-variable, LP/BP/HP), saturator, DC blocker.
  * *C:* gritty or white noise through a 4th-order filter and an asymmetric
    saturator.
* **Same rate.** Everything runs at 44100 Hz like the module's Teensy 4.0,
  with algorithm parameters updated every 128 samples, as on the module. The
  shared resampler converts to the host rate, so the sound is the same at
  any host rate.
* **60 algorithms:** the module's 3 hardware banks (30) plus the VCV port's 3
  extra banks (30).
* **Maquina additions:** the module has no envelope, so each channel gets
  Attack / Decay, Level and Mute. Channels that are silent (Level 0, muted,
  or envelope finished) are not computed.

Not reproducible: the module's analog filter stage (the VCV port's digital
filter model is used).

### Oneiroi

The engine is the module's own patch (`befaco/oneiroi/third_party/oneiroi`, patch 1.2.2,
vendored at a pinned commit) with the parts of the OWL library it uses
(`befaco/oneiroi/third_party/owlprogram`) and a small runtime in place of the OWL firmware
(`befaco/oneiroi/engine/compat`):

* **Same engine.** `befaco/oneiroi/engine/OneiroiCore.h` is the module's
  `Oneiroi.h` plus two marked hooks (the note envelope, oscillators
  optional). `make oneiroi-check` renders 40 scenarios (every source, filter
  mode and position, resonator / echo / ambience extremes, all modulation
  types, recording, clearing, sync) through both and requires bit-identical
  output.
* **Same rate and blocks.** 48 kHz in 32-frame blocks, as the module; a float
  resampler converts from and to the host rate (the FX's input too).
* **Same controls.** The module's UI layer is replaced by the plugin
  parameters, which get what its `Ui::Poll()` does: the same smoothing per
  control, fader curves, pitch / octave / unison maths and V/Oct glide. The
  modulation goes to the filter cutoff, as the module's default mapping.
* **Same memory.** The OWL firmware's allocator clears every block and the
  engine relies on it; Maquina's allocations are zero-filled too.
* **One fix:** at the ends of SPACETIME the module's ambience reads past the
  end of a delay line (other memory, on the module); Maquina's reads inside
  it (`befaco/oneiroi/third_party/oneiroi/MAQUINA.md`).

Not reproducible: the ARM CMSIS maths of the module's processor (the C
library is used), so the output matches the module's engine, not its DAC
bit for bit.

### Klang and Klong

The engines are Mutable Instruments' own DSP code, vendored unmodified
(`mutable-instruments/third_party/eurorack`: Plaits firmware 1.2 and Rings,
with stmlib; see its `MAQUINA.md`) and compiled with `TEST` defined, the
portable code path of Mutable's own desktop tests and of VCV Rack:

* **Same engine, same main loop.** `KlangHost` / `KlongHost` call the code as
  the modules' `FillBuffer()` does: Plaits' `Voice::Render` in 12-frame
  blocks, Rings' strummer then `Part` (or the string synth) in 24-frame
  blocks, with the input noise gate and the 16-bit output clipping.
  `tools/mutable_test` renders every Plaits model, every Rings model at
  several polyphonies and the FX's audio path, both through the hosts and
  through the module code directly, and requires bit-identical output.
* **Same rate.** Rings at 48 kHz; Plaits at 47872.34 Hz, the rate its DAC
  really runs at and the one its code is tuned for (VCV Rack runs it at
  48 kHz, 4.6 cents sharp). Float resamplers convert from and to the host
  rate.
* **Same controls.** Knobs are smoothed with the modules' pot coefficients,
  Rings' chord follows STRUCTURE with its hysteresis, Plaits' V/OCT gets its
  pitch low-pass. The modules' objects live in zeroed memory, as in their
  `.bss`.
* **Not reproduced:** the Arm-specific instructions stmlib uses on the
  modules (saturation, square root), replaced by its portable C++.

### Acida

The engine is a C++ port of OpenRB-338's Rust engine
(`openrb/acida/third_party/openrb-338`, the `orb-engine` crate; see its
`MAQUINA.md`), which itself reproduces ReBirth 2.01's output byte for byte:

* **Bit-exact port.** `openrb/acida/engine/AcidaDsp.cpp` follows `tb303.rs`,
  `dist.rs`, `pcf.rs`, `comp.rs`, `delay.rs` and the table builders operation
  for operation (double arithmetic in the same order, a float rounding where
  the Rust code stores one, no fused multiply-add: it is built as
  `libacida.a` without `-ffast-math`). `tools/acida_ref` runs scenario scripts
  (notes, slides, accents, both 303 engines, every PCF pattern, every
  distortion shape, the compressor, the delay) on the Rust engine itself;
  `tools/acida_test` runs them on the port and requires the same output bit
  for bit, and the computed tables too (`make acida-check`, needs cargo).
* **Same rate and blocks:** 44100 Hz in 64-frame blocks, resampled from and
  to the host rate.
* **Played like OpenRB's note mode.** ReBirth has no MIDI note input; OpenRB-338
  adds one, and Acida uses it: notes, not the 16-step pattern sequencer
  (which Acida does not have).
* **Same level.** Acida's output is ReBirth's mixer at its defaults (channel
  level 100, master 100) and is clamped to ReBirth's 16-bit range; that leaves
  about 10 dB of headroom (Gain adds up to +12 dB).

## Using the plugins

The drum and percussion voices (LXR, Dredrum, Plethora) first; Oneiroi has
its own section below.

* Every MIDI note triggers the voice; velocity works as on the hardware.
* **Key Track** (editor: KEY › Track):
  * Off (default): every note plays the sound at its set pitch.
  * On: the played note transposes it around C4 (60), so you can play a kick
    or tom chromatically.
  * The hardware's per-voice trigger-note setting is not a plugin parameter:
    in a DAW, put one instance on each track.
* **HiHat**: one **Decay**. For a closed and an open hat, use two HiHat
  instances with short and long decays, on their own tracks; choke them with
  the DAW if wanted.
  * The hardware's separate closed and open hats differ only in their decay
    times, so the plugin plays the closed-hat path.
  * Older presets with `amp_decay_closed` / `amp_decay_open` still load (the
    closed value is used).
  * Projects saved with the removed Trig Note or Hat settings still load;
    those values are ignored.
* **Remote controls (CLAP)**: the CLAP plugins expose 5 or 6 pages of 8 controls
  (the Machinista layout, `tools/pages.py`), which Bitwig shows as the device's
  remote-control pages and maps onto controllers.
* Parameter names are short so host knobs show them whole - see
  [Parameters](#parameters).
* Tempo-synced LFOs follow the host tempo.
* Latency (about 2 ms) is reported to the host.

**Dredrum**

* **Mode**: `Trig` (each note restarts the decays) or `Gate` (decays hold
  while the note is down, as the module's GATE firmware).
* Each oscillator has Wave, Pitch, Drop (pitch drop over the decay), Decay and
  Level, on the module's own 0..255 scales. **Decay** near maximum (≥248)
  never ends: a drone, as on the module.
* **Click** makes oscillator 3 play one cycle per trigger.
* Maquina additions, per oscillator:
  * **Track** (key tracking: note 60 plays the set pitch)
  * **Octave** (-4..+4) and **Semi** (-12..+12)
  * **Mute**, a performance switch: not stored in presets, never randomized
  * With Track off and Octave / Semi at 0 the firmware's own pitch maths runs
    unchanged.

**Plethora**

* **A** and **B**: pick one of the 60 algorithms, shape it with **X** and
  **Y**, then filter it (Lowpass / Bandpass / Highpass, Cutoff, Reso).
* **C**: gritty or white noise (**Grit** sets the density) through its own
  filter.
* Each channel has an envelope (**Attack**, **Decay**; Decay at maximum
  holds), **Level** and **Mute**.
  * By default only A sounds: B and C start at Level 0.
  * **Env Sync** makes B and C use A's envelope.
* **Mode**: `Trig` or `Gate` (hold while the note is down).
* Some algorithms are quiet at X/Y = 64 (BowedMetal, NoiseBells, PluckCloud,
  CellularSynth); turn X/Y or Level up.

**Oneiroi**

* **Notes:** a note opens the envelope (Attack, Decay, Sustain, Release) on
  the sources (looper and oscillators) and sets the pitch: the note relative
  to C4 (60) is added to Pitch and Octave, like the module's V/Oct input,
  with its glide. Mono, last note wins; going back to a held note is legato.
  Velocity does nothing (the module has none).
* **Drone** holds the envelope open: the module's behaviour, sounding without
  notes; notes still set the pitch.
* The effects come after the envelope, so echo and reverb tails ring on after
  the note ends. The filter's chaos noise (Reso from ~90 %) and a
  self-oscillating resonator also sound without notes, as on the module.
* The module's SHIFT functions are their own controls, next to their knob:
  SOS and Filter (looper), Octave and Unison (oscillators), Mode and Position
  (filter), Dissonance (resonator), Filter (echo), Auto Pan (ambience), Type
  (modulation).
* **Looper:** Record (on = recording, as the RECORD button or gate) and Clear
  (SHIFT + RECORD on the module; a button in the editor, any 0 → 1 change from
  the host). Oneiroi records its own output (the module's POST); Oneiroi FX
  has **Source** Input / Output (PRE / POST). The buffer starts with noise, as
  on the module, and is not saved with the project.
* **Sync:** Off (the module's internal clock, one looper cycle ≈ 0.18 Hz),
  or a sync pulse every Beat / Bar / 4 Bars of the host transport while it
  plays (the module's SYNC IN). It clocks the modulation, the echo time and
  auto-pan and restarts the looper. As on the module, the clock falls back
  to internal when no pulse comes for 2 s, so at slow tempos use Beat.
* **Randomizer:** the module's own (editor: RANDOM panel). TARGET All / Osc
  / Looper / Effects, AMOUNT Full / Mid (up to 10 %) / Low (up to 2 %);
  pitch moves in semitones. UNDO returns to the sound before the last roll,
  then REDO. It moves only the target's knobs, never faders, switches or
  SHIFT functions.
* **Per-parameter modulation and CV amounts** (the module's MOD / CV pages)
  are not parameters: use the host's modulators (Bitwig) on any parameter.
* **Oneiroi FX** has no oscillators and no envelope; Input sets the dry
  input level, the effects' dry/wet faders are in MIX.
* Latency (0.7 ms at 48 kHz, ~2 ms at 44.1 kHz) is reported to the host.

**Klang**

* **Notes** set the pitch (V/OCT around C4: with Coarse and Fine at 0, note
  60 plays C4) and trigger: the module's TRIGGER input is patched, so a note
  pings the low-pass gate and starts the internal decay envelope. Mono, last
  note wins; a new note while one is held retriggers, going back to a held
  note is legato.
* **Key Track** Off: every note plays the set pitch (drum models).
* **LPG Mode** Ping (default): the module with only TRIGGER patched; notes
  ping the LPG, Decay sets its length. Velocity: the velocity goes to the
  LEVEL input while the note is held (the LPG opens with the velocity and
  closes on note-off), and drives the accent of the drum models.
* **Env > FM / Timbre / Morph:** the attenuverters. With nothing patched into
  those CV inputs they set how much the internal decay envelope moves
  frequency, Timbre and Morph, as on the module.
* The models that make their own envelope (strings, modal, drums, 6-op FM
  envelopes) bypass the LPG, as on the module.
* **Output:** Out (both channels), Aux, Out + Aux (left / right) or their mix.
* **Model** lists the original 16 models first, then the 8 of firmware 1.2.
  The 6-op FM models play the module's built-in DX7 banks (Harmonics picks
  the patch); loading your own SysEx banks is not supported.

**Klong / Klong FX**

* **Klong:** the internal exciter. Every note strums a new voice at its pitch
  (V/OCT around C4); note-offs do nothing, the strings ring out and Damping
  sets how long. **Poly** 1-4 voices (3 is the module's hidden setting).
* The module's strummer ignores strums closer than 10 ms; Klong keeps such
  notes and strums them 10 ms apart, so a chord fills the polyphony.
* **Klong FX:** the input (left + right) excites the resonator, through the
  module's noise gate. **Notes** Internal: as the module with nothing in
  V/OCT and STRUM: the input's onsets strum, Coarse / Fine set the pitch.
  MIDI: notes on the FX's MIDI input set the pitch and strum.
* **Model:** the 6 resonator models, then the string synth easter egg
  ("Disastrous Peace") with its 6 effects (Peace Formant … Peace Rev 2).
  The chord of the sympathetic-quantized model and of the string synth
  follows Structure, as on the module.
* **Output:** Odd / Even (left / right, the module's two outputs) or Mono
  (both mixed, as with one jack on the module).
* No FM / CV amounts: modulate any parameter with the host instead.
* Latency (Klong FX, 48 kHz: 26 frames) is reported to the host.

**Acida / Acida FX**

* **Acida:** MIDI note 69 is A 440 at Tune 0 (Tune moves it -12..+12
  semitones in ReBirth's steps). Velocity at or above **Acc Vel** (default
  100, as in OpenRB) accents the note. A note played while another is held
  **slides** to it without a new attack; the gate stays open while notes
  are held, and releasing the newest note slides back to the one still held.
  Notes take effect at the engine's 64-frame blocks (1.45 ms).
* **Engine:** 2.0 (ReBirth 2's 303, the default) or Vintage (ReBirth 1's).
* **Acida FX:** Distortion and PCF on each channel, then the Compressor
  linked on both (as on ReBirth's master), then the Delay as a send: **Send**
  feeds it the mono sum and its output is added to the dry signal, as in
  ReBirth. Each has an on/off switch (Distortion is on by default).
* **PCF Wave** 1..55 picks one of the PCF's 55 trigger patterns, which run in
  time with the host transport (stopped: at the host tempo); **Delay Steps**
  1..32 in sixteenths or triplets at the host tempo.
* The output is clamped to +-1, as ReBirth's 16-bit output.
* Latency (48 kHz: Acida 107, Acida FX 139 frames) is reported to the host.

### Parameters

Three names per parameter:

* **Host name** - what the DAW shows (Bitwig remote knobs, parameter list,
  automation lanes) and, uppercased, Machinista. At most 9 characters
  (`SHORT_NAMES` in `tools/gen_params.py`; the build fails on longer ones).
  Oscillator and amp-envelope parameters have no prefix; the others are
  prefixed by their section: `Pitch` (pitch envelope), `Velo`, `FM`,
  `1`/`2` (mod oscillators), `T` (transient), `Flt`, `LFO`, `Noise`.
* **Symbol** - the stable id (LV2 symbol, preset files, Machinista profiles).
  Never renamed, so projects and presets survive name changes.
* **Long name** - the firmware's name. Used as the entries of the **Velo Dest**
  / **LFO Dest** lists, which preset files store by name
  (`lfo_target = Filter Freq`).

The editor uses its own labels under section titles (column *Editor*).

**LXR voices.** Ranges are the hardware's 0..127 scales; tune and pan are shown centred.

| Host name | Symbol | Editor | Long name (hardware, mod targets) | Range | Voices |
|---|---|---|---|---|---|
| Coarse | `osc_coarse` | Oscillator › Coarse | Osc Coarse | -60..67 | all |
| Fine | `osc_fine` | Oscillator › Fine | Osc Fine | -63..64 | all |
| Wave | `osc_wave` | Oscillator › Wave | Osc Wave | Sine, Triangle, Saw, Square, Noise, PWM | all |
| PWM | `osc_pwm` | Oscillator › PWM | Osc PWM | 0..127 | all |
| Noise Frq | `noise_freq` | Noise › Freq | Noise Freq | 0..127 | Snare |
| Noise Mix | `noise_mix` | Noise › Mix | Noise Mix | 0..127 | Snare |
| Attack | `amp_attack` | Amp EG › Attack | Amp Attack | 0..127 | all |
| Decay | `amp_decay` | Amp EG › Decay | Amp Decay | 0..127 | all |
| Slope | `amp_slope` | Amp EG › Slope | Amp Slope | 0..127 | all |
| Repeat | `amp_repeat` | Amp EG › Repeat | Amp Repeat | 0..127 | Snare, Cymbal |
| Pitch Dec | `pitch_decay` | Pitch EG › Decay | Pitch Decay | 0..127 | Kick, Tom, Snare |
| Pitch Slp | `pitch_slope` | Pitch EG › Slope | Pitch Slope | 0..127 | Kick, Tom, Snare |
| Pitch Amt | `pitch_amount` | Pitch EG › Amount | Pitch Amount | 0..127 | Kick, Tom, Snare |
| Velo Dest | `velo_dest` | Velocity › Dest | Velocity Dest | Off + the voice's parameters (long names) | all |
| Velo Amt | `velo_amount` | Velocity › Amount | Velocity Amount | 0..127 | all |
| Velo>Vol | `velo_volume` | Velocity › > Vol | Velocity > Vol | Off / On | all |
| FM Amt | `fm_amount` | FM › Amount | FM Amount | 0..127 | Kick, Tom |
| FM Freq | `fm_freq` | FM › Freq | FM Freq | -60..67 | Kick, Tom |
| FM Wave | `fm_wave` | FM › Wave | FM Wave | Sine, Triangle, Saw, Square, Noise, PWM | Kick, Tom |
| FM Mode | `fm_mode` | FM › Mode | FM Mode | FM, Mix | Kick, Tom |
| 1 Freq | `mod1_freq` | Mod Oscillators › Freq 1 | Mod1 Freq | -60..67 | Cymbal, HiHat |
| 2 Freq | `mod2_freq` | Mod Oscillators › Freq 2 | Mod2 Freq | -60..67 | Cymbal, HiHat |
| 1 Gain | `mod1_gain` | Mod Oscillators › Gain 1 | Mod1 Gain | 0..127 | Cymbal, HiHat |
| 2 Gain | `mod2_gain` | Mod Oscillators › Gain 2 | Mod2 Gain | 0..127 | Cymbal, HiHat |
| 1 Wave | `mod1_wave` | Mod Oscillators › Wave 1 | Mod1 Wave | Sine, Triangle, Saw, Square, Noise, PWM | Cymbal, HiHat |
| 2 Wave | `mod2_wave` | Mod Oscillators › Wave 2 | Mod2 Wave | Sine, Triangle, Saw, Square, Noise, PWM | Cymbal, HiHat |
| T Wave | `trans_wave` | Transient › Wave | Transient Wave | Snap, Offset, Click, Click 2… (14) | all |
| T Vol | `trans_volume` | Transient › Volume | Transient Vol | 0..127 | all |
| T Freq | `trans_freq` | Transient › Freq | Transient Freq | 0..127 | all |
| Cutoff | `filter_freq` | Filter › Cutoff | Filter Freq | 0..127 | all |
| Reso | `filter_reso` | Filter › Reso | Filter Reso | 0..127 | all |
| Flt Type | `filter_type` | Filter › Type | Filter Type | Lowpass, Highpass, Bandpass, Unity BP… (8) | all |
| Flt Drive | `filter_drive` | Filter › Drive | Filter Drive | 0..127 | all |
| LFO Rate | `lfo_rate` | LFO › Rate | LFO Rate | 0..127 | all |
| LFO Sync | `lfo_sync` | LFO › Sync | LFO Sync | Off, 4/1, 2/1, 1/1… (12) | all |
| LFO Amt | `lfo_amount` | LFO › Amount | LFO Amount | 0..127 | all |
| LFO Wave | `lfo_wave` | LFO › Wave | LFO Wave | Sine, Triangle, Saw Up, Saw Down… (8) | all |
| LFO Retrg | `lfo_retrigger` | LFO › Retrig | LFO Retrigger | Off / On | all |
| LFO Phase | `lfo_offset` | LFO › Phase | LFO Phase | 0..127 | all |
| LFO Dest | `lfo_target` | LFO › Target | LFO Target | Off + the voice's parameters (long names) | all |
| Volume | `volume` | Mix › Volume | Volume | 0..127 | all |
| Pan | `pan` | Mix › Pan | Pan | -63..64 | all |
| Decimate | `decimation` | Mix › Rate | Sample Rate | 0..127 | all |
| Drive | `drive` | Mix › Drive | Drive | 0..127 | all |
| Key Track | `note_pitch` | Key › Track | — (plugin only) | Off / On | all |
| Gain | `gain` | Mix › Gain | — (plugin only; output gain in dB, not in presets, not randomized) | -24..12 | all |

**Dredrum** (30 parameters). Values are the module's 0..255 pot scales; *n* = 1, 2, 3.
Host names start with the oscillator number.

| Host name | Symbol | Long name | Range |
|---|---|---|---|
| 1 / 2 / 3 Wave | `osc{1,2,3}_wave` | Osc *n* Wave | Sine, Square, Saw, Noise |
| 1 / 2 / 3 Pitch | `osc{1,2,3}_pitch` | Osc *n* Pitch | 0..255 |
| 1 / 2 / 3 Drop | `osc{1,2,3}_drop` | Osc *n* Pitch Drop | 0..255 |
| 1 / 2 / 3 Decay | `osc{1,2,3}_decay` | Osc *n* Decay | 0..255 |
| 1 / 2 / 3 Level | `osc{1,2,3}_level` | Osc *n* Level | 0..255 |
| 1 / 2 / 3 Track | `osc{1,2,3}_track` | Osc *n* Key Track (Maquina) | Off / On |
| 1 / 2 / 3 Mute | `osc{1,2,3}_mute` | Osc *n* Mute (Maquina, not in presets) | Off / On |
| 1 / 2 / 3 Octave | `osc{1,2,3}_octave` | Osc *n* Octave (Maquina) | -4..4 |
| 1 / 2 / 3 Semi | `osc{1,2,3}_semi` | Osc *n* Semitone (Maquina) | -12..12 |
| Click | `click` | Osc 3 Click | Off / On |
| Mode | `trig_mode` | Trigger Mode (not in presets) | Trig, Gate |
| Gain | `gain` | Output gain in dB (Maquina, not in presets) | -24..12 |

**Plethora** (32 parameters). Knobs are 0..127 over the module's full control
range. Host names start with the channel letter.

| Host name | Symbol | Long name | Range |
|---|---|---|---|
| A / B Algo | `{a,b}_algo` | Algorithm | 1-01 radioOhNo … 6-10 DualPulse (60; banks 1-3 = the module, 4-6 = the VCV port) |
| A / B X, A / B Y | `{a,b}_x`, `{a,b}_y` | X, Y | 0..127 |
| C Source | `c_source` | C Source | Gritty, White |
| C Grit | `c_grit` | C Grit | 0..127 |
| A / B / C Filter | `{a,b,c}_ftype` | Filter Type | Lowpass, Bandpass, Highpass |
| A / B / C Cutoff | `{a,b,c}_cutoff` | Cutoff | 0..127 |
| A / B / C Reso | `{a,b,c}_reso` | Resonance | 0..127 |
| A / B / C Attack | `{a,b,c}_attack` | Attack (Maquina) | 0..127 |
| A / B / C Decay | `{a,b,c}_decay` | Decay (Maquina) | 0..127 (max = hold) |
| A / B / C Level | `{a,b,c}_level` | Level (Maquina) | 0..127 (A 127, B and C 0) |
| A / B / C Mute | `{a,b,c}_mute` | Mute (Maquina, not in presets) | Off / On |
| Env Sync | `env_sync` | Envelope Sync (Maquina) | Off / On |
| Mode | `trig_mode` | Trigger Mode (not in presets) | Trig, Gate |
| Gain | `gain` | Output gain in dB (Maquina, not in presets) | -24..12 |

**Oneiroi / Oneiroi FX** (42 / 32 parameters). Knobs are 0..127 over the
module's control range. *Centred* knobs are -64..+63 with 0 at the module's
centre (0x speed, unison, no filter, smallest room). FX: no OSC and ENVELOPE
rows, plus Input and Source.

| Host name | Symbol | Editor | Range |
|---|---|---|---|
| Speed | `loop_speed` | Looper › Speed | centred: -64 = 2x reverse, -32 = 1x reverse, +32 = 1x, +63 = 2x |
| Start, Length | `loop_start`, `loop_length` | Looper › Start, Length | 0..127 |
| SOS | `loop_sos` | Looper › SOS | 0..127 (sound on sound: how much old material stays) |
| Loop Filt | `loop_filter` | Looper › Filter | centred: below 0 lowpass, above highpass |
| Record | `loop_record` | Looper › Record | Off / On (not in presets) |
| Rec Src | `loop_source` | Looper › Source | Input, Output (FX only; not in presets) |
| Clear | `loop_clear` | Looper › Clear | Off / On: clears on Off → On (not in presets) |
| Pitch | `osc_pitch` | Oscillators › Pitch | -60..60 tenths of a semitone (±6 semitones) |
| Octave | `osc_octave` | Oscillators › Octave | -4..4 (0: note 60 plays C4) |
| Detune | `osc_detune` | Oscillators › Detune | 0..127 (supersaw detune / wavetable offset) |
| Unison | `osc_unison` | Oscillators › Unison | centred: -64 = one octave down … +63 = one octave up |
| Osc 2 | `osc2_mode` | Oscillators › Osc 2 | Supersaw, Wavetable |
| Cutoff, Reso | `flt_cutoff`, `flt_reso` | Filter › Cutoff, Reso | 0..127 (Reso from 115: chaos noise, rising to 127) |
| Flt Mode | `flt_mode` | Filter › Mode | Lowpass, Bandpass, Highpass, Comb |
| Flt Pos | `flt_pos` | Filter › Position | I Start, II Pre-Echo, III Pre-Amb, IV End |
| Res Tune, Res Fdbk, Res Diss | `res_tune`, `res_feedback`, `res_dissonance` | Resonator › Tune, Feedback, Dissonance | 0..127 |
| Density, Repeats | `echo_density`, `echo_repeats` | Echo › Density, Repeats | 0..127 |
| Echo Filt | `echo_filter` | Echo › Filter | centred: below 0 lowpass, above highpass |
| Spacetime | `amb_spacetime` | Ambience › Spacetime | centred: 0 smallest/brightest, ± bigger and darker, below 0 reversed |
| Amb Decay, Auto Pan | `amb_decay`, `amb_autopan` | Ambience › Decay, Auto Pan | 0..127 |
| Mod Level, Mod Speed, Mod Type | `mod_level`, `mod_speed`, `mod_type` | Modulation › Level, Speed, Type | 0..127 (Type morphs: 0 chaos, 21 sine, 42 ramp, 64 saw, 85 square, 106 S&H, 127 envelope follower) |
| Input | `vol_input` | Mix › Input | 0..127 (FX only) |
| Looper, Sine, Saw / WT | `vol_looper`, `vol_sine`, `vol_osc2` | Mix › Looper, Sine, Saw / WT | 0..127 (Sine, Saw / WT: Oneiroi only) |
| Filter, Resonator, Echo, Ambience | `mix_filter`, `mix_resonator`, `mix_echo`, `mix_ambience` | Mix › Filter, Reso, Echo, Ambience | 0..127 dry/wet |
| Attack | `env_attack` | Envelope › Attack | 0..127: 0, 1 ms .. 10 s (Maquina; Oneiroi only) |
| Decay, Release | `env_decay`, `env_release` | Envelope › Decay, Release | 0..127: 5 ms .. 20 s (Maquina; Oneiroi only) |
| Sustain | `env_sustain` | Envelope › Sustain | 0..127 (Maquina; Oneiroi only) |
| Drone | `env_drone` | Envelope › Drone | Off / On (Maquina; Oneiroi only) |
| Sync | `sync` | Global › Sync | Off, Beat, Bar, 4 Bars (Maquina; not in presets) |
| Gain | `gain` | Mix › Gain | Output gain in dB (Maquina, not in presets) -24..12 |

**Klang** (14 + Gain parameters). Knobs are 0..127 over the module's knob range.

| Host name | Symbol | Editor | Range |
|---|---|---|---|
| Model | `model` | Model › Model | Virtual Analog … Hi-Hat (the original 16), VA + VCF … Chiptune (firmware 1.2) |
| Coarse | `coarse` | Model › Coarse | -36..36 semitones |
| Fine | `fine` | Model › Fine | -100..100 cents |
| Key Track | `key_track` | Model › Track | Off / On (On: note 60 = C4) |
| Harmonics, Timbre, Morph | `harmonics`, `timbre`, `morph` | Sound › Harmonics, Timbre, Morph | 0..127 |
| Env > FM, Env > Tmb, Env > Mrp | `fm_env`, `timbre_env`, `morph_env` | Env Mod › > FM, > Timbre, > Morph | -63..63 (the attenuverters) |
| Decay, Colour | `lpg_decay`, `lpg_colour` | LPG › Decay, Colour | 0..127 |
| LPG Mode | `lpg_mode` | LPG › Mode | Ping, Velocity (Maquina) |
| Output | `output` | Mix › Output | Out, Aux, Out + Aux, Mix (Maquina, not in presets) |
| Gain | `gain` | Mix › Gain | Output gain in dB (Maquina, not in presets) -24..12 |

**Klong / Klong FX** (9 / 10 + Gain parameters).

| Host name | Symbol | Editor | Range |
|---|---|---|---|
| Model | `model` | Model › Model | Modal, Sympathetic, Inharm String, FM Voice, Sympath Quant, String + Verb, Peace Formant, Peace Chorus, Peace Reverb, Peace Form 2, Peace Ensemble, Peace Rev 2 |
| Poly | `polyphony` | Model › Poly | 1..4 |
| Notes | `note_source` | Model › Notes | Internal, MIDI (FX only; not in presets) |
| Coarse | `coarse` | Model › Coarse | -48..12 semitones (the FREQUENCY knob's range) |
| Fine | `fine` | Model › Fine | -100..100 cents |
| Structure, Bright, Damping, Position | `structure`, `brightness`, `damping`, `position` | Resonator › Structure, Brightness, Damping, Position | 0..127 |
| Output | `output` | Mix › Output | Odd / Even, Mono (Maquina, not in presets) |
| Gain | `gain` | Mix › Gain | Output gain in dB (Maquina, not in presets) -24..12 |

**Acida** (9 + Gain parameters).

| Host name | Symbol | Editor | Range |
|---|---|---|---|
| Wave | `waveform` | Synth › Wave | Saw, Square |
| Tune | `tune` | Synth › Tune | -64..63 (ReBirth's knob: -12..+12 semitones) |
| Cutoff, Resonance, Env Mod, Decay, Accent | `cutoff`, `resonance`, `env_mod`, `decay`, `accent` | Synth › Cutoff, Reso, Env Mod, Decay, Accent | 0..127 |
| Engine | `engine` | Play › Engine | 2.0, Vintage |
| Acc Vel | `accent_vel` | Play › Acc Vel | 1..127: velocity from which a note is accented (Maquina, not in presets) |
| Gain | `gain` | Mix › Gain | Output gain in dB (Maquina, not in presets) -24..12 |

**Acida FX** (19 + Gain parameters).

| Host name | Symbol | Editor | Range |
|---|---|---|---|
| Dist, PCF, Comp, Delay | `dist_on`, `pcf_on`, `comp_on`, `delay_on` | On switch of each section | Off, On |
| Dist Amt, Dist Shp | `dist_amount`, `dist_shape` | Distortion › Amount, Shape | 0..127 (shape 0: hard clip) |
| PCF Mode | `pcf_mode` | PCF › Mode | Low Pass, Band Pass |
| PCF Wave | `pcf_wave` | PCF › Wave | 1..55 (the trigger pattern) |
| PCF Freq, PCF Reso, PCF Amt, PCF Decay | `pcf_freq`, `pcf_reso`, `pcf_amount`, `pcf_decay` | PCF › Freq, Reso, Amount, Decay | 0..127 |
| Comp Amt, Comp Thr | `comp_amount`, `comp_threshold` | Compressor › Amount, Thresh | 0..127 |
| Dly Mode | `delay_mode` | Delay › Mode | Triplets, 16ths |
| Dly Steps | `delay_steps` | Delay › Steps | 1..32 |
| Dly Send, Dly Fdbk | `delay_send`, `delay_feedback` | Delay › Send, Feedback | 0..127 |
| Dly Pan | `delay_pan` | Delay › Pan | -64..63 |
| Gain | `gain` | Mix › Gain | Output gain in dB (Maquina, not in presets) -24..12 |

#### Remote-control pages (CLAP)

Bitwig and other CLAP hosts get these pages of 8 knobs; Machinista uses the
same layout (`tools/pages.py`), with bank/patch browsing in place of the first
two knobs on the OSCILLATOR page.

| Voice | Page | Knobs 1-8 |
|---|---|---|
| Kick, Tom | OSCILLATOR | Coarse · Fine · Wave · PWM · Volume · Pan · Drive · Decimate |
|  | ENVELOPES | Attack · Decay · Slope · Pitch Dec · Pitch Slp · Pitch Amt · Drive · Decimate |
|  | FM / TRANSIENT | FM Amt · FM Freq · FM Wave · FM Mode · T Wave · T Vol · T Freq · Volume |
|  | FILTER / VELOCITY | Cutoff · Reso · Flt Type · Flt Drive · Velo Dest · Velo Amt · Velo>Vol · Key Track |
|  | LFO | LFO Rate · LFO Sync · LFO Amt · LFO Wave · LFO Retrg · LFO Phase · LFO Dest · Gain |
| Snare | OSCILLATOR | Coarse · Fine · Wave · PWM · Noise Frq · Noise Mix · Volume · Pan |
|  | ENVELOPES | Attack · Decay · Slope · Repeat · Pitch Dec · Pitch Slp · Pitch Amt · Volume |
|  | TRANSIENT / MIX | T Wave · T Vol · T Freq · Pan · Drive · Decimate · Volume · Key Track |
|  | FILTER / VELOCITY | Cutoff · Reso · Flt Type · Flt Drive · Velo Dest · Velo Amt · Velo>Vol · Volume |
|  | LFO | LFO Rate · LFO Sync · LFO Amt · LFO Wave · LFO Retrg · LFO Phase · LFO Dest · Gain |
| Cymbal | OSCILLATOR | Coarse · Fine · Wave · PWM · Volume · Pan · Drive · Decimate |
|  | MOD OSC | 1 Freq · 2 Freq · 1 Gain · 2 Gain · 1 Wave · 2 Wave · Drive · Decimate |
|  | ENVELOPE / TRANSIENT | Attack · Decay · Repeat · Slope · T Wave · T Vol · T Freq · Volume |
|  | FILTER / VELOCITY | Cutoff · Reso · Flt Type · Flt Drive · Velo Dest · Velo Amt · Velo>Vol · Key Track |
|  | LFO | LFO Rate · LFO Sync · LFO Amt · LFO Wave · LFO Retrg · LFO Phase · LFO Dest · Gain |
| HiHat | OSCILLATOR | Coarse · Fine · Wave · PWM · Volume · Pan · Drive · Decimate |
|  | MOD OSC | 1 Freq · 2 Freq · 1 Gain · 2 Gain · 1 Wave · 2 Wave · Drive · Decimate |
|  | ENVELOPE / TRANSIENT | Attack · Decay · Slope · T Wave · T Vol · T Freq · Volume · Pan |
|  | FILTER / VELOCITY | Cutoff · Reso · Flt Type · Flt Drive · Velo Dest · Velo Amt · Velo>Vol · Key Track |
|  | LFO | LFO Rate · LFO Sync · LFO Amt · LFO Wave · LFO Retrg · LFO Phase · LFO Dest · Gain |
| Dredrum | MIX | 1 Level · 2 Level · 3 Level · 1 Decay · 2 Decay · 3 Decay · Mode · Click |
|  | OSC 1 | 1 Wave · 1 Pitch · 1 Drop · 1 Decay · 1 Level · 1 Track · 1 Octave · 1 Semi |
|  | OSC 2 | 2 Wave · 2 Pitch · 2 Drop · 2 Decay · 2 Level · 2 Track · 2 Octave · 2 Semi |
|  | OSC 3 | 3 Wave · 3 Pitch · 3 Drop · 3 Decay · 3 Level · 3 Track · 3 Octave · 3 Semi |
|  | SWITCHES | Mode · Click · 1 Track · 2 Track · 3 Track · 1 Mute · 2 Mute · 3 Mute |
|  | MIX / OUT | Gain · 1 Level · 2 Level · 3 Level · 1 Mute · 2 Mute · 3 Mute · Mode |
| Plethora | A | A Algo · A X · A Y · A Filter · A Cutoff · A Reso · A Level · A Decay |
|  | A ENV / MIX | A Attack · A Decay · A Level · A Mute · B Level · C Level · Env Sync · Mode |
|  | B | B Algo · B X · B Y · B Filter · B Cutoff · B Reso · B Attack · B Decay |
|  | C | C Source · C Grit · C Filter · C Cutoff · C Reso · C Attack · C Decay · C Level |
|  | MUTES | A Mute · B Mute · C Mute · Gain · Mode · A Level · B Level · C Level |

| Oneiroi | OSC / MIX | Pitch · Detune · Sine · Saw / WT · Looper · Drone · Echo · Ambience |
|  | LOOPER | Speed · Start · Length · SOS · Loop Filt · Record · Clear · Looper |
|  | FILTER / RESO | Cutoff · Reso · Flt Mode · Flt Pos · Res Tune · Res Fdbk · Res Diss · Resonator |
|  | ECHO / AMBIENCE | Density · Repeats · Echo Filt · Echo · Spacetime · Amb Decay · Auto Pan · Ambience |
|  | MOD / OSC | Mod Level · Mod Speed · Mod Type · Octave · Unison · Osc 2 · Filter · Sync |
|  | ENVELOPE | Attack · Decay · Sustain · Release · Drone · Gain · Pitch · Octave |
| Oneiroi FX | INPUT / LOOPER | Input · Looper · Record · Rec Src · Clear · Sync · Echo · Ambience |
|  | LOOPER | Speed · Start · Length · SOS · Loop Filt · Record · Looper · Input |
|  | FILTER / RESO | (as Oneiroi) |
|  | ECHO / AMBIENCE | (as Oneiroi) |
|  | MOD / MIX | Mod Level · Mod Speed · Mod Type · Filter · Resonator · Echo · Ambience · Gain |
| Klang | SOUND | Coarse · Fine · Model · Harmonics · Timbre · Morph · Decay · Colour |
|  | ENV / TUNE | Env > FM · Env > Tmb · Env > Mrp · LPG Mode · Coarse · Fine · Key Track · Gain |
|  | MIX | Output · Gain · Model · Harmonics · Timbre · Morph · Decay · Coarse |
| Klong | RESONATOR | Coarse · Fine · Model · Poly · Structure · Bright · Damping · Position |
|  | TUNE / MIX | Coarse · Fine · Output · Gain · Structure · Bright · Damping · Position |
| Klong FX | RESONATOR | Coarse · Notes · Model · Poly · Structure · Bright · Damping · Position |
|  | TUNE / MIX | Coarse · Fine · Notes · Output · Gain · Structure · Damping · Position |
| Acida | SYNTH | Tune · Cutoff · Resonance · Env Mod · Decay · Accent · Wave · Gain |
|  | WAVE / MIX | Wave · Engine · Acc Vel · Gain · Cutoff · Resonance · Env Mod · Decay |
| Acida FX | DIST / PCF | Dist Amt · Dist Shp · PCF Freq · PCF Reso · PCF Amt · PCF Decay · Dist · PCF |
|  | PCF / COMP | Dist · PCF · PCF Wave · PCF Mode · Comp · Comp Amt · Comp Thr · Gain |
|  | DELAY | Delay · Dly Send · Dly Steps · Dly Mode · Dly Fdbk · Dly Pan · Dist Amt · PCF Freq |

On Machinista, Plethora has channel A only (pages A and A ENV, 12 parameters):
B and C stay at Level 0, so they cost no CPU on the Pi.

### Editor

Desktop builds have a NanoVG editor (flat, knob-first, three rows per voice).
The LV2 UI is a separate binary, so headless hosts never load it; the Pi build
(`make aarch64`) has no GUI at all.

* Knobs: drag up/down (Shift = fine), mouse wheel steps, double-click resets.
* Selectors: click the arrows to step, click the value for the list, or drag.
* **Presets**: the bar in the header shows the loaded preset (`*` = edited).
  Arrows step through all presets; click the name for the browser
  (categories per voice, tag filter, search, User). **SAVE** stores the
  current sound with a name, category and tags as a small text file in
  `~/.config/maquina/presets/<voice>/` (copy these between machines). A preset
  sets the sound only: Key Track (and Dredrum / Plethora Mode and Mutes) stay as they are.
  The factory presets are also the plugin's host presets/programs.
* **Randomize**, the RANDOM panel at the bottom: **RANDOM** rolls a new
  sound, **BACK** returns to the sound before the first roll (any manual edit
  keeps the current one). Groups: OSC (incl. noise / FM / mod oscillators),
  AMP EG, PITCH EG (Kick, Tom, Snare), TRANS, FILTER, LFO, VELO, PAN/RATE,
  DRIVE, VOLUME. Click a group's
  name to lock it; drag the range bar's handles to set the window the values
  are picked from (e.g. FILTER 40-100 % of each knob's range). Selectors
  (waves, types) pick any choice; the filter is never switched off and
  modulation targets are kept. VELO, PAN/RATE, DRIVE and VOLUME start
  locked; Key Track is never randomized. The settings are saved
  with the project. Desktop editor only (not on Machinista yet).
  * *Dredrum groups:* WAVE, PITCH, DECAY (up to 90 %, so no endless drones),
    LEVEL, and TUNE (Track / Octave / Semi, starts locked).
  * *Plethora groups:* ALGO, X / Y, FILTER, ENV, and LEVEL (starts locked).
  * *Per oscillator / channel:* each Dredrum OSC and Plethora channel section
    has **ROLL** (randomize only that one) and **RND ON/OFF** (whether the
    main RANDOM includes it). Mutes and Mode are never randomized.
* Amp / Pitch EG graphs are drawn from the firmware's own time and slope
  curves (real milliseconds). Drag the handles to set attack / decay /
  amount, drag the curve up/down for the slope.

## Building

Requirements: a C/C++ toolchain, GNU make, `python3`, `clang` (x86 builds use
its AST for the Arm-semantics rewrite), and for the tests `lilv` and
`lv2_validate` (lv2 package).

Optional, for Dredrum only:

* `avr-gcc` + `avr-libc`: `make dredrum-flash` rebuilds the firmware's noise
  bytes. The result is committed, so normal builds don't need it.
* `simavr`: `make dredrum-check`, the bit-exact comparison against the
  firmware.

```sh
git submodule update --init --recursive
make            # bin/: Maquina*.lv2 / .clap / .vst3 / JACK apps
make test
make install    # copies to ~/.lv2 ~/.clap ~/.vst3
```

Factory presets live in `<module>/presets/<voice>/*.maquina`, e.g.
`drum02/presets/kick/` (format in `common/Presets.hpp`). After adding or changing one run
`make presets` (validates them, regenerates the compiled table and the
Machinista banks), then `make`. `build/tools/preset_measure <voice> -f file`
renders a preset and prints level, length, pitch and brightness
(Oneiroi: `build/tools/oneiroi_test --measure file.maquina`).

### Raspberry Pi (aarch64)

```sh
make aarch64        # bin-aarch64/: lv2, clap, vst3
make test-aarch64   # runs every test under qemu-aarch64-static
```

No root and no cross-gcc needed: `tools/aarch64/make_sysroot.py` fetches a
Debian bookworm arm64 sysroot (glibc 2.36, the Raspberry Pi OS bookworm base;
checksums verified against the Debian index) and clang + lld build against it.
The binaries need glibc >= 2.34. Copy `bin-aarch64/*.lv2` to `~/.lv2` on the Pi.

### macOS

Universal (Intel + Apple Silicon) CLAP + VST3 for macOS 10.15 or later,
signed ad hoc. Built by GitHub Actions (`.github/workflows/macos.yml`) on
pushes to `macos*` branches, `v*` tags, or `gh workflow run macos.yml`; the
artifacts are a zip (`maquina-macos`) and a disk image (`maquina-macos-dmg`:
the plugins, an `Install Maquina.command` that copies them to
`~/Library/Audio/Plug-Ins` and clears the download quarantine, and the
install notes). On a Mac:

```sh
make macos          # MACOS_PLUGINS (default: all plugins) -> bin-macos/
make macos-test     # CLAP checks for both archs, universal + signed + exports
make macos-dist     # dist/Maquina-macOS-<version>.zip
make macos-dmg      # dist/Maquina-<version>-macOS.dmg, then a test install into a temp HOME
```

Each arch is built on its own (`bin-macos-x86_64/`, `bin-macos-arm64/`) and
`tools/macos/lipo_bundles.sh` merges them. Install to
`~/Library/Audio/Plug-Ins/CLAP` and `VST3`, then clear the download
quarantine: `xattr -dr com.apple.quarantine <bundle>`. User presets live in
`~/Library/Application Support/Maquina/presets/`.

### Machinista

Controller profiles for [Machinista](https://github.com/photogeen/machinista)
(Maschine controllers on a Pi) are in `machinista/profiles/` - see
`machinista/README.md`.

Factory presets as Machinista banks (one bank per category) are in
`machinista/banks/maquina-<voice>/`; copy each folder to Machinista's
`presets/plugin-banks/`.

Maquina Plethora's Machinista profile has channel A only, so its banks hold
only the factory presets that use channel A alone.

Maquina Oneiroi has a Machinista profile (all parameters, 6 pages); Oneiroi
FX does not (Machinista hosts instruments). Klang (3 pages) and Klong
(2 pages) have profiles with all parameters; Klong FX does not. Acida has one
(2 pages, all parameters); Acida FX does not. Not yet measured on a Pi: one
Oneiroi instance needs about 3 % of one x86 laptop core.

## Layout

One folder per module family, each with the same parts: `engine/` (the port
and its host class), `third_party/` (vendored sources), `plugins/<plugin>/`
(DistrhoPluginInfo.h + Makefile) and `presets/<voice>/` (factory presets,
.maquina text files).

```
drum02/                     Erica Synths LXR-02 firmware: Kick, Tom, Snare, Cymbal, HiHat
  engine/                   LXR shim (fw_shim/), host glue (lxr_host.c), engine build, EngineHost
  third_party/lxr-firmware/ LXR firmware sources, unmodified (GPL-3.0-only + NOTICE.md)
befaco/
  plethora/                 Noise Plethora
    engine/                 VCV Rack compatibility header (compat/), DSP helpers, PlethoraHost
    third_party/noise-plethora/  VCV port, unmodified (GPL-3.0-or-later; teensy/ parts MIT)
  oneiroi/                  Oneiroi + Oneiroi FX
    engine/                 OneiroiCore (Oneiroi.h + hooks), OWL runtime stand-ins (compat/), OneiroiHost
    third_party/oneiroi/    Befaco Oneiroi patch (GPL-3.0-or-later), one marked fix (MAQUINA.md)
    third_party/owlprogram/ OWL library files Oneiroi uses, unmodified (GPL-3.0)
euro-modules/
  dredrum/                  Dredrum
    engine/                 engine ported to portable C (bit-exact), DredrumHost
    third_party/dredrum/    Dredrum firmware (trig + gate), unmodified (GPL-3.0-or-later)
mutable-instruments/        Mutable Instruments (MIT code)
  klang/                    Klang (Plaits): engine/ KlangHost, plugins/klang, presets/klang
  klong/                    Klong + Klong FX (Rings): engine/ KlongHost, plugins/klong(-fx), presets/klong(_fx)
  third_party/eurorack/     Plaits + Rings DSP code and stmlib, unmodified (MIT; MAQUINA.md),
                            built as libklang.a / libklong.a (Makefile, eurorack.mk)
openrb/                     OpenRB-338 (ReBirth RB-338 reimplementation); Makefile builds libacida.a
  acida/                    Acida + Acida FX: engine/ (AcidaDsp: the bit-exact C++ port, AcidaHost),
                            plugins/acida(-fx), presets/acida(_fx)
    third_party/openrb-338/ orb-engine + rbs crates, the Rust reference (GPL-3.0-only; MAQUINA.md)
common/                     shared by all plugins: DPF plugin, editor, randomizer, presets, resamplers,
                            generated tables, plugin.mk, paths.mk (where everything lives)
tools/                      generators, tests, aarch64 sysroot, dredrum_flash + dredrum_ref (AVR reference),
                            oneiroi_ref (engine fidelity) + oneiroi_test (behaviour, preset measuring),
                            mutable_test (Klang/Klong engine fidelity + behaviour),
                            acida_ref (Rust reference, cargo) + acida_test (Acida bit-exactness + behaviour)
machinista/                 Machinista profiles + layout
dpf/                        DPF submodule
patches/dpf/                local DPF fixes (CLAP latency sync, CLAP remote-controls), applied by `make dpf-patches`
```

## Licence

GPL-3.0-only, like the LXR firmware it is built from (see `LICENSE`, and
`drum02/third_party/lxr-firmware/NOTICE.md` for the firmware's additional terms,
including the trademark terms above). This is a modified version of the LXR
firmware's sound engine, marked as such in every plugin's description.

Other included code, all compatible with GPL-3.0-only:

| Code | Authors | Licence |
|---|---|---|
| `euro-modules/dredrum` (firmware + port) | Jan Cumpelik, 2014 | GPL-3.0-or-later |
| `befaco/plethora/third_party/noise-plethora` (VCV port, algorithms from Befaco's Noise Plethora firmware) | VCV and contributors; Befaco | GPL-3.0-or-later |
| `befaco/plethora/third_party/noise-plethora/src/noise-plethora/teensy` (from the Teensy Audio library) | Paul Stoffregen and others | MIT |
| `befaco/oneiroi/third_party/oneiroi`, `befaco/oneiroi/engine/OneiroiCore.h` | Roberto Noris (Befaco Oneiroi) | GPL-3.0-or-later |
| `befaco/oneiroi/third_party/owlprogram` (OwlProgram LibSource) | Rebel Technology and contributors | GPL-3.0 (`fastlog.c` GPL-2.0-or-later, `fastpow.c` BSD-style) |
| `mutable-instruments/third_party/eurorack` (Plaits, Rings, stmlib) | Émilie Gillet (Mutable Instruments) | MIT |
| `openrb/acida/third_party/openrb-338` (orb-engine, rbs), `openrb/acida/engine/AcidaDsp.*` (port of it) | pingw33n (OpenRB-338) | GPL-3.0-only |
| `dpf` | DISTRHO | ISC |

Befaco's own Noise Plethora firmware framework is licensed CC BY-NC-SA and is
**not** used; only the GPL-3.0-or-later algorithm code, through the VCV port,
is. The VCV port's panel graphics (Befaco's) are not included.
See `euro-modules/dredrum/third_party/dredrum/README.md` and
`befaco/plethora/third_party/noise-plethora/README.md`.
