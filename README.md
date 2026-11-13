# Maquina

Five drum-voice instrument plugins (**Kick, Tom, Snare, Cymbal, HiHat**) built
from the open-source sound engine of the Erica Synths LXR-02 drum machine
([erica-synths/lxr-desktop-app](https://github.com/erica-synths/lxr-desktop-app)).

Formats: **LV2, CLAP, VST3** and a JACK standalone (via [DPF](https://github.com/DISTRHO/DPF)).
Platforms: Linux x86_64 and Raspberry Pi OS 64-bit (aarch64); macOS/Windows later.

> Unofficial. Not affiliated with, endorsed by or supported by Erica Synths.
> "Erica Synths", "LXR" and "LXR-02" are trademarks of Erica Synths; they are
> used here only to say what the sound engine is based on.

## How it stays close to the hardware

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

Not reproducible: the hardware's true random noise source (a seeded xorshift is
used), its analog output stage, and floating-point rounding of the Cortex-M7
build (fused multiply-adds, libm) - x86 and Arm builds agree to ~90 dB.

## Using the plugins

* MIDI note-on triggers the voice; velocity works as on the hardware.
* **Trig Note** (Trigger Note): `Any` (default) plays every note at the set pitch. Set a
  note to respond only to that note (the hardware's MIDI note setting).
* **Key Track** (Note > Pitch): with Trig Note = Any, transpose by the played note
  around C4 (60) - the hardware's "any" behaviour.
* **HiHat**: **Hat** = Closed / Open selects which hat this instance plays.
  For both, put one HiHat on each track; choke them with the DAW if wanted.
  One **Decay** sets the decay of the hat the instance plays (the hardware's
  separate closed/open decays are merged; "Amp Decay" as a velocity/LFO
  target follows the hat too). Older presets with `amp_decay_closed` /
  `amp_decay_open` still load (the closed value is used).
* **Remote controls (CLAP)**: the CLAP plugins expose 5 pages of 8 controls
  (the Machinista layout, `tools/pages.py`), which Bitwig shows as the device's
  remote-control pages and maps onto controllers.
* Parameter names are short so host knobs show them whole - see
  [Parameters](#parameters).
* Tempo-synced LFOs follow the host tempo.
* Latency (about 2 ms) is reported to the host.

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
Ranges are the hardware's 0..127 scales; tune and pan are shown centred.

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
| Trig Note | `trigger_note` | Trigger › Note | Trigger Note | Any, C#-1..G9 | all |
| Key Track | `note_pitch` | Trigger › Pitch | — (plugin only) | Off / On | all |
| Hat | `hat_mode` | Trigger › Hat | — (plugin only) | Closed / Open | HiHat |

#### Remote-control pages (CLAP)

Bitwig and other CLAP hosts get these pages of 8 knobs; Machinista uses the
same layout (`tools/pages.py`), with bank/patch browsing in place of the first
two knobs on the OSCILLATOR page.

| Voice | Page | Knobs 1-8 |
|---|---|---|
| Kick, Tom | OSCILLATOR | Coarse · Fine · Wave · PWM · Volume · Pan · Drive · Decimate |
|  | ENVELOPES | Attack · Decay · Slope · Pitch Dec · Pitch Slp · Pitch Amt · Drive · Decimate |
|  | FM / TRANSIENT | FM Amt · FM Freq · FM Wave · FM Mode · T Wave · T Vol · T Freq · Trig Note |
|  | FILTER / VELOCITY | Cutoff · Reso · Flt Type · Flt Drive · Velo Dest · Velo Amt · Velo>Vol · Key Track |
|  | LFO | LFO Rate · LFO Sync · LFO Amt · LFO Wave · LFO Retrg · LFO Phase · LFO Dest · Volume |
| Snare | OSCILLATOR | Coarse · Fine · Wave · PWM · Noise Frq · Noise Mix · Volume · Pan |
|  | ENVELOPES | Attack · Decay · Slope · Repeat · Pitch Dec · Pitch Slp · Pitch Amt · Volume |
|  | TRANSIENT / MIX | T Wave · T Vol · T Freq · Pan · Drive · Decimate · Trig Note · Key Track |
|  | FILTER / VELOCITY | Cutoff · Reso · Flt Type · Flt Drive · Velo Dest · Velo Amt · Velo>Vol · Volume |
|  | LFO | LFO Rate · LFO Sync · LFO Amt · LFO Wave · LFO Retrg · LFO Phase · LFO Dest · Volume |
| Cymbal | OSCILLATOR | Coarse · Fine · Wave · PWM · Volume · Pan · Drive · Decimate |
|  | MOD OSC | 1 Freq · 2 Freq · 1 Gain · 2 Gain · 1 Wave · 2 Wave · Drive · Decimate |
|  | ENVELOPE / TRANSIENT | Attack · Decay · Repeat · Slope · T Wave · T Vol · T Freq · Trig Note |
|  | FILTER / VELOCITY | Cutoff · Reso · Flt Type · Flt Drive · Velo Dest · Velo Amt · Velo>Vol · Key Track |
|  | LFO | LFO Rate · LFO Sync · LFO Amt · LFO Wave · LFO Retrg · LFO Phase · LFO Dest · Volume |
| HiHat | OSCILLATOR | Coarse · Fine · Wave · PWM · Volume · Pan · Drive · Decimate |
|  | MOD OSC | 1 Freq · 2 Freq · 1 Gain · 2 Gain · 1 Wave · 2 Wave · Drive · Decimate |
|  | ENVELOPE / TRANSIENT | Attack · Decay · Slope · Hat · T Wave · T Vol · T Freq · Key Track |
|  | FILTER / VELOCITY | Cutoff · Reso · Flt Type · Flt Drive · Velo Dest · Velo Amt · Velo>Vol · Hat |
|  | LFO | LFO Rate · LFO Sync · LFO Amt · LFO Wave · LFO Retrg · LFO Phase · LFO Dest · Trig Note |

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
  sets the sound only: Trig Note, Hat and Key Track stay as they are.
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
  locked; the trigger settings are never randomized. The settings are saved
  with the project. Desktop editor only (not on Machinista yet).
* Amp / Pitch EG graphs are drawn from the firmware's own time and slope
  curves (real milliseconds). Drag the handles to set attack / decay /
  amount, drag the curve up/down for the slope.

## Building

Requirements: a C/C++ toolchain, GNU make, `python3`, `clang` (x86 builds use
its AST for the Arm-semantics rewrite), and for the tests `lilv` and
`lv2_validate` (lv2 package).

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

## Layout

```
third_party/lxr-firmware/   firmware sources, unmodified (GPL-3.0-only + NOTICE.md)
engine/                     shim, host glue (lxr_host.c), engine build
plugins/common/             EngineHost (state, timing, resampler), DPF plugin, generated tables
plugins/<voice>/            per-plugin DistrhoPluginInfo.h + Makefile
presets/<voice>/            factory presets (.maquina text files)
tools/                      generators, tests, aarch64 sysroot
machinista/                 Machinista profiles + layout
dpf/                        DPF submodule
patches/dpf/                local DPF fixes (CLAP latency sync, CLAP remote-controls), applied by `make dpf-patches`
```

## Licence

GPL-3.0-only, like the firmware it is built from (see `LICENSE`, and
`third_party/lxr-firmware/NOTICE.md` for the firmware's additional terms,
including the trademark terms above). This is a modified version of the LXR
firmware's sound engine, marked as such in every plugin's description.
