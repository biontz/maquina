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

Then restart your DAW or rescan plugins. Other ways (double-click, by hand, with an AI agent)
are in `INSTALL-macOS.txt` and `INSTALL-with-AI.md` inside each disk image. Ableton Live loads
VST3 only. Logic and GarageBand (AU only) can't load them.

Linux (x86_64, Raspberry Pi) builds: from source, see the
[README](https://github.com/biontz/maquina#building).

Unofficial: not affiliated with or endorsed by the makers of the original instruments (see
the README). Free software under the GPL-3.0; the source is this release's tag.
