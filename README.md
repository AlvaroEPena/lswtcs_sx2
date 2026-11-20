# LEGO Star Wars: The Complete Saga - Switch 2

Native Switch 2 adaptation of [NaGaa95/lswtcs_nx](https://github.com/NaGaa95/lswtcs_nx), based on Android **2.0.2.02 / build 20202**.

Download the normal build from [Releases](https://github.com/AlvaroEPena/lswtcs_sx2/releases). Game binaries, data packs, APKs, and saves are not distributed.

## Requirements

- A Switch 2 with a compatible Seedsow/kaiHEN native homebrew environment and Homebrew Launcher. Tested with firmware **23.0.1** and [kaiHEN 20261117.1](https://github.com/ouncebrew/kaihen/releases/tag/20261117.1).
- Launch with application memory through a compatible native game host. Album/applet memory is insufficient.
- Your own Android copy of LEGO Star Wars: The Complete Saga, version **2.0.2.02 (20202)**, including its **ARM64** library (`lib/arm64-v8a/libTTapp.so`) and four data packs. The ARMv7 library used by the Vita port is incompatible.

## Installation and updates

1. Download `lswtcs-sx2.nro` from the release.
2. Create `/switch/lswtcs/` on the SD card and put the NRO there.
3. Extract the ARM64 library and four data packs from your own game installation into the same folder:

```text
/switch/lswtcs/
  lswtcs-sx2.nro
  libTTapp.so
  Audio.dat
  Levels.dat
  Others.dat
  Textures.dat
```

Launch **LSWTCS Switch 2** through Homebrew Launcher. When updating an existing installation, replace only `lswtcs-sx2.nro`; retain `SavedGames/` and `config.txt`. Saves live under `/switch/lswtcs/SavedGames/`.

## Port menu and FPS counter

Press **L + R + D-pad Down** together to open or close the port menu. Release the buttons before choosing an option.

- **D-pad Up/Down:** select a row.
- **FPS counter:** press **A**, **Left**, or **Right** to toggle the counter.
- **FPS size:** press **A** or **Right** to cycle **Small / Medium / Large**; **Left** cycles backward.
- **B**, or **A** on **Back:** close the menu.

The FPS counter appears in the upper-left corner. Visibility and size save automatically in `config.txt`. Menu input is captured, but gameplay continues behind the menu.

## Configuration

`config.txt` is created on first launch:

| Setting | Values |
| --- | --- |
| `screen_width`, `screen_height` | `-1` for automatic resolution, or explicit dimensions |
| `language` | `-1` for system language, or the upstream language index |
| `sfx_volume` | Gameplay mono SFX volume in percent; default `70` |
| `show_fps` | `0` off, `1` on; also adjustable in the port menu |
| `fps_size` | `0` Small, `1` Medium, `2` Large; default Medium |

## Release status

Boot, a playable New Game cantina, port-menu/FPS controls, and restoring a saved game were confirmed on a Switch 2 during development. The normal release retains the successful save-load fix and removes the diagnostic capture system. A full-game playthrough has not been validated.

## Building

Use a Switch 2 toolchain with devkitA64, libounce, OunceVK EGL/GLES, SDL2, and libkaihen. The tested environment was [ouncebrew/ounce-devkit](https://github.com/ouncebrew/ounce-devkit) at commit `f3478152dc553a104dd991efca4bde0c6e7cfe3e` (ounce-devkit 20261114, libounce `9382b1ec48ac`, OunceVK `f5afcd27a98d`, kaiHEN `eb81e1912586`).

With that environment installed:

```sh
source /opt/devkitpro/devkita64.sh
make -j6
```

Or build the devkit Docker image from its repository, tag it `ounce-devkit`, then run `bash tools/build-container.sh` in this repository. Output: `lswtcs-sx2.nro`. Game files are not required to compile the port. Release builds disable debug logging and exclude recording, sampling, and crash-dump modules.

## Credits and licenses

- **NaGaa95** - original Switch port, preserved as this fork's upstream.
- **gm666q** - [PS Vita port](https://github.com/gm666q/lswtcs-vita) and OpenSL ES work.
- **TheOfficialFloW** and **fgsfds** - Android loader and Switch loader groundwork.
- **ouncebrew contributors** - Switch 2 toolchain and kaiHEN runtime.
- **AlvaroEPena** - Switch 2 adaptation and release maintenance.

The port source is MIT-licensed unless otherwise noted; vendored OpenSL ES code retains its AOSP Apache-2.0 notices. LEGO Star Wars and its game assets belong to their respective owners. This project is unaffiliated with those owners.
