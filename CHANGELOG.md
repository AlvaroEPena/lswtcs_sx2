# v0.4.0 - Switch 2 normal build

First public Switch 2 release of this fork, based on NaGaa95's Switch port.

## Changes

- Native Switch 2 build using libounce, OunceVK, and kaiHEN.
- Adapted library loading, memory allocation, threading/TLS, timing, and controller handling for Switch 2.
- Fixed the save-loading crash caused by missing active-player selection; retained a narrow guard for the cantina's null-player proximity check.
- Added the **L + R + D-pad Down** port menu, persistent FPS-counter toggle, and Small/Medium/Large FPS sizes.
- Removed diagnostic recording, thread sampling, crash dumps, GL/audio/I/O tracing, and routine debug-log writes from the release build.

## Install / update

Download **lswtcs-sx2.nro** and place it in **/switch/lswtcs/**. For an existing installation, replace only the NRO and keep your saves and configuration.

New installations also require your own Android **2.0.2.02 / build 20202** files: **arm64-v8a/libTTapp.so**, **Audio.dat**, **Levels.dat**, **Others.dat**, and **Textures.dat**. Put those five files beside the NRO. Game files and saves are not included in this release.

Requires a compatible native Switch 2 Seedsow/kaiHEN environment. Development testing used firmware **23.0.1** and **kaiHEN 20261117.1**. Launch through Homebrew Launcher with application memory; album/applet mode is insufficient.

## Port menu and FPS counter

- Open/close the menu: **L + R + D-pad Down**, then release the buttons.
- Select a row: **D-pad Up/Down**.
- Toggle **FPS counter**: **A**, **Left**, or **Right** on that row.
- Change **FPS size**: **A/Right** cycles Small / Medium / Large; **Left** cycles backward.
- Close: **B**, the menu shortcut, or **A** on **Back**.

Settings save automatically to **config.txt**. The counter appears at the upper left. Gameplay continues behind the menu.

Boot, New Game in the cantina, menu/FPS controls, and saved-game loading were confirmed during development. The release passed native build checks and offline save-load/menu regressions; a full-game playthrough is not validated.

Based on [NaGaa95/lswtcs_nx](https://github.com/NaGaa95/lswtcs_nx), with credits and source available in this fork. **SHA256SUMS.txt** contains the binary checksum.
