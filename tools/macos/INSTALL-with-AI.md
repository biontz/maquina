# Install Maquina with an AI coding agent

If you use an AI coding agent that can run Terminal commands (for example Claude Code or
Codex CLI), it can install Maquina for you and check that everything is right.

1. Open the disk image (double-click `Maquina-<version>-macOS.dmg`, e.g. in `~/Downloads`).
2. Open Terminal (Cmd+Space, type Terminal, Return) and start your agent there.
3. Paste the whole block below. The agent asks before anything important.

Without an agent: see `INSTALL-macOS.txt` (one Terminal line does the same).

```text
Install (or update) the Maquina audio plugins (CLAP + VST3) on this Mac from the Maquina disk image, then check the install. Older Maquina plugins in ~/Library/Audio/Plug-Ins are replaced, new ones are added.

The disk image contains: an installer script "Install Maquina.command", folders CLAP/ (Maquina*.clap bundles) and VST3/ (Maquina*.vst3 bundles), README.md, INSTALL-macOS.txt, LICENSE.

Steps:
1. Find the image. If it is open it is mounted as /Volumes/Maquina <version>: `ls -d /Volumes/Maquina*`. If there is more than one, ask me which one to use. If none is open, look for the file (`ls -t ~/Downloads/Maquina-*-macOS.dmg ~/Desktop/Maquina-*-macOS.dmg 2>/dev/null`), ask me which one, and mount it read-only: `hdiutil attach -nobrowse -readonly "<file>"`. Below, VOL is that mounted folder.
2. Ask me to quit my DAWs if any is running (`pgrep -fl "Bitwig|Ableton|REAPER|Cubase|Studio One"`); a running DAW keeps the old plugins loaded. Wait until I confirm.
3. List what is installed now, for the report at the end:
   ls -la ~/Library/Audio/Plug-Ins/CLAP/Maquina* ~/Library/Audio/Plug-Ins/VST3/Maquina* 2>/dev/null
4. Look at the image: `ls -la "VOL" "VOL/CLAP" "VOL/VST3"`. Count the .clap and .vst3 bundles (they should be the same number). Read "VOL/Install Maquina.command" and tell me in two or three sentences what it does before running it. It should only copy Maquina*.clap / Maquina*.vst3 into ~/Library/Audio/Plug-Ins/{CLAP,VST3} (removing the older copy of each first) and run xattr to clear com.apple.quarantine on them. If it does anything else, stop and tell me.
5. Run it without its "press Return" prompt:
   MAQUINA_INSTALL_YES=1 sh "VOL/Install Maquina.command"
6. Verify:
   - every bundle from VOL/CLAP is now in ~/Library/Audio/Plug-Ins/CLAP and every bundle from VOL/VST3 in ~/Library/Audio/Plug-Ins/VST3, with today's modification date; list any Maquina bundle there that is NOT in the image or NOT from today
   - for each installed bundle: `codesign --verify --strict <bundle>` passes, and `lipo -archs <bundle>/Contents/MacOS/<name without extension>` shows "x86_64 arm64"
   - `xattr -lr ~/Library/Audio/Plug-Ins/CLAP/Maquina* ~/Library/Audio/Plug-Ins/VST3/Maquina*` prints no com.apple.quarantine
7. If you mounted the image yourself in step 1, eject it: `hdiutil detach "VOL"`.
8. Report a short table: plugin, CLAP installed (yes/no), VST3 installed (yes/no), archs, installed before (from step 3). Then tell me how to make my DAW see them: Bitwig: Settings > Locations > Plug-ins, rescan. Ableton Live (VST3 only): Settings > Plug-Ins, "Use VST3 Plug-in System Folders" on, then Rescan. Reaper: Settings > Plug-ins > VST / CLAP, Re-scan. Logic/GarageBand can't load them (AU only).

Rules: don't use sudo. Don't change anything outside ~/Library/Audio/Plug-Ins/CLAP/Maquina* and ~/Library/Audio/Plug-Ins/VST3/Maquina* (reading is fine). Never touch ~/Library/Application Support/Maquina: it holds my own presets. Don't download anything. If a step fails, show me the exact error and stop.
```

Your presets are saved in `~/Library/Application Support/Maquina/presets/<plugin>/` and are
never touched by the installer.
