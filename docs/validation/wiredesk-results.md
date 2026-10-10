# WireDesk validation — 2026-10-11

This report covers the rename/setup/update changes. The earlier pipeline measurements remain in [ethernet-results.md](ethernet-results.md).

| Check | Result |
|---|---|
| Local universal Swift release (arm64/x86_64) and deep bundle signature validation | PASS on Xcode 27; Intel execution not tested |
| Native Mac launch and visual review | PASS; dark adaptive dashboard, status/cards/controls; actual-width two-column layout |
| Direct gateway-free Ethernet | PASS: USB Ethernet2.5Gb/s with IPv4link-local was found independently of default-route reachability; a TLS listener bound to that IPv4 without a gateway |
| Automatic identity creation | PASS: standard RSA3072 PKCS#12, directory700/files600; no private material in pairing |
| Qt sender/UI/updater shared code on Mac | PASS; Windows-specific code is checked separately by Actions |
| Pairing parser invalid URL/version/port/IP/hash/duplicate/extra fields | PASS |
| Encoder AVC framing/keyframe recovery and no silent120 software fallback | PASS |
| Real Swift TLS/Qt handshake/stream/reconnect/pinning/interface refusal/queue overload | PASS; loopback test seam is compile-only, absent in app |
| Protocol malformed headers and hardware VideoToolbox240-frame1080p120 fixture | PASS |
| Official Sparkle signature verified with app's pinned public key | PASS |
| Archive mutation | PASS: rejected |
| Windows native compilation / pairing and encoder tests | PASS in [Actions 38082677868](https://github.com/b0rdik/WireDesk/actions/runs/38082677868) |
| Windows package / per-user install-launch-uninstall | Final smoke test pending; CI executes it before artifact upload |
| Actual installed Mac/Windows OTA cycle | Pending; signature check alone is not an install-cycle check |
| Physical VDD / two-computer Ethernet stream / real120FPS scanout / end-to-end latency | Not tested: WindowsPC hardware test is unavailable |

Native OS distribution signing/notarization is not configured. Initial experimental Mac build is ad-hoc; Windows Authenticode credentials were not supplied. OTA Ed25519 signatures are separate from OS signing. VDD installation/privileged network changes remain explicit; normal session setup, identity, pairing import, mode application and reconnect are automated.
