# Changelog

All notable changes to this project are documented in this file.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [0.1.3] - 2026-10-15

### Added
- Senkusha responder on UDP port 9297 (`senkusha_server.cpp`): Takion v7 handshake,
  protocol request ack, BIG/BANG, echo (RTT) pings, inbound and outbound MTU tests.
  Clients no longer wait for a Senkusha timeout (up to about 15 s) before the stream starts
  and receive measured RTT and MTU instead of fallback values.

### Known issues
- Senkusha bandwidth command is not implemented.

## [0.1.2] - 2026-10-15

### Added
- NVENC intra refresh (`enableIntraRefresh`, period 60 frames, count 20) when supported by
  the GPU, logged as `[nvenc] intra refresh: on`.
- Warning in the log when a frame needs more than 200 FEC units.

### Fixed
- Video artifacts on NVIDIA/NVENC. The encoder ran without a bitrate limit and produced IDR
  and P frames of 200-500 KB. These exceeded the FEC capacity (data + parity units are limited
  to 255 per frame), so any packet loss made a frame unrecoverable. The encoder now follows
  the client's bitrate hint (10 Mbit/s) and frames stay under about 50 KB.
- IDR request handling: a client request made inside the throttle window is no longer
  discarded; it is kept and served once the minimum interval between IDR frames has passed.
- Feedback loop of oversized IDR frames, packet loss and new IDR requests.

### Changed
- Frame pacing and timer resolution tuned for steadier packet delivery.
- Hard cap on the effective bitrate (15 Mbit/s).

### Known issues
- Some frames are still reported as corrupt by the client; a client log is needed to measure
  the remaining `FEC failed` rate.
- Takion message type 13 from the client is logged as `Expected BIG` and ignored.
- Senkusha (Takion v7) times out and the client falls back to default values.
- `set_bitrate failed: 12` on AMD AMF does not affect the stream.

## [0.1.1] - 2026-10-14

### Fixed
- MSVC build of `full_server.cpp`: helpers moved into one properly closed anonymous namespace,
  `FullServer::` methods defined inside `namespace ps`.
- Removed the duplicate `CtrlCrypto` definition from `full_server.cpp` (kept in `full_server.h`).
- Logging function renamed `logf` -> `srv_log`: it clashed with `::logf(float)` from `<math.h>`.
- `FullServer::ctrl_send` now returns the result of `send_all`.
- Takion: the server's first data seq (BANG) is set to `server_tag`. The client takes its
  expected initial seq from the server tag, not from `initial_seq_num` in INIT_ACK, and was
  dropping BANG and STREAMINFO.

### Added
- Ctrl channel `/sess/ctrl`: after the `RP-Server-Type` response the server sends a session id
  (type 0x33) and accepts ctrl messages from the client.
- Explicit includes (`<atomic>`, `<mutex>`, `<thread>`, `<vector>`, ...).

## [0.1.0]

- Initial release: registration (PIN), session init, Takion stream, HEVC video (AMD AMF /
  NVIDIA NVENC), Opus audio, virtual gamepad.
