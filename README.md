# Maquina

Seven drum and percussion instrument plugins, each built from the open-source
code of a hardware instrument:

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

Formats: **LV2, CLAP, VST3** and a JACK standalone (via [DPF](https://github.com/DISTRHO/DPF)).
Platforms: Linux x86_64 and Raspberry Pi OS 64-bit (aarch64); macOS/Windows later.

> Unofficial. Not affiliated with, endorsed by or supported by Erica Synths,
> Befaco, VCV or the Dredrum authors. "Erica Synths", "LXR" and "LXR-02" are
> trademarks of Erica Synths, "Befaco" and "Noise Plethora" belong to Befaco;
> these names are used here only to say what each sound engine is based on.

## How it stays close to the hardware

### LXR voices (Kick, Tom, Snare, Cymbal, HiHat)

The firmware's DSP code is compiled **unmodified** from `third_party/lxr-firmware`
(pinned commit in `UPSTREAM_COMMIT`), with a small hardware shim
(`engine/fw_shim`) in place of the STM32 HAL:

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
(`third_party/dredrum/trig` and `gate`, vendored unmodified) is ported to
portable C in `engine/dredrum/dredrum_core.c`:

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
(`third_party/noise-plethora`, vendored unmodified at a pinned commit), with a
small compatibility header (`engine/plethora/compat`) in place of VCV Rack:

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

## Using the plugins

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
* **Remote controls (CLAP)**: the CLAP plugins expose 5 pages of 8 controls
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

Factory presets live in `presets/<voice>/*.maquina` (format in
`plugins/common/Presets.hpp`). After adding or changing one run
`make presets` (validates them, regenerates the compiled table and the
Machinista banks), then `make`. `build/tools/preset_measure <voice> -f file`
renders a preset and prints level, length, pitch and brightness.

### Raspberry Pi (aarch64)

```sh
make aarch64        # bin-aarch64/: lv2, clap, vst3
make test-aarch64   # runs every test under qemu-aarch64-static
```

No root and no cross-gcc needed: `tools/aarch64/make_sysroot.py` fetches a
Debian bookworm arm64 sysroot (glibc 2.36, the Raspberry Pi OS bookworm base;
checksums verified against the Debian index) and clang + lld build against it.
The binaries need glibc >= 2.34. Copy `bin-aarch64/*.lv2` to `~/.lv2` on the Pi.

### Machinista

Controller profiles for [Machinista](https://github.com/photogeen/machinista)
(Maschine controllers on a Pi) are in `machinista/profiles/` - see
`machinista/README.md`.

Factory presets as Machinista banks (one bank per category) are in
`machinista/banks/maquina-<voice>/`; copy each folder to Machinista's
`presets/plugin-banks/`.

Maquina Plethora's Machinista profile has channel A only, so its banks hold
only the factory presets that use channel A alone.

## Layout

```
third_party/lxr-firmware/   LXR firmware sources, unmodified (GPL-3.0-only + NOTICE.md)
third_party/dredrum/        Dredrum firmware (trig + gate), unmodified (GPL-3.0-or-later)
third_party/noise-plethora/ VCV port of the Noise Plethora, unmodified (GPL-3.0-or-later; teensy/ parts MIT)
engine/                     LXR shim, host glue (lxr_host.c), engine build
engine/dredrum/             Dredrum engine ported to portable C (bit-exact)
engine/plethora/            VCV Rack compatibility header + DSP helpers for the Plethora port
plugins/common/             EngineHost / DredrumHost / PlethoraHost, resampler, DPF plugin, editor, generated tables
plugins/<voice>/            per-plugin DistrhoPluginInfo.h + Makefile
presets/<voice>/            factory presets (.maquina text files)
tools/                      generators, tests, aarch64 sysroot, dredrum_flash + dredrum_ref (AVR reference)
machinista/                 Machinista profiles + layout
dpf/                        DPF submodule
patches/dpf/                local DPF fixes (CLAP latency sync, CLAP remote-controls), applied by `make dpf-patches`
```

## Licence

GPL-3.0-only, like the LXR firmware it is built from (see `LICENSE`, and
`third_party/lxr-firmware/NOTICE.md` for the firmware's additional terms,
including the trademark terms above). This is a modified version of the LXR
firmware's sound engine, marked as such in every plugin's description.

Other included code, all compatible with GPL-3.0-only:

| Code | Authors | Licence |
|---|---|---|
| `third_party/dredrum`, `engine/dredrum` (port) | Jan Cumpelik, 2014 | GPL-3.0-or-later |
| `third_party/noise-plethora` (VCV port, algorithms from Befaco's Noise Plethora firmware) | VCV and contributors; Befaco | GPL-3.0-or-later |
| `third_party/noise-plethora/src/noise-plethora/teensy` (from the Teensy Audio library) | Paul Stoffregen and others | MIT |
| `dpf` | DISTRHO | ISC |

Befaco's own Noise Plethora firmware framework is licensed CC BY-NC-SA and is
**not** used; only the GPL-3.0-or-later algorithm code, through the VCV port,
is. The VCV port's panel graphics (Befaco's) are not included.
See `third_party/dredrum/README.md` and `third_party/noise-plethora/README.md`.
