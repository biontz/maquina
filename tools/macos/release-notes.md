Free instrument and effect plugins built from the open-source code of hardware instruments,
Eurorack modules and a classic software synth. What each plugin is and where it comes from:
[README](https://github.com/biontz/maquina#readme).

## macOS downloads (CLAP + VST3, universal: Intel + Apple Silicon, macOS 10.15+)

- **All 17 plugins:** `Maquina-<version>-macOS.dmg`
- **One instrument** (with its FX version where there is one: Oneiroi, Klong, Acida, BL-101):
  `Maquina-<Plugin>-<version>-macOS.dmg`
- `Maquina-macOS-<version>.zip`: all plugins as a zip, for installing by hand
- `SHA256SUMS.txt`: checksums of all files

## Install

Open the .dmg file(s), then paste this into Terminal (Cmd+Space, "Terminal"):

```
for v in /Volumes/Maquina*/; do MAQUINA_INSTALL_YES=1 sh "$v/Install Maquina.command"; done
```

Then restart your DAW or rescan plugins. Other ways (double-click, by hand, with an AI agent)
are in `INSTALL-macOS.txt` and `INSTALL-with-AI.md` inside each disk image. The plugins are
signed ad hoc, not by a registered Apple developer; the installer clears macOS's download
quarantine. Ableton Live loads VST3 only. Logic and GarageBand (AU only) can't load them.

Linux (x86_64, Raspberry Pi) builds: from source, see the
[README](https://github.com/biontz/maquina#building).

Unofficial: not affiliated with or endorsed by the makers of the original instruments (see
the README). Free software under the GPL-3.0; the source is this release's tag.
