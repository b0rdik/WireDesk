# WireDesk updates and releases

Feeds: `https://github.com/b0rdik/WireDesk/releases/latest/download/mac-appcast.xml` and `windows-appcast.xml`. Archives use standard Sparkle/WinSparkle Ed25519 verification, with the public key in the Mac plist, Windows updater and `updates/public-key.txt`. HTTPS protects feed delivery; the pinned key protects archive authenticity. Private keys are never shipped or committed.

The repository Actions secret **SPARKLE_EDDSA_PRIVATE_KEY** holds the base64 raw 32-byte Ed25519 seed. Keep an encrypted offline backup. Replacing this secret alone cannot rotate keys in already installed clients; use the frameworks’ documented migration path.

Update root `VERSION`, `Sources/BetterCastReceiverDesktop/VERSION`, `CFBundleVersion` and `CFBundleShortVersionString` in the Mac plist to the same increasing semver. `scripts/check-version.py` verifies all four. Push a matching `vX.Y.Z` tag. The build-and-release workflow builds both platforms, runs tests, signs and verifies each update using the official Sparkle tool, then publishes packages, checksums and both feeds in the same GitHub release. Build-only CI does not receive the private key. Release runs only after both builds pass. Do not publish unrelated/latest releases in this repository without both feeds.

Manual checks are in the Mac footer/menu and Windows Settings. Automatic checks default to daily and can be disabled. Mac automatic downloads follow Sparkle policy; Windows installation requires WinSparkle confirmation. Neither client requests shutdown while a display session is active; Mac defers relaunch until idle. Video transport always remains on its selected Ethernet path; the separate update channel may use the internet connection.

Initial builds are experimental: the app is ad-hoc signed and the Windows installer has no Authenticode signature. OTA signature verification does not replace OS trust/notarization. Production signing needs owner-provided Apple Developer ID/notarization and Windows signing credentials. Do not disable OS protections.

Verification: use the official `sign_update --verify --ed-key-file KEY ARCHIVE SIGNATURE`; tampered archives must fail. A locally installed old version must detect the new full semver, fetch the archive, verify it, and restart into the new version. Windows CI checks compilation/packaging, not a physical installer/update cycle on a user PC.

References: [Sparkle documentation](https://sparkle-project.org/documentation/), [WinSparkle publishing](https://github.com/vslavik/winsparkle/blob/master/docs/src/content/docs/guides/publishing-updates.mdx).
