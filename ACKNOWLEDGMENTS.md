# Acknowledgments

WireDesk is derived from [BetterCast by Stephen Lovino](https://github.com/StephenLovino/BetterCast), licensed under GPL-3.0. Original copyright, notices and Git history are retained. The following original acknowledgments remain applicable.


BetterCast is original code, but several design decisions were informed by
studying other open-source projects. No source code from these projects is
included in BetterCast. The debt is one of ideas, and it deserves credit.

- [SideScreen](https://github.com/tranvuongquocdat/SideScreen) (MIT) —
  studying its streaming pipeline led directly to two of v18's improvements:
  applying flow control *before* the encoder rather than after it (so network
  congestion skips frames instead of corrupting the picture), and removing
  VideoToolbox's DataRateLimits cap on infrastructure Wi-Fi. BetterCast's
  per-second pipeline log format is deliberately shaped like SideScreen's so
  the two projects' diagnostics can be compared side by side.
- [targetBridge](https://github.com/swellweb/targetBridge) (MIT) by Marco
  Caciotti — the inspiration for BetterCast's 5K Retina profile and for
  treating "an old 5K iMac as a display over Thunderbolt" as a first-class
  use case.
- [scrcpy](https://github.com/Genymobile/scrcpy) (Apache-2.0) — the
  repeat-previous-frame idea behind the static-content frame pump, and the
  ordered-queue decoding approach used by the Android receiver.
- [Moonlight](https://github.com/moonlight-stream) (GPL-3.0) — the Android
  MediaCodec low-latency vendor keys used by the Android receiver.

## Dedicated Ethernet mode

- [VirtualDrivers/Virtual-Display-Driver](https://github.com/VirtualDrivers/Virtual-Display-Driver) — MIT, Copyright (c) 2024 Virtual Display. Integration refers to release 25.7.23 and reuses its installed monitor through Windows display APIs; no driver code or binaries are vendored by this change. Retain its MIT notice when distributing a driver package.
- Qt Network TLS uses the Qt license/backend already supplied with the desktop build; preserve Qt notices and applicable source/relinking obligations.
- FFmpeg/libswscale and optional NVENC/AMF/QSV features remain external dependencies. Windows build enables these explicitly. libx264 is a GPL dependency; do not use nonfree/GPL-incompatible FFmpeg configurations for distribution.
- Network, Security, CryptoKit, CoreVideo and QuartzCore are system frameworks on macOS. No custom cryptographic construction is used.

## Signed updates

- [Sparkle 2.10.0](https://github.com/sparkle-project/Sparkle) — MIT; notice included in the Mac application.
- [WinSparkle 0.9.4](https://github.com/vslavik/winsparkle) — MIT/Expat; notices in `licenses/` and Windows packages. Both use standard Ed25519 archive signatures.
