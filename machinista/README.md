# Maquina on Machinista

[Machinista](https://github.com/photogeen/machinista) hosts LV2 instruments on
a Raspberry Pi 4/5 with a Native Instruments Maschine controller. It maps a
plugin onto the controller's 8 encoders through a JSON profile.

`profiles/maquina-*.json` are those profiles, one per plugin. Each has pages
of eight controls; the LXR's own menu pages are grouped so no encoder is
dead:

| plugin | pages |
|---|---|
| Kick, Tom | OSCILLATOR (bank, patch, osc, volume, pan) · ENVELOPES · FM / TRANSIENT · FILTER / VELOCITY · LFO (+ gain) |
| Snare | OSCILLATOR (osc + noise) · ENVELOPES · TRANSIENT / MIX · FILTER / VELOCITY · LFO (+ gain) |
| Cymbal, HiHat | OSCILLATOR · MOD OSC · ENVELOPE / TRANSIENT · FILTER / VELOCITY · LFO (+ gain) |
| Dredrum | MIX (bank, patch, levels, decays) · OSC 1 · OSC 2 · OSC 3 · SWITCHES (mode, click, track, mutes) · MIX / OUT (gain, levels, mutes, mode) |
| Plethora | A (bank, patch, algorithm, X/Y, filter) · A ENV (envelope, level, mute, mode, gain) - channel A only |

The display shows the plugins' short host names uppercased (COARSE, T VOL,
1 FREQ...); the full list of names, symbols and ranges, and every page knob by
knob, is in the main README under [Parameters](../README.md#parameters).

## Install (untested on a Pi so far)

1. Build for the Pi: `make aarch64` (in the repository root).
2. Copy the bundles to the Pi: `bin-aarch64/Maquina*.lv2` -> `~/.lv2/`
   (or the LV2 directory Machinista scans).
3. Copy `profiles/maquina-*.json` into Machinista's `app/plugin_profiles_data/`.
4. Restart Machinista; the Maquina instruments appear in the plugin list.

## Notes

* LXR voices use the LXR's 0..127 scales; coarse tune and pan are shown
  centred (-60..67 / -63..64) as on the hardware display. Dredrum uses the
  module's 0..255 pot scales, Plethora 0..127.
* **Plethora on Machinista is channel A only**, to keep the Pi's CPU load to
  one channel. B and C stay at Level 0, so they are silent and not computed.
  The factory banks hold the presets that use channel A alone.
* Every note triggers: each pad hit plays the sound at its set pitch. Turn
  **KEY TRACK** on to play it chromatically. For a closed and an open hat,
  use two HiHat sounds with short and long **DECAY**.
* `@bank` / `@patch` select Machinista parameter banks. The factory presets
  are in `banks/maquina-<voice>/` (one bank per category: 808, 909, ...): copy
  each folder to Machinista's `presets/plugin-banks/`. Without bank files the
  built-in "Init" bank (the plugin defaults) is used. A patch also sets
  KEY TRACK to Off.
* Regenerate after changing parameters: `python3 tools/gen_params.py`, `make`,
  `python3 tools/gen_machinista_profiles.py`. LV2 symbols are stable API -
  profiles and saved projects refer to them.
