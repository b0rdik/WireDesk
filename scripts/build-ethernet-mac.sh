#!/bin/bash
# Local app build. No notarization or distribution identity is assumed.
set -euo pipefail
cd "$(dirname "$0")/.."
APP_OUTPUT="${1:-outputs/BetterCast-Ethernet.app}"
if [ -e "$APP_OUTPUT" ]; then echo "Refusing to overwrite existing app: $APP_OUTPUT" >&2; exit 1; fi
swift build -c release --product BetterCastSender
BINARY_DIR="$(swift build -c release --show-bin-path)"
mkdir -p "$APP_OUTPUT/Contents/MacOS" "$APP_OUTPUT/Contents/Resources"
cp "$BINARY_DIR/BetterCastSender" "$APP_OUTPUT/Contents/MacOS/BetterCastSender"
cp BetterCastSender-Info.plist "$APP_OUTPUT/Contents/Info.plist"
cp assets/branding/BetterCastIcon.icns "$APP_OUTPUT/Contents/Resources/AppIcon.icns"
for lproj in localization/*.lproj; do cp -R "$lproj" "$APP_OUTPUT/Contents/Resources/"; done
codesign --force --options runtime --sign "${SIGN_IDENTITY:--}" --entitlements BetterCastSender-Release.entitlements "$APP_OUTPUT"
codesign --verify --deep --strict "$APP_OUTPUT"
printf 'Created %s (local signature; distribution requires your Developer ID and notarization).\n' "$APP_OUTPUT"
