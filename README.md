# Takion Host (unofficial Remote Play server for Windows)

Experimental Windows server that lets Chiaki-compatible Remote Play clients (for example
[chiaki-ng](https://github.com/chiaki-ng/chiaki-ng) on Android) connect to your **PC** and stream the
desktop or a game: video (HEVC), audio (Opus) and gamepad input through a virtual controller.

> **Disclaimer.** This project is not affiliated with, endorsed or certified by Sony Interactive
> Entertainment LLC. "PlayStation" and related marks belong to their owners. Use it only with
> hardware and accounts you own. No warranty.

## Features

- Registration (PIN), session init and Takion stream compatible with Chiaki clients
- Video encoding with **AMD AMF** or **NVIDIA NVENC** (HEVC), DXGI screen capture
- Reed-Solomon FEC and packet pacing in a separate sender thread
- Audio capture and Opus streaming
- Virtual gamepad via ViGEm: DualShock 4 or Xbox 360 (`controller_type` in `server.ini`)
- Bitrate limits that follow the client's request
- Qt GUI (Russian / English) to start the server and edit settings

## Requirements

- Windows 10/11, x64
- AMD (AMF) or NVIDIA (NVENC) GPU with an up-to-date driver
- [ViGEmBus](https://github.com/nefarius/ViGEmBus) driver for the virtual gamepad
  (the project is retired, but the last release still works)
- To build: Visual Studio 2022 Build Tools (C++), CMake, OpenSSL, Opus (fetched by CMake),
  and **Qt 6 for MSVC** (`msvc2022_64` kit) for the GUI. The MinGW kit will not work with MSVC.

## Build

### Server

```powershell
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

The result is `build\Release\takion-host.exe`.

### GUI

```powershell
cmake -S gui -B gui\build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=C:/Qt/6.x.x/msvc2022_64
cmake --build gui\build --config Release
C:/Qt/6.x.x/msvc2022_64/bin/windeployqt.exe --qmldir gui --release gui/build/Release/TakionServerGUI.exe
```

Replace `6.x.x` with your Qt version.

## Install and run

**The GUI must be placed in the same folder as `takion-host.exe`.** It starts the server from its own
folder and reads and writes `server.ini` and `gui.ini` there.

1. Create a folder (for example `C:\takion-host`) and copy into it:
   - `takion-host.exe` from `build\Release`
   - everything from `gui\build\Release` (`takion_gui.exe` and the Qt DLLs/plugins from `windeployqt`)
2. Copy `server.ini.example` to `server.ini` in that folder and edit it
   (set `rp_key = auto` to generate your own key).
3. Start `takion_gui.exe` (or run `takion-host.exe` directly from a console).
4. The 8-digit registration PIN is shown on start. In the client choose manual registration, enter the PC
   address and the PIN.

## Settings (`server.ini`)

| Key | Meaning |
|---|---|
| `pin` | 8-digit registration PIN |
| `rp_key` | 32 hex characters or `auto` |
| `bitrate_max_mbps` / `bitrate_min_mbps` | Video bitrate limits, Mbit/s |
| `adaptive_bitrate` | Lower/raise bitrate from loss reports |
| `respect_client_bitrate` | Do not exceed the bitrate asked by the client |
| `controller_type` | `ds4` or `x360` |

## Known limitations

- Experimental: expect bugs, especially under heavy GPU load in games
- No remote access over the Internet; use a local network (5 GHz Wi-Fi or Ethernet is recommended)
- Senkusha (MTU/RTT test) is not implemented; clients fall back to default values

## Credits and license

Parts of the protocol implementation are based on [Chiaki](https://github.com/thestr4ng3r/chiaki) /
[chiaki-ng](https://github.com/chiaki-ng/chiaki-ng), licensed under AGPL-3.0 with an OpenSSL exception.
This project is therefore distributed under the **GNU AGPL-3.0** (see `LICENSE`). Third-party components
are listed in `NOTICE.md`.
