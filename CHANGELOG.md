# Changelog

All notable changes to this project are documented in this file.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [0.1.4] - 2026-10-16

### Added
- Console discovery on UDP 9302 (`discovery_server.cpp`): the PC answers the standard `SRCH` broadcast with
  host id, type, name and protocol version, so Chiaki-based clients list it and can register it without typing an IP.
- Stable console identity (`host_identity.h`): the MAC sent in the registration reply is derived from `nickname`
  and `rp_key` and equals the discovery `host-id`, so repeated registrations no longer create a new console each time.
- "Encoder" tab in the GUI: video encoder (Auto / NVIDIA NVENC / AMD AMF), NVENC preset (P1-P7), AMD AMF preset
  (speed / balanced / quality) and an intra refresh switch. New `server.ini` keys: `encoder`, `nvenc_preset`,
  `amf_preset`, `intra_refresh`, plus `nickname` for the console name.
- `CONNECTIONQUALITY` message sent to the client once per second, so the client's stats overlay shows the measured bitrate.
- Senkusha responder on UDP 9297 (Takion v7 handshake, echo/RTT pings, inbound and outbound MTU tests): clients no
  longer wait for a Senkusha timeout and receive measured RTT and MTU instead of fallback values.
- Optional NVENC and AMF intra refresh (off by default, experimental).
- Warning in the log when a frame needs more than 200 FEC units.

### Fixed
- Video artifacts on NVIDIA/NVENC. The encoder ran without a bitrate limit and produced IDR and P frames of
  200-500 KB, which exceeded the FEC capacity (data + parity units are limited to 255 per frame), so any packet loss
  made a frame unrecoverable. The encoder now follows the client's bitrate hint and frames stay under ~50 KB.
- `encoder = amf` failed with "AMF factory not initialized": replacing the auto-selected AMF encoder released the
  shared AMF runtime. The runtime is now reference-counted.
- IDR requests inside the throttle window are no longer discarded; they are served once the minimum interval has passed.
- Feedback loop of oversized IDR frames, packet loss and new IDR requests.
- No repeated IDR frames at stream start (the client acknowledges STREAMINFO before video).
- VPS/SPS/PPS are no longer repeated inside every IDR frame (the client already gets them in STREAMINFO), which removes
  the client warning `Unexpected NAL unit type 32`.
- NVENC intra refresh is really enabled when selected (the flag was previously always off).
- GUI: startup status text is translated; the language switch refreshes the AMF preset name.
- Build: `NvencEncoder::set_options` is visible from `main.cpp`.

### Changed
- Frame pacing and timer resolution tuned for steadier packet delivery.
- AMD AMF uses the preset chosen in the settings (default `speed`, as before).

### Known issues
- Some frames are still reported as corrupt by the client on Wi-Fi.
- Packet loss and dropped frames in some client stats overlays stay at 0 (they are measured on the client).
- Takion message type 13 from the client is logged as `Expected BIG` and ignored.
- Senkusha bandwidth command is not implemented.
- `set_bitrate failed: 12` on AMD AMF does not affect the stream.

## [0.1.3] - 2026-10-15

### Added
  - Senkusha responder on UDP port 9297 (senkusha_server.cpp): Takion v7 handshake,
  - protocol request ack, BIG/BANG, echo (RTT) pings, inbound and outbound MTU tests.
  - Clients no longer wait for a Senkusha timeout (up to about 15 s) before the stream starts
  - and receive measured RTT and MTU instead of fallback values.

### Known issues
  - Senkusha bandwidth command is not implemented.


## [0.1.2] - 2026-10-15

### Added
- Senkusha responder, NVENC intra refresh, bitrate cap and related diagnostics (see 0.1.4 for the final state).

### Fixed
- Video artifacts caused by an unlimited encoder bitrate; IDR request handling.

## [0.1.1] - 2026-10-14

### Fixed
- MSVC build of `full_server.cpp`: helpers moved into one properly closed anonymous namespace,
  `FullServer::` methods defined inside `namespace ps`.
- Removed the duplicate `CtrlCrypto` definition from `full_server.cpp` (kept in `full_server.h`).
- Logging function renamed `logf` -> `srv_log`: it clashed with `::logf(float)` from `<math.h>`.
- `FullServer::ctrl_send` now returns the result of `send_all`.
- Takion: the server's first data seq (BANG) is set to `server_tag`. The client takes its expected initial seq
  from the server tag, not from `initial_seq_num` in INIT_ACK, and was dropping BANG and STREAMINFO.

### Added
- Ctrl channel `/sess/ctrl`: after the `RP-Server-Type` response the server sends a session id (type 0x33) and
  accepts ctrl messages from the client.
- Explicit includes (`<atomic>`, `<mutex>`, `<thread>`, `<vector>`, ...).

## [0.1.0]

- Initial release: registration (PIN), session init, Takion stream, HEVC video (AMD AMF / NVIDIA NVENC),
  Opus audio, virtual gamepad.
