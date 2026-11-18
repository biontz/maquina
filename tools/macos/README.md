# Maquina

Made to run on **[Machinista](https://github.com/photogeen/machinista)**, software for the
Raspberry Pi made for Native Instruments' Maschine controllers (see
[Maquina on Machinista](https://github.com/biontz/maquina#machinista)), and just as much for
use in a **DAW**. Tested in Bitwig Studio on Linux, and in Ableton Live and Bitwig Studio on
macOS. Not yet tested on the Pi.

Seventeen free instrument and effect plugins for macOS (CLAP + VST3, universal: Intel and
Apple Silicon, macOS 10.15 or later). Each one is built from the open-source code of a
hardware instrument, a Eurorack module or a classic software synth, so it sounds like the
original, not like an imitation of it.

**This disk image** holds the plugins in its `CLAP/` and `VST3/` folders. The descriptions
below cover all Maquina plugins; each one, singly or all as a set, is on the
[Releases page](https://github.com/biontz/maquina/releases).

**Install:** see `INSTALL-macOS.txt` (Terminal, double-click or by hand) or
`INSTALL-with-AI.md` (a prompt for an AI coding agent that installs and checks everything).

Why Terminal? macOS marks everything downloaded from the internet as "quarantined".
Apps and plugins from developers who pay Apple for a Developer ID and have their software
checked by Apple ("notarized") pass that check. Maquina is free software and signed only
"ad hoc", so macOS refuses to load the plugins and shows messages like "can't be opened"
or "is damaged". The DAW then skips them or crashes on the scan. The line below
installs the plugins and removes the quarantine mark from them (and only from them),
without needing an admin password. It does what the installer in the disk image would do
after the "Open Anyway" dialogs in System Settings, but in one step.

Open the .dmg file(s), then paste this into Terminal (Cmd+Space, "Terminal"):

```
for v in /Volumes/Maquina*/; do MAQUINA_INSTALL_YES=1 sh "$v/Install Maquina.command"; done
```

Then restart your DAW or rescan plugins.

Hosts: any CLAP or VST3 host, e.g. Bitwig Studio, Reaper, Cubase. **Ableton Live** loads
VST3 only, so use the `.vst3` versions there. Logic and GarageBand need AU and can't load
them.

Every plugin has an editor with factory presets, a preset browser (categories, tags,
ratings, search), your own presets, and a randomizer with per-group locks and ranges.

---

## The plugins

### Kick, Tom, Snare, Cymbal, HiHat

![Maquina Kick](screenshots/kick.png)

The five drum voices of the **Erica Synths LXR-02** drum machine. The firmware's sound
engine is compiled unmodified and runs at the hardware's sample rate, so the voices match
the machine: oscillators with FM, transient samples, filters, LFOs, the LXR's envelopes and
its drive. Kick and Tom share one voice design, Snare adds a noise source, Cymbal and HiHat
have two modulation oscillators for metallic sounds.

Origin: [erica-synths/lxr-desktop-app](https://github.com/erica-synths/lxr-desktop-app)
(firmware by Erica Synths, GPL-3.0-only with additional terms). Maquina's drum voices are a
**modified version** of the LXR firmware's sound engine.

| | |
|---|---|
| ![Maquina Tom](screenshots/tom.png) | ![Maquina Snare](screenshots/snare.png) |
| ![Maquina Cymbal](screenshots/cymbal.png) | ![Maquina HiHat](screenshots/hihat.png) |

### Dredrum

![Maquina Dredrum](screenshots/dredrum.png)

The **Dredrum** Eurorack module: an 8-bit percussion and drone generator with three
oscillators (pitch, pitch drop, decay, level each), from kicks and snares to chip noise.
The module's own engine (written for an 8-bit microcontroller) runs unmodified.

Origin: [buranelectrix/dredrum](https://github.com/buranelectrix/dredrum) by Jan Cumpelik
(GPL-3.0).

### Plethora

![Maquina Plethora](screenshots/plethora.png)

The **Befaco Noise Plethora** Eurorack module: 60 noise algorithms (radio static, cluster
oscillators, chaotic maps, bit-shifters...) on two channels, each with X/Y controls and a
resonant filter, plus a third filtered-noise channel. Maquina adds an envelope per channel
so it plays from MIDI notes.

Origin: the VCV Rack port, [VCVRack/Befaco](https://github.com/VCVRack/Befaco), with the
algorithms from Befaco's Noise Plethora firmware (GPL-3.0).

### Oneiroi and Oneiroi FX

![Maquina Oneiroi](screenshots/oneiroi.png)

The **Befaco Oneiroi** Eurorack module, an ambient and drone synth by Roberto Noris:
oscillators with supersaw and wavetables, a looper, filter, resonator, echo and reverb
("ambience"), all modulated by its own LFO. **Oneiroi** is the whole voice played by MIDI
notes; **Oneiroi FX** is its looper and effects on an audio track.

Origin: [Befaco/Oneiroi](https://github.com/Befaco/Oneiroi) (GPL-3.0), running on parts of
Rebel Technology's OWL library.

![Maquina Oneiroi FX](screenshots/oneiroi-fx.png)

### Klang

![Maquina Klang](screenshots/klang.png)

Based on the **Mutable Instruments Plaits** macro oscillator by Émilie Gillet: 24 synthesis
models (virtual analog, FM, wavetables, chords, speech, strings, modal and analog drums...)
with its low-pass gate. Plaits firmware 1.2, the original DSP code.

Origin: [pichenettes/eurorack](https://github.com/pichenettes/eurorack) (MIT). As Mutable
Instruments asks, the plugin has its own name rather than the module's.

### Klong and Klong FX

![Maquina Klong](screenshots/klong.png)

Based on the **Mutable Instruments Rings** resonator: modal, sympathetic-string, string and
FM resonators (and the string-synth easter egg). **Klong** is played by MIDI notes with its
internal exciter; **Klong FX** puts the resonator on an audio track, excited by the input.

Origin: [pichenettes/eurorack](https://github.com/pichenettes/eurorack) (MIT).

![Maquina Klong FX](screenshots/klong-fx.png)

### Acida and Acida FX

![Maquina Acida](screenshots/acida.png)

A 303-style bass line: the engine of **OpenRB-338** by pingw33n, a bit-exact
reimplementation of the classic ReBirth RB-338 software synth. **Acida** is the bass line
played by MIDI notes (accent by velocity, slide by overlapping notes); **Acida FX** is the
distortion, PCF filter, compressor and tempo delay on an audio track.

Origin: [pingw33n/openrb-338](https://github.com/pingw33n/openrb-338) (GPL-3.0).

![Maquina Acida FX](screenshots/acida-fx.png)

### BL-101 and BL-101 FX

![Maquina BL-101](screenshots/bl101.png)

A classic one-oscillator analog monosynth, extended to 8 voices: VCO with pulse, saw, sub
and noise, 4-pole filter, ADSR, LFO; Mono, Unison, Poly and Chord modes; OSC DRAW and OSC
CHOP for drawn and chopped waveforms. **BL-101 FX** is its chorus, ping-pong delay, plate
reverb and lo-fi on an audio track.

Origin: [S1Vave](https://github.com/AndrewGG88/S1Vave) by Andrew GG, a firmware for the
M-VAVE FM-1 (GPL-3.0), bit-exact.

![Maquina BL-101 FX](screenshots/bl101-fx.png)

### LA-50

![Maquina LA-50](screenshots/la50.png)

An LA-synthesis synth (the late-80s "linear arithmetic" sound) that plays **D-50 patches**:
import a D-50 `.syx` bank and its patches become presets. The editor follows the PG-1000
programmer. Built from [dsynkant](https://github.com/ngeiswei/dsynkant)'s D-50 patch model
and [Munt](https://github.com/munt/munt)'s model of the LA32 chip; the rest is written from
the D-50 manuals. Not a verified D-50 clone, and Roland's PCM samples and factory patches
are not included (built-in stand-ins instead).

---

## Your presets

Saved in `~/Library/Application Support/Maquina/presets/<plugin>/`, one `.maquina` file per
preset. To back them up or move them to another Mac, copy that folder.

## Problems?

Contact the person who gave you this disk image, or open an issue on
[github.com/biontz/maquina](https://github.com/biontz/maquina/issues). Please say which
macOS version, which host (Bitwig, Live, ...) and which plugin.

## Source code and licence

Maquina is free software under the **GNU General Public License v3.0 only** (see `LICENSE`
in this disk image). It comes with **no warranty**. The complete source code, including
all third-party code and its licences, is at
**[github.com/biontz/maquina](https://github.com/biontz/maquina)**.

| Included code | Authors | Licence |
|---|---|---|
| LXR-02 firmware sound engine (modified) | Erica Synths | GPL-3.0-only (+ additional terms) |
| Dredrum firmware | Jan Cumpelik | GPL-3.0-or-later |
| Noise Plethora (VCV port) | VCV and contributors; Befaco | GPL-3.0-or-later (Teensy Audio parts MIT) |
| Oneiroi | Roberto Noris (Befaco) | GPL-3.0-or-later |
| OWL library parts | Rebel Technology and contributors | GPL-3.0 |
| Plaits, Rings, stmlib | Émilie Gillet (Mutable Instruments) | MIT |
| OpenRB-338 engine | pingw33n | GPL-3.0-only |
| S1Vave | Andrew GG, on zvenson's OberVave | GPL-3.0-only |
| Munt (LA32 parts) | Dean Beeler, Jerome Fisher, Sergey V. Mikayev | LGPL-2.1-or-later |
| dsynkant (patch model) | Nil Geisweiller | GPL-2.0-or-later |
| DPF plugin framework | DISTRHO | ISC |

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
> BL-101 is not affiliated with S1Vave's author (Andrew GG), M-VAVE or
> Roland; S1Vave's name and logo are its author's and are not used in the
> plugins (the logo is not included), nor are Roland's model names.
> LA-50 is not affiliated with Roland; "D-50" is used only to say which patch
> format it reads. Roland's PCM waves and factory patches are not included.
