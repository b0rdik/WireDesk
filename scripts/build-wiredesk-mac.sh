#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
APP_OUTPUT="${1:-outputs/WireDesk.app}"
if [ -e "$APP_OUTPUT" ]; then echo "Refusing to overwrite $APP_OUTPUT" >&2; exit 1; fi
if [ "${WIREDESK_UNIVERSAL:-0}" = 1 ]; then
    swift build -c release --product WireDesk --arch arm64 --arch x86_64
    BINARY_DIR="$(swift build -c release --arch arm64 --arch x86_64 --show-bin-path)"
else
    swift build -c release --product WireDesk
    BINARY_DIR="$(swift build -c release --show-bin-path)"
fi
mkdir -p "$APP_OUTPUT/Contents/MacOS" "$APP_OUTPUT/Contents/Resources" "$APP_OUTPUT/Contents/Frameworks"
cp "$BINARY_DIR/WireDesk" "$APP_OUTPUT/Contents/MacOS/WireDesk"
cp BetterCastSender-Info.plist "$APP_OUTPUT/Contents/Info.plist"
cp assets/branding/WireDesk.icns "$APP_OUTPUT/Contents/Resources/AppIcon.icns"
cp LICENSE "$APP_OUTPUT/Contents/Resources/LICENSE-BetterCast-GPL-3.0.txt"
cp ACKNOWLEDGMENTS.md "$APP_OUTPUT/Contents/Resources/ACKNOWLEDGMENTS.md"
for lproj in localization/*.lproj; do cp -R "$lproj" "$APP_OUTPUT/Contents/Resources/"; done
SPARKLE_ROOT=".build/artifacts/sparkle/Sparkle"
SPARKLE_FRAMEWORK="$SPARKLE_ROOT/Sparkle.xcframework/macos-arm64_x86_64/Sparkle.framework"
ditto "$SPARKLE_FRAMEWORK" "$APP_OUTPUT/Contents/Frameworks/Sparkle.framework"
cp "$SPARKLE_ROOT/LICENSE" "$APP_OUTPUT/Contents/Resources/LICENSE-Sparkle.txt"
IDENTITY="${SIGN_IDENTITY:--}"
if [ "$IDENTITY" != "-" ]; then
    SPARKLE_CODES="$APP_OUTPUT/Contents/Frameworks/Sparkle.framework/Versions/B"
    for code in "$SPARKLE_CODES/XPCServices/Downloader.xpc" "$SPARKLE_CODES/XPCServices/Installer.xpc" "$SPARKLE_CODES/Autoupdate" "$SPARKLE_CODES/Updater.app" "$APP_OUTPUT/Contents/Frameworks/Sparkle.framework"; do
        codesign --force --options runtime --preserve-metadata=entitlements --sign "$IDENTITY" "$code"
    done
    codesign --force --options runtime --sign "$IDENTITY" --entitlements BetterCastSender-Release.entitlements "$APP_OUTPUT"
else
    # Local/experimental build: retain the vendor-signed Sparkle framework and do not enable hardened library validation.
    # Distribution with Developer ID uses the branch above. No OS security setting is modified.
    codesign --force --sign - --entitlements BetterCastSender-Release.entitlements "$APP_OUTPUT"
fi
codesign --verify --deep --strict "$APP_OUTPUT"
printf 'Created %s. Developer ID/notarization requires your distribution credentials.\n' "$APP_OUTPUT"
