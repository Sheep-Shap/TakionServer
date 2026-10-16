# Takion Host (unofficial Remote Play server for Windows)

Experimental Windows server that lets Chiaki-compatible Remote Play clients (for example
[chiaki-ng](https://github.com/chiaki-ng/chiaki-ng) or Pylux on Android) connect to your **PC** and stream the
desktop or a game: video (HEVC), audio (Opus) and gamepad input through a virtual controller.
The official Sony PS Remote Play app is **not** supported.

> **Disclaimer.** This project is not affiliated with, endorsed or certified by Sony Interactive
> Entertainment LLC. "PlayStation" and related marks belong to their owners. Use it only with
> hardware and accounts you own. No warranty.

## Features

- Registration (PIN), session init and Takion stream compatible with Chiaki clients
- The PC shows up in the client's console list (discovery on UDP 9302), so it can be registered without typing an IP
- Video encoding with **AMD AMF** or **NVIDIA NVENC** (HEVC), DXGI screen capture
- Encoder settings in the GUI: encoder choice, NVENC preset (P1-P7), AMF preset (speed / balanced / quality), intra refresh
- Reed-Solomon FEC and packet pacing in a separate sender thread
- Audio capture and Opus streaming
- Virtual gamepad via ViGEm: DualShock 4 or Xbox 360 (`controller_type` in `server.ini`)
- Bitrate limits that follow the client's request
- Senkusha (RTT / MTU probe) responder, so clients do not wait for a timeout before the stream starts
- Connection-quality messages, so the client's stats overlay shows the measured bitrate
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
   - everything from `gui\build\Release` (the GUI executable and the Qt DLLs/plugins from `windeployqt`)
2. Copy `server.ini.example` to `server.ini` in that folder and edit it
   (set `rp_key = auto` to generate your own key).
3. Start the GUI (or run `takion-host.exe` directly from a console).
4. Allow the ports below in Windows Firewall (private network).
5. The 8-digit registration PIN is shown on start. In the client either pick the PC from the list of
   discovered consoles, or choose manual registration and enter the PC address; then enter the PIN.

## Network ports

Allow these ports for `takion-host.exe` in Windows Firewall (private network):

| Port | Protocol | Purpose |
|---|---|---|
| 9295 | TCP + UDP | Registration, session init, control channel, registration search (`SRC3`) |
| 9296 | UDP | Takion stream (video, audio, gamepad) |
| 9297 | UDP | Senkusha (RTT / MTU test) |
| 9302 | UDP | Console discovery (`SRCH`) |

## Settings (`server.ini`)

Most video settings can be changed in the GUI (tab "Encoder"); changes apply on the next server start.

| Key | Meaning |
|---|---|
| `pin` | 8-digit registration PIN |
| `nickname` | Console name shown in the client (1-32 characters) |
| `rp_key` | 32 hex characters or `auto` |
| `encoder` | `auto`, `nvenc` or `amf` |
| `nvenc_preset` | NVENC preset 1 (fastest) to 7 (best quality); 3 is a good default |
| `amf_preset` | AMD AMF preset: `speed`, `balanced` or `quality` |
| `intra_refresh` | `1` / `0`: experimental intra refresh for the active encoder (off by default) |
| `bitrate_max_mbps` / `bitrate_min_mbps` | Video bitrate limits, Mbit/s |
| `adaptive_bitrate` | Lower/raise bitrate from loss reports |
| `respect_client_bitrate` | Do not exceed the bitrate asked by the client |
| `controller_type` | `ds4` or `x360` |

Changing `nickname` or `rp_key` changes the console identity: delete the old entry in the client and register again.

## Troubleshooting

**Artifacts or a broken picture.** Keep the video bitrate close to what the client asks for (about
10 Mbit/s over Wi-Fi). Set `respect_client_bitrate = 1` and a moderate `bitrate_max_mbps`. Very high limits make
the encoder produce huge frames (especially IDR frames) that do not fit into the FEC budget and are easily lost
on Wi-Fi. At startup the console should print a line like `[nvenc] target=10000000 ...` (or `[amf] target=...`)
when the client's bitrate is applied. Use 5 GHz Wi-Fi or Ethernet and disable Wi-Fi power saving on the phone.

**The stream starts slowly.** The client probably cannot reach the Senkusha responder. Make sure UDP port 9297 is
allowed. The console prints `[senkusha] responder listening on UDP 9297` on start, and the client log should show
`Senkusha determined average RTT` instead of `Senkusha connect timeout`.

**The PC is not in the client's console list.** Make sure UDP port 9302 is allowed and that the phone and the PC are
in the same subnet. The console prints `[discovery] listening on UDP 9302 ...` on start and
`[discovery] search from ...` for every search. If the client does not use the standard search, register by IP.

**`AMF factory not initialized` or `Failed to initialize video encoder`.** Update the GPU driver. With
`encoder = amf` the AMD AMF runtime (`amfrt64.dll`) must be present.

**Client log shows `FEC failed` / `Missing unit`.** Packets are lost between the PC and the client, or the frame
was too large for the FEC budget. Lower the bitrate and prefer 5 GHz Wi-Fi or Ethernet.

## Known limitations

- The official PS Remote Play app is not supported. Use [chiaki-ng](https://github.com/chiaki-ng/chiaki-ng) or another open-source client.
- Experimental: expect bugs, especially under heavy GPU load in games
- No remote access over the Internet; use a local network (5 GHz Wi-Fi or Ethernet is recommended)
- Senkusha supports the RTT and MTU tests only; the bandwidth command is ignored
- Some clients show packet loss and dropped frames as 0 in their stats overlay; these are measured on the client
- Gyro / motion data is parsed but not verified on real hardware
- A small share of frames can still be lost on Wi-Fi and show short artifacts

## Changelog

See [CHANGELOG.md](CHANGELOG.md).

## Acknowledgments

- [@cocainumgaming1488-arch](https://github.com/cocainumgaming1488-arch): testing of the NVENC encoder path on NVIDIA GPUs.

## Credits and license

Parts of the protocol implementation are based on [Chiaki](https://github.com/thestr4ng3r/chiaki) /
[chiaki-ng](https://github.com/chiaki-ng/chiaki-ng), licensed under AGPL-3.0 with an OpenSSL exception.
This project is therefore distributed under the **GNU AGPL-3.0** (see `LICENSE`). Third-party components
are listed in `NOTICE.md`.
